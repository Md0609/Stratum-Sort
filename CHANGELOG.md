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
| Platform validation | **narrow: arm64 macOS only** |

`1.0.0` is a promise of API stability. That promise is not worth making
until the library has been built somewhere other than one laptop. It is
not an immediate goal.

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
- Verified on Apple clang 21 and GCC 15, arm64 macOS only. Never built on
  Linux, Windows or x86.
- `make sanitizers` and `make fuzz` need Clang on macOS: Homebrew GCC does
  not ship a linkable ASan there.
- Not stable, not thread-safe per instance, `Θ(n)` auxiliary memory.
