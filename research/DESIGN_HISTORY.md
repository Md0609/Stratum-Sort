# Design history

The algorithm reached its current shape through ten iterations. This is
the record of what was tried, what was measured, and what was thrown
away — including, deliberately, the ideas that failed, which are the more
instructive half.

The primary documents are [`ALGORITHM.md`](ALGORITHM.md) and
[`BENCHMARKS.md`](BENCHMARKS.md). Everything referenced below lives in
[`history/`](history/) and is written in Spanish; it is a working record,
not polished documentation.

---

## The shape of the process

Each change followed the same protocol, and the protocol tightened every
time it failed to catch something:

1. **Measure the terrain first.** Decompose where the cost actually is
   before writing any acceptance criterion.
2. **State one hypothesis.**
3. **Write two criteria**: that the mechanism does what it claims, and
   that it actually helps. Both are needed — a change can pass the first
   and still be worthless.
4. **Measure by alternating** the two versions in one session.
5. **Decide binary**: accept, or revert without compensating.

## What was accepted

| Change | Effect |
|---|---|
| **Span arithmetic** — hold the range as `max − min`, never `max − min + 1` | −30% on whole-universe inputs; removed the only known violation of the range-collapse property |
| **Cap the fan-out by the observed range** in refinement | Removes 100% of unreachable buckets. **No measurable time effect**; kept as provably dead work at zero risk |
| **Sorted certificate** — record when a bin's span is zero | Removes 100% of the algorithm's comparisons on two datasets. **Production gain ~2%, below the noise floor**; kept for the same reason |
| **Block copy in the join** | **−17% to −42%** on three datasets. The largest single improvement in the project, and it is one line |
| **Separate λ from `t`** | **−15% to −23%** on the four datasets without redundancy. Also *halved* the worst-case residual, since the bound scales with λ |

Two of these were kept despite delivering nothing measurable. That is a
deliberate standard: work that is provably dead and costs nothing to
remove is worth removing, but the justification is honesty about the code,
not a performance claim.

## What was rejected

| Idea | Why |
|---|---|
| **Remove the depth cap** ([O8](history/O8_resolucion_y_reversion_paso3.md)) | Implemented, measured, **reverted**. Costs up to +109% and never wins over seven adversary shapes. The `Θ(n log n)` term it removed does not exist at these constants — the bit budget already bounds the residual |
| **Require balanced splits** ([review](history/REVIEW_refinamiento_terminal.md)) | Rejected before implementing, on two independent grounds: it forces `Θ(n log n)`, and no rule depending only on the observed range can guarantee it |
| **Retune the fused parameter** ([4b](history/STEP4b_target_sweep.md)) | Swept 8…256. Flat between 24 and 64, incumbent within 2% of optimal. The historical claim of an 18–23% win **did not reproduce** — at 16 it was 13% slower |
| **Explore the (λ, t) plane** ([O17](history/O17_lambda_tau_plane.md)) | 21 configurations. Nothing beat the incumbent. Three of the four apparent winners were overfitting to the adversarial generator's own parameter |
| **Use the input as one of the buffers** | Ceiling measured at ~5% on one dataset, 0% on the three where the join is expensive |
| **Eliminate per-call allocations** | Ceiling 5.8–6.2% on four datasets, ~0% on five. At the noise threshold |

## The errors, and what they cost

The genuinely useful part of this record.

**An asymptotic argument is not a motivation until you check it matters at
the real constants.** The plan for v9 was built around eliminating a
`Θ(n log n)` worst case. Six steps in, measurement showed the term does
not exist at the shipped constants: the bit budget bounds the residual to
a constant independent of `n`. The specification had to be rewritten
and its headline objective withdrawn.

**Four times, an acceptance criterion measured the wrong thing.**

| | The criterion measured | What the decision needed |
|---|---|---|
| Fan-out cap | all empty buckets | only the *unreachable* ones |
| Depth cap | that the mechanism worked | that it *helped* |
| Parameter sweep | the *fused* optimum | the effect of *separating* |
| Sorted certificate | the *research* build | the *release* build |

The pattern is always the same: the criterion was written against whatever
the available instrument produced, rather than against what the hypothesis
claimed. Rule 3 above exists because of this.

**Results do not transfer between machines.** Conclusions from an x86
Xeon with libstdc++ inverted on an Apple M4 with libc++: a parameter that
won by 18–23% lost by 13%, and the distribution phase went from 47% of the
running time to 15%. Every number in this repository was re-measured from
scratch on the current machine before being used for anything.

**Two defects were found by code, not by reading.** A full technical audit
([AUDIT_v10](history/AUDIT_v10.md)) read every line and missed both. An
assertion added afterwards caught an integer overflow escaping a function;
a test caught a documented guarantee that was false in one build
configuration. An invariant written in a comment is not checked — writing
it as an assertion or a test is what checks it.

**The rule about research builds was broken in the most visible place.**
The final review ([VALIDATION_final](history/VALIDATION_final.md)) found
that the README's own performance table had been measured with
`make baseline` — a tool that cannot be built without instrumentation,
because it reads `sorter.metrics()` — while being labelled a release
build. The distortion was 5–20%, and not even of constant sign. Knowing a
rule and having the tooling enforce it are different things; there are now
two separate binaries so that the mistake is not available to make.

## Iteration index

| | Focus | Outcome |
|---|---|---|
| v1–v3 | Original specification, recursive refinement, first retuning | 2.15× over v1 |
| v4–v5 | Measurement platform, complexity fitting, disorder metrics | No algorithm change |
| v6 | Three new heuristics (micro-histogram, density-aware bins, difficulty-score skipping) | All three measured net-negative and **removed entirely** |
| v7 | Production/research build split; constant-factor work | 4–6.5% |
| v8 | Structural change to refinement, five variants | All five rejected; the phase breakdown it produced redirected v9 |
| v9 | Span arithmetic, fan-out cap, sorted certificate, block copy | The `Θ(n log n)` premise was refuted mid-plan |
| v10 | Separate λ from `t`; parameter-plane sweep; two audits; cleanup for publication | Current |

Full detail per iteration in [`history/`](history/).
