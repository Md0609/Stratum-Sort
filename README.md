# Stratum Sort

A linear-time sorting algorithm for integer, enum and floating-point
keys — and for records sorted by one — written as a reference
implementation: header-only, no dependencies, every constant justified,
every invariant asserted, every performance claim measured rather than
argued.

```cpp
#include "stratum/StratumSort.hpp"

std::vector<int64_t> data = /* ... */;
stratum::sort(data);                                        // ascending

std::vector<double> prices = /* ... */;
stratum::sort(prices);                                      // IEEE totalOrder

struct Row { uint64_t id; float score; uint32_t flags; };
std::vector<Row> rows = /* ... */;
stratum::stable_sort_by_key(rows, [](const Row& r) { return r.id; });

stratum::StratumSort<int64_t> sorter;                       // 0.10.0's API,
sorter.sort(data);                                          // unchanged
```

**CI measures it sorting a million random 64-bit integers in 0.33× the
time of `std::sort` on Linux x86_64 (GCC), 0.37× on Windows x86_64 (MSVC)
and 0.57× on macOS arm64 (AppleClang).** Already-sorted input costs one
read-only pass. It still loses on some inputs — ones built against its own
partition, low-entropy keys on Apple Silicon, nearly-sorted input under
MSVC, nearly-sorted floating point — and every one of those is in the
tables below, with the conditions they were taken under.

---

## The problem

Comparison sorts cannot beat `Ω(n log n)`, and `std::sort` is an excellent
implementation of that bound. But a comparison sort deliberately ignores
something it is allowed to use: for numeric keys you can do *arithmetic*
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

Look at the data once to find its minimum and maximum — and, in the same
pass, whether it is already sorted, in which case the work is done. Cut
the observed range into equal-width intervals sized so a typical interval
receives about λ elements, and drop every element into its interval in
one pass. Intervals are ordered by construction: everything in interval
*j* is at most everything in interval *j+1*.

Any interval that came out too crowded gets split again — **using its own
minimum and maximum, not the global ones.** That is the whole idea, and
it is what separates this from a one-shot distribution sort: a tight
cluster hidden inside a wide interval is resolved at *its* scale, not the
array's. Each of those refinement levels is a **stratum**, hence the name.

When an interval is small enough, an ordinary comparison sort finishes
it. Then concatenate — no merge, because the pieces were already in the
right order and, as it turns out, already in the right place.

Floating-point keys and records go through the same engine: a key is
mapped to a 64-bit unsigned integer that preserves its order (for `float`
and `double`, IEEE 754's `totalOrder`), and the record moves with it.

## Why it is linear

Each refinement level consumes at least one bit of the interval's
*observed span*, and a key only has `w` bits. **The recursion depth is
bounded by the key width, not by `n`** — which is the whole structural
difference from a divide-and-conquer comparison sort, whose depth is
`log n` by construction.

A leaf that still needs a comparison sort has size at most
`λ · 2^(w/(D+1))` — about 9 000 with the automatic parameters, a constant
independent of `n`. What that leaf costs depends on which local sort it
reaches, and only the largest band is `O(m log m)`:

| leaf size `m` | local sort (stable variant) | worst case |
|---|---|---|
| `m ≤ 64` | insertion sort (same) | `O(m²)`, with `m ≤ 64` |
| `64 < m ≤ 384` | quicksort, no depth limit (merge sort) | `O(m²)`, with `m ≤ 384` (`O(m log m)`) |
| `m > 384` | introsort, heapsort fallback (merge sort) | `O(m log m)` guaranteed |

The quadratic bands do not break linearity, and the reason is not that 384
is a small number: when `m ≤ C` for a fixed `C`, `m² ≤ C·m`, so the cost is
linear in the leaf with constant `C/2 = 192`. Leaves tile `[0, n)` without
overlap, so `Σ m ≤ n` and the total across all leaves is `O(n)`.

This is linear in the same sense radix sort is linear: the key width is a
fixed property of the type. It does not contradict the `Ω(n log n)`
comparison lower bound, because the algorithm does arithmetic on keys.

The full proof — four hypotheses, five lemmas, a phase-by-phase cost
table, an adversarial review of itself, and §13, which re-derives every
bound for the 0.11.0 engine — is in
[`research/ALGORITHM.md`](research/ALGORITHM.md).

## Complexity

| | |
|---|---|
| **Time, best / average / worst** | `Θ(n)` in the worst case, for every input and every argument, under H1–H4 below; one read-only pass on sorted input |
| **Auxiliary space** | **Bounded, whatever `n` is** (pre-release, research/ALGORITHM.md §14): by default `min(n·sizeof(E) + (2⌈n/λ⌉ + 2)·4, 16 MiB)` — 1.06× the input for an 8-byte key up to 16 MiB, then 16 MiB flat; with a `Workspace` budget `M`, at most `max(M, 44 KB)`. Zero on sorted and reversed input. **Exception:** the stable sorts of records (`stable_sort_by_key`, `sorted_indices`) need `n` records / pairs. 0.10.0: 3.28× the input |
| **Recursion depth** | `≤ D + 1 = 7` refinement levels |
| **Stable** | `stable_sort`, `stable_sort_by_key`, `sorted_indices`: **yes**. `sort`, `sort_by_key`, `StratumSort<T>`: no |
| **Threads** | one `const` sorter may serve any number of threads, one `Workspace` each; no locks, no global state |

The hypotheses are part of the claim, not fine print:

| | |
|---|---|
| **H1** | The key maps to `w ≤ 64` bits preserving its order: integral types but `bool`, enums, `float`/`double`. Enforced by `static_assert`. |
| **H2** | `λ ≥ 1`, `t ≥ λ` and **`D ≥ 1`** are bounded independently of `n`. Enforced: explicit arguments are clamped to at most 10 000, the automatic choice takes one of two fixed values, and `D = 6` is a compile-time constant. |
| **H3** | Unit-cost RAM; allocating or releasing `k` words costs `O(k)`; `n + λ` fits in `size_t`. |
| **H4** | `memcpy` of `k` elements costs `Θ(k)`. |

With every argument clamped, the largest leaf a comparison sort can
receive is about `5.7·10⁶` elements for *any* arguments, and the bound
holds uniformly. `Θ(n²)` is not reachable — quicksort only sees
`m ≤ 384` and insertion sort `m ≤ 64`.

## What 0.11.0 changed

0.10.0 listed seven limitations. Where each one stands now:

| 0.10.0 limitation | 0.11.0 | how |
|---|---|---|
| Integral keys ≤ 64 bits only | **reduced**: + enums, `float`, `double`, records by key | an order-preserving key map; `totalOrder` proved over all 2³² floats |
| Not stable, no key/value | **eliminated** | `stable_sort`, `stable_sort_by_key`, `sorted_indices` |
| Not thread-safe per instance | **eliminated** | `Workspace`; `sort(data, ws) const` |
| `Θ(n)` extra memory, 3.28× | **eliminated** except for stable sorts of records: ≤ 16 MiB by default, any budget down to 44 KB, still `Θ(n)` time (pre-release) | the same partition in place, in passes of ≤ 1 024 groups; caller's array as a buffer, no index cache, no tree |
| ~6× slower than `std::sort` on sorted input | **eliminated**: 0.05–0.39× | detected inside the min/max pass, which it resumes |
| λ tuned to one cache | **eliminated** | automatic λ from `n`, measured on four environments |
| Timings from one machine | **eliminated** | CI benchmark on Linux, macOS and Windows |

The partition — and with it the proof — did not change.
[`CHANGELOG.md`](CHANGELOG.md) has the full list.

## Limitations

What remains, and why:

- **Keys: at most 64 bits, and numeric.** Strings, `__int128`,
  `long double` and multi-field keys are rejected at compile time. The
  bound's constant is exponential in `w/(D+1)` (hypothesis H1): a 128-bit
  key would allow comparison-sorted leaves of ~5·10⁶ elements. Structural
  for this algorithm.
- **Stable sorts of records keep `Θ(n)` memory.** `stable_sort_by_key`
  needs `n` records of scratch and `sorted_indices` `n` (key, index)
  pairs; the in-place engine that bounds every other sort is not stable,
  and no bounded stable strategy with a linear worst case and usable
  constants is known to us. An explicit budget below that throws
  `std::length_error` instead of degrading. `stable_sort` of numbers is
  not affected (equal keys are identical). Structural for this design.
- **Bounded memory costs time on some inputs.** Under a small budget the
  sorted-prefix shortcut needs a buffer it may not have (sorted input
  with a long unsorted tail: +36% at 600 KiB, +1409% at the 44 KB floor,
  still 0.82× `std::sort`), and the floor's American-flag passes are
  1.1–2× slower than the partner buffer. The default policy keeps the
  partner buffer up to 16 MiB, where it is the faster choice.
- **Where it loses to `std::sort`** (`n = 10⁶`, ratio > 1 means slower):

  | input | Linux, GCC | macOS arm64, AppleClang | Windows, MSVC |
  |---|---|---|---|
  | adversarial — built against its own partition | 0.73× | **1.67×** | **1.23×** |
  | low entropy (AND of four random words) | 0.55× | **1.66×** | 1.00× |
  | nearly sorted, `int64_t` | 0.70× | 0.76× | **1.23×** (1.63× at `n = 10⁵`) |
  | sorted + a few outliers, `int64_t` | 0.19× | 0.34× | **1.09×** (1.55× at `n = 10⁵`) |
  | nearly sorted, `float`/`double` | **1.07–1.33×** | **1.42–1.47×** | **2.84–2.86×** |
  | few distinct `float`/`double` values | 0.26–0.45× | 0.45–0.50× | **1.36–1.38×** |

  The adversarial input exhausts the refinement depth on purpose, so the
  residual leaves go to comparison sorts; the bound keeps them below
  ~9 000 elements, which is what keeps it linear, but not cheaper than
  `std::sort` on every CPU. Uniformly drawn floats are exponentially
  denser near the top of their range in key space, so they need an extra
  refinement level. Under MSVC, Stratum's nearly-sorted path is also
  relatively slower than under GCC (0.59× of its own random time, against
  0.32×), a codegen difference not yet explained. A branch in the float
  key map made MSVC lose worse still; 0.11.0 removed it (few distinct
  floats: 1.79× → 1.36×).
- **Timings on shared CI runners** are ratios, alternated on identical
  inputs, but still from shared virtual machines that GitHub does not
  guarantee to be the same hardware from run to run: between two runs of
  identical integer code, `int64` random on the Linux runner went from
  0.23× to 0.33× at `n = 10⁶`, and nearly sorted under MSVC from 1.06× to
  1.23×. The M1 runner is a 3-core VM. Run `make bench` on your
  own hardware before relying on a number.

## Performance

Measured by the `Benchmarks` workflow on GitHub's runners with
[`benchmarks/stratum_bench.cpp`](benchmarks/stratum_bench.cpp), all tables
from one run (commit `f90ac4b`): Stratum and `std::sort` alternated on
identical copies of each input, median of 9 repetitions (21 at
`n = 10⁵`), a fresh sorter per call (so allocation is included), release
build, C++17.

| | Linux x86_64 | macOS arm64 | Windows x86_64 |
|---|---|---|---|
| compiler | GCC 13.3.0, libstdc++ | AppleClang 21.0.0, libc++ | MSVC 19.51 |
| CPU | Intel Xeon 6973P-C, 4 cores, L2 2 MiB | Apple M1 (virtual), 3 cores | AMD EPYC 7763, 4 cores, L2 512 KiB |
| flags | `-O3 -DNDEBUG` | `-O3 -DNDEBUG` | `/O2 /Ob2 /DNDEBUG` |

### `int64_t`, `n = 10⁶` — time relative to `std::sort` (below 1 is faster)

| input | Linux | macOS | Windows |
|---|---|---|---|
| random | **0.33×** | **0.57×** | **0.37×** |
| already sorted | 0.05× | 0.39× | 0.05× |
| reversed | 0.13× | 0.39× | 0.05× |
| nearly sorted (1% swaps) | 0.70× | 0.76× | 1.23× |
| local disorder | 0.76× | 0.74× | 0.96× |
| sorted + a few outliers | 0.19× | 0.34× | 1.09× |
| sorted + random tail | 0.04× | 0.05× | 0.07× |
| organ pipe | 0.03× | 0.02× | 0.09× |
| sawtooth | 0.61× | 0.47× | 0.46× |
| many duplicates | 0.13× | 0.25× | 0.17× |
| two values | 0.24× | 0.35× | 0.41× |
| low entropy | 0.55× | 1.66× | 1.00× |
| normal (Gaussian) | 0.34× | 0.62× | 0.49× |
| clustered | 0.32× | 0.70× | 0.59× |
| whole 64-bit universe | 0.33× | 0.47× | 0.37× |
| adversarial (depth-exhausting) | 0.73× | 1.67× | 1.23× |
| worst case for the bound | 0.42× | 0.98× | 0.63× |
| McIlroy quicksort killer | 0.13× | 0.08× | 0.15× |

At `n = 10⁷` (λ = 32): random 0.43× / 0.67× / 0.43×, sorted 0.07× / 0.42× /
0.04×, adversarial 0.46× / 0.72× / 0.45×.

### Other key types and records, `n = 10⁶`

| input | Linux | macOS | Windows |
|---|---|---|---|
| `uint32_t` random | 0.30× | 0.51× | 0.36× |
| `uint8_t` random (every `uint8_t` shape wins) | 0.04× | 0.08× | 0.04× |
| `float` random | 0.29× | 0.63× | 0.49× |
| `double` random | 0.34× | 0.63× | 0.50× |
| `double` nearly sorted | 1.33× | 1.42× | 2.84× |
| 16-byte records, `sort_by_key` vs `std::sort` | 0.36× | 0.26× | 0.39× |
| 16-byte records, `stable_sort_by_key` vs `std::stable_sort` | 0.26× | 0.25× | 0.41× |
| 64-byte records, `stable_sort_by_key` vs `std::stable_sort` | 0.47× | 0.30× | 0.47× |

The complete tables — both sizes, every shape, p10–p90, ns/element,
elements/s, `std::stable_sort`, and a λ sweep — are the artifacts of each
`Benchmarks` run, and `make bench` reproduces them locally.

### Against 0.10.0

Same machine, same process, the two versions alternated on identical
inputs, each against the adversary built for its own λ (Intel Xeon, GCC,
`research/perf/VersionTimings.cpp`):

| `n = 10⁶` | random | sorted | reversed | nearly sorted | duplicates | normal | clustered | adversarial | worst case |
|---|---|---|---|---|---|---|---|---|---|
| time vs 0.10.0 | −39% | −96% | −95% | −62% | −85% | −33% | −53% | −12% | −26% |

All 18 shapes are faster. At `n = 10⁷`: random −29%, sorted −95%, nearly
sorted −29%, duplicates −71%, adversarial −41%.

### Memory — peak auxiliary bytes, measured at the allocator

`int64_t`, random keys (pre-release; research/history/V11_memoria.md has
every type, shape and size):

| `n` | 0.10.0 | unlimited budget (partner buffer) | **default (automatic)** | 600 KiB budget | budget 0 (floor) | `std::sort` |
|---|---|---|---|---|---|---|
| 10⁶ | 26 MB | 8.5 MB | 8.5 MB | 600 KiB | 44 KB | 0 |
| 10⁷ | 263 MB | 82.5 MB | **16 MiB** | 600 KiB | 44 KB | 0 |
| 10⁸ | — | 825 MB | **16 MiB** | 600 KiB | 44 KB | 0 |

Other inputs, default policy, bytes per element at `n = 10⁶`:

| input | 0.10.0 | 0.11.0 |
|---|---|---|
| `uint32_t`, random | — | 4.50 |
| `uint8_t`, random (any shape but organ pipe, 0.5) | ~10 | **< 0.01** |
| already sorted / reversed | 26.25 | **0** |
| many duplicates | 26.25 | **0** (counters only) |
| sorted + random tail | 26.25 | 0.09 |
| 16-byte records, stable | — | 16.5 (stable sorts of records are not bounded) |

### Comparison counts

Exact and immune to machine load — but they count **only the local sorts**
and are not evidence for the complexity of the whole algorithm, which
rests on the proof. Comparisons per element in the local sorts, automatic
parameters:

| dataset | `n = 10⁴` | `10⁶` | `3·10⁶` | `10⁷` |
|---|---|---|---|---|
| uniform, constant density | 5.09 | 5.08 | 5.08 | 9.01 |
| whole-universe span | 5.12 | 5.10 | 5.10 | 9.02 |
| adversarial (depth-exhausting) | 7.90 | 6.79 | 6.91 | 12.16 |

Flat within each parameter regime: the step at `10⁷` is λ going from 16 to
32 above `2²²` (0.10.0, always at λ = 32: 9.01 on uniform input). Leaves
hold about λ elements, and insertion sort costs about `(λ+1)/4`
comparisons per element. `make research && ./build/research_ComplexityScaling`.

## Requirements

A C++17 compiler. Nothing else — the library is header-only and pulls in
only standard headers. Also compiles cleanly as C++20 and C++23.

### Portability — what has actually been verified

| | |
|---|---|
| **Verified by CI** | **Linux x86_64** (Ubuntu 24.04) — GCC 13 and Clang 18: C++17/20/23 Release and C++17 Debug under CMake; ASan/UBSan, ThreadSanitizer and the differential fuzz under Make; the benchmark.<br>**macOS arm64** (macOS 26) — AppleClang 21: C++17 Release and Debug under CMake; the benchmark.<br>**Windows x86_64** (Windows Server 2025) — MSVC 19.51: C++17 Release and Debug and C++20 Release under CMake, full test suite and the ODR link guard; the benchmark. |
| **Not verified** | 32-bit targets, big-endian machines, and any other compiler or compiler version. On macOS and Windows: sanitizers and the Make build. On Windows: C++23. |

Nothing in the implementation is platform-specific, but "should work" is
not "was tested", and this table says which is which.

## Install

Copy `include/stratum/` into your project, or with CMake:

```cmake
add_subdirectory(stratumsort)
target_link_libraries(your_target PRIVATE StratumSort::stratumsort)
```

Full API, the stability contract, threads, memory and tuning:
[`docs/usage.md`](docs/usage.md).

## Build and test

```bash
make && make test
```

`make test` runs the edge cases and a dataset sweep; the exact-division
suite (12.2 million checks); the float-key suite, exhaustive over all 2³²
`float` bit patterns; the stability suite (3 694 checks against
`std::stable_sort` with identifying payloads); the concurrency suite; and
the API contract — 155 checks in the release configuration and 171 in the
research one, covering the whole `(λ, t)` parameter space, spans reaching
the entire key universe, every key type, the presorted paths, the
automatic parameters and the strong exception guarantee.

Under sanitizers (Clang on macOS — Homebrew GCC does not ship a linkable
ASan there):

```bash
make sanitizers      # ASan + UBSan
make tsan            # ThreadSanitizer
make fuzz N=200000   # differential, against std::sort / std::stable_sort
```

`make fuzz` draws the element type, size, both parameters, the value
distribution and the presortedness at random, including floats and
records, and checks against the standard library. Seeded, so a failure
reproduces.

```bash
make bench SUITE=quick   # this machine, against std::sort
```

## Download

```bash
make package        # -> dist/stratumsort-v0.11.0.zip
```

The package contains the library, its tests, the benchmark, one example
and the usage documentation — everything needed to build and use it, and
nothing else.

A release also carries one package per platform, each built and tested by
CI on that platform's native runner: `stratumsort-v0.11.0-linux-x86_64.tar.gz`,
`stratumsort-v0.11.0-macos-arm64.tar.gz` and
`stratumsort-v0.11.0-windows-x86_64.zip`. Each is the source package plus a
`BUILDINFO.txt` recording the compiler and the test results, with a
`.sha256` beside it. None contains compiled code — the library is
header-only; what they add is the record of the native environment each
was validated in. `packaging/platform_package.py` builds one on the
machine it runs on.

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
  guarantee rests on, and §13 for the 0.11.0 engine.
- [`BENCHMARKS.md`](research/BENCHMARKS.md) — measurement methodology.
- [`DESIGN_HISTORY.md`](research/DESIGN_HISTORY.md) — what was tried, what
  was measured, and what was rejected, including the ideas that failed.
- [`history/`](research/history/) — one document per step, in Spanish;
  `V11_informe.md` is the full 0.11.0 audit and report.
- [`perf/`](research/perf/) — the before/after instruments of 0.11.0,
  against the frozen 0.10.0 headers in [`baselines/`](research/baselines/).

## License

MIT. See [`LICENSE`](LICENSE).
