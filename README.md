# Stratum Sort

A linear-time sorting algorithm for 64-bit integer keys, written as a
reference implementation: header-only, no dependencies, every constant
justified, every invariant asserted, every performance claim measured
rather than argued.

```cpp
#include "stratum/StratumSort.hpp"

std::vector<int64_t> data = /* ... */;
stratum::StratumSort<int64_t> sorter;
sorter.sort(data);          // in place, ascending
```

**On the reference machine it sorts a million random 64-bit integers in
10.7 ms against `std::sort`'s 15.8 ms.** It also loses to `std::sort` by
6× on already-sorted input. Both numbers are below.

---

## The problem

Comparison sorts cannot beat `Ω(n log n)`, and `std::sort` is an excellent
implementation of that bound. But a comparison sort deliberately ignores
something it is allowed to use: for integer keys you can do *arithmetic*
on the values, not just compare them. Knowing a key is `517` tells you
roughly where it belongs in the output; knowing only that it is "greater
than 340" does not.

Counting sort and radix sort exploit that, and both are rigid: counting
sort needs a small value range, radix sort processes fixed digit
positions regardless of how the data is actually distributed.
[Flashsort](https://en.wikipedia.org/wiki/Flashsort) goes further — it
derives equal-width buckets from the observed minimum and maximum — but
it does so **once**, globally.

## How it works

Look at the data once to find its minimum and maximum. Cut that range
into equal-width intervals sized so a typical interval receives about λ
elements, and drop every element into its interval in one pass. Intervals
are ordered by construction: everything in interval *j* is at most
everything in interval *j+1*.

Any interval that came out too crowded gets split again — **using its own
minimum and maximum, not the global ones.** That is the whole idea, and
it is what separates this from a one-shot distribution sort: a tight
cluster hidden inside a wide interval is resolved at *its* scale, not the
array's. Each of those refinement levels is a **stratum**, hence the name.

When an interval is small enough, an ordinary comparison sort finishes
it. Then concatenate — no merge, because the pieces were already in the
right order and, as it turns out, already in the right place.

## Why it is linear

Each refinement level consumes at least one bit of the interval's
*observed span*, and a key only has `w` bits. **The recursion depth is
bounded by the key width, not by `n`** — which is the whole structural
difference from a divide-and-conquer comparison sort, whose depth is
`log n` by construction.

A leaf that still needs a comparison sort has size at most
`λ · 2^(w/(D+1)) ≈ 18 000` — a constant, independent of `n`. An individual
leaf costs `O(m log m)`, but summing over leaves gives `O(n · log 18 000)`,
so the total is `Θ(n)`.

This is linear in the same sense radix sort is linear: the key width is a
fixed property of the type. It does not contradict the `Ω(n log n)`
comparison lower bound, because the algorithm does arithmetic on keys.

The full proof — four hypotheses, five lemmas, a phase-by-phase cost
table and an adversarial review of itself — is in
[`research/ALGORITHM.md`](research/ALGORITHM.md).

## Complexity

| | |
|---|---|
| **Time, best / average / worst** | `Θ(n)` |
| **Auxiliary space** | `2n·sizeof(T) + n·sizeof(size_t) + O(n/λ)` bytes — about 3.1× the input for an 8-byte key |
| **Recursion depth** | `min(w, 6)`; stack use is `O(1)` in practice |
| **Stable** | no |

## Limitations

The things worth knowing before choosing this over `std::sort`:

- **Integral keys up to 64 bits only.** Enforced at compile time. Floating
  point, strings and key/value pairs are out of scope.
- **Not stable**, and not extensible to key/value pairs as written.
- **Not thread-safe per instance** (one instance owns scratch buffers).
  Separate instances are independent.
- **Uses `Θ(n)` extra memory** where `std::sort` uses `O(log n)`.
- **Loses to `std::sort` on already-sorted input**, by about 6×.
- **The default λ is tuned to a cache size** — roughly `n·64/λ ≲ L2`. The
  default suits `n ≈ 10⁶` with a 4 MiB L2; much larger inputs want a
  larger λ.
- **All measurements come from one machine.** This project has repeatedly
  found conclusions that invert across platforms. Treat the numbers as
  reproducible, not universal.

## Performance

Release build, Apple M4, `n = 10⁶`, alternating runs on identical copies;
median of nine repetitions and of three sessions. `ratio` is Stratum Sort
÷ `std::sort`, so below 1.0 is faster.

| Dataset | Stratum (ms) | `std::sort` (ms) | ratio |
|---|---|---|---|
| Small range, many elements | 1.93 | 5.29 | **0.36** |
| Huge range, sparse | 10.48 | 15.70 | **0.67** |
| Whole-universe span | 10.49 | 15.68 | **0.67** |
| Random uniform | 10.68 | 15.81 | **0.68** |
| Normal (Gaussian) | 11.77 | 15.05 | **0.78** |
| Concentrated cluster | 4.86 | 5.61 | **0.87** |
| Many repeated values | 2.60 | 2.42 | 1.07 † |
| Adversarial (depth-exhausting) | 25.82 | 15.84 | 1.63 |
| Reverse sorted | 4.80 | 1.24 | 3.86 |
| Already sorted | 4.27 | 0.72 | 5.93 |

† ~18% run-to-run dispersion and identical internal counters, so a tie
rather than a loss.

```bash
make timings
```

Methodology in [`research/BENCHMARKS.md`](research/BENCHMARKS.md) — it
matters more than the numbers.

## Requirements

A C++17 compiler. Nothing else. The library is header-only.

## Install

Copy `include/stratum/` into your project, or with CMake:

```cmake
add_subdirectory(stratumsort)
target_link_libraries(your_target PRIVATE StratumSort::stratumsort)
```

Full API and tuning guidance: [`docs/usage.md`](docs/usage.md).

## Build and test

```bash
make && make test
```

Three suites: edge cases and a dataset sweep, then 124 contract checks in
the release configuration and 133 in the research one, covering the whole
`(λ, t)` parameter space, spans reaching the entire key universe, every
integral key type, instance reuse and the strong exception guarantee.

Two more run under ASan and UBSan:

```bash
make sanitizers
make fuzz N=200000
```

`make fuzz` is a differential test against `std::sort` with element type,
size, both tuning parameters and value distribution drawn at random.
Three oracles at once — agreement with `std::sort`, the internal
assertions, and the sanitizers. Seeded, so a failure reproduces.

## Download

```bash
make package        # -> dist/stratumsort-v1.0.0.zip
```

The package contains the library, its tests, one example and the usage
documentation — everything needed to build and use it, and nothing else.

## Research

The design record is in [`research/`](research/) and is **not** part of
the download. It is meant to be read in the repository:

- [`ALGORITHM.md`](research/ALGORITHM.md) — the complete technical
  description and the `Θ(n)` proof.
- [`BENCHMARKS.md`](research/BENCHMARKS.md) — measurement methodology.
- [`DESIGN_HISTORY.md`](research/DESIGN_HISTORY.md) — what was tried, what
  was measured, and what was rejected, including the ideas that failed.
- [`history/`](research/history/) — one document per step, in Spanish.

## License

MIT. See [`LICENSE`](LICENSE).
