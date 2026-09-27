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

**[`BENCHMARKS.md`](BENCHMARKS.md)** — measurement methodology: the three
build configurations, why only one of them may be quoted, the alternating
protocol, the 6% significance threshold, and the known traps in this
codebase.

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
