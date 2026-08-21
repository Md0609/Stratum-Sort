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
about 0.69× the time `std::sort` takes.** It also loses to `std::sort` by
6× on already-sorted input. Both numbers, and the conditions they were
taken under, are below.

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
- **All timings come from one machine.** Correctness is verified on Linux
  x86_64, macOS arm64 and Windows x86_64, but the performance numbers below
  are from the macOS machine only, and this project has repeatedly found
  timing conclusions that invert across platforms.

## Performance

Two different kinds of number follow, and they are not interchangeable.

### Complexity — deterministic counters

Comparison counts, which are exact and immune to cache behaviour and to
machine load. If the algorithm carried an `n log n` term, comparisons per
element would grow by a factor of 1.75 across the three decades below.

| dataset | `n = 10⁴` | `10⁶` | `10⁷` | fitted exponent |
|---|---|---|---|---|
| Uniform, constant density | 9.05 | 9.01 | 9.01 | **0.9997** |
| Whole-universe span (`2⁶⁴−1`) | 9.01 | 9.02 | 9.03 | **0.9998** |
| Adversarial (depth-exhausting) | 14.06 | 14.91 | 15.52 | **1.0137** |

This is *evidence consistent with* the `Θ(n)` proof, not a substitute for
it. `make research && ./build/research_ComplexityScaling`.

### Comparative performance — wall clock

Median of nine repetitions and of three sessions, alternating Stratum Sort
and `std::sort` on identical copies of the same input.

| Dataset | Stratum (ms) | `std::sort` (ms) | ratio |
|---|---|---|---|
| Small range, many elements | 1.94 | 5.48 | **0.36** |
| Huge range, sparse | 10.84 | 16.08 | **0.67** |
| Whole-universe span | 10.91 | 16.18 | **0.67** |
| Random uniform | 10.99 | 16.12 | **0.68** |
| Normal (Gaussian) | 12.43 | 15.42 | **0.80** |
| Concentrated cluster | 5.28 | 5.85 | **0.90** |
| Many repeated values | 2.39 | 2.54 | 0.94 † |
| Adversarial (depth-exhausting) | 27.08 | 16.29 | 1.66 |
| Reverse sorted | 4.96 | 1.27 | 3.86 |
| Already sorted | 4.48 | 0.75 | 5.95 |

**Environment.** Apple M4, 16 GB, macOS 26.6.1, Apple clang 21.0.0
(libc++), `-std=c++17 -O3 -DNDEBUG`, `n = 10⁶`, `int64_t`, otherwise idle
machine. Median of nine repetitions and of three sessions; the three
sessions agreed to within 2% on every row. **Ratios travel better than
absolute milliseconds across machines** — the two sorts are timed on the
same input in the same second, which is what makes the ratio robust.

† *Many repeated values* is a tie: ~18% run-to-run dispersion, and its
internal counters are identical across configurations.

```bash
make timings
```

Methodology, including why only the release build's clock may be quoted,
is in [`research/BENCHMARKS.md`](research/BENCHMARKS.md).

## Requirements

A C++17 compiler. Nothing else — the library is header-only and pulls in
only `<algorithm>`, `<cassert>`, `<cstring>`, `<vector>` and friends.
Also compiles cleanly as C++20 and C++23.

### Portability — what has actually been verified

| | |
|---|---|
| **Verified** | **Linux x86_64** — GCC 14 (libstdc++) and Clang 19 (libc++), C++17/20/23, Make and CMake, Release and Debug, ASan/UBSan.<br>**macOS arm64** — Apple clang 21 (libc++) and GCC 15 (libstdc++), C++17/20/23, Make and CMake, Release and Debug, ASan/UBSan.<br>**Windows x86_64** — MSVC 19.51 (`windows-latest`), C++17 Release and Debug and C++20 Release, CMake, full test suite and the ODR link guard. |
| **Not verified** | 32-bit targets, big-endian machines, and compilers older than the three above. On Windows specifically: C++23, the Make build, and ASan/UBSan were not exercised. |

Nothing in the implementation is platform-specific, but "should work" is
not "was tested", and this table says which is which.

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

Three suites: edge cases and a dataset sweep, then 125 contract checks in
the release configuration and 134 in the research one, covering the whole
`(λ, t)` parameter space, spans reaching the entire key universe, every
integral key type, instance reuse and the strong exception guarantee.

Two more run under ASan and UBSan (Clang required on macOS — Homebrew GCC
does not ship a linkable ASan there):

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
make package        # -> dist/stratumsort-v0.9.0.zip
```

The package contains the library, its tests, one example and the usage
documentation — everything needed to build and use it, and nothing else.

## Research

The design record lives in [`research/`](research/) and is **deliberately
not part of the download.** Two layers, on purpose:

| | |
|---|---|
| **Want to use the algorithm?** | Download the release asset, or copy `include/stratum/`. |
| **Want to see how it was designed and proved?** | Read `research/` in the repository. |

To be exact about what that means: GitHub always lets anyone clone the
whole repository, and nothing here pretends otherwise. What is true is
that `research/` is not part of the released package — the algorithm is
the product, the research is documentation of how it came to be.

> **If you are reading this from the downloaded package**, the links below
> are not included in it; they resolve in the repository.

Contents:

- [`ALGORITHM.md`](research/ALGORITHM.md) — the complete technical
  description and the `Θ(n)` proof, including the four hypotheses the
  guarantee rests on and why `D ≥ 1` is one of them.
- [`BENCHMARKS.md`](research/BENCHMARKS.md) — measurement methodology.
- [`DESIGN_HISTORY.md`](research/DESIGN_HISTORY.md) — what was tried, what
  was measured, and what was rejected, including the ideas that failed.
- [`history/`](research/history/) — one document per step, in Spanish.

## License

MIT. See [`LICENSE`](LICENSE).
