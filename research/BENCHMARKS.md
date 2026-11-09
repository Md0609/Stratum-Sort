# Measurement methodology

The numbers in this project are only useful if the way they were produced
is known. This document is that.

## Build configurations

Three, and confusing them invalidates everything:

| | metrics | assertions | timings meaningful? |
|---|---|---|---|
| **release** | no | no | **yes — this is the only one** |
| **test** | no | yes | no: ~5% slower, one assertion sits in the innermost loop |
| **research** | yes | yes | no: several times slower |

The research build exists because the deterministic counters — comparisons,
bins, subdivisions, depth, per-phase time — are exact and reproducible
where the clock is not. It is a laboratory instrument. Quoting a research
timing as a performance result is the single easiest way to publish a
wrong number, and it has happened twice in this project: once when a
change appeared to give −19.8% in the research build and ~0% in release
(it had removed a million counter increments that release does not have),
and once when the README's own headline table was measured with
`make baseline`, which cannot be anything but a research build because it
reads `sorter.metrics()`.

**Which tool to use, therefore:**

| | build | quote its times? |
|---|---|---|
| `make timings` | release | **yes** — ratios against `std::sort`, with their ranges |
| `make ab` | release (two commits) | **yes — and the only source of a verdict** (G3) |
| `make baseline` | research | no; quote its *counters* |
| `make profile` | research | no; relative phase shares and allocation counts only |
| `make counters` | research | it has no times; its counters are exact (G2, A2/A3) |
| `make overhead` | both | only as "research costs X% more than release" |

The gap between the two is not a constant you can divide out. Measured
here, release was 5–20% faster than research, and the spread was uneven
across datasets — it lands wherever the counters happen to be incremented
most. That is why they are separate binaries rather than one binary with a
flag.

## Protocol

- **Alternate.** Build both versions, run A, B, A, B… in one session.
  Separately collected batches produced a false 8.8% result that reversed
  under alternation.
- **Report ranges, not just medians.** If the ranges overlap, there is no
  result. Several apparent 6–14% effects turned out to be overlapping.
- **Significance threshold: 6%.** Measured, not chosen: three independent
  runs of the same binary disagree by 1.5–5.9% on this machine.
- **Prefer counters.** A change that alters no deterministic counter
  cannot have changed the algorithm's behaviour, so any timing difference
  is noise by construction. That argument settled several disputes that
  the clock could not.

## Known measurement traps in this codebase

- **`ManyRepeated` is not resolvable by clock.** Its dispersion is ~18%,
  and its counters are provably identical across every parameter setting,
  so a timing difference there is always noise.
- **`Concentrated` has a 12% noise floor**, established by measuring
  configurations that provably do identical work and still disagreed by
  that much.
- **The adversarial dataset is built against a specific λ.** Comparing it
  across configurations without rebuilding it produces conclusions that
  are artefacts of the generator. A parameter change that appeared to give
  −60% on it gave exactly 0% once the adversary was rebuilt.
- **Results do not transfer between machines.** A parameter that won by
  18–23% on an x86 Xeon with libstdc++ lost by 13% on an Apple M4 with
  libc++. The phase breakdown inverted too: the distribution phase went
  from 47% of the time to 15%.

## The battery (objective.md §4)

One definition, in `datasets/Battery.hpp`, used by `timings`, the A/B,
`make counters`, the differential fuzz and `research_AdversaryBattery`, so
"all the benchmarks" means the same 26 inputs everywhere:

| Group | Datasets |
|---|---|
| current (10) | RandomUniform, SortedAscending, SortedDescending, ManyRepeated, NormalGaussian, Concentrated, SmallRangeManyEl, HugeRangeFewEl, FullRangeExtremes, AdversarialPeeling |
| new (8) | NearlySorted (ascending + `n/100` random transpositions), SortedPlusTail (ascending, last 1 % replaced by uniform values in `[0, n)`), OrganPipe (`0…peak…0`), Sawtooth (ascending runs of 1000), FewDistinct2 / 16 / 256 (k values evenly spread over `[0, 2³⁰)`), AllEqual |
| adversary (8) | Adv1AllEqual … Adv8TotalCost: the eight families of `research/tools/AdversaryBattery.cpp`, which now takes them from the same header |

- **Bit-for-bit continuity.** For `int64_t` the ten current datasets are
  `DatasetGenerator`'s own output, unchanged (pinned by `tests/main.cpp`),
  and `research_AdversaryBattery` prints exactly what it printed before.
- **Types.** `int64_t` and `uint32_t`. For `uint32_t`, the datasets whose
  values fit are the same values; HugeRangeFewEl, FullRangeExtremes and
  AdversarialPeeling are re-derived from the 32-bit key universe, since
  their point is the span of the type.
- **The adversaries are rebuilt against the configuration being measured.**
  The header does not include the library; every caller passes the λ the
  sorter actually uses and the `MAX_SUBDIVISION_DEPTH` of the headers it was
  compiled with. In an A/B, each side's binary therefore builds its own
  adversary (the trap described below cannot happen).
- **n/a, not a substitute.** Adv7MaximalLeaf needs a core larger than `n`
  below `n ≈ 10⁵`, and Adv8TotalCost needs at least two groups; where a
  family cannot be built the tools print `n/a`. Adv8TotalCost has `k` whole
  groups, so its real size is slightly below `n`; every tool prints `n_real`.
- Every dataset comes from its own generator seeded with 42, so it does
  not depend on which others ran before it.

## `make timings` — release, one binary

```bash
make timings                                    # n = 1e5, 1e6, int64_t, whole battery
make timings TIMINGS_ARGS="--sizes all --types all"      # 1e3…1e7, int64_t and uint32_t
make timings TIMINGS_ARGS="--csv --datasets Sawtooth,AllEqual --reps 15"
```

Options: `--sizes LIST|all` (`1e5` notation accepted), `--types
int64,uint32|all`, `--datasets LIST`, `--reps N` (9), `--warmup N` (0),
`--seed S` (42), `--csv`, `--tag TEXT`. Each repetition times Stratum Sort
and then `std::sort` on copies of the same input; the `std::sort` output is
the reference, so **every repetition is also a correctness check**, and the
exit status is 1 if any sort disagreed. The table reports the median of the
per-repetition ratios and their min–max. `--csv` prints one row per
repetition — `tag,type,dataset,n,n_real,rep,stratum_ms,std_ms,status`
(`status` is `ok`, `MISMATCH` or `na`) — after `#` comment lines with the
system, flags, λ, t and D. A full `--sizes all --types all` run needs about
1 GB of memory at `n = 10⁷`.

`make timings` gives ratios, never a verdict: one binary, one session, and
nothing to compare against.

## `make ab` — alternating A/B of two commits (G3)

```bash
make ab A=master B=WORKTREE                  # the G3 gate of a pending change
make ab A=v0.10.0 B=master SESSIONS=3        # two commits
make ab A=master B=master SIZES=1e4,1e5,1e6  # noise check: the same code twice
```

What it does (`research/tools/ab_compare.sh`):

1. Extracts `include/` of each revision with `git archive` (`WORKTREE` means
   the uncommitted `include/`), and compiles the **working tree's**
   `benchmarks/timings.cpp` against each, with the release flags. The
   harness is shared on purpose: an older commit has neither the battery
   nor the CSV mode, and an A/B is meant to measure a change to the
   library on identical inputs. Each binary still builds its adversaries
   against its own λ and D.
2. Runs `SESSIONS` sessions × `REPS` repetitions; in each repetition, for
   each size, one process of A and one of B (`--reps 1 --warmup 1`), in the
   order A,B on odd repetitions and B,A on even ones (ABBA).
3. Hands `raw.csv` to `research/tools/ab_table.py` (Python 3 standard
   library only), which prints the fixed G3 table and the verdict.

Variables: `A` (default `master`), `B` (default `WORKTREE`), `SESSIONS`
(3), `REPS` (9), `WARMUP` (1), `SIZES` (`1e4,1e5,1e6,1e7`), `TYPES`
(`int64`), `DATASETS` (all), `FLOORS` (a CSV `dataset,threshold` of
measured noise floors — SS-1 — of which the larger of it and the built-in
threshold is used), `OUT` (default `build/ab/<A>_vs_<B>/`, with `raw.csv`,
`table.md` and `meta.txt`).

**The verdict, defined exactly** (objective.md §4):

| | definition |
|---|---|
| ratio vs `std::sort` | per repetition, Stratum Sort ÷ `std::sort`, same input, same process |
| ratio B/A | per repetition, B's Stratum Sort time ÷ A's, from the same (session, repetition) slot |
| median | over all repetitions of all sessions |
| range | min–max of the per-**session** medians |
| `gana` | median ≤ 1 − u **and** the whole range below 1.0 |
| `pierde` | median ≥ 1 + u **and** the whole range above 1.0 |
| `empate` | anything else |
| u | 6 %; ManyRepeated 18 %; Concentrated 12 %; or a larger measured floor from `FLOORS` |

**G3 rejects** a row when (a) B/A is `pierde` — B slower than A beyond the
noise floor — or (b) A was `gana` against `std::sort` and B is not. The
exit status is 1 on a rejection or on any disagreement with `std::sort`.
With fewer than 3 sessions the table says the verdicts are only
indicative, because the range then collapses to one or two values.

**Cost.** Measured on the 4-core Xeon VM where this was written, with
warm-up on: one session of `n = 10⁷` (whole battery, `int64_t`, 9
repetitions) took 811 s, and 3 sessions of `10⁴–10⁶` with both types took
~6 min. The default run (3 × 9, `10⁴…10⁷`, `int64_t`) is therefore about
45 minutes there; `TYPES=int64,uint32` doubles it. Quote the configuration
with the table whenever it is not the default.

**What it does not remove.** The three sessions are consecutive, not on
separate days or reboots, so they share whatever slow drift the machine
has; and on a shared VM a neighbour's load lands in some repetitions and
not others. A run of `master` against itself is the way to see the floor
on a given machine, and it has produced false `pierde` rows on this one
(see "A/B of the same code, measured" below). That is what SS-1 measures per dataset.

### A/B of the same code, measured (Linux x86_64 VM, 4 cores, GCC 13.3)

Measured when the tool was written (2026-11-09), on a VM shared with
other work, so it is a statement about that machine on that day:

| run | config | rows | B/A verdicts | G3 |
|---|---|---:|---|---|
| `master` vs `master`, no warm-up | 3×9, `10⁴–10⁶`, int64 | 76 | 75 empate, 1 pierde (AllEqual 10⁵: 1.060, sessions 1.02–1.17) | RECHAZA |
| `master` vs `master`, warm-up 1 | 3×9, `10⁴–10⁶`, int64 | 76 | 76 empate | PASA |
| `master` vs SS-0 tree (release `.text` identical except one immediate, the length of the embedded flags string) | 3×9, `10⁴–10⁶`, int64 + uint32 | 152 | 150 empate, 1 gana, 1 pierde (FewDistinct2 10⁶ int64: 1.064, sessions 1.01–1.07); plus one (b) row (Adv5Alternating 10⁵ uint32: A/std 0.93 `gana`, B/std 0.99 `empate`) | RECHAZA |

Every one of those non-`empate` rows is noise by construction — the code
was the same. With 6 % and three back-to-back sessions this machine
produces roughly one false row per hundred; that is a property of the
machine and of consecutive sessions, not something to fix by moving the
threshold. Until SS-1 has measured the Linux floors per dataset (and
passed them with `FLOORS=`), read an isolated `pierde` just over 6 % with
a session range touching 1.0 as "re-run it", and treat
`make release-identity` as decisive for research-only changes.

## Total-work counter (objective.md A2) — `make counters`

The research build counts everything the algorithm does to elements, in
every phase — not only the comparisons of the local sorts, which is all
`SortMetrics::comparisons()` sees. `SortMetrics::work(phase)` and
`totalWork()` return `{reads, writes, comparisons, bucketOps}` for the
phases `analyze`, `scanRange`, `distribute`, `refine`, `localSort` and
`join`. The cost model, also stated in `include/stratum/Metrics.hpp`:

- an element-vs-element comparison reads 2 elements, a comparison against
  a held value (insertion key, pivot) reads 1, a swap reads 2 and writes 2;
- `analyze`: n reads, 2n comparisons; `scanRange` of a bin of k: k reads,
  2(k − 1) comparisons;
- a distribution step over k elements and b buckets (`distribute` at the
  top level, `refine` for every split): 2k reads, k writes, b bucket
  operations; k reads and k writes on the one-bucket `memcpy` path;
- `join`: n reads, n writes; local sorts: every read, write and comparison
  as it happens (a reversal is k/2 swaps).

The bulk passes are recorded as exact closed forms once per pass, so the
counter costs the research build almost nothing outside the local sorts.
`tests/api_contract.cpp` (§10, research build) pins it: a hand-computed
case (`{3,1,2}`: 18 reads, 10 writes, 13 comparisons), the closed forms,
agreement of the `refine` row with the independent `workByDepth` counter,
determinism across instances and reuse, and linear growth on uniform
input (`n = 10⁴, 10⁵, 10⁶`: fitted exponent within 1 ± 0.02, work/n within
10 %).

`make counters` prints it for the whole battery, both types and
`n = 10³…10⁷` as CSV (`COUNTERS_ARGS="--sizes … --types … --datasets …"`
to narrow it), then the A3 summary: the least-squares exponent of
`total ~ a·nᵇ` per dataset and the largest work/n. **G2** is a `diff` of
two such CSVs: every number is exact, so any difference is a change in
behaviour, never noise. The exponent is only meaningful where the value
density does not change with n (`DatasetGenerator::randomUniformScaled`
explains why RandomUniform, with a fixed range, is not such a case).

**Release is untouched.** The counter is fed through two new wrappers,
`notePassWork` and `noteElementMoves`, that are empty in a release build
like every other `note*` wrapper. Empty is a semantic guarantee, not an
object-code one: an empty call can still perturb a compiler's register
allocation, and three placements did with GCC 13 (next to `std::reverse`,
next to the call of `analyze()`, and after the join loop). They were moved
— the reversal is booked in `detectRun`, the `analyze()` pass is booked on
the metrics side together with the top-level distribution it always
precedes — until the release machine code came out **byte-identical**:

```bash
make release-identity BASE=master            # GCC
make release-identity BASE=master CXX=clang++
```

compiles `benchmarks/timings.cpp` and `examples/basic.cpp` against `BASE`'s
`include/` and against the working tree's, with the release flags, and
compares the `.text` sections byte for byte (`research/tools/release_identity.sh`).
For this change it reports identical `.text` with GCC 13.3 and Clang 18.1;
an explicit instantiation of `StratumSort<int64_t>`, `<uint32_t>` and
`<uint8_t>` diffed at the assembly level showed no instruction change
either, only the two empty wrapper bodies an explicit instantiation emits.
Run it after touching any wrapper: for a research-only change it is
stronger evidence than any A/B, which can only bound a difference by the
noise floor.

## Reproducing

```bash
make timings    # release timings vs std::sort - the quotable numbers
make ab A=master B=WORKTREE   # the G3 table: alternating A/B in release
make baseline   # research build: the 10 current datasets with their deterministic counters
make profile    # research build: per-phase time shares and heap-allocation counts
make counters   # research build: total-work counter over the whole battery (G2, A3)
make overhead   # cost of the instrumentation: one source, two configurations
make release-identity BASE=master   # release .text byte-identical to BASE?
make research   # builds every research program (build/research_*)
```

`make baseline` and `make profile` still cover only the ten current
datasets; the counters of the whole battery are `make counters`. There is
no `make analysis` or `make experiments`: `research/analysis/main.cpp` and
`research/experiments/main.cpp` both map to `build/research_main`, as does
`research/tools/main.cpp`, which is the one `make research` builds; the
other two drivers have to be compiled by hand.

All datasets are generated from fixed seeds, so a given binary produces
the same input every time.

## Reference machine

Apple M4, arm64, Apple clang 21, libc++, 4 MiB L2, 16 GB RAM.
Every number in this repository comes from that machine unless stated
otherwise. **They have not been reproduced anywhere else**, and the
project has direct evidence that they would not transfer unchanged.
