# Stratum Sort 0.11.0

A linear-time sorting algorithm for integer, enum and floating-point
keys, and for records sorted by one. Header-only, C++17, no dependencies.

This is the distributed package. It is self-contained: everything needed
to build, test and use the library is in this archive, and nothing here
refers to a file that is not.

```cpp
#include <stratum/StratumSort.hpp>

std::vector<int64_t> data = /* ... */;
stratum::sort(data);                        // in place, ascending

std::vector<double> prices = /* ... */;
stratum::sort(prices);                      // IEEE-754 totalOrder

stratum::stable_sort_by_key(rows, [](const Row& r) { return r.id; });

stratum::StratumSort<int64_t> sorter;       // the 0.10.0 interface,
sorter.sort(data);                          // unchanged
```

---

## Install

The library is the headers under `include/stratum/`. Copy that directory
into your project and add it to your include path — there is nothing to
compile or link.

With CMake:

```cmake
add_subdirectory(stratumsort)
target_link_libraries(your_target PRIVATE StratumSort::stratumsort)
```

## Minimal working example

`examples/basic.cpp`, built by `make examples`, shows integers, doubles,
records sorted stably by a key, and the class with named parameters and a
workspace. Its first lines:

```cpp
#include <stratum/StratumSort.hpp>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    std::vector<int64_t> data{5, 3, 9, 1, 7, 3};
    stratum::sort(data);
    for (auto x : data) std::cout << x << ' ';   // 1 3 3 5 7 9
    std::cout << '\n';
}
```

Build it by hand with nothing but a compiler:

```bash
c++ -std=c++17 -O2 -Iinclude examples/basic.cpp -o basic && ./basic
```

`docs/usage.md` is the API reference: which function to call, the
stability contract, threads, memory and tuning.

## What it does

Look at the data once to find its minimum and maximum — and, in the same
pass, whether it is already sorted, in which case the work is done. Cut
the observed range into equal-width intervals sized so a typical interval
receives about λ elements, and drop every element into its interval in one
pass. Intervals are ordered by construction, so no merge is ever needed.

Any interval that came out too crowded is split again — **using its own
minimum and maximum, not the global ones.** A tight cluster hidden inside
a wide interval gets resolved at *its* scale. Each refinement level is a
*stratum*, hence the name. When an interval is small enough, an ordinary
comparison sort finishes it, and the pieces are already in place.

Floating-point keys and records go through the same engine: the key is
mapped to a 64-bit unsigned integer that preserves its order — for
`float` and `double`, IEEE 754's `totalOrder` — and the record moves with
it.

## The complexity guarantee, and what it rests on

**Stratum Sort is Θ(n) in the worst case, for every input and every
argument, under the four hypotheses below.**

The recursion depth is bounded by the key width, not by `n`: every
refinement level consumes at least one bit of the interval's observed
span. A leaf that still needs a comparison sort therefore has size at most

```
λ · 2^(w/(D+1))  ≈  9 045        λ = 16, w = 64, D = 6
```

— a constant independent of `n`. The automatic parameters use λ = 32 only
above `2²²` elements, where the bound at that `n` is below 7 300, so no
sort with automatic parameters hands a comparison sort more than 9 045
elements. What a leaf costs depends on which local sort it reaches:

| leaf size `m` | local sort (stable variant) | worst case |
|---|---|---|
| `m ≤ 64` | insertion sort (same) | `O(m²)`, with `m ≤ 64` |
| `64 < m ≤ 384` | quicksort, no depth limit (merge sort) | `O(m²)`, with `m ≤ 384` |
| `m > 384` | introsort, heapsort fallback (merge sort) | `O(m log m)` guaranteed |

The quadratic bands do not break linearity: when `m ≤ C` for a fixed `C`,
`m² ≤ C·m`, so the cost is linear in the leaf. Leaves tile `[0, n)`
without overlap, so the total across all leaves is `O(n)`. The lower bound
is immediate: the initial scan reads every element.

This is linear in the same sense radix sort is linear. It does not
contradict the `Ω(n log n)` comparison lower bound, because the algorithm
does arithmetic on keys rather than only comparing them.

**The hypotheses are not decoration. They are:**

| | |
|---|---|
| **H1** | The key maps to `w ≤ 64` bits preserving its order: integral types but `bool`, enums, `float`, `double`. Enforced by `static_assert`. |
| **H2** | `λ ≥ 1`, `t ≥ λ` and **`D ≥ 1`** are bounded independently of `n`. Enforced: explicit arguments are clamped to at most 10 000, the automatic choice takes one of two fixed values, and `D = 6` is a compile-time constant. |
| **H3** | Unit-cost RAM: arithmetic and one array access are `O(1)`, allocating or releasing `k` words costs `O(k)`, and `n + λ` fits in `size_t`. |
| **H4** | `memcpy` of `k` elements costs `Θ(k)`. |

`D ≥ 1` is load-bearing: at `D = 0` there is no refinement at all and a
single bin can hold `Θ(n)` elements, making the worst case `Θ(n log n)`.

The full proof — five lemmas, a phase-by-phase cost table, the adversary
families that saturate the bound, an adversarial review of the proof by
itself, and the re-derivation for the 0.11.0 engine — is in the project
repository, not in this package.

## Complexity, space, guarantees

| | |
|---|---|
| **Time, best / average / worst** | `Θ(n)` under H1–H4; one read-only pass on already-sorted input |
| **Auxiliary space** | bounded whatever `n` is: by default `min(n·sizeof(E) + (2⌈n/λ⌉ + 2)·4, 16 MiB)` — 1.06× the input for an 8-byte key up to 16 MiB (0.10.0: 3.28×); with a `Workspace` budget `M`, at most `max(M, 44 KB)`. Nothing on sorted or reversed input. The stable sorts of records need `n` records |
| **Recursion depth** | at most 7 refinement levels |
| **Deterministic** | yes — no randomness anywhere, and parameters depend on `n` only |
| **Stable** | `stable_sort`, `stable_sort_by_key`, `sorted_indices`: yes. `sort`, `sort_by_key`, `StratumSort<T>`: no |
| **Thread-safe** | the free functions, yes; one `const StratumSort<T>` shared by many threads, yes, with one `Workspace` per thread; `sorter.sort(v)` on one instance from two threads, no |
| **Exception safety** | strong in a release build: every allocation happens before the first write to your array |
| **Instrumentation** | `STRATUM_ENABLE_METRICS` is a whole-program switch. It changes the class layout, so the class is tagged with an inline namespace: a translation unit that disagrees fails to link rather than misbehaving silently |

## Limitations

- **Keys: at most 64 bits, and numeric.** Strings, `__int128`,
  `long double` and multi-field keys are rejected at compile time.
- **Stable sorts of records keep `Θ(n)` memory**: `stable_sort_by_key`
  needs `n` records of scratch, `sorted_indices` `n` (key, index) pairs,
  and an explicit budget below that throws `std::length_error`. Every
  other sort is bounded (above).
- **Bounded memory costs time on some inputs**: a small budget cannot
  hold the buffer of the sorted-prefix shortcut, and the 44 KB floor is
  1.1–2× slower than the default (still at most 1.04× `std::sort` at
  10⁷ 64-bit keys).
- **Records** must be trivially copyable for `sort_by_key`; for other
  types use `sorted_indices`.
- **It loses to `std::sort` on some inputs**: ones built against its own
  partition (1.2–1.7× on macOS arm64 and Windows), low-entropy keys on
  Apple M1 (1.7×), nearly-sorted input under MSVC (1.2×), and
  nearly-sorted floating point (1.1–2.9×). The table below has the
  numbers.

## Performance

Measured on GitHub's runners by `benchmarks/stratum_bench.cpp`, which is in
this package: Stratum and `std::sort` alternated on identical copies of
each input, median of 9 repetitions, a fresh sorter per call, release
build. Time relative to `std::sort`, `n = 10⁶` (below 1 is faster):

| input | Linux x86_64 (Xeon), GCC 13 | macOS arm64 (M1), AppleClang 21 | Windows x86_64 (EPYC), MSVC 19.51 |
|---|---|---|---|
| random `int64_t` | 0.33× | 0.57× | 0.37× |
| already sorted | 0.05× | 0.39× | 0.05× |
| reversed | 0.13× | 0.39× | 0.05× |
| nearly sorted | 0.70× | 0.76× | 1.23× |
| sorted + random tail | 0.04× | 0.05× | 0.07× |
| many duplicates | 0.13× | 0.25× | 0.17× |
| normal (Gaussian) | 0.34× | 0.62× | 0.49× |
| low entropy | 0.55× | 1.66× | 1.00× |
| adversarial (depth-exhausting) | 0.73× | 1.67× | 1.23× |
| McIlroy quicksort killer | 0.13× | 0.08× | 0.15× |
| random `uint8_t` | 0.04× | 0.08× | 0.04× |
| random `double` | 0.34× | 0.63× | 0.50× |
| 16-byte records, stable, vs `std::stable_sort` | 0.26× | 0.25× | 0.41× |

**Ratios travel better than absolute milliseconds** — they are measured on
the same machine in the same second — but these are shared virtual
machines. Run the benchmark on your own hardware:

```bash
make bench SUITE=quick      # or: ci, types, lambda, full
```

It prints the compiler, standard, flags, CPU, caches and core count with
every table, and reports medians with the p10–p90 spread, ns/element,
elements/s, `std::stable_sort`, and the peak auxiliary memory measured at
the allocator.

## Building and testing

```bash
make            # library check, tests, timings and the example
make test       # every suite below, assertions active
make examples   # run the minimal example
make bench      # the benchmark against std::sort
```

`make test` runs the edge cases and a dataset sweep; the exact-division
suite (the reciprocal that replaces the divide instruction is checked to
be exact, 12.2 million checks); the float-key suite, exhaustive over all
2³² `float` bit patterns; the stability suite (3 694 checks against
`std::stable_sort` with identifying payloads); the concurrency suite; and
the API contract — 155 checks in the release configuration and 171 in the
research one.

Three more need a sanitizer runtime, so they are **not** part of
`make all` — on macOS they require Clang:

```bash
make sanitizers        # ASan/UBSan
make tsan              # ThreadSanitizer: one shared sorter, several threads
make fuzz N=200000     # randomised differential test against the standard library
```

`make fuzz` draws element type (integers, floats, records), size, both
tuning parameters, the value distribution and the presortedness at random
and requires exact agreement with `std::sort` or `std::stable_sort`. It
is seeded, so a failure reproduces.

With CMake:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

## Verified platforms

`0.11.0` is not `1.0.0`: the API is new and has no field history yet.

| | |
|---|---|
| **Verified by CI** | **Linux x86_64** (Ubuntu 24.04) — GCC 13 and Clang 18: C++17/20/23 Release and C++17 Debug under CMake; ASan/UBSan, ThreadSanitizer and the differential fuzz under Make; the benchmark.<br>**macOS arm64** (macOS 26) — AppleClang 21: C++17 Release and Debug under CMake; the benchmark.<br>**Windows x86_64** (Windows Server 2025) — MSVC 19.51: C++17 Release and Debug and C++20 Release under CMake, full test suite and the ODR link guard; the benchmark. |
| **Not verified** | 32-bit targets, big-endian machines, and any other compiler or compiler version. On macOS and Windows: sanitizers and the Make build. On Windows: C++23. |

Nothing in the implementation is platform-specific, but "should work" is
not "was tested", and this table says which is which.

## Contents of this package

```
include/stratum/    the library — this is the deliverable
  StratumSort.hpp     the class, its documented guarantees; includes everything
  StratumSort.tpp     the class's implementation
  Sort.hpp            the free functions: sort, stable_sort, sort_by_key, ...
  KeyTraits.hpp       how each key type maps to an ordered 64-bit key
  Workspace.hpp       the scratch memory of a sort, reusable, one per thread
  Config.hpp          tuning constants and the automatic parameters
  Metrics.hpp         optional instrumentation (STRATUM_ENABLE_METRICS)
  detail/             the engine and the exact reciprocal division
tests/              the suites above
datasets/           input generators used by the tests and the benchmark
benchmarks/         stratum_bench.cpp (multi-shape, multi-type) and timings.cpp
examples/basic.cpp  minimal working example
docs/usage.md       API reference and tuning guide
BUILDINFO.txt       platform packages only: the runner, compiler and test
                    results of the CI job that built the package
```

## License

MIT. See `LICENSE`.
