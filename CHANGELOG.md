# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
