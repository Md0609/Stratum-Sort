# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.10.0] — unreleased

Fixes a correctness defect that shipped in 0.9.0, and corrects
documentation claims that an audit of 0.9.0 found inaccurate. The public
API, `Config.hpp` and the tuning parameters are unchanged.

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
  it. Every confirmed reproduction uses non-default parameters; with the
  defaults it was not reproduced in 527 targeted adversarial cases, which
  is not a proof that the defaults are unaffected.

### Added
- API contract section 7, regression tests for that fallback: inputs
  measured to exhaust the introsort budget at sizes 385 to 4096;
  ascending, descending, duplicate-heavy, extreme and random leaves at
  sizes 384 to 4096; every fixed-width integer key type; and 400 seeded
  randomised and adversarial cases compared against `std::sort`.
  Contract checks go from 125 to 129 in the release build and from 134 to
  140 in the research build. Compiled against the 0.9.0 headers, the
  section fails.
- With `STRATUM_ENABLE_METRICS`, `metrics().algorithmUsage()` now counts
  `"HeapSort"` when the fallback runs, so a test can tell "introsort
  finished" from "introsort fell back". Without the macro it compiles to
  nothing.

### Changed
Documentation only.
- Leaf cost is stated per local-sort band. 0.9.0 said every leaf costs
  `O(m log m)`, which is false for `64 < m ≤ 384`: that band goes to a
  quicksort with no depth limit, whose worst case is quadratic. Linearity
  still holds because `m` is bounded there by a constant.
- `Θ(n)` is qualified: it holds for `λ` and `t` fixed independently of
  `n`, under H1–H4. If `λ` or `t` scale with `n`, which the constructor
  allows, the worst case is `Θ(n log n)`.
- Auxiliary memory: "about 3.1×" is replaced by measured peaks. For an
  8-byte key: 3.28× at the defaults on uniform input, 4.46× on
  refinement-heavy input, 16.56× on uniform input at `λ = t = 1`. For a
  1-byte key: about 10× for large `n` and 12.25× for small `n`.
- Comparison counts are no longer presented as evidence for the overall
  complexity: the counter only sees the local sorts. The table is kept,
  labelled as a measurement of that phase.

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
