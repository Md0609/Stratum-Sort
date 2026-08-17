# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.9.0] — unreleased

First public release candidate. The algorithm, its proof, its tests and
its packaging are complete; the version is `0.9.0` rather than `1.0.0`
because the API has no field history yet and the project has been built on
one platform only. See "Portability" in the README.

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
