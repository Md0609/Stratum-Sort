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
| `make timings` | release | **yes — only this one** |
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

## Reproducing

```bash
make timings    # release timings vs std::sort - the quotable numbers
make baseline   # the same battery with deterministic counters (research build)
make profile    # per-phase breakdown and heap-allocation counts
make overhead   # cost of the instrumentation: one source, two configurations
make analysis   # complexity-model fitting across input sizes
make experiments
```

All datasets are generated from fixed seeds, so a given binary produces
the same input every time.

## Reference machine

Apple M4, arm64, Apple clang 21, libc++, 4 MiB L2, 16 GB RAM.
Every number in this repository comes from that machine unless stated
otherwise. **They have not been reproduced anywhere else**, and the
project has direct evidence that they would not transfer unchanged.
