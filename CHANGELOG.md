# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.9.0] — unreleased

First public release candidate. `0.9.0`, not `1.0.0`, and the difference
is deliberate:

| what 0.9.0 means here | |
|---|---|
| Algorithm | complete; no known correctness or complexity defect |
| API | functional and documented, but with **no field history** — nobody has used it yet, so nothing has been stress-tested by contact with real callers |
| Tests | four suites, including a randomised differential fuzz under ASan/UBSan |
| Theory | `Θ(n)` documented together with the hypotheses it needs |
| Platform validation | Linux x86_64 and macOS arm64 verified; **Windows and MSVC never built** |

`1.0.0` is a promise of API stability. Two of the three target platforms
are now verified; Windows with MSVC is the one that remains, and until it
is built and tested there the promise is not worth making.

### Added
- `stratum::StratumSort<T>` — header-only linear-time sort for integral
  keys up to 64 bits.
- Compile-time rejection of non-integral types, `bool`, and types wider
  than 64 bits, each with an explanatory message.
- Four test suites: edge cases and dataset sweep, 124/133 API contract
  checks across the whole `(λ, t)` parameter space, range-arithmetic
  limits under ASan/UBSan, and a randomised differential fuzz against
  `std::sort`.
- `make` and CMake builds; `make package` produces the distributable
  archive from an explicit file list.
- `docs/usage.md` — API reference and tuning guide.
- `research/` — the design record, the `Θ(n)` proof, the measurement
  methodology and the adversary battery. Not part of the package.

### Known limitations
- Verified on Linux x86_64 (GCC 14, Clang 19) and macOS arm64 (Apple
  clang 21, GCC 15). **Never built on Windows or with MSVC.**
- The distributed archive is byte-reproducible on a given platform, but
  the hash differs between macOS and Linux because the two `zip`
  implementations compress differently.
- `make sanitizers` and `make fuzz` need Clang on macOS: Homebrew GCC does
  not ship a linkable ASan there.
- Not stable, not thread-safe per instance, `Θ(n)` auxiliary memory.
