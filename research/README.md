# Research

**Not part of the distributed package.** `make package` ships the library,
its tests, one example and `docs/usage.md` — nothing from this directory.
Everything here is meant to be read in the repository.

Four categories are kept deliberately separate throughout, because
conflating them is how a project ends up believing its own benchmarks:

| tag | meaning |
|---|---|
| **THEOREM** | a statement claimed to hold under the stated hypotheses |
| **PROOF** | a derivation from the code's invariants and those hypotheses |
| **EXPERIMENTAL EVIDENCE** | a measurement that *validates* a proof; it never establishes one |
| **ENGINEERING MEASUREMENT** | a number that informed a decision but proves nothing about complexity |

## The three primary documents

**[`ALGORITHM.md`](ALGORITHM.md)** — the complete technical description,
written so the algorithm can be reimplemented from it alone, in any
language, without reading the C++. Section 8 is the complexity proof:

- **§8.0** the four hypotheses (H1–H4), including why `D ≥ 1` is
  load-bearing and not decoration
- **§8.1–8.3** five structural lemmas: span contraction, the fan-out cap
  as a free escape hatch, the necessary condition for a leaf to stay
  expensive, the top-level budget, strict peeling
- **§8.4** THEOREM — the residual bound `B(n)` and its global supremum `M`
- **§8.5–8.6** cost of every phase, and the only place a `log` appears
- **§8.7** a tempting strengthening of the bound that is **false**, with
  the explicit counterexample that refutes it
- **§8.8** what each of `w`, `λ`, `t` and `D` actually contributes
- **§8.10** an adversarial review of the proof by itself
- **§13** the 0.11.0 engine: why the partition is the same partition,
  the hypotheses revisited, the memory accounting and the arena bound,
  the presorted paths, the stable variant, the counting fill, the
  automatic parameters

**[`BENCHMARKS.md`](BENCHMARKS.md)** — measurement methodology: the three
build configurations, why only one of them may be quoted, the alternating
protocol, the 6% significance threshold, the known traps in this
codebase, and the cross-platform benchmark of 0.11.0.

**[`DESIGN_HISTORY.md`](DESIGN_HISTORY.md)** — what was tried, what was
measured, what was rejected. Including the ideas that failed, which are
the more instructive half, and the four times an acceptance criterion
measured the wrong thing.

## The working record

[`history/`](history/) holds one document per step, in Spanish, written as
the work happened. It is a laboratory notebook, not polished
documentation: specifications that were later withdrawn, measurements that
did not reproduce, two full audits and a falsification report. Nothing has
been retro-edited to look better than it was.

Highlights, if you only read a few:

- `V11_memoria.md` — pre-release memory study: whether `Θ(n)` auxiliary
  memory is necessary (it is not, except for stable sorts of records),
  the audit, the theory, the candidates, every measurement, and why the
  default stays unbounded (a bounded default was faster on one Xeon and
  slower on the CI's AMD EPYC and Apple runners)
- `V11_informe.md` — the 0.11.0 audit and report: the diagnosis of every
  0.10.0 limitation, its classification, each change with its before and
  after, what was rejected, and what remains
- `SPEC_v9.md` — the specification whose headline objective was refuted
  mid-plan
- `O8_resolucion_y_reversion_paso3.md` — a change implemented, measured
  and reverted
- `AUDIT_v10.md`, `AUDIT_v10_second.md` — full technical audits
- `VALIDATION_final.md` — the five defects found by re-reading the code
  from scratch before publication

## The programs

Each answered exactly one question. They are not part of the library, and
`make package` excludes them — but `make research` still compiles all of
them, because **a study whose source no longer builds cannot be re-run,
and a result nobody can re-run is not evidence.**

| directory | what lives there |
|---|---|
| `tools/` | measurement harnesses: the instrumented baseline, per-phase profile, instrumentation overhead, improvement ceilings |
| `analysis/` | complexity-model fitting across input sizes |
| `experiments/` | one-off studies: parameter sweeps, the (λ, t) plane, adversary construction, the cardinality anomaly, empty-bin decomposition |
| `experiments/legacy/` | earlier versions of the algorithm, reconstructed and kept only so version comparisons have a real data point. They keep their historical names on purpose. |

```bash
make research     # builds all of them; runs none
```

### `perf/` and `baselines/` — the 0.11.0 instruments

Release-build programs, each answering one before/after question of
0.11.0, most of them against the frozen 0.10.0 headers in
`baselines/v0_10_0/` so that the old and new versions run alternated in
one process:

| program | question |
|---|---|
| `perf/MemoryProfile.cpp` | peak auxiliary bytes at the allocator, per version, key width, λ and shape |
| `perf/BucketIndexStrategies.cpp` | store the bucket index, or recompute it (hardware division, reciprocal) |
| `perf/InPlaceDistribution.cpp` | out-of-place against in-place (American flag) distribution |
| `perf/PresortedDetection.cpp` | three ways of detecting presorted input, and what each costs on the rest |
| `perf/LambdaSweep.cpp` | the best λ per `n`, key width and shape; `--t-sweep` for `t/λ` |
| `perf/RecordStrategies.cpp` | move records, or sort (key, index) pairs, by record size |
| `perf/VersionTimings.cpp` | 0.10.0 against the current header against `std::sort`, each version against its own adversary |
| `perf/MemoryAudit.cpp` | pre-release memory study: auxiliary bytes four ways (heap, usable, RSS, stack), one process per row, per version, memory policy, type and shape |
| `perf/MemoryModes.cpp` | pre-release memory study: time of each memory policy (unlimited, automatic, explicit budgets) on the same inputs |
| `perf/StableByIndex.cpp` | pre-release memory study: stable sort of records through (key, index) pairs (candidate C, not adopted) |
| `perf/InPlaceTuning.cpp` | pre-release memory study: the in-place engine's constants (radix bits, block size) against the partner buffer; built per configuration by the `tune` job of `bench.yml` |

`baselines/v0_11_pre/` is the 0.11 header before the memory study, frozen
the same way; `data/memoria/` holds the raw CSVs of that study and
`data/memoria/ci/` the CI reports it cites (benchmark and tuning runs,
named by commit, platform, CPU and compiler).

```bash
make research-perf
./build/perf_VersionTimings 1000000
```
