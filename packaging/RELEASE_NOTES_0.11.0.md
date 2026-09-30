# Stratum Sort 0.11.0

> **Draft, not published.** Written before the pre-release memory study
> (research/history/V11_memoria.md): auxiliary memory can now be bounded
> on request — any `Workspace` budget down to 44 KB whatever `n` is, still
> `Θ(n)` time — except for the stable sorts of records. The default stays
> the partner buffer (`Workspace::kUnlimited`, 1.06× the input), which is
> what the memory sections below describe; the budgets must be added to
> them before a release.

0.10.0 closed with a list of seven limitations. This release takes them
one at a time, measures each on three platforms, and removes or shrinks
most of them — without touching the partition, so the `Θ(n)` worst-case
proof still holds, re-derived for the new engine.

Stratum Sort is a linear-time sorting algorithm for integer, enum and
floating-point keys, and for records sorted by one. Header-only, C++17,
no dependencies.

**Upgrading needs no source change.** Every 0.10.0 call site compiles and
sorts as before; `StratumSort<T>()` now picks λ from `n`, and every sorter
allocates about a third of the memory.

## At a glance

| 0.10.0 limitation | 0.11.0 |
|---|---|
| integral keys only | + enums, `float`/`double` (IEEE 754 `totalOrder`), records by key |
| not stable, no key/value | `stable_sort`, `stable_sort_by_key`, `sorted_indices` |
| not thread-safe per instance | one `const` sorter, any number of threads, one `Workspace` each |
| 3.28× the input in scratch | **1.06×**; nothing on sorted input |
| ~6× slower than `std::sort` on sorted input | **0.05–0.39×** |
| λ tuned to one cache | automatic λ from `n`, measured on four environments |
| timings from one machine | a benchmark workflow on Linux, macOS and Windows |

## Memory: 3.28× → 1.06×

0.10.0 allocated 26.25 bytes per 8-byte key: two cascading buffers, a
cached bucket index per element, a refinement tree and per-node
histograms. Only the fan-out is essential; the rest was implementation.

- **The caller's array is one of the two buffers.** The top-level pass
  copies the input into the workspace while it counts, and places it back.
- **No cached bucket index.** Both passes recompute `⌊offset/width⌋`,
  with an exact reciprocal (Granlund–Montgomery) instead of a divide
  instruction — which turned out 23–52% *faster* than storing the index.
- **No tree.** Nodes are refined depth-first and each leaf is finished the
  moment it is found. All histograms share one arena of `2⌈n/λ⌉ + 2`
  32-bit counters; a proof that it always suffices is in `ALGORITHM.md`
  §13.3.

| peak auxiliary bytes per element (`n = 10⁶`) | 0.10.0 | 0.11.0 |
|---|---|---|
| `int64_t`, default parameters | 26.25 | **8.50** |
| `int64_t`, `λ = t = 1` | 132.6 | 16.0 |
| `uint8_t`, random | ~10 | **< 0.01** |
| already sorted or reversed | 26.25 | **0** |
| many duplicates | 26.25 | **0** (counters only) |

Two allocations per sort instead of about thirty, and the peak no longer
depends on the input's shape. The remaining `n`-element buffer is
structural: an in-place distribution was implemented and measured 3.5–14×
slower at this fan-out, and it is not stable.

## Presorted input in one pass

The min/max scan now also measures the sorted prefix — and when the
prefix breaks, its extremes are its two ends, so the scan *resumes* there
instead of restarting. One pass in every case. Then:

- ascending: done — no write, no allocation;
- non-increasing: one reversal;
- a sorted prefix of at least half the input: sort only the tail, then
  one backward merge that buffers only the tail;
- anything else: the engine, as before.

Two alternatives were measured and rejected: counting descents inside the
min/max pass costs +27% on *every* input; a separate prefix scan costs a
whole extra pass when the prefix breaks late.

## Keys, records and stability

The engine now sees elements through a key map into 64-bit unsigned
integers that preserves their order:

- **integral types** (signed ones with the sign bit flipped — every span
  and bucket is the number 0.10.0 computed), including `char`, `wchar_t`,
  `char16_t`, `char32_t`;
- **enums**, through their underlying type;
- **`float` and `double`**, in IEEE 754 `totalOrder`: `-NaN < -∞ < … <
  -0 < +0 < … < +∞ < +NaN`. The map is a bijection, so `-0.0` and NaN
  payloads survive, and it is proved over all 2³² `float` patterns. Without
  NaN, every result is also a valid `std::sort` result.
- **records**, `sort_by_key(v, key)` and `stable_sort_by_key(v, key)`,
  for trivially copyable records; `sorted_indices(first, last, key)` for
  anything else.

**Stability, exactly.** Only three steps of the algorithm can reorder
equal keys: the quicksort/introsort that finishes a leaf above 64
elements, reversing a descending leaf, and reversing a non-increasing
input. The distribution itself is stable. The stable variant replaces
exactly those three — a bottom-up merge sort whose buffer is the leaf's
own range in the other buffer (free by construction), and reversals that
restore the order of equal runs. It runs at the speed of the unstable
variant on the record benchmarks and uses the same memory. 3 694 checks
compare it with `std::stable_sort` on records carrying their original
position, including thousands of duplicates per key and inputs that force
the merge sort.

## Threads

A `Workspace<E>` holds everything a sort writes besides your array.
`sorter.sort(data, workspace)` is `const`: one sorter can serve any
number of threads, each with its own workspace, with no locks and no
global state. The free functions build a workspace per call.
`sorter.sort(data)` keeps 0.10.0's contract (the instance's own
workspace, one thread at a time). A ThreadSanitizer suite runs in CI.

## Automatic λ

0.10.0's λ = 32 was the optimum on one machine at `n ≈ 10⁶` and came with
a warning to raise it for larger inputs. With the per-bin overhead gone,
the optimum moved. Every λ from 8 to 1024 was re-measured, alternated in
one process, on four environments (Xeon/GCC, GitHub's Linux runner,
Apple M1/AppleClang, AMD EPYC/MSVC). Worst slowdown against the best λ of
each row:

| | `n ≤ 10⁶`, λ = 16 | λ = 32 | `n = 10⁷`, λ = 16 | λ = 32 |
|---|---|---|---|---|
| Xeon, Linux, GCC | 1.10 | 1.29 | 1.12 | 1.00 |
| GitHub runner, Linux, GCC | 1.09 | 1.39 | 1.09 | 1.17 |
| Apple M1, AppleClang | 1.17 | 1.40 | 1.17 | 1.10 |
| AMD EPYC, Windows, MSVC | 1.05 | 1.15 | 1.27 | 1.04 |

So `StratumSort<T>()` and `Parameters{}` now use `(λ, t) = (16, 32)` up to
`2²²` elements and `(32, 64)` above. A function of `n` only — never of
the cache or the core count, so that the order `sort_by_key` gives equal
keys cannot change between machines. Explicit arguments keep 0.10.0's
exact meaning, and `effectiveParameters(n)` reports what a call uses. As
a side effect, the largest leaf an automatic sort can hand a comparison
sort drops from 18 090 to 9 045 elements.

## Counting instead of moving

When the key span of a node is below its fan-out, every bucket holds a
single key value (Lemma 2 of the proof). If the element *is* its key —
integers, enums, floats — the node is finished by counting and writing
each value its number of times, without moving anything. At the top level
that needs no element buffer at all: `uint8_t` keys and few-distinct-value
inputs sort with a few hundred bytes of scratch.

## Performance

Measured by the new `Benchmarks` workflow on GitHub's runners, one run
(commit `f90ac4b`): `benchmarks/stratum_bench.cpp`, Stratum and
`std::sort` alternated on identical copies, median of 9 repetitions, a
fresh sorter per call. Time relative to `std::sort`, `n = 10⁶`:

| input | Linux x86_64 (Xeon), GCC 13 | macOS arm64 (M1), AppleClang 21 | Windows x86_64 (EPYC), MSVC 19.51 |
|---|---|---|---|
| random `int64_t` | **0.33×** | **0.57×** | **0.37×** |
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

These are shared virtual machines that GitHub does not guarantee to be
the same hardware from run to run: between two runs of identical integer
code, `int64` random on the Linux runner moved from 0.23× to 0.33×. Run
`make bench` on the hardware you care about.

Against 0.10.0 itself — same machine, same process, the two versions
alternated on identical inputs, each against the adversary built for its
own λ (Intel Xeon, GCC, `research/perf/VersionTimings.cpp`): **all 18
shapes are faster.** At `n = 10⁶`: random −39%, sorted −96%, reversed
−95%, nearly sorted −62%, duplicates −85%, normal −33%, clustered −53%,
adversarial −12%, worst case −26%. At `n = 10⁷`: random −29%, sorted
−95%, nearly sorted −29%, duplicates −71%, adversarial −41%.

## Where it still loses

- **Inputs built against its own partition** — 1.67× on Apple M1, 1.23×
  under MSVC. The adversary exhausts the refinement depth so that leaves
  reach comparison sorts; the bound keeps them under 9 045 elements, which
  keeps the sort linear, but not cheaper than `std::sort` on every CPU.
- **Low-entropy keys on Apple M1** (1.66×).
- **Nearly-sorted input under MSVC** — 1.23× at `n = 10⁶`, 1.63× at
  `10⁵`; sorted input with a few outliers, 1.09× and 1.55×.
- **Floating point under MSVC and nearly-sorted floating point** —
  nearly sorted: 1.07–1.33× on Linux, 1.42–1.47× on the M1, 2.84–2.86×
  under MSVC; few distinct values under MSVC: 1.36–1.38×. Uniformly drawn
  floats are exponentially denser near the top of their range in key
  space and need an extra refinement level. A branch on the sign bit made
  MSVC worse still; removing it took few-distinct floats from 1.79× to
  1.36× and random floats from 0.54× to 0.49×, the rest is structural.
- **Memory**: the stable sorts of records still need `n` elements of
  scratch where `std::stable_sort` needs `n/2`; every other sort is
  bounded (see the note at the top).

## The proof

The partition is unchanged — grids, buckets, stopping rule, local sorts,
and even the order of elements inside each leaf — so Lemmas 1–5 and the
residual theorem hold verbatim; the research build's counters agree with
0.10.0's exactly on the parameter-ceiling adversaries. `research/ALGORITHM.md` §13
maps every 0.10.0 name to the new engine and re-derives what the new
machinery touches: the hypotheses (H1 now about the key), the memory
accounting and the arena bound, the cost of galloping, the linearity of
the presorted paths, the stable variant, the counting fill, the automatic
parameters and the exception guarantee.

## Tests

- `tests/fast_division.cpp`: the reciprocal is exact — every divisor below
  2¹⁶, every power of two ± 2, the widths the algorithm produces, 4 M random
  pairs (12.2 M checks), also under ASan/UBSan.
- `tests/float_keys.cpp`: `totalOrder` exhaustively over 2³² floats.
- `tests/stability.cpp`: 3 694 checks against `std::stable_sort`.
- `tests/concurrency.cpp`: shared sorter, one workspace per thread; also
  under ThreadSanitizer.
- API contract sections 10–13: presorted paths, the strong exception
  guarantee on the prefix path, automatic parameters, counting fill, and
  that inputs of at most λ elements allocate nothing. 155 checks in
  release (0.10.0: 132), 171 in research (148).
- Differential fuzz: presorted shapes, floats, records against
  `std::stable_sort`.

## Upgrading

Replace `include/stratum/` and rebuild. Behaviour to be aware of:

- `StratumSort<T>()` chooses λ per call: the accessors report `(16, 32)`,
  `effectiveParameters(n)` the values a sort of `n` uses. Pass explicit
  arguments to keep 0.10.0's fixed `(32, 64)`.
- `DEFAULT_TARGET_ELEMENTS_PER_BIN` is 16 and `DEFAULT_LEAF_THRESHOLD` 32.
- The class is still unstable; with integral keys that is unobservable.
- Nothing else changes for 0.10.0 callers.

## Install

| file | built and tested on |
|---|---|
| `stratumsort-v0.11.0.zip` | source package (`make package`) |
| `stratumsort-v0.11.0-linux-x86_64.tar.gz` | Ubuntu 24.04 x86_64, GCC |
| `stratumsort-v0.11.0-macos-arm64.tar.gz` | macOS 26 arm64, AppleClang |
| `stratumsort-v0.11.0-windows-x86_64.zip` | Windows Server 2025 x86_64, MSVC |

As in 0.10.0, the platform packages hold no compiled code: each is the
source package plus a `BUILDINFO.txt` recording the native environment it
was validated in, with a `.sha256` beside it.

## Why 0.11.0 and not 1.0.0

The free-function API, `Workspace` and `Parameters` are new and have no
field history yet. 1.0.0 is a promise of API stability, and it should
follow use, not precede it.

## License

MIT.
