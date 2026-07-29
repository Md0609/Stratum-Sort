# Dynamic Range Sort

A linear-time sorting algorithm for integer keys, written as a reference
implementation: every constant is justified, every invariant is asserted,
and every performance claim is measured rather than argued.

```cpp
#include "drs/DynamicRangeSort.hpp"

std::vector<int64_t> data = /* ... */;
drs::DynamicRangeSort<int64_t> sorter;
sorter.sort(data);          // in place, ascending
```

---

## The problem

Comparison sorts cannot beat `Ω(n log n)`, and `std::sort` is an
excellent implementation of that bound. But a comparison sort deliberately
ignores something it is allowed to use: for integer keys you can do
*arithmetic* on the values, not just compare them. Knowing that a value is
`517` tells you roughly where it belongs in the output; knowing only that
it is "greater than 340" does not.

Counting sort and radix sort exploit that, but both are rigid: counting
sort needs a small value range, radix sort processes fixed digit
positions regardless of how the data is actually distributed. Neither
adapts to the shape of the input.

**Dynamic Range Sort turns a global ordering problem into a collection of
local ones, using a partition derived only from the observed range of the
data itself.** Where the data is dense it cuts finely, where it is sparse
it does not bother — and it decides that per region, from that region's
own extremes.

## The idea in one paragraph

Look at the data once to find its minimum and maximum. Cut that range
into equal-width intervals, sized so that a typical interval receives
about λ elements, and drop every element into its interval in one pass.
Intervals are ordered by construction: everything in interval *j* is at
most everything in interval *j+1*. Any interval that came out too
crowded gets split again — but using **its own** minimum and maximum, not
the global ones, so the resolution adapts to local density. When an
interval is small enough, sort it with an ordinary comparison sort.
Finally, concatenate. No merge step is needed: the pieces were already in
the right order and, as it turns out, already in the right place.

## Why it is linear

Each refinement level consumes at least one bit of the interval's
*observed span*, and a key only has `w` bits. So the recursion depth is
bounded by the key width, not by `n` — which is the whole difference
between this and a divide-and-conquer comparison sort, whose depth is
`log n` by construction.

Concretely, with the shipped constants a bin can survive at most six
degenerate refinements, and the largest range that can then reach a
comparison sort is bounded by `λ · 2^(w/D) ≈ 52 000` elements — **a
constant independent of `n`**. Total work is therefore `Θ(n)`, in exactly
the same sense in which radix sort is linear: treating the key width as a
constant. This does not contradict the `Ω(n log n)` comparison lower
bound, because the algorithm does arithmetic on keys.

The full derivation, with every invariant proved, is in
[`docs/ALGORITHM.md`](docs/ALGORITHM.md).

## Complexity

| | |
|---|---|
| **Time, best case** | `Θ(n)` — all keys equal: one pass, zero comparisons |
| **Time, average** | `Θ(n)` — about `(λ+1)/4` comparisons per element |
| **Time, worst case** | `Θ(n)` with a bounded constant, for fixed `w` and `λ` |
| **Auxiliary space** | `Θ(n)` — about `3n · sizeof(T)` bytes, roughly 3.1× the input |
| **Recursion depth** | `min(w, 6)`; stack use is `O(1)` in practice |

## Guarantees

- **Correct** for every input: verified against `std::sort` across the
  whole parameter space, all integral key types, and inputs spanning the
  entire key universe.
- **Strong exception safety** in a release build: the caller's array is
  written only in the final phase, so an allocation failure leaves the
  input untouched.
- **Deterministic**: no randomness anywhere; the same input always
  produces the same sequence of operations.
- **No global state**: distinct instances are independent.

## Limitations

Stated plainly, because they are the things a reader should know before
choosing this over `std::sort`:

- **Integral keys only.** Every formula is integer arithmetic. Floating
  point, strings and key/value pairs are out of scope.
- **Not stable.** Unobservable for the integral keys it accepts, since
  equal elements are indistinguishable — but it rules out a
  straightforward extension to key/value pairs.
- **Not thread-safe per instance.** One instance owns mutable scratch
  buffers. Separate instances are fine.
- **Uses `Θ(n)` extra memory.** `std::sort` uses `O(log n)`.
- **Loses to `std::sort` on already-sorted input** by roughly 6× on the
  reference machine: libc++ resolves that case in essentially one pass,
  while this algorithm always does at least three. Detecting sortedness
  up front would be a heuristic, and heuristics of that kind were measured
  and removed earlier in this project.
- **The default `λ` is tuned to a cache size.** The useful lower bound on
  `λ` depends on `n` and on the L2 capacity, roughly
  `n · 64 / λ ≲ L2`. The default suits `n ≈ 10⁶` on a machine with a
  4 MiB L2; much larger inputs want a larger `λ`.
- **All measurements come from one machine.** This project has repeatedly
  found that conclusions invert across platforms — a parameter that won
  by 18–23% on one machine lost by 13% on another. Treat the numbers as
  reproducible, not as universal.

## Performance

Release build, Apple M4, `n = 10⁶`, median of alternating runs. `ratio` is
DRS ÷ `std::sort`, so below 1.0 means DRS is faster.

| Dataset | DRS (ms) | `std::sort` (ms) | ratio |
|---|---|---|---|
| Small range, many elements | 1.96 | 5.19 | **0.38** |
| Huge range, sparse | 10.68 | 15.29 | **0.70** |
| Whole-universe span | 10.74 | 15.31 | **0.70** |
| Random uniform | 10.82 | 15.31 | **0.71** |
| Normal (Gaussian) | 12.12 | 14.44 | **0.84** |
| Concentrated cluster | 4.80 | 5.50 | **0.87** |
| Many repeated values | 2.27 | 2.33 | **0.98** |
| Reverse sorted | 5.23 | 1.19 | 4.40 |
| Already sorted | 4.67 | 0.68 | 6.85 |
| Adversarial (depth-exhausting) | 26.40 | 15.16 | 1.74 |

The last row is a synthetic input built specifically to force the
refinement to its depth limit. It is not representative of anything; it
is there because a worst case that is never measured is not a worst case,
it is a hope.

Reproduce with `make baseline`. See
[`docs/BENCHMARKS.md`](docs/BENCHMARKS.md) for the methodology, which
matters more than the numbers.

## Project layout

```
include/drs/          The algorithm. This is the deliverable.
  Config.hpp            Tuning constants, each with its justification
  DynamicRangeSort.hpp  Public interface and documented guarantees
  DynamicRangeSort.tpp  Implementation
  DRSMetrics.hpp        Instrumentation (research builds only)

tests/                Correctness
  main.cpp              Edge cases and dataset sweep
  api_contract.cpp      Contract tests across the whole parameter space
  edge_sanitizers.cpp   Range-arithmetic limits, run under sanitizers

benchmarks/           Measurement tools
analysis/             Complexity-model fitting
experiments/          One-off studies behind the design decisions
  legacy/               Earlier reconstructions, kept for comparison only
datasets/             Reproducible input generators

docs/                 ALGORITHM.md, BENCHMARKS.md, DESIGN_HISTORY.md
  history/              The working record: one document per step, in Spanish
```

Each one-off study under `experiments/`, `analysis/` and `benchmarks/`
produced exactly one number that this project relies on. They are not part
of the library, but `make` still compiles all of them (`make studies`),
because a study whose source no longer builds cannot be re-run — and a
result nobody can re-run is not evidence.

## Building

Requires a C++17 compiler. No dependencies.

```bash
make            # build everything, including every research program
make test       # correctness: three suites, assertions active
make sanitizers # range-arithmetic limits under ASan and UBSan
make baseline   # timings against std::sort over the dataset battery
make profile    # per-phase time breakdown and allocation counts
```

There are three build configurations, and the difference matters:

| | metrics | assertions | use for |
|---|---|---|---|
| release | no | no | production; the only meaningful timings |
| test | no | **yes** | correctness; ~5% slower, never for timing |
| research | yes | yes | instrumentation; several times slower |

`make test` uses the test configuration, so every internal invariant is
checked on every run.

## Running the tests

```bash
make test
```

Three suites: edge cases and a dataset sweep, then 124 contract checks in
the release configuration and 133 in the research configuration. The
contract suite covers the full `(λ, t)` parameter space, inputs whose span
is the entire key universe, every integral key type, instance reuse across
sizes, and the strong exception guarantee — verified by forcing
`bad_alloc` at each of the first 40 allocation points and confirming the
input survives intact.

Under ASan and UBSan — the suite that pushes spans to the whole key
universe, which is where an overflow would hide:

```bash
make sanitizers
```

## Documentation

- [`docs/ALGORITHM.md`](docs/ALGORITHM.md) — the complete technical
  description. Written so the algorithm can be reimplemented from it
  alone, without reading the C++.
- [`docs/BENCHMARKS.md`](docs/BENCHMARKS.md) — measurement methodology
  and how to reproduce.
- [`docs/DESIGN_HISTORY.md`](docs/DESIGN_HISTORY.md) — what was tried,
  what was measured, and what was rejected. Including the ideas that
  failed, which are the more instructive half.

## License

MIT. See [`LICENSE`](LICENSE).
