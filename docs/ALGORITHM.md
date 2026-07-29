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

This is the part that is easy to get wrong, and this project got it wrong
twice before measuring it.

### The bit budget

Each refinement level consumes `log₂(s)` bits of the bin's observed span,
where `s = ceil(m/λ)` for a bin of size `m`. The span has at most `w` bits.
So along any root-to-leaf path:

```
Σ log₂(sᵢ)  ≤  log₂(span₀)  ≤  w
```

With `s ≥ 2` this bounds the depth by `w` even with no cap. **The depth is
bounded by the key width, not by `n`.** That is the structural difference
from a comparison-based divide and conquer, whose depth is `log n` by
construction.

### The residual handed to a comparison sort

A leaf larger than `t` can only arise by exhausting the depth cap. For a
bin of size `m` to survive `D` degenerate levels, the bit budget requires

```
D · log₂(m / λ)  ≤  w        ⟺        m  ≤  λ · 2^(w/D)
```

With λ = 32, w = 64, D = 6 that is **m ≤ ~52 000 — a constant independent
of `n`.**

Therefore the aggregate cost of every comparison sort the algorithm ever
performs is at most `n · log₂(52 000) ≈ 16n`. It is linear.

**Measured confirmation.** On the adversarial dataset built specifically
to exhaust the depth cap, the comparison-count exponent fitted over three
orders of magnitude of `n` is **0.994 [0.993, 0.995]**. If the cap
produced an `n log n` term the exponent would grow with `n`. It does not.

### The hard constraint this creates

The bound scales with λ. Raising λ to 1024 makes `m_max ≈ 1.7·10⁶`, no
longer small compared to a realistic `n`, and a `Θ(n log n)` term
reappears. **Any change to λ must re-check this bound.** Raising `t` does
not affect it.

### Total cost, with constants

Per element, for the shipped configuration:

| phase | cost per element |
|---|---|
| analyse | 1 read |
| distribute | ~4 accesses |
| refine | ~4 accesses × depth (≤ 6) |
| run detection | ≤ 1 read |
| local sort | `(λ+1)/4 ≈ 8` average; `≤ t/2 = 32` worst |
| join | 1 read + 1 write |

Best case `Θ(n)` with constant ≈ 6 (all keys equal: one pass, zero
comparisons). Average `Θ(n)` with constant ≈ 20. Worst case `Θ(n)` with
constant ≈ 44–66 depending on which local sort dominates.

**What "linear" means here.** The constant contains `w` through the depth
bound and through the residual. It is linear treating the key width as a
constant — exactly the sense in which radix sort is linear. It is not a
bound in the comparison model and does not contradict `Ω(n log n)`,
because the algorithm does arithmetic on keys.

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
