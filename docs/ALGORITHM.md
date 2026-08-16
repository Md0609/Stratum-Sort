# Dynamic Range Sort — complete technical description

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

`D` looks like a safety net that costs asymptotic quality. It is the
opposite: it is what makes the worst case linear, and §8 derives why.

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
[`BENCHMARKS.md`](BENCHMARKS.md) validate it; they do not establish it.
Where a step rests on an assumption, the assumption is named.

### 8.0 Hypotheses

The result is `Θ(n)` **under these hypotheses, all of which are enforced
or checkable**:

| | |
|---|---|
| **H1** | `T` is an integral type of width `w ≤ 64` bits. Enforced by three `static_assert`s (integral, not `bool`, `sizeof(T) ≤ 8`). |
| **H2** | `λ`, `t`, `D` are fixed constants, chosen independently of `n`. |
| **H3** | Unit-cost RAM: arithmetic on `uint64_t` and `size_t`, and one array access, are `O(1)`. |
| **H4** | `memcpy` of `k` elements costs `Θ(k)`. |

H1 is not cosmetic. Before it was enforced, `DynamicRangeSort<__int128>`
compiled — `std::is_integral<__int128>` is true on common toolchains — and
overflowed the heap, because the offset arithmetic is carried in
`uint64_t`. The proof below needs `w` bounded; the code now needs it too.

Throughout: `n` = element count, `w` = key width in bits, `λ` = target
occupancy, `t` = leaf threshold (`t ≥ λ`, enforced by the constructor),
`D` = `MAX_SUBDIVISION_DEPTH`. For a bin `B`: `m = |B|` is its size and
`σ = max(B) − min(B)` its **observed span** (§4). `s` is the number of
buckets a split produces and `W` their common width.

### 8.1 The three structural lemmas

**LEMMA 1 (span contraction).** *If a bin of span `σ` is split into `s`
buckets of width `W = ⌊σ/s⌋ + 1`, every child has span `σ′ ≤ W − 1 = ⌊σ/s⌋`.*

Bucket `b` covers exactly the `W` consecutive values
`[origin + bW, origin + (b+1)W − 1]`, so any two elements inside it differ
by at most `W − 1`. ∎

**LEMMA 2 (a binding cap ends the subtree for free).** *If the fan-out cap
binds — that is, `σ < ⌈m/λ⌉`, so the code sets `s = σ + 1` — then every
child has span exactly `0`.*

`W = ⌊σ/(σ+1)⌋ + 1 = 0 + 1 = 1`, so by Lemma 1 every child has `σ′ ≤ 0`.
A bin of span `0` is detected by `refine` (`observed.minimum ==
observed.maximum`), flagged `sorted`, and **costs zero comparisons** no
matter how large it is. ∎

This lemma is what makes the whole argument work, and it is easy to miss:
the cap is not a special case to be handled, it is an *escape hatch that
costs nothing*. A bin can only remain expensive by never triggering it.

**LEMMA 3 (an expensive split has a non-binding cap).** *If a leaf has
span `> 0` and was produced by depth exhaustion, then every one of its `D`
ancestor splits had `σᵢ ≥ ⌈mᵢ/λ⌉`, hence `sᵢ = ⌈mᵢ/λ⌉`.*

Contrapositive of Lemma 2: had any ancestor's cap bound, this leaf's span
would be `0`. ∎

### 8.2 Where the bit budget comes from, and why the top level spends some

Two distinct splits happen:

1. **One top-level split** in `distribute`, over the whole array, with
   `s_top = ⌈n/λ⌉` buckets — *this is the term the old bound omitted.*
2. **At most `D` refinement splits**, since `refine` is entered at
   `depth = 0` and stops at `depth ≥ D`.

So a root-to-leaf path has at most `D + 1` splits, not `D`.

**The origin of the `2^w/n` term.** Let `σ_g ≤ 2^w − 1` be the global
span. If the top-level cap binds, Lemma 2 makes the entire sort free, so
assume it does not. Then by Lemma 1 every top-level bin has span

```
σ₀  ≤  ⌊σ_g / s_top⌋  ≤  σ_g · λ / n  <  2^w · λ / n
```

That is the whole content of the `2^w/n` factor: **the top-level split has
already spent `log₂(n/λ)` of the `w` available bits before refinement
starts.** A larger array forces a finer top-level grid, which leaves less
span for refinement to consume — and therefore a *smaller* residual.

### 8.3 Why the surviving child cannot keep everything

Claimed separately because it is a genuinely different fact, and because
one plausible-looking strengthening of it is **false** (§8.7).

**LEMMA 4 (strict peeling).** *If a split has `σ ≥ 2` and a non-binding
cap, every child is a proper subset: `m′ ≤ m − 1`.*

`min(B)` has offset `0`, so bucket `0`. `max(B)` has offset `σ`, so bucket
`⌊σ/W⌋`. Since `s ≥ 2`, `W = ⌊σ/s⌋ + 1 ≤ σ/2 + 1 ≤ σ` for `σ ≥ 2`, hence
`⌊σ/W⌋ ≥ 1`. The two extremes land in different buckets. ∎

(For `σ = 1` the cap binds — `⌈m/λ⌉ ≥ 2 > 1 = σ` whenever `m > λ` — so
Lemma 2 applies and the subtree is free.)

Lemma 4 guarantees progress, so refinement terminates without `D`. But
note what it does **not** give: losing one element per level over `D`
levels leaves `m − D` elements. **Lemma 4 alone does not bound `m`.** The
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

The leaf has span `> 0`, so `σ_D ≥ 1`, giving `(m/λ)^D ≤ σ₀`. Substituting
`σ₀ < 2^w λ / n` from §8.2:

```
(m/λ)^D  <  2^w · λ / n      ⟹      m^D  <  λ^(D+1) · 2^w / n
```

Together with the trivial `m ≤ n`, that is the first form. For the second,
`min(n, (K/n)^(1/D))` with `K = λ^(D+1) 2^w` is maximised where the two
branches meet, at `n^(D+1) = K`, giving

```
m  ≤  K^(1/(D+1))  =  λ · 2^(w/(D+1))          ∎
```

**With the shipped constants** (`λ = 32`, `w = 64`, `D = 6`):

```
m  ≤  32 · 2^(64/7)  ≈  18 093        for every n
m  ≤  9 270                            at n = 10⁶
m  ≤  6 300                            at n = 10⁷
```

The bound **shrinks as `n` grows**. That is the opposite of what an
`n log n` term would do, and it is the single most important consequence
of §8.2.

### 8.5 Cost of each phase

Two facts are used repeatedly. **(F1)** At every level the bins partition
`[0, n)` (Property 4), so the sizes at one level sum to `n`. **(F2)** A
bin only splits if `m > t ≥ λ`, so `s ≤ ⌈m/λ⌉ < m/λ + 1 < 2m/λ`; summing
over a level, each level has at most `2n/λ` nodes, and there are at most
`D + 1` levels, so the tree has

```
N  ≤  2(D+1)·n/λ  =  O(n/λ)      nodes in total.
```

| phase | per bin | summed | why |
|---|---|---|---|
| `analyze` | — | `Θ(n)` | one pass, once |
| `planPartition` / `planRefinement` | `O(1)` | `O(N) = O(n/λ)` | fixed arithmetic per node |
| `countAndPlace` | `O(m + s) = O(m)` | `O((D+1)·n) = O(n)` | two passes over `m` plus `O(s)` bookkeeping; F1 per level, F2 for `s` |
| `scanRange` | `O(m)` | `O((D+1)·n) = O(n)` | one pass per splitting node |
| `detectRun` | `O(m)` | `O(n)` | leaves partition `[0, n)` |
| `appendLeaves` | `O(m)` | `O(n + N) = O(n)` | one `memcpy` per leaf (H4) |
| scratch growth | — | `O(n)` | `bucketOfScratch_` only grows, capped at `n` |

Every row is linear because of F1: the work is per *element* per *level*,
and the number of levels is `D + 1`, a constant by H2.

### 8.6 Cost of the local sorts — the only place `log` appears

This is the step that could break linearity, so it is worth separating
carefully.

A leaf of size `m` is finished by one of:

| branch | condition | worst-case cost |
|---|---|---|
| certified sorted | `σ = 0` | `O(1)` |
| ascending / descending run | detected by `detectRun` | `O(m)` |
| `insertionSort` | `m ≤ 64` | `≤ m²/2 ≤ 32m` |
| `quickSort` | `64 < m ≤ 384` | `O(m²) ≤ 192m` |
| `introSort` | `m > 384` | `≤ c · m log₂ m` |

The first four rows are `O(m)` **with a constant that does not depend on
`m`**, because `m` is itself bounded by a constant in each. Summed over
the leaves, F1 gives `O(n)` immediately.

The `introSort` row is the interesting one. A single leaf genuinely costs
`Θ(m log m)`; there is no way around that, and the algorithm does not
pretend otherwise. What saves linearity is the *aggregate*:

```
Σ_leaves  c · mᵢ log₂ mᵢ   ≤   c · (max log₂ mᵢ) · Σ mᵢ   =   c · n · log₂ m_max
```

by F1. So the total is linear **if and only if `m_max` is bounded
independently of `n`** — which is exactly the Theorem of §8.4. With
`m_max ≤ 18 093`, `log₂ m_max ≤ 14.15`, and the whole comparison-sorting
effort of the algorithm is at most `≈ 14c·n`.

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

**It is refuted by this project's own data.** It predicts `m ≤ 1 457` at
`n = 10⁶`; the adversary sweep produces a leaf of **2 048 elements with
span > 0** reaching `introSort` at that size. The error: the argument
assumes the bin's elements occupy distinct values. Nothing forbids
duplicates, and a bin with many duplicates can be far narrower than its
size. The only lower bound the invariants actually give is `σ ≥ ⌈m/λ⌉`,
which is what §8.4 uses.

Recorded because the correct bound and the false one differ only in a
subscript, and because the false one *looks* better.

### 8.8 What each parameter is actually doing

| | role in the proof | role in the constant |
|---|---|---|
| **`w`** | Makes the residual **finite**. It sets the total bit budget; the bound is exponential in `w/(D+1)`. It does **not** appear in the `n`-dependence. | `w = 64` → `m_max ≈ 18 093`. A 128-bit key would give `≈ 10⁷` — still `O(1)` in `n`, but useless in practice. Hence H1. |
| **`λ`** | Sets the fan-out `s = ⌈m/λ⌉`, hence the bits spent per level. Appears as `λ^((D+1)/D)`. | Also sets typical leaf size, so `≈ (λ+1)/4` comparisons per element on average. |
| **`D`** | **Not needed for termination** (Lemma 4 gives that) and **not needed for linearity**. It bounds the number of passes at `D + 1` instead of `w + 1`. | Trades passes for residual: smaller `D` → fewer passes, larger `m_max`. |
| **`t`** | Bounds leaves that exit *by size* at `m ≤ t`, so their insertion sort costs `≤ t/2` per element. Does **not** enter the residual bound. | `t = 64` → `≤ 32` comparisons per element worst case in that branch. |

**On `D`, explicitly**, because the earlier version of this document had it
backwards. Without any depth cap, Lemma 1 with `s ≥ 2` gives `σᵢ₊₁ ≤ σᵢ/2`,
so the depth is at most `w` and refinement stops only at `m ≤ t` or
`σ = 0`: the residual becomes `t = 64` and the cost is `O((w+1)·n) = O(65n)`.
**Still linear.** Capping at `D = 6` cuts the passes from 65 to 7 and pays
for it with a residual of up to `18 093` instead of `64`. Both settings are
`Θ(n)`; `D` is a constant-factor decision, and the measurements in
[O8](history/O8_resolucion_y_reversion_paso3.md) are what chose it — not
an asymptotic argument.

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
grow with `n`. There are exactly three ways to attack, and each is closed
by a named invariant:

1. **Enlarge `σ₀`.** Blocked by §8.2: `σ₀ ≤ ⌊σ_g/⌈n/λ⌉⌋` and `σ_g ≤ 2^w−1`
   under H1. Growing `n` *shrinks* `σ₀`. The adversary would need `w` to
   grow with `n` — which is what H1 forbids, and what the `sizeof(T) ≤ 8`
   `static_assert` now enforces.
2. **Avoid paying bits at some level.** Blocked by Lemma 3: the only way to
   split without contracting the span by `⌈m/λ⌉` is to trigger the cap, and
   Lemma 2 makes every descendant span-`0`, hence free.
3. **Skip the top-level split.** It always runs. If its cap binds, Lemma 2
   makes the entire sort free.

An adversary can still saturate the bound — the sweep reaches `2 048`
against a proven ceiling of `9 270` at `n = 10⁶` — but the ceiling itself
falls as `n^(−1/D)`. **No construction can make a span-positive leaf grow
with `n` while H1 holds.**

The honest residual risk is not in the argument but in its hypotheses: H3
(unit-cost RAM) hides the memory hierarchy, and the wall-clock exponent on
`FullRangeExtremes` is `1.08` while its *comparison* exponent is `0.9998`.
That gap is cache behaviour, and it is the reason `λ` carries a
cache-derived lower bound in [`Config.hpp`](../include/drs/Config.hpp) —
a real effect on real machines that no `O(·)` statement describes.

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
