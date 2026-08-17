# Stratum Sort — complete technical description

This document is self-contained. Everything needed to reimplement the
algorithm from scratch, in any language, is here: the motivation, the
derivation of every formula, the meaning of every parameter, the proof of
every invariant, and the design decisions with the alternatives that were
rejected and why.

Notation used throughout:

| symbol | meaning |
|---|---|
| `n` | number of elements |
| `w` | bits in the key type (64 for `int64_t`) |
| `λ` | target occupancy per bin (default 32) |
| `t` | leaf threshold (default 64) |
| `D` | maximum refinement depth (default 6) |
| `span` | `max − min` of a range of values, **never** `max − min + 1` |

---

## 1. Motivation

A comparison sort learns about the data only through questions of the form
"is `a` before `b`?". Each such question yields at most one bit, there are
`n!` possible orderings, so `log₂(n!) = Θ(n log n)` questions are needed.
That bound is tight and `std::sort` essentially achieves it.

But for integer keys the bound describes a self-imposed restriction. A
comparison sort refuses to look at the *value*. If you are told a key is
`517` out of a range `[0, 1000]`, you know it belongs roughly halfway
along the output — information no sequence of comparisons gives you as
cheaply.

Counting sort and radix sort exploit this, and both are rigid:

- **Counting sort** allocates one counter per distinct value. Linear when
  the range is small, useless when it is `2⁶⁴`.
- **Radix sort** processes fixed digit positions. It never looks at how
  the data is actually distributed, so it does the same work on a tight
  cluster as on a uniform spread.

The question this algorithm answers is: **can the partition be derived
from the data's own observed range, adapting its resolution to local
density, while keeping the linear-time behaviour?**

## 2. Intuition

Imagine sorting a pile of dated documents. You do not compare them pairwise.
You glance at the pile, see it spans 1990 to 2020, and lay out thirty
folders — one per year. One pass, each document into its folder. The
folders are now in order relative to each other, so you never have to
merge them.

Some folders are fat. Take the fat one, look at **its own** dates — say
they are all in March and April of 2011 — and split *that folder* by day.
Note what just happened: you did not split it by month across 1990–2020,
you split it by the range you actually observed *inside it*. That is the
entire idea.

Repeat until each folder is thin enough to sort by hand. Then stack the
folders in order. Done.

Three properties fall out of this and they are what the rest of the
document formalises:

1. The folders are ordered, so joining is concatenation, never merging.
2. Splitting a folder by its own range is what makes the resolution adapt
   to density.
3. Each split narrows the range of everything inside it, and a range can
   only be narrowed so many times before it is a single value — which is
   why the recursion is bounded by the key width, not by `n`.

## 3. The parameters

### 3.1 λ — target occupancy

λ is the number of elements you would *like* each bin to receive. It
determines how many bins to make:

```
binCount = ceil(n / λ)
```

and, when a bin is split again, into how many pieces:

```
splits = ceil(count / λ)
```

λ is the only parameter with a first-order effect on running time, and it
trades two opposing costs against each other:

- **Local sorting grows with λ.** Leaves end up holding about λ elements
  and are finished with Insertion Sort, which is quadratic in leaf size.
  For occupancy distributed as Poisson(λ) the expected comparisons per
  element are

  ```
  E[k²] / (4·E[k]) = (λ² + λ) / (4λ) = (λ + 1) / 4
  ```

  For λ = 32 that predicts 8.25 comparisons per element. Measured on
  uniform data: 8.99. The 9% gap is the run detection, which the model
  does not count.

- **Scattering grows as λ shrinks.** The distribution pass writes into
  `n/λ` output streams simultaneously, each holding one cache line live,
  so the write working set is `(n/λ) · 64` bytes. When that approaches the
  L2 capacity, throughput collapses.

**The useful lower bound on λ therefore depends on `n` and on the cache,
not on the algorithm:** roughly `n · 64 / λ ≲ L2`. Measured on a 4 MiB L2
at `n = 10⁶`:

| λ | write working set | vs L2 | penalty |
|---|---|---|---|
| 8 | 7.63 MB | 1.91× | **+43%** |
| 16 | 3.81 MB | 0.95× | +13% |
| 32 | 1.91 MB | 0.48× | baseline |
| 64 | 0.95 MB | 0.24× | +0% |

λ = 32 is a practical optimum for `n ≈ 10⁶` on that machine. It is **not
universal**: at `n = 10⁷` the same condition puts the lower bound near
160.

### 3.2 t — leaf threshold

A bin holding at most `t` elements stops being refined. Precondition:
`t ≥ λ`; a bin of the target occupancy must be allowed to be a leaf.

**t is not a tuned value, it is a safe lower bound.** Its only job is to
sit above the upper tail of the occupancy distribution, so that a bin is
not refined merely for landing slightly above average. Once it clears that
tail, raising it further changes nothing.

The reason to separate it from λ at all is arithmetic. If `t = λ`, then
occupancy is Poisson(λ) and

```
P(X > λ) ≈ 0.5
```

so **half of all elements enter refinement because of the arithmetic, not
because the data needs it.** With λ = 32 and t = 64,
`P(Poisson(32) > 64) ≈ 2·10⁻⁷` and refinement essentially disappears for
well-spread data.

Verified: with λ = 32, the internal counters for t = 64, 96 and 128 are
byte-identical on every dataset. Once t clears the tail its exact value is
irrelevant, so there is nothing to sweep.

### 3.3 D — maximum refinement depth

The recursion terminates on its own (§7). `D` bounds how much effort is
spent trying before the remainder is handed to a comparison sort whatever
its size.

`D` is often described — including by earlier versions of this document —
as "what makes the worst case linear". **That is wrong**, and §8.8 shows
why: refinement is already linear with no cap at all, because each level
at least halves the observed span, so the depth is bounded by `w` whether
or not `D` exists. What `D` actually does is trade passes for residual
size: it cuts the passes from `w+1 = 65` to `D+1 = 7`, and pays for it by
letting a leaf reach a comparison sort with up to `λ·2^(w/(D+1)) ≈ 18 000`
elements instead of `t = 64`. Both settings are `Θ(n)`; `D` is a
constant-factor decision that measurement made.

## 4. The span, and why not the range

Every formula is expressed over `span = max − min`, never over the number
of distinct values `max − min + 1`.

**Reason.** The `+1` form needs `w+1` bits. For `int64_t` and an input
containing both `INT64_MIN` and `INT64_MAX` it is exactly `2⁶⁴`, which
wraps to zero in 64-bit arithmetic and collapses the entire partition into
one bucket. The span is at most `2⁶⁴ − 1` and is always representable.

This is not hypothetical. An earlier version of this algorithm used the
`+1` form; on such an input the top level degenerated to a single bin and
one whole refinement level was wasted — measured at **+43% time** on
otherwise identical data.

## 5. Building the intervals

Given an observed range and a number of elements:

```
span     = max − min
binCount = ceil(length / λ)
if span < binCount:  binCount = span + 1        (cap, see below)
width    = span / binCount + 1                  (integer division)
```

and the bucket of a value is

```
bucketOf(v) = (v − min) / width                 (integer division)
```

Three things need proving.

### PROPERTY 1 — the width formula is the natural one

`width = span/s + 1` equals `ceil((span+1)/s)`.

*Proof.* Write `span = q·s + r` with `0 ≤ r < s`. Then

```
ceil((span+1)/s) = q + ceil((r+1)/s) = q + 1
```

because `1 ≤ r+1 ≤ s`. And `span/s + 1 = q + 1`. ∎

So this is exactly "the range cut into `s` equal parts, rounded up",
written in a form that cannot overflow.

### PROPERTY 2 — the index is always valid, so no clamping is needed

`bucketOf(v) ≤ s − 1` for every `v` in `[min, max]`.

*Proof.* With `width = floor(span/s) + 1`,

```
s·width = s·floor(span/s) + s = (span − r) + s > span
```

since `r < s`. Hence `span/width < s`, so `floor(offset/width) ≤ s − 1`
for every offset in `[0, span]`. ∎

This matters practically: a defensive clamp at this point would silently
absorb an arithmetic bug instead of exposing it. The implementation
asserts the property instead of clamping.

### The cap, and why it cannot change the partition

The cap `binCount ≤ span + 1` stops the algorithm creating buckets
narrower than one value — buckets no element can ever reach, which would
still be allocated, zeroed, prefix-summed and iterated.

*It cannot change the result.* The cap only binds when `span < binCount`,
and then

```
before:  width = span/binCount + 1 = 0 + 1 = 1
after:   width = span/(span+1) + 1 = 0 + 1 = 1
```

Identical widths mean an identical value-to-bucket assignment. Only the
unreachable tail disappears.

### The single-bucket case

When `binCount = 1` the width carries no information — every value maps to
bucket 0 — and its exact value `span + 1` may not be representable: it is
exactly `2⁶⁴` when the input spans the whole universe. Set it to 1 so the
postcondition `width ≥ 1` holds unconditionally, and handle the
single-bucket case without computing any index at all.

An implementation that lets the wrapped `0` escape can still be correct,
if it happens to short-circuit the single-bucket case before dividing.
That is luck, not a contract, and it is exactly the kind of thing that
breaks when someone later reorders two checks.

## 6. Distribution: count, then place

Moving elements into buckets is a two-pass counting step, and it is the
only place elements ever move.

```
pass 1:  for each element, compute its bucket; store the index; ++histogram[bucket]
prefix:  cursor = start
         for each bucket b:  bucketStart[b] = cursor
                             writeCursor[b] = cursor
                             cursor += histogram[b]
pass 2:  for each element i:  dst[ writeCursor[bucketOf[i]]++ ] = src[i]
```

**Why two passes are necessary.** The destination offset of an element
depends on how many elements precede it in earlier buckets, and that is
not known until every element has been classified. Counting first is what
lets the second pass write each element straight to its final place, with
no insertion and no reallocation.

**Why the write cursor is advanced as it goes.** It preserves the relative
order of elements landing in the same bucket. That is not needed for
correctness here (the keys are integers), but it is the property that
would make a key/value variant stable at this stage.

The same routine is used unchanged for the top-level distribution and for
every refinement split. That is deliberate: two implementations of the
same rule drift apart, and in this project they did — the top level had a
cap the refinement lacked, for several versions, with nothing documenting
the difference.

## 7. Refinement

For a bin at `[start, start+count)`:

```
if count == 0:                     leaf (empty)
if count <= t or depth >= D:       leaf
observed = min and max OF THIS BIN ONLY
if observed.min == observed.max:   leaf, marked "already sorted"
otherwise:                         split by the same formulas of §5,
                                   applied to the observed range,
                                   then recurse on each child at depth+1
```

Three things deserve emphasis.

**The bin's own range, never the global one.** This is the defining rule.
The top level cuts the global range into equal widths, which is the wrong
resolution wherever the data is dense. Re-deriving the width from the
bin's own extremes is what makes the partition adapt to local density —
and it is what lets a tight cluster hidden inside a wide interval be
resolved in one more level instead of `log(width)` levels.

**The size check comes before the range scan.** A bin that is going to be
a leaf never pays for a min/max scan it does not need.

**The degenerate-range certificate.** When `observed.min == observed.max`,
every element is identical, so the bin is sorted by definition and the
data does not even need to move. Recording that fact saves the local sort
a full `O(count)` rescan to rediscover it. On inputs with few distinct
values this is not a micro-optimisation: it is **100% of the algorithm's
comparisons**.

### PROPERTY 3 — the recursion terminates

*Claim.* The observed span decreases strictly at every refinement level.

*Proof.* Let the bin have span `≥ 1` and be split into `s ≥ 2` buckets of
width `w = floor(span/s) + 1`. Every child occupies an interval of exactly
`w` consecutive values, so its own span is at most `w − 1 = floor(span/s)`.
For `s ≥ 2` and `span ≥ 1`, `floor(span/s) < span`. ∎

The span is a non-negative integer that strictly decreases, so the
recursion terminates **even with no depth cap**. `D` bounds the effort,
not the termination.

*Why `s ≥ 2` always holds:* a bin only reaches the split when
`count > t ≥ λ`, so `ceil(count/λ) ≥ 2`; and when the cap of §5 binds,
`s` becomes `span + 1 ≥ 2`.

### PROPERTY 4 — the leaves tile `[0, n)` in ascending order

*Claim.* The final leaves partition the array positions `[0, n)` with no
gap and no overlap, in ascending value order — and therefore **every leaf
already occupies its final position**.

*Proof.* The distribution writes the children of a bin at consecutive
offsets starting at the parent's own start, in ascending bucket order, and
`bucketOf` is monotone non-decreasing in the value. So the children tile
the parent's range in ascending value order. The top-level bins tile
`[0, n)` by the same argument. Induct over the depth. ∎

This is the invariant that makes the last phase a concatenation rather
than a merge, and the implementation checks it executably in debug builds
rather than only asserting it in prose.

## 8. Why the worst case is linear

This section is a proof, not a benchmark. The measurements in
[`BENCHMARKS.md`](BENCHMARKS.md) validate it; they do not establish it. It
is written to be read on its own: no step appeals to the project's
history, and every step names the hypothesis or the code invariant it
uses.

### 8.0 Hypotheses

The result is `Θ(n)` **under the following four hypotheses**, which are
sufficient (no claim is made that they are necessary).

| | |
|---|---|
| **H1** | `T` is an integral type of width `w = 8·sizeof(T) ≤ 64` bits. Enforced by three `static_assert`s (integral, not `bool`, `sizeof(T) ≤ 8`). |
| **H2** | `λ ≥ 1`, `t ≥ λ` and **`D ≥ 1`** are constants chosen independently of `n`. |
| **H3** | Unit-cost RAM: arithmetic on `uint64_t` and `size_t`, and one array access, cost `O(1)`; **allocating or releasing a block of `k` words costs `O(k)`**; and `n + λ` fits in `size_t`. |
| **H4** | `memcpy` of `k` elements costs `Θ(k)`. |

**On H1.** Not cosmetic. Before it was enforced,
`StratumSort<__int128>` compiled — `std::is_integral<__int128>` is
true on common toolchains — and overflowed the heap, because the offset
arithmetic is carried in `uint64_t`. The proof needs `w` bounded; the code
needs it too. Note `w` is the width of the *type*, so a narrower key makes
every bound below tighter, never looser.

**On H2, and why `D ≥ 1` is not decoration.** At `D = 0` the theorem is
false, and the algorithm with it. `refine`'s first test is
`count ≤ t || depth ≥ D`, so with `D = 0` it fires at `depth = 0` for
every top-level bin and no refinement ever happens. Then a single
top-level bin can hold `Θ(n)` elements — take `n−2` values alternating
`0, 1, 0, 1, …` plus two at `2⁶²`, so that bucket `0` is far wider than
`1` and collects them all — and that bin goes straight to `introSort`:
`Θ(n log n)`. The proof never covered this (§8.4 divides by `D`), but the
hypothesis list has to say so. Any `D ≥ 1` is fine; `D` may also grow with
`n`, which only shrinks the residual.

`λ` and `t` are constructor arguments, so a caller can violate H2. The
asymptotic claim is about a **fixed** configuration: for each
choice of `(λ, t, D)` there is a constant `c(λ, t, D, w)` with
`T(n) ≤ c·n` for all `n`. A caller who scales a parameter with the input —
`t = n`, say, which turns the whole array into one leaf — is outside the
hypothesis, and the conclusion has to be re-derived. §8.8 quantifies what
each parameter costs inside the constant.

**On H3.** The allocation clause is needed because `refine` allocates: see
§8.5, which accounts for every word. The `n + λ` clause is needed because
`binCount` is computed as `(length + λ − 1) / λ`, which would wrap for
`n > SIZE_MAX − λ + 1` and destroy the `s_top = ⌈n/λ⌉` that Lemma 4 Case C
rests on. It is not a live risk — reaching `distribute` already allocates
`2n` elements plus `n` `size_t`, so any input that runs at all has
`n ≤ SIZE_MAX/8` — but the proof should say it rather than assume it.

**Scope: this is the release build** (`NDEBUG`, no `STRATUM_ENABLE_METRICS`).
§8.5 revisits the other two configurations, which are also `Θ(n)` but with
larger constants.

Throughout: `n` = element count, `w` = key width in bits, `λ` = target
occupancy, `t` = leaf threshold (`t ≥ λ`, enforced by the constructor),
`D` = `MAX_SUBDIVISION_DEPTH`. For a bin `B`: `m = |B|` is its size and
`σ = max(B) − min(B)` its **observed span** (§4). `s` is the number of
buckets a split produces and `W` their common width. A leaf is **cheap**
if it reaches no comparison sort, and **expensive** otherwise; §8.6 shows
an expensive leaf necessarily has `σ ≥ 1`.

### 8.1 The structural lemmas

**LEMMA 1 (span contraction).** *If a bin of span `σ` is split into `s`
buckets of width `W = ⌊σ/s⌋ + 1`, every child has span `σ′ ≤ W − 1 = ⌊σ/s⌋`.*

Bucket `b` covers exactly the `W` consecutive values
`[origin + bW, origin + (b+1)W − 1]`, so any two elements inside it differ
by at most `W − 1`. ∎

**LEMMA 2 (a binding cap ends the subtree cheaply).** *If the fan-out cap
binds — that is, `σ < ⌈m/λ⌉`, so the code sets `s = σ + 1` — then every
child has span exactly `0`, is a leaf, and is **cheap**: it reaches no
comparison sort, at a cost of `O(m′)`.*

`W = ⌊σ/(σ+1)⌋ + 1 = 0 + 1 = 1`, so by Lemma 1 every child has `σ′ ≤ 0`,
i.e. `σ′ = 0`. Such a child is a leaf, by one of two routes, and it is
worth separating them because **they do not cost the same**:

- If `refine` reaches its range scan, `observed.minimum ==
  observed.maximum` sets the `sorted` flag and `sortRefined` returns
  immediately: `O(1)`, zero comparisons.
- If instead the child leaves `refine` at an earlier test — because
  `count == 0`, because `count ≤ t`, or because `depth ≥ D` — its range is
  never scanned, so `sorted` stays `false`. (An empty child is `O(1)`:
  `sortLeaf` and `detectRun` both return at once for `count < 2`.) It still reaches no comparison sort:
  `detectRun` walks it, finds every element equal, and returns
  `Ascending`. But that walk costs `2(m′−1)` comparisons, so the cost is
  `O(m′)`, **not zero**.

Either way no comparison sort runs, which is all the proof needs. Since
leaves partition `[0, n)` (F1 in §8.5), the `O(m′)` route sums to `O(n)`. ∎

This lemma is what makes the whole argument work, and it is easy to miss:
the cap is not a special case to be handled, it is an *escape hatch*. A
bin can only remain expensive by never triggering it.

**LEMMA 3 (an expensive split has a non-binding cap).** *If a leaf has
span `> 0` and was produced by depth exhaustion, then every one of its `D`
ancestor splits had `σᵢ ≥ ⌈mᵢ/λ⌉`, hence `sᵢ = ⌈mᵢ/λ⌉`.*

Contrapositive of Lemma 2: had any ancestor's cap bound, this leaf's span
would be `0`. ∎

### 8.2 Where the bit budget comes from, and why the top level spends some

Two distinct splits happen:

1. **One top-level split** in `distribute`, over the whole array, with
   `s_top = ⌈n/λ⌉` buckets. **This split is easy to forget, and forgetting
   it is what makes the exponent come out as `w/D` instead of `w/(D+1)`.**
2. **At most `D` refinement splits**, since `refine` is entered at
   `depth = 0` and stops at `depth ≥ D`.

So a root-to-leaf path has at most `D + 1` splits, not `D`.

**LEMMA 4 (the top-level budget).** *Let `σ_g` be the global span and `σ₀`
the span of any top-level bin. Then either no expensive leaf exists at all
(so the sort runs no comparison sort), or*

```
σ₀  ≤  σ_g · λ / n  <  2^w · λ / n
```

`planPartition` computes `s_top` in two steps — `s_top = ⌈n/λ⌉`, then
`if (σ_g < s_top) s_top = σ_g + 1` — and sets `W_top = 1` when
`s_top = 1`, else `W_top = ⌊σ_g/s_top⌋ + 1`. Those three branches are
exactly the three cases, and they must be taken separately because
**Lemma 1 does not apply to the first one**.

**Case A — `s_top = 1`.** The code overrides the width to `1`, and
`countAndPlace` short-circuits: every element goes to bucket `0`. So the
single top-level bin *is* the whole array and `σ₀ = σ_g`. Lemma 1 would
wrongly give `σ₀ ≤ W_top − 1 = 0`; it does not apply, because the width
the code uses is not `⌊σ_g/s_top⌋ + 1`.

The conclusion holds anyway, but the two ways of reaching `s_top = 1` must
be handled separately — **they are not the same situation, and one of them
places no bound on `n` at all.**

**Case A1 — `⌈n/λ⌉ = 1`, i.e. `n ≤ λ`.** Then `σ_g·λ/n ≥ σ_g = σ₀`, so
the inequality holds trivially. And the theorem of §8.4 is vacuous here
for a stronger reason: `n ≤ λ ≤ t`, so `refine` returns at its first test
(`count ≤ leafThreshold_`) with `depth = 0`. No split happens and no leaf
is produced by depth exhaustion. The whole array is one leaf of at most
`λ` elements, finished by whichever local sort its size selects — for the
shipped `λ = 32 ≤ L₁` that is insertion sort at `O(λ²)`, and for any fixed
`λ` it is `O(λ log λ)` at worst. Either way `O(1)` under H2.

**Case A2 — the cap with `σ_g = 0`.** Here `n` is **unconstrained**: every
element is equal, so `binCount = σ_g + 1 = 1` for any `n`, however large.
It is *not* true that `n ≤ λ`, and it is *not* true that `refine` returns
at its first test — for `n > t` it falls through to the range scan and
returns a `sorted` leaf at the degenerate-range test. The correct argument
is simply that `σ₀ = σ_g = 0`, so by §8.6 no leaf anywhere in this sort
can be expensive: first branch of the lemma. (The inequality also holds,
both sides being `0`.)

Conflating A1 and A2 — concluding `n ≤ λ` from `s_top = 1` — would be a
genuine error, since A2 admits arbitrarily large `n`.

**Case B — the cap binds** (`σ_g < ⌈n/λ⌉`, hence `s_top = σ_g + 1 ≥ 2`).
Then `W_top = ⌊σ_g/(σ_g+1)⌋ + 1 = 1`, so by Lemma 1 every top-level bin
has span `0`. By Lemma 2's argument every one of them is cheap, so no
expensive leaf exists: the first branch of the lemma. (The total is still
`O(n)`, not zero work — a span-`0` bin that exits by size is walked by
`detectRun`.)

**Case C — the general case** (`s_top = ⌈n/λ⌉ ≥ 2` and the cap does not
bind). Lemma 1 applies as stated:

```
σ₀  ≤  W_top − 1  =  ⌊σ_g / ⌈n/λ⌉⌋  ≤  σ_g / (n/λ)  =  σ_g·λ/n
```

using `⌈n/λ⌉ ≥ n/λ`. With `σ_g ≤ 2^w − 1` from H1, `σ₀ < 2^w·λ/n`. ∎

Only Case C can produce a leaf **by depth exhaustion**, which is the
hypothesis §8.4 needs. Note the weaker phrasing is deliberate: Case A1 can
perfectly well produce an *expensive* leaf — `n = 3` with values `3, 1, 2`
gives one leaf that `detectRun` calls `Unsorted` and insertion sort
finishes — but that leaf has `m ≤ λ ≤ t`, so §8.6 covers it through the
`t` branch, not through `M`.

That is the whole content of the `2^w/n` factor: **the top-level split has
already spent `log₂(n/λ)` of the `w` available bits before refinement
starts.** A larger array forces a finer top-level grid, which leaves less
span for refinement to consume — and therefore a *smaller* residual.

### 8.3 Why the surviving child cannot keep everything

Claimed separately because it is a genuinely different fact, and because
one plausible-looking strengthening of it is **false** (§8.7).

**LEMMA 5 (strict peeling).** *If a split has `σ ≥ 2` and a non-binding
cap, every child is a proper subset: `m′ ≤ m − 1`.*

`min(B)` has offset `0`, so bucket `0`. `max(B)` has offset `σ`, so bucket
`⌊σ/W⌋`. Since `s ≥ 2`, `W = ⌊σ/s⌋ + 1 ≤ σ/2 + 1 ≤ σ` for `σ ≥ 2`, hence
`⌊σ/W⌋ ≥ 1`. The two extremes land in different buckets. ∎

(For `σ = 1` the cap binds — `⌈m/λ⌉ ≥ 2 > 1 = σ` whenever `m > λ` — so
Lemma 2 applies and the subtree is cheap.)

Lemma 5 guarantees progress, so refinement terminates without `D`. But
note what it does **not** give: losing one element per level over `D`
levels leaves `m − D` elements. **Lemma 5 alone does not bound `m`.** The
bound comes from the bit budget, not from peeling.

### 8.4 The residual bound

**THEOREM (residual).** *Let a leaf have span `> 0` and be produced by
depth exhaustion, with `m` elements. Then*

```
m  ≤  min( n,  ( λ^(D+1) · 2^w / n )^(1/D) )
```

*and, free of `n`,*

```
m  ≤  λ · 2^(w/(D+1))
```

**Proof.** Let `σ₀ ⋯ σ_D` and `m₀ ≥ ⋯ ≥ m_D = m` be the spans and sizes
along the leaf's ancestor chain (sizes are non-increasing because children
are subsets). By Lemma 3 every split has `sᵢ = ⌈mᵢ/λ⌉ ≥ m/λ`, so by
Lemma 1

```
σ_D  ≤  σ₀ / ∏ᵢ sᵢ  ≤  σ₀ · (λ/m)^D
```

The leaf has span `> 0`, so `σ_D ≥ 1`, giving `(m/λ)^D ≤ σ₀`. The leaf was
produced by depth exhaustion, so at least one refinement split occurred:
that rules out Case A1 (`refine` returns at its first test, `D ≥ 1` by H2)
and rules out A2 and B (every top-level span is `0`, so no descendant has
span `> 0`). We are therefore in Case C, and `σ₀ < 2^w λ / n`:

```
(m/λ)^D  <  2^w · λ / n      ⟹      m^D  <  λ^(D+1) · 2^w / n
```

Together with the trivial `m ≤ n`, that is the first form. For the second,
`min(n, (K/n)^(1/D))` with `K = λ^(D+1) 2^w` is maximised where the two
branches meet, at `n^(D+1) = K`, giving

```
m  ≤  K^(1/(D+1))  =  λ · 2^(w/(D+1))          ∎
```

**Two different objects, and the difference matters.** The theorem gives a
*family* of bounds indexed by `n`, and a *single* number that dominates
the family:

```
B(n)  =  min( n,  (λ^(D+1)·2^w / n)^(1/D) )      the bound at a given n;
                                                  DEPENDS on n, and for
                                                  n > λ·2^(w/(D+1)) it
                                                  decreases like n^(−1/D)

M     =  sup_n B(n)  =  λ · 2^(w/(D+1))           the global supremum;
                                                  a CONSTANT, free of n,
                                                  attained at n = M
```

Saying "a constant that shrinks as `n` grows" would be a contradiction.
`B(n)` shrinks; `M` is the constant. Linearity needs only `M < ∞`
(§8.6); `B(n)` matters because it is what an adversary at a given size
actually faces, and it is far tighter than `M` for realistic inputs.

**With the shipped constants** (`λ = 32`, `w = 64`, `D = 6`):

```
M      =  32 · 2^(64/7)  =  18 089.4…     for every n
B(10⁶) ≈  9 268
B(10⁷) ≈  6 314
```

That `B(n)` decreases is the opposite of what an `n log n` term would do,
and it is the direct consequence of Lemma 4.

### 8.5 Cost of each phase

Two facts are used repeatedly.

**(F1a) The leaves partition `[0, n)` exactly**, so their sizes sum to
`n`. This is Property 4 of §7, and it is what the `detectRun` and
`appendLeaves` rows below, Lemma 2, and the aggregation in §8.6 use.

**(F1b) The nodes at any one depth are pairwise disjoint subintervals of
`[0, n)`, so their sizes sum to at most `n`.** This is *not* Property 4
and must not be confused with it: the nodes at depth `i` do **not** cover
`[0, n)`, because positions already absorbed by leaves at shallower depths
are absent. `≤ n` is all the cost table needs, and it follows by induction
on `i`: `countAndPlace` writes the children of a bin into
`[start, start + count)` of the other buffer, contiguously and in bucket
order, so the children of disjoint parents are disjoint.

Together these turn every "`O(m)` per bin" into "`O(n)` per level".

**(F2) The tree has `O(n/λ + 1)` nodes.** A bin splits only when
`m > t ≥ λ`, hence `m/λ > 1`, and then it produces

```
s  ≤  ⌈m/λ⌉  <  m/λ + 1  <  2m/λ
```

children (the middle step is the definition of the ceiling; the last uses
`m/λ > 1`). Summing over the splitting bins of one depth and applying F1b,
depth `i+1` holds fewer than `2n/λ` nodes. Level `0` holds
`s_top ≤ ⌈n/λ⌉ ≤ n/λ + 1` nodes. With at most `D + 1` levels,

```
N  <  (D+1)·(2n/λ + 1)  =  O(n/λ + 1)      nodes in total.
```

Empty children are included in this count: `refine` creates a node for
every bucket, occupied or not, and returns immediately for the empty
ones at `O(1)` each.

| phase | per bin | summed | why |
|---|---|---|---|
| `analyze` | — | `Θ(n)` | one pass, once |
| `planPartition` / `planRefinement` | `O(1)` | `O(N)` | fixed arithmetic per node (F2) |
| `countAndPlace` | `O(m + s) = O(m)` | `O((D+1)·n) = O(n)` | two passes over `m` plus `O(s)` bookkeeping; F1b per level, F2 for `s` |
| `scanRange` | `O(m)` | `O((D+1)·n) = O(n)` | one pass per bin that passes the size/depth test — splitting nodes **and** degenerate-range leaves; disjoint per depth (F1b) |
| `detectRun` | `O(m)` | `O(n)` | leaves partition `[0, n)` (F1a) |
| `appendLeaves` | `O(m)` | `O(n + N) = O(n)` | one `memcpy` per leaf (H4) |
| allocation | `O(s)` | `O(n)` | accounted below |

Every row is linear because of F1a/F1b: the work is per *element* per
*level*, and the number of levels is `D + 1`, a constant by H2.

**Every word allocated, under H3.** There are six sources, totalling
`O(n)` words:

| what | when | words |
|---|---|---|
| `bufferA_`, `bufferB_` | once, in `distribute` | `2n` |
| `bucketOfScratch_` | grows only, capped at `n` | `≤ n`, amortised `O(n)` over all growths |
| `writeCursorScratch_` | grows only, capped at `max s ≤ n/λ` | `≤ n/λ` |
| `bucketStart`, `bucketSize` | **two per splitting node**, size `s` each | `Σ 2s ≤ 4n/λ` per level, `≤ 4(D+1)n/λ` total |
| `RefinedRange` tree | one node per bin, plus its `children` vector | `O(N)` |
| `sort`'s own locals | the top-level `bucketStart`, `bucketSize` and the `roots` vector, all sized `s_top` | `O(n/λ + 1)` |

The fourth row is the one H3 exists for: those are local vectors inside
`refine`, so there are `O(N)` separate allocate/release pairs rather
than one big buffer. By F2 the sizes still sum to `O(n/λ)` per depth, so
under H3 their total cost is `O(n)` (F1b). The two growth-only scratch vectors need no amortisation argument at all,
and invoking one would silently import a library detail (the standard
guarantees amortised `push_back`, not a growth policy for `resize`). The
stronger fact: the first call that reaches the histogram path is always
the top-level one, with the largest `count = n` and the largest bucket
array `s_top ≥ ⌈m/λ⌉` of the whole run. So each scratch vector is resized
**at most once per `sort()`**, straight to its final size.

**The other two build configurations.** With `STRATUM_ENABLE_METRICS` the
instrumentation adds `O(1)` work per counted event — a counter increment,
a `push_back`, or a lookup in a map with at most five fixed keys — and the
events are one per comparison, per leaf and per split, all of which are
`O(n)` by the table above. Its auxiliary vectors (`binSizes_`,
`subdivisionRecords_`) grow to `O(N)` entries. So the research
build is also `Θ(n)` in time and space; its constant is several times
larger, which is why its clock must never be quoted. Assertions
(`NDEBUG` off) add `O(1)` per element in `countAndPlace` and one `O(N)`
tiling check per sort: also `Θ(n)`.

One detail specific to release: the third argument of the `noteSplit`
call in `refine` is `*std::max_element(bucketSize...)`, which is `O(s)`.
In release `noteSplit` has an empty body, and a compiler is *permitted*
to elide the argument under the as-if rule — but the language does not
oblige it to. Either way the cost is `O(s)` per split, hence `O(n)`
overall, so linearity does not depend on the elision happening.

### 8.6 Cost of the local sorts — the only place `log` appears

This is the step that could break linearity, so it is worth separating
carefully.

**First, why an expensive leaf has `σ ≥ 1`.** `sortLeaf` runs a
comparison sort only when `detectRun` returns `Unsorted`, which requires
some adjacent pair with `buf[i] < buf[i−1]`, hence two distinct values,
hence `σ ≥ 1`. A leaf with `σ = 0` is either flagged `sorted` by `refine`
(`O(1)`), or — if it left `refine` through **either** disjunct of the
`count ≤ t || depth ≥ D` test, so that its range was never scanned — is
recognised by `detectRun` as ascending (`O(m)`). Either way it reaches no
comparison sort. This is what licenses `σ_D ≥ 1` in §8.4.

A leaf of size `m` is finished by one of:

| branch | condition | worst-case comparisons |
|---|---|---|
| certified sorted | `σ = 0` | `O(1)` to consume — the `O(m)` that produced the certificate is the `scanRange` row of §8.5 |
| ascending / descending run | `detectRun` | `O(m)` |
| `insertionSort` | `m ≤ L₁ = 64` | `≤ m(m−1)/2 ≤ (L₁/2)·m = 32m` |
| `quickSort` | `L₁ < m ≤ L₂ = 384` | `≤ m²/2 + O(m) ≤ (L₂/2 + c₀)·m` |
| `introSort` | `m > L₂` | `≤ c · m log₂ m` |

**Where `32` and `192` come from.** Both are `⌈L/2⌉` for the branch's own
upper limit `L`, and both are pure algebra, not measurements:

- Insertion sort performs at most `Σ_{i=1}^{m−1} i = m(m−1)/2 < m²/2`
  comparisons. The branch is entered only for `m ≤ L₁`, so
  `m²/2 = (m/2)·m ≤ (L₁/2)·m = 32m`.
- Quicksort's worst case is `Θ(m²)`. Bounding the partition scans gives at
  most `m²/2` comparisons, plus an `O(m)` term: `partition` charges three
  for the median-of-three and two per loop exit, and there are at most
  `Θ(m)` partition calls and sub-cutoff insertion sorts. The branch is
  entered only for `m ≤ L₂`, so `m²/2 ≤ (L₂/2)·m = 192m` and the total is
  `≤ (192 + c₀)·m` for an absolute constant `c₀`. The `O(m)` term must not
  be silently dropped — `m²/2 + O(m) ≤ 192m` does **not** follow from
  `m²/2 ≤ 192m` — but it changes nothing, because `c₀` is `n`-free.

The point is that `L₁` and `L₂` are **dispatch thresholds fixed in
`Config.hpp`, independent of `n`, of `λ` and of `t`**. A quadratic
algorithm run on inputs of bounded size is linear in the input size, with
a constant set by that bound. So the first four rows are `O(m)` with
`n`-free constants, and F1 sums them to `O(n)` immediately.

The `introSort` row is the interesting one. A single leaf genuinely costs
`Θ(m log m)`; there is no way around that, and the algorithm does not
pretend otherwise. What saves linearity is the *aggregate*:

```
Σ_leaves  c · mᵢ log₂ mᵢ   ≤   c · (max log₂ mᵢ) · Σ mᵢ   =   c · n · log₂ m_max
```

by F1. So the total is linear **if and only if the largest expensive leaf
is bounded independently of `n`** — which is exactly what §8.4 supplies.
Every expensive leaf comes from one of the two exits of the same `if`
in `refine`, so its size is at most

```
max( t,  M )  =  max( t,  λ·2^(w/(D+1)) )  =  max(64, 18 090)  =  18 090
```

Hence `log₂ max(t, M) ≤ 14.15`. Note it is the *global supremum* `M` that
appears here, not `B(n)`: linearity needs one `n`-free number, and `M` is
it.

Combining every row, the total local-sorting cost is at most

```
max( 2,  32,  192 + c₀,  c·log₂ max(t, M) ) · n
```

— the maximum over the branches, not the `introSort` row alone. Each entry
is `n`-free, so the whole of §8.6 is `O(n)`.

**Comparisons versus time.** The table counts comparisons; §8.9 sums it as
time. Under H3 the two differ by at most a constant factor per leaf: every
algorithm here performs `O(1)` moves and index operations per comparison,
plus `O(m)` overhead, so `time = O(comparisons + m)` and the `O(m)` term is
absorbed by F1a.

**The distinction the reader should carry away:** an individual leaf is
`O(m log m)`, the algorithm is `O(n)`, and the bridge between the two is a
bound on `m` that does not involve `n`.

### 8.7 A tempting strengthening that is FALSE

It is natural to argue: *the surviving child must contain `m` elements
inside a window of `W` values, so it needs `≈ m` distinct values, so
`σ ≥ s·m` rather than `σ ≥ s`.* That yields the sharper-looking

```
m  ≤  λ · (2^w / n)^(1/(D+2))          ← NOT TRUE
```

**The error is the word "distinct".** A bin holds a *multiset*. Nothing in
`refine` forbids duplicates, and a bin of `m` elements with many repeats
can be far narrower than `m`. The only lower bound on `σ` the code
actually guarantees is the one in Lemma 3 — `σ ≥ ⌈m/λ⌉`, from the cap not
binding — which is weaker by a factor of `λ`. Substituting `σ ≥ s·m` where
only `σ ≥ s` is available is where the extra `2` in `D+2` comes from.

**And it is false, not merely unproven — here is the counterexample.**
Take `n = 10⁶`, `T = int64_t`, the shipped `λ = 32`, `t = 64`, `D = 6`.
Writing offsets from the global minimum, the input is the multiset

```
 2048 elements alternating  1, 0, 1, 0, …      (1024 of each value)
    6 singletons at         65¹, 65², …, 65⁶
997946 filler elements all at  σ_g = 31250 · 65⁶ = 2 356 840 332 031 250
```

Trace it. The top level takes `s_top = ⌈10⁶/32⌉ = 31250`; the cap does not
bind, so `W_top = ⌊σ_g/31250⌋ + 1 = 65⁶ + 1`. Bucket `0` covers offsets
`[0, 65⁶]` and therefore collects exactly the 2054 special elements; the
filler lands in bucket `31249` as one all-equal, certified bin.

Now refine that bucket. At level `i` it holds `m = 2048 + (6−i)` elements
with `σ = 65^(6−i)`:

| `i` | `m` | `σ` | `s = ⌈m/32⌉` | cap binds? | `W = ⌊σ/s⌋+1` | bucket of the top element |
|---|---|---|---|---|---|---|
| 0 | 2054 | 65⁶ | 65 | no (`σ ≥ s`) | 65⁵+1 | 64 |
| 1 | 2053 | 65⁵ | 65 | no | 65⁴+1 | 64 |
| 2 | 2052 | 65⁴ | 65 | no | 65³+1 | 64 |
| 3 | 2051 | 65³ | 65 | no | 65²+1 | 64 |
| 4 | 2050 | 65² | 65 | no | 65+1 | 64 |
| 5 | 2049 | 65 | 65 | no (`65 < 65` is false) | 2 | 32 |

Every level peels exactly one singleton — Lemma 5 with equality — and
`⌈m/32⌉ = 65` throughout because `m` stays in `[2049, 2054]`. At depth
`6` the surviving bin holds the 2048 alternating elements with `σ = 1`,
hits `depth ≥ D`, and leaves `refine` unscanned. `detectRun` sees
`1, 0, 1, 0, …` and returns `Unsorted`; `2048 > L₂ = 384`, so it goes to
`introSort`.

So `m = 2048` while the strengthened bound claims `m ≤ 1 457`. The bound
is false. The real theorem is untroubled: `(m/λ)^D = 64⁶ ≈ 6.87·10¹⁰` sits
below `σ₀ = 65⁶ ≈ 7.54·10¹⁰`, and `2048 < B(10⁶) ≈ 9 268`.

Every row above is integer arithmetic, so the construction is checkable by
hand; nothing in this subsection rests on running anything.

Nothing above depends on this subsection: §8.4 never uses the false
claim. It is recorded because the two bounds differ only in a subscript,
and because the wrong one *looks* sharper.

### 8.8 What each parameter is actually doing

| | role in the proof | role in the constant |
|---|---|---|
| **`w`** | Makes `M` **finite**. It sets the total bit budget, and `M` is exponential in `w/(D+1)`. It does **not** appear in the `n`-dependence. | `w = 64` → `M ≈ 18 090`. A 128-bit key would give `≈ 10⁷` — still `O(1)` in `n`, but useless in practice. Hence H1. |
| **`λ`** | Sets the fan-out `s = ⌈m/λ⌉`, hence the bits spent per level. Enters `B(n)` as `λ^((D+1)/D)` and `M` as `λ`. | Also sets typical leaf size, so `≈ (λ+1)/4` comparisons per element on average. |
| **`D`** | **Not needed for termination** (Lemma 5 gives that) and **not needed for linearity** (see below). It bounds the number of passes at `D + 1` instead of `w + 1`. | Trades passes for residual: smaller `D` → fewer passes, larger `M`. |
| **`t`** | Bounds leaves that exit *by size* at `m ≤ t`. Does **not** enter `B(n)` or `M`; it enters the final bound only through `max(t, M)` in §8.6. | For `t ≤ L₁` the branch is insertion sort and the constant is `t/2` — `32` at `t = 64`. For a larger fixed `t` it is quicksort or introsort, still `O(1)` per leaf under H2, with constant `max(t/2, c·log₂ t)`. |

**On `D`, explicitly**, because the claim "`D` is what makes it linear" is
a natural one and it is wrong — provided `D ≥ 1` (H2; at `D = 0` there is
no refinement at all and the claim reverses, see §8.0). The argument below
runs *upward*, towards larger `D`, not downward towards zero.

First, `s ≥ 2` always. If the cap does not bind, `s = ⌈m/λ⌉ ≥ 2` because a
bin only splits when `m > t ≥ λ`; if it binds, `s = σ + 1 ≥ 2` because
`planRefinement` is only reached with `σ ≥ 1`. (The `assert(binCount >= 2)`
in the code records this; it does not establish it, and it does not run in
the release build this proof describes.)

Now suppose the depth cap were removed entirely. Lemma 1 with `s ≥ 2` gives `σᵢ₊₁ ≤ ⌊σᵢ/2⌋`, so `σ_k ≤ (2^w−1)/2^k` and
the span reaches `0` after at most `w` levels: **the depth is bounded by
`w` with no cap at all.** Refinement would then stop only at `m ≤ t` or
`σ = 0`, so every expensive leaf would satisfy `m ≤ t`, and the total
would be

```
O((w+1)·n)  +  O((t/2)·n)   =   O(65n) + O(32n)   =   Θ(n)
```

**Still linear.** Capping at `D = 6` cuts the passes from `w+1 = 65` to
`D+1 = 7` and pays for it with `M = 18 090` instead of `t = 64`. Both
settings are `Θ(n)` with different constants; `D` is a constant-factor
decision that measurement made, not an asymptotic necessity.

### 8.9 Total

Summing §8.5 and §8.6, with `D`, `λ`, `t`, `w` constants by H1–H2:

```
T(n)  =  O(n)  +  O((D+1)·n)  +  O(n·log₂ m_max)  =  Θ(n)
```

`Θ` and not just `O`, since `analyze` alone reads every element.

**What "linear" means here.** The constant contains `w`, through the
depth bound and through `m_max`. This is linear in exactly the sense radix
sort is linear: the key width is a fixed parameter of the type, not a
function of `n`. It is not a bound in the comparison model and does not
contradict `Ω(n log n)`, because the algorithm does arithmetic on keys.

### 8.10 Adversarial review of this proof

*What would an adversary have to build to make a leaf grow with `n`?*

By §8.4 they need `(m/λ)^D ≤ σ₀` with `m` growing, so they need `σ₀` to
grow with `n`. There are four ways to attack, and each is closed by a
named invariant:

1. **Enlarge `σ₀`.** Blocked by Lemma 4, Case C: `σ₀ ≤ ⌊σ_g/⌈n/λ⌉⌋` with
   `σ_g ≤ 2^w−1` under H1. Growing `n` *shrinks* `σ₀`. The adversary would
   need `w` to grow with `n` — which is what H1 forbids and what the
   `sizeof(T) ≤ 8` `static_assert` enforces.
2. **Avoid paying bits at some level.** Blocked by Lemma 3: the only way to
   split without contracting the span by `⌈m/λ⌉` is to trigger the cap, and
   Lemma 2 makes every descendant span-`0`, hence cheap (no comparison sort).
3. **Escape through the top-level special cases.** Blocked by Lemma 4.
   `s_top = 1` has two sub-cases that must not be conflated: A1 (`n ≤ λ`,
   so `refine` never splits) and A2 (`σ_g = 0`, where `n` is **unbounded**
   but every span is `0`). Case B, a binding top-level cap, makes every
   top bin span-`0`. None can produce an expensive leaf.
4. **Exploit duplicates.** This is the attack that breaks the *false*
   bound of §8.7, and it is exactly why the real proof never counts
   distinct values. Lemma 3's `σ ≥ ⌈m/λ⌉` holds for multisets.

An adversary can still saturate the bound — a construction reaching
`2 048` at `n = 10⁶` exists, against a proven ceiling of `B(10⁶) ≈ 9 268` —
but the ceiling itself falls as `n^(−1/D)`. **No construction can make a
span-positive leaf grow with `n` while H1 and H2 hold.**

The honest residual risk is not in the argument but in its hypotheses.
**H3 (unit-cost RAM) hides the memory hierarchy**, and on real hardware
that gap is measurable: on inputs spanning the whole key universe the
wall-clock growth exponent exceeds `1` while the *comparison* exponent
stays at `1.000`. The extra cost is cache behaviour, not work, which is
why §3.1 derives a lower bound on `λ` from the L2 capacity. No `O(·)`
statement describes it, and none claims to.

## 9. Local sorting

A leaf is finished by:

1. One `O(k)` scan detecting whether it is already fully ascending
   (nothing to do) or fully descending (one reversal).
2. Otherwise a comparison sort chosen by size: Insertion Sort up to 64,
   QuickSort up to 384, Introsort above.

**The dispatch thresholds describe the local sorts and nothing else.**
They must not be derived from `t`. Which algorithm suits a range of `k`
elements is a property of the algorithms; the rule that decided to stop
refining is a property of the partitioning. Tying them together means
changing one silently changes the other — a defect this project shipped
for several versions.

**Why the run detection exists.** Real data often arrives partly ordered,
and after distribution a bin's contents are frequently already in order.
`O(k)` to check is cheap insurance against `O(k²)`.

**Why it makes the sort unstable.** Reversing a descending run swaps
elements that would compare equal under a weaker ordering. Harmless for
integer keys, where equal elements are indistinguishable — but it is the
reason this cannot be extended to key/value pairs as written.

## 10. The join

By Property 4 each leaf already occupies its final position. The join is
therefore not a merge and does not even need a write cursor: each leaf is
copied to exactly its own offset. The only question per leaf is which of
the two buffers it currently lives in.

**The cascading buffers.** A bin at even refinement depth lives in buffer
A, at odd depth in buffer B; a split reads one and writes the other at the
*same absolute offsets*. That is what keeps a leaf's position meaningful
regardless of how deep it was refined.

**Use a block copy, not an element loop.** The element loop that was here
originally could not be vectorised: source and destination are both
`vector<T>&` and the compiler cannot rule out aliasing, so it emitted a
scalar loop with a dependency between each store and the next load —
measured at 15.7 GB/s against a machine capable of far more. A block copy
is valid without any runtime check, because the three vectors involved are
distinct objects, the element type is trivially copyable, and the
destination range is exactly the leaf's own range by Property 4.
Replacing that one loop was worth **17–42%** on three datasets.

## 11. Design decisions, and what was rejected

The instructive half. Each of these was implemented, measured, and
discarded.

### Rejected: removing the depth cap

**Idea.** Refinement terminates on its own (Property 3), so the depth cap
is unnecessary; without it, no leaf larger than `t` ever reaches a
comparison sort, and the `Θ(n log n)` worst case disappears.

**Measured.** The mechanism works exactly as claimed — zero fallbacks,
maximum leaf size drops to exactly `t`. And it costs **up to +109%**. A
sweep over seven adversary shapes found the uncapped version never wins.

**Why.** The premise was wrong. The bit budget already bounds the residual
to a constant (§8), so the `Θ(n log n)` term the change was removing does
not exist at these constants. The cap is not a compromise; it is load
bearing.

### Rejected: requiring balanced splits

**Idea.** A split that leaves 999 999 elements in one child and 1 in the
other has made no progress. Require every split to shrink the largest
subproblem by a constant factor.

**Rejected on two independent grounds, before implementing.**

*It is undesirable.* Progress by size gives depth `log_{1/α}(n/t) =
Θ(log n)` and total cost `Θ(n log n)` — precisely the barrier this
algorithm avoids. Progress by *range* gives depth `w/log₂ s`, independent
of `n`. Those are the only two progress measures available and they are
not interchangeable.

*It is unachievable.* For any splitting rule that depends only on the
bin's observed range and size, the boundaries are fixed before the
interior is examined. By pigeonhole some interval has width `≥ span/s`;
place all but two elements inside it and the largest child holds `m−2`.
Randomising the boundaries does not help: a cluster of diameter `δ` much
smaller than the interval width lands in one bucket for *any* offset. The
problem is resolution, not alignment.

And the motivating example is not pathological anyway: the 999 999-element
child immediately recomputes its own range, which is now tight, and splits
cleanly. It costs one extra pass, and the bit budget bounds how often that
can repeat.

### Rejected: retuning the fused parameter

Before λ and `t` were separated, a sweep of the single fused parameter over
`{8 … 256}` found the curve flat between 24 and 64 with the incumbent
within 2% of optimal. The historical claim that a much smaller value won
by 18–23% **did not reproduce** on this machine — at 16 it was 13%
*slower*. The two arms of the curve are explained: below, the scatter
working set exceeds L2; above, Insertion Sort's quadratic term takes over.

### Rejected: several constant-factor changes

- **Eliminating unreachable buckets** in the refinement: provably
  behaviour-neutral, removes 100% of the unreachable buckets, **no
  measurable time effect**. Kept anyway as dead-work removal at zero risk.
- **Using the input array as one of the two buffers**: would halve the
  auxiliary memory, but the copies it avoids are exactly the ones that do
  not happen on the datasets where the join is expensive. Ceiling measured
  at ~5% on one dataset.
- **Eliminating per-call allocations**: ceiling measured at 5.8–6.2% on
  four datasets and ~0% on five. At the noise threshold; not done.

### The pattern

Four separate times in this project, a change was justified by an
asymptotic argument that turned out to be irrelevant at the actual
constants, or by a measurement that turned out to be measuring the wrong
configuration. The habits that caught them:

- Measure the terrain **before** writing the acceptance criterion.
- Write the criterion against the **hypothesis**, not against whatever the
  available instrument happens to produce.
- Always require two things: that the mechanism does what it claims, and
  that it actually helps.
- Compare by alternating the two versions in one session; separately
  collected batches produced a false 8.8% result.
- Prefer deterministic counters to the clock, and always state which build
  configuration a timing came from.

## 12. Reimplementation checklist

Everything needed, in order:

1. `span = max − min`, never `max − min + 1`.
2. `binCount = ceil(n/λ)`, capped at `span + 1`; `width = span/binCount + 1`;
   normalise `width` to 1 when `binCount = 1`.
3. `bucketOf(v) = (v − min)/width` — no clamping; assert it is in range.
4. Two-pass count-and-place, advancing a per-bucket write cursor.
5. Refine a bin when `count > t` and `depth < D` and its own span is
   non-zero, using **its own** min and max.
6. Mark zero-span bins as already sorted and skip them entirely.
7. Two cascading buffers; a split writes the other one at the same
   absolute offsets.
8. Local sort by size, with thresholds independent of `t`.
9. Join by copying each leaf to its own offset — no cursor, no merge.
10. Assert: index in range, `width ≥ 1`, `splits ≥ 2`, leaves tile `[0,n)`.

If your implementation is correct, the leaves will tile `[0, n)` exactly.
If they do not, one of steps 2, 4 or 7 is wrong.
