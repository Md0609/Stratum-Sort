# Frozen baselines

`v0_10_0/` is the published 0.10.0 implementation, byte for byte, with one
mechanical change: `namespace stratum` became `namespace stratum_v0_10_0`,
so that it can be compiled into the same binary as the current header.

It exists for one reason. This project only accepts a performance or memory
claim that was measured by **alternating** the two versions in one process
on identical inputs — comparing numbers taken in separate sessions once
produced a false 8.8% result here. The programs in `research/perf/` include
both headers and run them side by side:

| program | what it compares |
|---|---|
| `MemoryProfile` | peak auxiliary bytes at the allocator, per key width, `n`, `λ` and input shape |
| `VersionTimings` | wall clock, 0.10.0 vs current vs `std::sort`, alternated per repetition |

Never edit the files in `v0_10_0/`. A baseline that moves is not a baseline.
