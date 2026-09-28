# Design history

The algorithm reached its current shape through eleven iterations. This is
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
| **Use the input as one of the buffers** | Ceiling measured at ~5% on one dataset, 0% on the three where the join is expensive. **Reversed in 0.11.0** — see below |
| **Eliminate per-call allocations** | Ceiling 5.8–6.2% on four datasets, ~0% on five. At the noise threshold. **Reversed in 0.11.0** — see below |

## 0.11.0: turning the limitations into capabilities

0.10.0 ended with seven documented limitations. 0.11.0 took them as the
specification: for each, find exactly where it came from, classify it
(eliminable, reducible, structural, too invasive for one release), and
measure every change before and after, alternated in one process against
the frozen 0.10.0 headers (`baselines/v0_10_0/`). The full record, in
Spanish, is [`V11_informe.md`](history/V11_informe.md); the proofs are
[`ALGORITHM.md`](ALGORITHM.md) §13.

### Accepted

| Change | Effect |
|---|---|
| **Caller's array as one of the two buffers; no cached bucket index; no tree; one counter arena** | Peak memory 3.28× → 1.06× the input (`int64_t`), 16.57× → 2.00× at `λ = 1`; −24% to −70% time on all 18 benchmark shapes from this step alone |
| **Exact reciprocal division** (Granlund–Montgomery) instead of storing the bucket index | Recomputing turned out **23–52% faster** than storing and reloading — the cache was costing time as well as memory |
| **Sorted-prefix detection inside the min/max pass, resuming the scan where the prefix breaks** | Sorted input 2.27× slower than `std::sort` → 0.09× (Xeon); one pass in every case |
| **Sorted prefix ≥ n/2: sort the tail, merge backwards** | Sorted + random tail 0.55× → 0.05× `std::sort`; organ pipe 0.37× → 0.06× |
| **λ = 16, t = 32 up to 2²² elements; 32/64 above** | Worst slowdown against the best λ of each row: 1.40× → about 1.2×, on four environments; residual bound 18 090 → 9 045 |
| **Traits: one engine for integers, enums, floats, records** | `totalOrder` for floats, proved over 2³² patterns; integral keys run the 0.10.0 code comparison for comparison |
| **Stable variant: replace exactly the three reordering steps** | Same speed and memory as the unstable variant on the record benchmarks |
| **Workspace; `sort(data, ws) const`** | One sorter, many threads, no locks; checked under ThreadSanitizer |
| **Counting fill for width-1 grids when the element is its key** | `uint8_t` memory 10× → a few hundred bytes; many duplicates −49% on the Xeon, and the loss to MSVC's `std::sort` on few-distinct integer inputs (1.4–2.7×) became a win (0.17–0.41×) |
| **Branch-free float key map** | MSVC compiled the sign-bit conditional to a branch: few-distinct floats 1.79× → 1.36×; GCC/Clang 0–15% faster |

**Two 0.10.0 rejections reversed, and why that is consistent.** "Use the
input as one of the buffers" and "eliminate per-call allocations" were
rejected on *time*: their measured ceilings were at the noise threshold.
0.11.0's objective was *memory*, where the same changes are worth 3×, and
the rebuilt engine then measured faster as well. The measurements were
right for the question they answered; the question changed.

**λ = 16 was "13% slower" in v4b and is the default now.** That sweep
ran on the 0.10.0 engine, whose per-bin cost (a tree node and two
histograms, ~64 bytes) punished a larger fan-out. With a 4-byte counter per
bucket the optimum moved. Re-measured, not inherited.

### Rejected, or not done

| Idea | Why |
|---|---|
| **In-place distribution** (American-flag cycle leader) | Same partition, same division, measured on the top-level step: +251% at 10⁵, +533% at 10⁶, +1303% at 10⁷. At `⌈n/λ⌉` buckets every step of a cycle is a dependent cache miss; in-place radix sorts use 256 buckets for exactly this reason. Not stable either. Fixing it changes the partition and Lemma 4: too invasive for 0.11 |
| **Count descents inside the min/max pass** | +27% on *every* input, to speed up a minority |
| **Separate prefix scan before the min/max pass** | A whole extra pass whenever the prefix breaks late |
| **"No n-dependent λ rule"** — concluded on three machines, committed, then **retracted** | The fourth environment (MSVC on AMD EPYC, 512 KiB L2) lost 27% at `n = 10⁷` with λ = 16. The commit that concluded otherwise stays in the history, followed by the correction |
| **λ from the cache size** | Would make the order of equal keys in `sort_by_key` depend on the machine. The rule reads `n` only |
| **Automatic switch to indirect sorting for large records** | Measured (`perf/RecordStrategies.cpp`): moving records wins up to ~64 bytes, indirect from 128–256 bytes and large `n`. The crossover depends on the machine, so it is documented (`sorted_indices`) rather than chosen silently |
| **Natural-run merging for nearly-sorted input** | Would help the shapes where MSVC's `std::sort` still wins, but needs a run detector that does not tax random input; not attempted in this release |

### What the process caught this time

- **An unfair comparison, caught before publication.** The
  depth-exhausting adversary is built against one λ. With the default at
  16, 0.11.0 received an adversary aimed at it and 0.10.0 (λ = 32) one that
  was not, and 0.11.0 came out 34–56% *slower*. Each version against its
  *own* adversary, 0.11.0 is faster (−12% at 10⁶, −41% at 10⁷ in the final
  measurement). `VersionTimings` now always builds both, and says so on
  the row.
- **A test that silently tested something else.** Building the 4096-core
  adversary at λ = 16 overflowed `int64_t`; the check still passed, on a
  different input. It now checks for overflow.
- **A guarantee the code did not keep.** The documentation said an input
  of at most λ elements allocates nothing; the sorted-prefix path
  allocated for `{9, 9, 2, 8, 2}`. Found by reading a number an example
  printed, fixed, and pinned by a contract check that fails without the
  fix.
- **A compiler-specific cost.** The float branch was invisible on GCC and
  Clang and cost MSVC up to 25%; only a benchmark that runs on every
  compiler finds that.

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
| v10 | Separate λ from `t`; parameter-plane sweep; two audits; cleanup for publication | Released as 0.10.0 |
| v11 | Memory, presorted input, automatic λ, key types, stability, workspaces, cross-platform benchmark — the partition untouched | Current (0.11.0) |

Full detail per iteration in [`history/`](history/).
