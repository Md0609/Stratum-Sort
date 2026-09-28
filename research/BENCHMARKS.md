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
| `make bench` (`benchmarks/stratum_bench.cpp`) | release | **yes** — the cross-platform instrument since 0.11.0 |
| `make timings` | release | yes — the 0.10.0 battery, kept |
| `make research-perf` (`research/perf/`) | release | yes — the before/after instruments of 0.11.0 |
| `make baseline` | research | no; quote its *counters* |
| `make profile` | research | no; relative phase shares only |

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

- **An adversary is built against one λ — so compare each version with
  its own.** 0.10.0 defaults to λ = 32, 0.11.0 to 16. Feeding both the
  adversary built for λ = 16 made 0.11.0 look 34–56% *slower* than 0.10.0;
  each against its own, it is faster. `VersionTimings` builds, for each
  version, the adversary for *its* default, and prints
  "(each vs its own adversary)" on those rows.
- **Memory placement moves the clock.** Reusing one workspace across
  repetitions came out up to 30% *slower* than allocating it afresh on the
  `clustered` shape, reproducibly across processes — but not when the data
  array was reused too. It is the relative placement of two 8 MB buffers,
  not the algorithm. `stratum_bench` therefore
  reports both a fresh sorter per call (what `StratumSort<T>().sort(v)`
  pays) and a reused workspace, and `VersionTimings` a "reuse" column.
- **Codegen differs by compiler, not only by CPU.** A conditional on the
  sign bit in the float key map was a `cmov` for GCC and Clang and a
  branch for MSVC, which made floats 1.9× slower than integers on the
  Windows runner only. Only a benchmark that runs on every compiler finds
  that.
- **The automatic λ changes at `2²²`.** A size sweep that crosses it shows
  a step in comparisons per element (5.1 → 9.0) that is the parameter
  change, not a superlinear term; fit exponents on one side of it.

## Cross-platform benchmark (0.11.0)

`benchmarks/stratum_bench.cpp` and `.github/workflows/bench.yml` run on
every push to the Linux x86_64, macOS arm64 and Windows x86_64 runners.

- **Environment in the output.** Compiler and version, C++ standard,
  build configuration and flags, OS and architecture, CPU model, cache
  sizes and logical cores are printed above every table. A number without
  them is not quoted.
- **Alternation.** Stratum, Stratum with a reused workspace, `std::sort`
  and `std::stable_sort` run in turn on identical copies of the same input
  inside every repetition.
- **Statistics.** Median, p10–p90 and min/max per contender; the ratio is
  median over median. ns/element and elements/s alongside.
- **Memory.** Peak auxiliary bytes per element, measured by replacing
  global `operator new`/`delete` (`AllocationTracker.hpp`), not estimated.
- **Shapes.** 18: random, sorted, reversed, nearly sorted, local
  disorder, few outliers, sorted + random tail, organ pipe, sawtooth,
  duplicates, two values, low entropy, normal, clustered, the whole 64-bit
  universe, the depth-exhausting adversary, the worst case for the bound,
  and a McIlroy adversary built against the platform's own `std::sort`.
  Plus `uint32_t`, `uint8_t`, `float`, `double` (with ±0, ±∞ and
  denormals), 16- and 64-byte records against `std::sort` and
  `std::stable_sort`, sizes 10⁵, 10⁶ and 10⁷, and a λ sweep.
- **Every result is checked** against `std::sort` (the "ok" column).
- **Suites:** `quick` (int64, 10⁶), `ci`, `types`, `lambda`, `full`.
  `make bench SUITE=ci`; the workflow uploads Markdown and CSV.

Caveat: GitHub's runners are shared virtual machines — the macOS one is a
3-core Apple M1 VM. Ratios alternated in one process survive that far
better than absolute times, but they are not a substitute for a run on the
hardware you care about.

## Reproducing

```bash
make bench SUITE=quick   # cross-platform benchmark vs std::sort / std::stable_sort
make research-perf       # 0.11.0's before/after instruments (research/perf/)
make timings    # release timings vs std::sort - the 0.10.0 battery
make baseline   # the same battery with deterministic counters (research build)
make profile    # per-phase breakdown and heap-allocation counts
make overhead   # cost of the instrumentation: one source, two configurations
make analysis   # complexity-model fitting across input sizes
make experiments
```

All datasets are generated from fixed seeds, so a given binary produces
the same input every time.

## Machines

Up to 0.10.0 every number came from one machine — Apple M4, arm64, Apple
clang 21, libc++, 4 MiB L2 — and was never reproduced elsewhere.

0.11.0's numbers come from four environments, named wherever a number is
quoted: an Intel Xeon with GCC on Linux (the local development machine,
where the alternated before/after measurements of `research/perf/` were
taken), and the three `Benchmarks` runners — GitHub's Linux x86_64 runner
(GCC 13), macOS arm64 (Apple M1 VM, AppleClang 21) and Windows x86_64
(AMD EPYC 7763, MSVC 19.51). They disagree, sometimes in sign: the
depth-exhausting adversary costs 0.74× `std::sort` on the Linux runner and
1.74× on the M1.
