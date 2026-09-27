# Stratum Sort 0.10.0

Fixes a sorting defect that shipped in 0.9.0 and is reachable with the
default parameters. **Every 0.9.0 user should upgrade:** on the affected
path the output was not sorted, and nothing reported it.

It also makes the `Θ(n)` worst case hold for every constructor argument:
`λ` and `t` are now capped at 10 000, so a caller can no longer make them
grow with `n`.

Stratum Sort is a linear-time sorting algorithm for integral keys up to
64 bits. Header-only, C++17, no dependencies.

## The defect

A leaf of more than 384 elements is finished by introsort, and introsort
falls back to `heapSort` once it exhausts its partitioning budget. That
fallback never built a valid heap. `siftDown` computed a node's children
as if the node being repaired were the first slot of the heap, but heap
indexing is defined relative to the heap's actual first slot. The two
agree only when the node *is* the first slot, which is true in the
extraction loop and false in every heapify step but the last.

The result was **output that was not sorted**, with the multiset of values
intact, and neither the internal assertions nor ASan/UBSan reported it.
Minimal reproduction against 0.9.0:

```cpp
std::vector<uint8_t> v(385);
for (std::size_t i = 0; i < v.size(); ++i) v[i] = uint8_t(385 - i);

stratum::StratumSort<uint8_t> sorter(1000, 64);
sorter.sort(v);   // 0.9.0: v is not sorted. 0.10.0: v is sorted.
```

**Who is affected.** Reaching the fallback takes two things at once: a
leaf of more than 384 elements, and input that makes introsort exhaust its
budget inside that leaf. With the **default parameters** a leaf that large
can only come from exhausting the refinement depth, and that input can be
built. This 392-element `int64_t` array comes back unsorted from 0.9.0's
`StratumSort<int64_t>()`:

- 385 values `(385 − i) mod 190`, for `i = 0 … 384`, in that order;
- six values `190·13^k`, for `k = 1 … 6`, each of which one refinement
  level peels off;
- one maximum, `13·(190·13⁶ + 1) − 1`, which makes the first top-level
  bucket hold everything else.

Larger `λ` or `t` make the fallback easier to reach, as in the `uint8_t`
example above. The dataset generators the test suites use do not reach it
at the defaults; the inputs that do are constructed for it. Upgrade
whatever parameters you use.

## The fix

`siftDown(arr, base, root, end)` now takes the heap's first slot (`base`)
separately from the node under repair (`root`), and both call sites pass
the leaf's first index as `base`. The change is confined to `heapSort` and
`siftDown`: both are private, and neither allocates.

## Worst-case `Θ(n)` for every constructor argument

Up to 0.9.0 the constructor accepted any `λ` and `t`. The `Θ(n)` bound
assumed both were constants independent of `n` (hypothesis H2), but a
caller could pass `data.size()` for both: the whole array then became one
leaf, finished by introsort, and the worst case was `Θ(n log n)`.

The constructor now clamps `λ` into `[1, 10 000]` and `t` into
`[λ, 10 000]`, and `D ≥ 1` is a `static_assert`. H2 is therefore a
property of the code. The largest leaf a comparison sort can receive is at
most `10 000 · 2^(64/7) ≈ 5.7·10⁶` elements for any arguments, and the
proof in `research/ALGORITHM.md` §8.9 sums the phases with every
constant independent of `n` and of the arguments: `T(n) = Θ(n)` in the
worst case, for every input and every configuration.

10 000 is the smallest ceiling that changes no configuration the project
uses: its tests pass `t = 10 000` and `λ` up to 4 385. A larger ceiling
could not lower the worst-case constant, which is a maximum over every
admissible `(λ, t)`: a larger ceiling only adds configurations, and it
raises the leaf bound above. The accessors report the clamped values.

## New tests

Section 7 of `tests/api_contract.cpp` covers the fallback directly:

- inputs measured to exhaust the introsort budget, at sizes 385 to 4096;
- ascending, descending, duplicate-heavy, extreme and random leaves at
  sizes 384 to 4096, so both sides of the quicksort/introsort boundary are
  pinned;
- every fixed-width integer key type, including the `uint8_t`
  counterexample above;
- 400 seeded randomised and adversarial cases compared against
  `std::sort`, which checks "sorted" and "same multiset" in one
  comparison and prints the seed of any counterexample;
- the 392-element default-parameter counterexample above.

Section 9, with new checks in section 2, covers the ceilings: the clamping
of `λ = n`, `n + 1`, `SIZE_MAX`, `INT_MAX`, `0`, `t < λ` and `λ ≪ t`;
correctness at, below and far above the ceilings on sorted, reversed,
duplicate-heavy, concentrated, bucket-boundary, depth-exhausting and
worst-order inputs (a McIlroy adversary for quicksort, the introsort-
exhausting sawtooths for heapSort); and, in the research build, that
`λ = t = n` keeps every leaf within the ceiling and the comparisons per
element flat from `n = 20 000` to `160 000`.

Contract checks go from 125 to 132 in the release build and from 134 to
148 in the research build. Compiled against the 0.9.0 headers, with the
two ceiling constants supplied since 0.9.0 does not define them, 6 of the
132 and 12 of the 148 fail, the default-parameter counterexample among
them.

Those two are possible because, with `STRATUM_ENABLE_METRICS`,
`metrics().algorithmUsage()` now counts `"HeapSort"` when the fallback
runs, so "introsort finished" and "introsort fell back" can be told
apart. Without the macro the call compiles to nothing.

## Documentation corrections

An audit of 0.9.0 found four claims that did not hold. They are corrected
in the repository and in this release's archive. The 0.9.0 release page
and its archive keep their original text.

- **Leaf cost.** 0.9.0 said every leaf costs `O(m log m)`. That is false
  for `64 < m ≤ 384`, which goes to a quicksort with no depth limit and a
  quadratic worst case. The cost is now stated per band, and linearity
  still holds: with `m ≤ C` for a fixed `C`, `m² ≤ C·m`.
- **`Θ(n)`** is stated with its hypotheses H1–H4, and H2 is now enforced
  by the constructor (see above).
- **Auxiliary memory.** "About 3.1×" is replaced by measured peaks. For an
  8-byte key: 3.28× at the defaults on uniform input, 4.46× on
  refinement-heavy input, 16.56× on uniform input at `λ = t = 1`. For a
  1-byte key: about 10× for large `n` and 12.25× for small `n`.
- **Comparison counts** are no longer presented as evidence for the
  overall complexity. The counter only sees the local sorts; the table is
  kept, labelled as a measurement of that phase.

## What did not change

- The public API's signatures: the constructor
  `StratumSort(targetElementsPerBin = 32, leafThreshold = 64)`, `sort`,
  `targetElementsPerBin()` and `leafThreshold()`. What changes is that
  arguments above 10 000 are lowered to it.
- The default parameters, the local-sort thresholds, and the analysis,
  distribution, refinement and join code.
- Peak memory at the defaults: identical to 0.9.0 in every configuration
  measured.
- The ODR guard: `STRATUM_ENABLE_METRICS` still selects the inline ABI
  namespace, and translation units that disagree still fail to link.

## Upgrading

Replace `include/stratum/` with the one in this archive and rebuild. No
source change is needed. A caller that passed `λ` or `t` above 10 000
gets 10 000; the output is the same sorted array.

## Install

This release has one source package and one package per platform:

| file | built and tested on |
|---|---|
| `stratumsort-v0.10.0.zip` | source package (`make package`) |
| `stratumsort-v0.10.0-linux-x86_64.tar.gz` | Ubuntu 24.04 x86_64, GCC |
| `stratumsort-v0.10.0-macos-arm64.tar.gz` | macOS 26 arm64, AppleClang |
| `stratumsort-v0.10.0-windows-x86_64.zip` | Windows Server 2025 x86_64, MSVC |

The library is header-only, so a platform package holds no compiled code:
it is the source package, unchanged, plus a `BUILDINFO.txt` recording the
runner image, the compiler and the test results of the CI job that built
it on that platform. What it adds is the record of the native environment
the release was validated in. Each file has a `.sha256` beside it:

```bash
sha256sum -c stratumsort-v0.10.0-linux-x86_64.tar.gz.sha256        # Linux
shasum -a 256 -c stratumsort-v0.10.0-macos-arm64.tar.gz.sha256      # macOS
```

```powershell
# Windows: prints True when the file matches
(Get-FileHash stratumsort-v0.10.0-windows-x86_64.zip -Algorithm SHA256).Hash -eq `
    (Get-Content stratumsort-v0.10.0-windows-x86_64.zip.sha256).Split(' ')[0]
```

What is byte-reproducible, and what is not:

- **Source package.** The same tree and the same `zip` implementation give
  the same bytes; CI builds it twice and compares. Linux and macOS `zip`
  compress differently, so their hashes differ.
- **Platform package, same environment.** Every entry has a fixed time,
  owner and mode, in sorted order, and `BUILDINFO.txt` has no timestamp:
  the same source package on the same runner image, with the same
  compiler and CMake, gives the same bytes. Checked locally on Linux; the
  CI job checks only that two archivings of one build agree.
- **Across runners.** Not reproducible, by construction: `BUILDINFO.txt`
  names the runner image, compiler and CMake, and the format is `tar.gz`
  or `zip`. A local build never matches CI's, as it records neither commit
  nor image. Every file except `BUILDINFO.txt` is byte-identical to the
  source package's.

Then either copy `include/stratum/` into your project:

```bash
c++ -std=c++17 -O2 -Iinclude your_program.cpp
```

or use CMake:

```cmake
add_subdirectory(stratumsort-v0.10.0)   # or stratumsort-v0.10.0-<os>-<arch>
target_link_libraries(your_target PRIVATE StratumSort::stratumsort)
```

```bash
make test          # three suites: edge cases, 132 + 148 contract checks
make timings       # release timings against std::sort
make examples      # minimal working example
```

Two further suites need a linkable AddressSanitizer, so they are separate
targets; on macOS they require Clang, since Homebrew GCC has no linkable
ASan there:

```bash
make sanitizers
make fuzz N=200000
```

## Verified for this release

| | |
|---|---|
| Linux x86_64, local | Ubuntu 24.04, GCC 13.3.0 and Clang 18.1.3: `make test` 132/132 and 148/148, `make sanitizers` clean, `make fuzz N=200000` without disagreement and `make examples`, with both compilers; `make research`, `AdversaryBattery`, `ComplexityScaling` and `ComplexityReview`. CMake 3.28 at C++17 Release and Debug and C++20 and C++23 Release: 5/5 with each compiler, including the ODR link guard, and the headers-only external consumer built with that same compiler. `make package-verify` OK; the Linux platform package built, tested and byte-reproducible across runs. |
| CI | The run for the release commit: 13 build-and-test jobs on Ubuntu 24.04 (GCC 13.3.0 and Clang 18.1.3, C++17/20/23), macOS 26 (AppleClang 21.0.0, C++17) and Windows Server 2025 (MSVC 19.51, C++17 and C++20), each also building the external consumer with its own compiler; the ASan/UBSan and fuzz jobs with GCC and Clang; the source-package job; and the three platform-package jobs. |

Not re-run for this release: GCC 14 and Clang 19 with libc++ on Linux,
and GCC 15 on macOS, which 0.9.0 reported as verified locally; and the
wall-clock timings in the README, which are unchanged from 0.9.0.

## Known limitations

As in 0.9.0: Windows is exercised with MSVC under CMake only, without
C++23, the Make build or ASan/UBSan; one research-build assertion prints
`NOT CHECKED` on MSVC Debug; the source package is byte-reproducible per
platform but its hash differs between macOS and Linux; the sort is not
stable and not thread-safe per instance, and uses `Θ(n)` auxiliary memory.

Also: the 16.56× figure above is **not an upper bound at `λ = 1`**. On
the adversarial input in `datasets/DatasetGenerator.hpp`
(`adversarialPeeling`, 8-byte key), peak auxiliary memory at `λ = 1`
measured between 35.8× and 39.9× the input, with `t = 1` or `t = 64`
and `n` from 10⁴ to 4·10⁶.

## Why 0.10.0 and not 1.0.0

`1.0.0` is a promise of API stability. This release fixes a defect, bounds
the two tuning parameters and corrects documentation; it does not make
that promise.

## License

MIT.
