# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.11.0] — unreleased

Turns most of 0.10.0's documented limitations into capabilities: a third
of the memory, presorted input in a single pass, floating-point and enum
keys, records with a stable variant, one sorter shared across threads, and
a λ chosen from `n` instead of fixed. The partition — and with it the
`Θ(n)` proof — is unchanged; `research/ALGORITHM.md` §13 re-derives every
bound the new machinery touches. Every 0.10.0 call site compiles and
sorts as before.

### Changed
- **Memory: 3.28× → 1.06× the input** (`int64_t`, default parameters,
  measured at the allocator). The caller's array is now one of the two
  cascading buffers; the per-element bucket-index cache is gone (the index
  is recomputed with an exact reciprocal division, which is 23–52% *faster*
  than storing it); the refinement tree is gone (depth-first visit); all
  histograms share one arena of `2⌈n/λ⌉ + 2` 32-bit counters. At
  `λ = t = 1`: 16.57× → 2.00×. Two allocations per sort instead of ~30,
  and none of the peak depends on the input's shape.
- **Automatic parameters.** `StratumSort<T>()` and `Parameters{}` choose
  `(λ, t) = (16, 32)` for `n ≤ 2²²` and `(32, 64)` above, per call.
  0.10.0's fixed `λ = 32` was tuned to one cache; re-measured on four
  environments, it was up to 1.40× slower than the best λ, the automatic
  rule is within 1.2×. Deterministic: a function of `n` only, never of the
  machine. Explicit `(λ, t)` keep 0.10.0's fixed, clamped contract.
  Effect on the bound: the largest leaf a comparison sort can receive under
  automatic parameters drops from 18 090 to 9 045.
- `DEFAULT_TARGET_ELEMENTS_PER_BIN` is now 16 and `DEFAULT_LEAF_THRESHOLD`
  32: the values an explicit `StratumSort<T>(λ)` falls back to.
- `float`/`double` key encoding is branch-free, so MSVC no longer compiles
  it to an unpredictable branch on the sign bit.

### Added
- **Presorted input in one pass.** The min/max analysis now also measures
  the ascending prefix, resuming the min/max scan where the prefix breaks.
  Ascending input returns after that pass (0 bytes, 0 writes);
  non-increasing input is reversed; a sorted prefix of at least half the
  input is kept, the tail sorted and one backward merge buffers only the
  tail. `std::sort` ratio on sorted input: 5.95× slower → 0.03–0.42×.
- **Counting fill.** A width-1 grid over keys that are their own element
  (Lemma 2's case) is finished by counting and writing, without moving
  elements: `uint8_t`, few distinct values and similar inputs need only
  the counters. `uint8_t` memory 10× → a few hundred bytes.
- **Free functions** (`Sort.hpp`): `stratum::sort`, `stable_sort`,
  `sort_by_key`, `stable_sort_by_key` (vectors and pointer ranges, with or
  without a `Workspace` and `Parameters`), and `sorted_indices` for any
  element type.
- **Key types:** every integral type but `bool` (including `char`,
  `wchar_t`, `char16_t`, `char32_t`), enums through their underlying type,
  and `float`/`double` in IEEE 754 `totalOrder` — bijective on bit
  patterns, so `-0.0` and NaN payloads survive; proved over all 2³² floats.
- **Stable variant.** Only three steps could reorder equal keys (the
  leaves' quicksort/introsort, and two reversals); the stable variant
  replaces exactly those — a bottom-up merge sort whose buffer is the
  leaf's own range in the other buffer, and run-preserving reversals.
- **`Workspace<E>`** and `sort(data, workspace) const`: one `const`
  sorter shared by any number of threads, each with its own workspace,
  without locks. `sort(data)` keeps 0.10.0's per-instance contract.
- `StratumSort(const Parameters&)`, `automaticParameters()`,
  `effectiveParameters(n)`, `scratchBytes()`, `releaseScratch()`,
  `sort(T* first, T* last)`.
- Tests: `tests/fast_division.cpp` (the reciprocal is exact, 12.2 M
  checks), `tests/float_keys.cpp` (exhaustive over 2³² floats),
  `tests/stability.cpp` (3 694 checks against `std::stable_sort` with
  identifying payloads), `tests/concurrency.cpp` (also under
  ThreadSanitizer, `make tsan`); API contract sections 10–13 (presorted
  paths, exception safety on the prefix path, automatic parameters,
  counting fill); floats and records in the differential fuzz. Contract
  checks: 132 → 154 (release), 148 → 170 (research).
- **Cross-platform benchmark:** `benchmarks/stratum_bench.cpp` and the
  `Benchmarks` workflow on Linux x86_64, macOS arm64 and Windows x86_64.
  18 input shapes including a McIlroy adversary against the platform's own
  `std::sort`; `std::sort` and `std::stable_sort` alternated on identical
  copies; medians with p10–p90, ns/element, elements/s, peak auxiliary
  bytes at the allocator; compiler, standard, flags, CPU, caches and cores
  printed with every table.
- `research/perf/` (the before/after instruments of this release) and
  `research/baselines/v0_10_0/` (the frozen 0.10.0 headers they compare
  against).

### Fixed
- A research-build test silently overflowed `int64_t` when building its
  adversary at `λ = 16` and tested a different input than intended; it now
  checks for overflow, and the 4096-element case is pinned at `(32, 64)`.

### Not changed
- The partition, the proof, the local-sort thresholds, `D = 6`, the
  ceilings `Λ = 10 000`, the strong exception guarantee, and the ABI
  namespace mechanism of `STRATUM_ENABLE_METRICS`.

## [0.10.0] — 2026-09-28

Fixes a correctness defect that shipped in 0.9.0, makes the `Θ(n)`
worst case hold for every constructor argument, and corrects
documentation claims that an audit of 0.9.0 found inaccurate. The public
API's signatures and the default parameters are unchanged.

### Fixed
- **The introsort fallback did not sort.** `heapSort`, which introsort
  falls back to when it exhausts its partitioning budget, never built a
  valid heap: `siftDown` located a node's children relative to the node
  being repaired instead of the heap's first slot. On that path the
  output was **not sorted**, with the multiset intact, and no internal
  assertion or sanitizer reported it. Minimal reproduction:
  `StratumSort<uint8_t>(1000, 64)` on `n = 385` with
  `v[i] = uint8_t(385 - i)`. Reaching the fallback needs a leaf of more
  than 384 elements *and* input that exhausts introsort's budget inside
  it. That is reachable with the **default parameters**, where a leaf
  above 384 can only come from exhausting the refinement depth: a
  constructed 392-element `int64_t` input — 385 values in the order
  `(385 − i) mod 190`, six values each peeled off at one refinement level,
  and one maximum — comes back unsorted from 0.9.0's
  `StratumSort<int64_t>()`. Larger `λ` or `t` make the fallback easier to
  reach. The dataset generators the test suites use do not reach it at the
  defaults; the inputs that do are constructed for it.

### Added
- API contract section 7, regression tests for that fallback: inputs
  measured to exhaust the introsort budget at sizes 385 to 4096;
  ascending, descending, duplicate-heavy, extreme and random leaves at
  sizes 384 to 4096; every fixed-width integer key type; and 400 seeded
  randomised and adversarial cases compared against `std::sort`; and the
  392-element default-parameter counterexample above.
- API contract section 9 and new checks in section 2, for the parameter
  ceilings: the clamping of `λ = n`, `n + 1`, `SIZE_MAX`, `INT_MAX`, `0`,
  `t < λ` and `λ ≪ t`; correctness at, below and far above the ceilings on
  sorted, reversed, duplicate-heavy, concentrated, bucket-boundary,
  depth-exhausting and worst-order inputs; and, in the research build,
  that `λ = t = n` keeps every leaf within the ceiling and the comparisons
  per element flat from `n = 20 000` to `160 000`.
- Contract checks go from 125 to 132 in the release build and from 134 to
  148 in the research build. Compiled against the 0.9.0 headers, with the
  two ceiling constants supplied since 0.9.0 does not define them, 6 and
  12 of them fail, the default-parameter counterexample among them.
- Per-platform packages, built and tested by CI on each platform's native
  runner: `stratumsort-v0.10.0-linux-x86_64.tar.gz`,
  `stratumsort-v0.10.0-macos-arm64.tar.gz` and
  `stratumsort-v0.10.0-windows-x86_64.zip`. Each is the source package
  plus a `BUILDINFO.txt` recording the native environment it was validated
  in, with a `.sha256` file; none contains compiled code, since the
  library is header-only. The source package `stratumsort-v0.10.0.zip` is
  unchanged in form.
- With `STRATUM_ENABLE_METRICS`, `metrics().algorithmUsage()` now counts
  `"HeapSort"` when the fallback runs, so a test can tell "introsort
  finished" from "introsort fell back". Without the macro it compiles to
  nothing.

### Changed
- **`λ` and `t` are capped at 10 000.** The constructor lowers larger
  values to 10 000 (`MAX_TARGET_ELEMENTS_PER_BIN`, `MAX_LEAF_THRESHOLD`),
  so hypothesis H2 — `λ` and `t` independent of `n` — is guaranteed by the
  code instead of assumed of the caller. Up to 0.9.0, `λ = t = n` put the
  whole array in one leaf and the worst case was `Θ(n log n)`; now the
  largest leaf a comparison sort can receive is about `5.7·10⁶` elements
  for any arguments, and `Θ(n)` holds in the worst case for every input
  and every argument. 10 000 is the smallest ceiling that changes no
  configuration the project uses. `D ≥ 1` is now a `static_assert`.

Documentation:
- Leaf cost is stated per local-sort band. 0.9.0 said every leaf costs
  `O(m log m)`, which is false for `64 < m ≤ 384`: that band goes to a
  quicksort with no depth limit, whose worst case is quadratic. Linearity
  still holds because `m` is bounded there by a constant.
- `Θ(n)` is stated with its hypotheses H1–H4, and the proof now sums the
  phases explicitly, with every constant independent of `n` and of the
  constructor arguments (`research/ALGORITHM.md` §8.9).
- Auxiliary memory: "about 3.1×" is replaced by measured peaks. For an
  8-byte key: 3.28× at the defaults on uniform input, 4.46× on
  refinement-heavy input, 16.56× on uniform input at `λ = t = 1`. For a
  1-byte key: about 10× for large `n` and 12.25× for small `n`.
- Comparison counts are no longer presented as evidence for the overall
  complexity: the counter only sees the local sorts. The table is kept,
  labelled as a measurement of that phase.
- The verified-platform tables list the compilers and systems CI actually
  runs: Ubuntu 24.04 with GCC 13.3.0 and Clang 18.1.3, macOS 26 with
  AppleClang 21.0.0, Windows Server 2025 with MSVC 19.51.36257.

## [0.9.0] — 2026-08-21

First public release candidate. `0.9.0`, not `1.0.0`, and the difference
is deliberate:

| what 0.9.0 means here | |
|---|---|
| Algorithm | complete; no known correctness or complexity defect |
| API | functional and documented, but with **no field history** — nobody has used it yet, so nothing has been stress-tested by contact with real callers |
| Tests | four suites, including a randomised differential fuzz under ASan/UBSan |
| Theory | `Θ(n)` documented together with the hypotheses it needs |
| Platform validation | Linux x86_64, macOS arm64 and Windows x86_64 all verified in CI |

`1.0.0` is a promise of API stability. All three target platforms are now
verified, but the promise still is not worth making: no caller outside
this repository has ever used the API, so nothing about its shape has been
tested by contact with real use.

### Added
- `stratum::StratumSort<T>` — header-only linear-time sort for integral
  keys up to 64 bits.
- Compile-time rejection of non-integral types, `bool`, and types wider
  than 64 bits, each with an explanatory message.
- Four test suites: edge cases and dataset sweep, 125/134 API contract
  checks across the whole `(λ, t)` parameter space, range-arithmetic
  limits under ASan/UBSan, and a randomised differential fuzz against
  `std::sort`.
- `make` and CMake builds; `make package` produces the distributable
  archive from an explicit file list.
- `docs/usage.md` — API reference and tuning guide.
- `research/` — the design record, the `Θ(n)` proof, the measurement
  methodology and the adversary battery. Not part of the package.

### Known limitations
- Verified on Linux x86_64 (GCC 14, Clang 19), macOS arm64 (Apple clang
  21, GCC 15) and Windows x86_64 (MSVC 19.51). Windows coverage is
  narrower than the other two: C++17 Release and Debug and C++20 Release,
  under CMake, without the Make build and without ASan/UBSan.
- One test assertion is not reachable on MSVC Debug. The research build
  deliberately does not offer the strong exception guarantee, and proving
  that requires making a small allocation fail; on MSVC's Debug standard
  library an allocation that size belongs to the library's own container
  bookkeeping, which allocates inside `noexcept` functions where a failure
  is `std::terminate` rather than an exception. The suite prints the case
  as `NOT CHECKED` there instead of asserting coverage it does not have.
  The production strong exception guarantee is unaffected and is verified
  on all three platforms.
- The distributed archive is byte-reproducible on a given platform, but
  the hash differs between macOS and Linux because the two `zip`
  implementations compress differently.
- `make sanitizers` and `make fuzz` need Clang on macOS: Homebrew GCC does
  not ship a linkable ASan there.
- Not stable, not thread-safe per instance, `Θ(n)` auxiliary memory.
