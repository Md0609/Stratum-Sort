# Stratum Sort 0.9.0

A linear-time sorting algorithm for integral keys up to 64 bits.
Header-only, C++17, no dependencies.

This is the distributed package. It is self-contained: everything needed
to build, test and use the library is in this archive, and nothing here
refers to a file that is not.

```cpp
#include <stratum/StratumSort.hpp>

std::vector<int64_t> data = /* ... */;
stratum::StratumSort<int64_t> sorter;
sorter.sort(data);              // in place, ascending
```

---

## Install

The library is four headers under `include/stratum/`. Copy that directory
into your project and add it to your include path — there is nothing to
compile or link.

With CMake:

```cmake
add_subdirectory(stratumsort)
target_link_libraries(your_target PRIVATE StratumSort::stratumsort)
```

## Minimal working example

This is the opening of `examples/basic.cpp`, built by `make examples`:

```cpp
#include <stratum/StratumSort.hpp>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    std::vector<int64_t> data{5, 3, 9, 1, 7, 3};

    stratum::StratumSort<int64_t> sorter;
    sorter.sort(data);

    for (auto x : data) std::cout << x << ' ';   // 1 3 3 5 7 9
    std::cout << '\n';
}
```

Build it by hand with nothing but a compiler:

```bash
c++ -std=c++17 -O2 -Iinclude examples/basic.cpp -o basic && ./basic
```

## What it does

Look at the data once to find its minimum and maximum. Cut that range into
equal-width intervals sized so a typical interval receives about λ
elements, and drop every element into its interval in one pass. Intervals
are ordered by construction, so no merge is ever needed.

Any interval that came out too crowded is split again — **using its own
minimum and maximum, not the global ones.** A tight cluster hidden inside
a wide interval gets resolved at *its* scale. Each refinement level is a
*stratum*, hence the name. When an interval is small enough, an ordinary
comparison sort finishes it, and the pieces are concatenated.

## The complexity guarantee, and what it rests on

**Stratum Sort is Θ(n) in the worst case under the four hypotheses below.**

The recursion depth is bounded by the key width, not by `n`: every
refinement level consumes at least one bit of the interval's observed
span. A leaf that still needs a comparison sort therefore has size at most

```
λ · 2^(w/(D+1))  ≈  18 000        for the shipped λ = 32, w = 64, D = 6
```

— a constant independent of `n`. An individual leaf costs `O(m log m)`,
but summing over leaves gives `O(n · log 18 000)`, so the total is linear.
The lower bound is immediate: the initial min/max scan reads every element
unconditionally.

This is linear in the same sense radix sort is linear. It does not
contradict the `Ω(n log n)` comparison lower bound, because the algorithm
does arithmetic on keys rather than only comparing them.

**The hypotheses are not decoration. They are:**

| | |
|---|---|
| **H1** | `T` is integral with `w = 8·sizeof(T) ≤ 64` bits. Enforced by `static_assert`. |
| **H2** | `λ ≥ 1`, `t ≥ λ` and **`D ≥ 1`** are constants chosen independently of `n`. |
| **H3** | Unit-cost RAM: arithmetic and one array access are `O(1)`, allocating or releasing `k` words costs `O(k)`, and `n + λ` fits in `size_t`. |
| **H4** | `memcpy` of `k` elements costs `Θ(k)`. |

`D ≥ 1` is load-bearing: at `D = 0` there is no refinement at all and a
single bin can hold `Θ(n)` elements, making the worst case `Θ(n log n)`.

**On small `D`.** The bound above is a supremum over all `n`; at a given
`n` the real bound is `min(n, (λ^(D+1)·2^w/n)^(1/D))`, which is *vacuous*
until `n` exceeds `λ · 2^(w/(D+1))`. That threshold is about `1.8·10⁴` at
`D = 6` — below any realistic input — but about `8.5·10⁷` at `D = 2` and
`1.4·10¹¹` at `D = 1`. So a small `D` is still asymptotically linear and
still practically useless: over any input size you would actually sort,
the algorithm can hand most of the array to a comparison sort. **`D = 6`
is the studied and shipped configuration**; the others are not
recommended.

The full proof — five lemmas, a phase-by-phase cost table, the adversary
families that saturate the bound, and an adversarial review of the proof
by itself — is in the project repository, not in this package.

## Complexity, space, guarantees

| | |
|---|---|
| **Time, best / average / worst** | `Θ(n)` under H1–H4 |
| **Auxiliary space** | `2n·sizeof(T) + n·sizeof(size_t) + O(n/λ)` bytes — about 3.1× the input for an 8-byte key, but about 10× for a 1-byte key, since the index array is one `size_t` per element regardless of `T` |
| **Recursion depth** | `min(w, 6)`; stack use is `O(1)` in practice |
| **Deterministic** | yes — no randomness anywhere |
| **Stable** | no |
| **Thread-safe** | per instance, no; distinct instances are independent |
| **Exception safety** | strong in a release build: every allocation happens before the first write to your array |

## Limitations

- **Integral keys up to 64 bits only.** Floating point, strings and
  key/value pairs are out of scope, and rejected at compile time.
- **Not stable**, and not extensible to key/value pairs as written.
- **`Θ(n)` auxiliary memory** where `std::sort` uses `O(log n)`.
- **Loses to `std::sort` on already-sorted input**, by about 6×: libc++
  resolves that case in essentially one pass, this algorithm always makes
  at least three.
- **The default λ is tuned to a cache size** — roughly `n·64/λ ≲ L2`. The
  default suits `n ≈ 10⁶` with a 4 MiB L2; raise it for much larger
  inputs. See `docs/usage.md`.

## Performance

Two different kinds of number, which should not be conflated.

**Complexity evidence — deterministic comparison counts.** Exact, and
immune to cache behaviour and machine load. An `n log n` term would raise
comparisons per element by a factor of 1.75 across these three decades:

| dataset | `n = 10⁴` | `10⁶` | `10⁷` | fitted exponent |
|---|---|---|---|---|
| Uniform, constant density | 9.05 | 9.01 | 9.01 | 0.9997 |
| Whole-universe span | 9.01 | 9.02 | 9.03 | 0.9998 |
| Adversarial (depth-exhausting) | 14.06 | 14.91 | 15.52 | 1.0137 |

This is evidence *consistent with* the proof, not a substitute for it.

**Comparative performance — wall clock.** Median of nine repetitions,
alternating Stratum Sort and `std::sort` on identical copies of the same
input. `n = 10⁶`, `int64_t`, Apple M4, macOS 26.6, Apple clang 21
(libc++), `-std=c++17 -O3 -DNDEBUG`.

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

Median of three sessions; the sessions agreed to within 2%. Reproduce with
`make timings`. **Ratios travel better than absolute milliseconds** — they
are measured on the same machine in the same second, which is the point of
alternating the two sorts.

## Building and testing

```bash
make            # library check, tests, timings and the example
make test       # three suites: edge cases, and 124 + 133 contract checks
make timings    # release timings against std::sort
make examples   # run the minimal example
```

Two more suites need a linkable AddressSanitizer, so they are **not** part
of `make all` — on macOS they require Clang:

```bash
make sanitizers        # range-arithmetic limits under ASan/UBSan
make fuzz N=200000     # randomised differential test against std::sort
```

`make fuzz` draws element type, size, both tuning parameters and the value
distribution at random and requires exact agreement with `std::sort`.
Three oracles run at once: that agreement, the library's internal
assertions, and the sanitizers. It is seeded, so a failure reproduces.

With CMake:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

## Verified platforms

`0.9.0` is a release candidate. The version is not `1.0.0` because the API
has no field history and platform validation is narrow.

| | |
|---|---|
| **Verified** | arm64 macOS 26.6, Apple clang 21 (libc++) and GCC 15 (libstdc++), Make and CMake, Release and Debug, C++17 / C++20 / C++23 |
| **Not verified** | Linux, Windows, x86, MSVC, older compilers |

Nothing in the implementation is platform-specific, but "should work" is
not "was tested", and this table says which is which.

## Contents of this package

```
include/stratum/    the library — this is the deliverable
  StratumSort.hpp     public interface and documented guarantees
  StratumSort.tpp     implementation
  Config.hpp          tuning constants, each with its justification
  Metrics.hpp         optional instrumentation; compiled in only when
                      STRATUM_ENABLE_METRICS is defined
tests/              four suites
datasets/           input generators used by the tests and the benchmark
benchmarks/         timings.cpp — the only binary whose clock may be quoted
examples/basic.cpp  minimal working example
docs/usage.md       API reference and tuning guide
```

## License

MIT. See `LICENSE`.
