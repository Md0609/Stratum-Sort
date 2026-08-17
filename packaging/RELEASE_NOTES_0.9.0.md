# Stratum Sort 0.9.0

First public release candidate.

Stratum Sort is a linear-time sorting algorithm for integral keys up to
64 bits. Header-only, C++17, no dependencies.

```cpp
#include <stratum/StratumSort.hpp>

std::vector<int64_t> data = /* ... */;
stratum::StratumSort<int64_t> sorter;
sorter.sort(data);              // in place, ascending
```

## What makes it different

Counting sort needs a small value range. Radix sort processes fixed digit
positions regardless of how the data is actually distributed. Flashsort
goes further and derives equal-width buckets from the observed minimum and
maximum — but it does so **once, globally**.

Stratum Sort re-derives the partition at every level. An interval that
comes out too crowded is split again **using its own minimum and maximum,
not the array's**. A tight cluster hidden inside a wide interval is
resolved at *its* scale rather than at the scale of the whole input. Each
of those refinement levels is a stratum, which is where the name comes
from.

Two consequences fall out of that. The intervals are value-ordered by
construction, so the pieces are concatenated rather than merged — there is
no merge step at all. And every level consumes at least one bit of the
interval's observed span, so **the recursion depth is bounded by the key
width, not by `n`**. That is the structural difference from a
divide-and-conquer comparison sort, whose depth is `log n` by definition.

## Complexity

**Θ(n) in the worst case, under four stated hypotheses.**

A leaf that still needs a comparison sort has size at most
`λ · 2^(w/(D+1)) ≈ 18 000` for the shipped parameters — a constant
independent of `n`. An individual leaf costs `O(m log m)`, but summing
over leaves gives `O(n · log 18 000)`. The lower bound is immediate: the
initial min/max scan reads every element unconditionally.

The hypotheses are part of the claim, not fine print:

| | |
|---|---|
| **H1** | `T` is integral with `w = 8·sizeof(T) ≤ 64` bits. Enforced by `static_assert`. |
| **H2** | `λ ≥ 1`, `t ≥ λ` and **`D ≥ 1`** are constants chosen independently of `n`. |
| **H3** | Unit-cost RAM; allocating or releasing `k` words costs `O(k)`; `n + λ` fits in `size_t`. |
| **H4** | `memcpy` of `k` elements costs `Θ(k)`. |

`D ≥ 1` is load-bearing: at `D = 0` there is no refinement and the worst
case becomes `Θ(n log n)`. This is linear in the same sense radix sort is
linear, and it does not contradict the `Ω(n log n)` comparison lower
bound, because the algorithm does arithmetic on keys rather than only
comparing them.

The proof — five lemmas, a phase-by-phase cost table, adversary families
that saturate the bound to within 0.1%, and an adversarial review of the
proof by itself — is in `research/ALGORITHM.md` in the repository.

## API

The entire public surface:

```cpp
namespace stratum {
template <typename T>
class StratumSort {
public:
    explicit StratumSort(std::size_t targetElementsPerBin = 32,
                         std::size_t leafThreshold        = 64);
    void sort(std::vector<T>& data);
    std::size_t targetElementsPerBin() const;
    std::size_t leafThreshold() const;
};
}
```

Both constructor arguments are hints and are clamped, so no combination
can misbehave. Most callers never touch them.

## Install

Download `stratumsort-v0.9.0.zip` from this release, then either copy
`include/stratum/` into your project:

```bash
c++ -std=c++17 -O2 -Iinclude your_program.cpp
```

or use CMake:

```cmake
add_subdirectory(stratumsort-v0.9.0)
target_link_libraries(your_target PRIVATE StratumSort::stratumsort)
```

```bash
make test          # three suites: edge cases, 124 + 133 contract checks
make timings       # release timings against std::sort
make examples      # minimal working example
```

Two further suites need a linkable AddressSanitizer, so they are separate
targets; on macOS they require Clang, since Homebrew GCC has no linkable ASan there:

```bash
make sanitizers
make fuzz N=200000
```

## Product and research are separate

**The release asset contains the algorithm only.** It is self-contained:
its own README, its tests, one example, the usage guide, and no research
material. A user who only wants to sort things never has to read or
download anything else.

**The research lives in the repository**, in `research/`: the Θ(n) proof,
the measurement methodology, the design history with the ideas that were
tried and rejected, the adversary battery, and the one-off studies behind
each constant. It is readable online and is deliberately not part of the
package. `make package-verify` proves the boundary holds by planting decoy
files and failing if any reaches the archive.

## Performance

Two kinds of number, which are not interchangeable.

**Complexity evidence — deterministic comparison counts**, immune to cache
behaviour and machine load. An `n log n` term would raise comparisons per
element by a factor of 1.75 over these three decades:

| dataset | `n = 10⁴` | `10⁶` | `10⁷` | fitted exponent |
|---|---|---|---|---|
| Uniform, constant density | 9.05 | 9.01 | 9.01 | 0.9997 |
| Whole-universe span | 9.01 | 9.02 | 9.03 | 0.9998 |
| Adversarial (depth-exhausting) | 14.06 | 14.91 | 15.52 | 1.0137 |

This is evidence *consistent with* the proof, not a substitute for it.

**Comparative performance — wall clock.** `n = 10⁶`, `int64_t`, Apple M4,
macOS 26.6, Apple clang 21 (libc++), `-std=c++17 -O3 -DNDEBUG`, idle
machine. Median of nine repetitions and of three sessions, alternating
both sorts on identical copies of the same input.

| Dataset | Stratum (ms) | `std::sort` (ms) | ratio |
|---|---|---|---|
| Small range, many elements | 1.94 | 5.48 | **0.36** |
| Huge range, sparse | 10.84 | 16.08 | **0.67** |
| Whole-universe span | 10.91 | 16.18 | **0.67** |
| Random uniform | 10.99 | 16.12 | **0.68** |
| Normal (Gaussian) | 12.43 | 15.42 | **0.80** |
| Concentrated cluster | 5.28 | 5.85 | **0.90** |
| Many repeated values | 2.39 | 2.54 | 0.94 |
| Adversarial (depth-exhausting) | 27.08 | 16.29 | 1.66 |
| Reverse sorted | 4.96 | 1.27 | 3.86 |
| Already sorted | 4.48 | 0.75 | 5.95 |

Ratios travel better across machines than absolute milliseconds.

## Limitations

- **Windows and MSVC have never been built.** Linux x86_64 and macOS
  arm64 are both verified; nothing in the implementation is
  platform-specific, but "should work" is not "was tested".
- **Integral keys up to 64 bits only**, rejected at compile time otherwise.
- **Not stable**, and not extensible to key/value pairs as written.
- **`Θ(n)` auxiliary memory** where `std::sort` uses `O(log n)` — about
  3.1× the input for an 8-byte key, and about 10× for a 1-byte key, since
  the index array is one `size_t` per element regardless of `T`.
- **Loses to `std::sort` on already-sorted input**, by about 6×.
- **Not thread-safe per instance**; distinct instances are independent.
- **All timings come from one machine.** Correctness is verified on two
  platforms, but the milliseconds above are macOS only.

## Why 0.9.0 and not 1.0.0

The algorithm is complete, the tests are thorough and the guarantee is
documented with its hypotheses. Linux x86_64 and macOS arm64 are both
verified. But `1.0.0` is a promise of API stability, and that promise is
not worth making while the library has no field history and has never been
compiled with MSVC. `1.0.0` follows that, not a calendar.

## Verified in this release

| | |
|---|---|
| Compilers | Apple clang 21.0.0, GCC 15.2.0, GCC 14.4.0, Clang 19.1.7 |
| Standards | C++17, C++20, C++23 — every compiler above |
| Build systems | Make, and CMake 4.4 in Release and Debug |
| Test suites | edge cases, dataset sweep, 124 + 133 API contract checks, ASan/UBSan range limits, 100 000-case differential fuzz against `std::sort` |
| Platforms | Linux x86_64 (GCC 14 + libstdc++, Clang 19 + libc++) and macOS 26.6.1 arm64 (Apple clang 21 + libc++, GCC 15 + libstdc++) |
| Package | builds and passes its tests when extracted outside the repository, with both compilers and both build systems |

The archive is **byte-reproducible on a given platform**: two runs of
`make package` over the same tree produce the same SHA-256. The hash
differs between macOS and Linux, because the two `zip` implementations
compress identical bytes differently.

## License

MIT.
