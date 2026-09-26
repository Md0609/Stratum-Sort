# Stratum Sort 0.10.0

Fixes a sorting defect that shipped in 0.9.0. **Every 0.9.0 user should
upgrade:** on the affected path the output was not sorted, and nothing
reported it.

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
budget inside that leaf. Every confirmed reproduction uses non-default
constructor arguments, such as `λ = 1000`. With the defaults the fallback
was not reached in 527 targeted adversarial cases during the audit. That
is a search that came up empty, not a proof, so upgrade whatever
parameters you use.

## The fix

`siftDown(arr, base, root, end)` now takes the heap's first slot (`base`)
separately from the node under repair (`root`), and both call sites pass
the leaf's first index as `base`. The change is confined to `heapSort` and
`siftDown`: both are private, and neither allocates.

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
  comparison and prints the seed of any counterexample.

Contract checks go from 125 to 129 in the release build and from 134 to
140 in the research build. Compiled against the 0.9.0 headers the same
test file reports 126/129 and 135/140: the three correctness checks fail
in both builds, and in the research build so do the two that assert the
fallback was actually reached.

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
- **`Θ(n)`** is qualified: it holds for `λ` and `t` fixed independently of
  `n`, under hypotheses H1–H4. If `λ` or `t` scale with `n`, which the
  constructor allows, the worst case is `Θ(n log n)`.
- **Auxiliary memory.** "About 3.1×" is replaced by measured peaks. For an
  8-byte key: 3.28× at the defaults on uniform input, 4.46× on
  refinement-heavy input, 16.56× on uniform input at `λ = t = 1`. For a
  1-byte key: about 10× for large `n` and 12.25× for small `n`.
- **Comparison counts** are no longer presented as evidence for the
  overall complexity. The counter only sees the local sorts; the table is
  kept, labelled as a measurement of that phase.

## What did not change

- The public API: the constructor
  `StratumSort(targetElementsPerBin = 32, leafThreshold = 64)`, `sort`,
  `targetElementsPerBin()` and `leafThreshold()`.
- `Config.hpp`, the default parameters, and the analysis, distribution,
  refinement and join code.
- Peak memory: identical to 0.9.0 in every configuration measured.
- The ODR guard: `STRATUM_ENABLE_METRICS` still selects the inline ABI
  namespace, and translation units that disagree still fail to link.

## Upgrading

Replace `include/stratum/` with the one in this archive and rebuild. No
source change is needed.

## Install

Download `stratumsort-v0.10.0.zip` from this release, then either copy
`include/stratum/` into your project:

```bash
c++ -std=c++17 -O2 -Iinclude your_program.cpp
```

or use CMake:

```cmake
add_subdirectory(stratumsort-v0.10.0)
target_link_libraries(your_target PRIVATE StratumSort::stratumsort)
```

```bash
make test          # three suites: edge cases, 129 + 140 contract checks
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
| Code identity | This release adds version strings and documentation on top of commit `2c803c7`. Library, tests, examples, benchmark and datasets are identical to it, and the test, contract, timing and example binaries rebuilt after the version change are byte-identical to those built before it. |
| CI on `2c803c7` | 16/16 jobs green: GCC and Clang on `ubuntu-latest` at C++17 Release and Debug and C++20 and C++23 Release; Apple clang on `macos-latest` at C++17 Release and Debug; MSVC on `windows-latest` at C++17 Release and Debug and C++20 Release. Each of those 13 jobs also builds a headers-only external consumer. The other 3 run the ASan/UBSan suites and a 50 000-case fuzz, once with GCC and once with Clang, and build the package, checking it for leaks and byte-for-byte reproducibility. |
| Linux x86_64, local | GCC 13.3 and Clang 18.1: `make test` 129/129 and 140/140, `make sanitizers` clean and `make fuzz N=200000` without disagreement, with both compilers. CMake 3.28, Release and Debug: 5/5, including the ODR link guard. `make package-verify` OK. |

Not re-run for this release: macOS with GCC, which 0.9.0 reported as
verified locally, and the wall-clock timings in the README, which are
unchanged from 0.9.0.

## Known limitations

As in 0.9.0: Windows is exercised with MSVC under CMake only, without
C++23, the Make build or ASan/UBSan; one research-build assertion prints
`NOT CHECKED` on MSVC Debug; the archive is byte-reproducible per
platform but its hash differs between macOS and Linux; the sort is not
stable and not thread-safe per instance, and uses `Θ(n)` auxiliary memory.

Also: the 16.56× figure above is **not an upper bound at `λ = 1`**. On
the adversarial input in `datasets/DatasetGenerator.hpp`
(`adversarialPeeling`, 8-byte key), peak auxiliary memory at `λ = 1`
measured between 35.8× and 39.9× the input, with `t = 1` or `t = 64`
and `n` from 10⁴ to 4·10⁶.

## Why 0.10.0 and not 1.0.0

`1.0.0` is a promise of API stability. This release fixes a defect and
corrects documentation; it does not make that promise.

## License

MIT.
