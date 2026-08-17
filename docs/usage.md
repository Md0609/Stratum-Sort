# Using Stratum Sort

Header-only, C++17, no dependencies. This document is the API reference
and the tuning guide. For *why* the algorithm works, see
`research/ALGORITHM.md` in the repository.

## Installing

Copy `include/stratum/` anywhere on your include path, or with CMake:

```cmake
add_subdirectory(stratumsort)
target_link_libraries(your_target PRIVATE StratumSort::stratumsort)
```

There is nothing to compile or link — `stratumsort` is an INTERFACE
target that only sets an include directory and `cxx_std_17`.

## The API

```cpp
#include "stratum/StratumSort.hpp"

namespace stratum {

template <typename T>
class StratumSort {
public:
    explicit StratumSort(std::size_t targetElementsPerBin = 32,
                         std::size_t leafThreshold        = 64);

    void sort(std::vector<T>& data);        // in place, ascending

    std::size_t targetElementsPerBin() const;
    std::size_t leafThreshold() const;
};

}
```

That is the entire public surface.

## Element type

`T` must be an integral type of at most 64 bits. Three `static_assert`s
enforce it:

| rejected | why |
|---|---|
| non-integral | every formula is exact integer arithmetic |
| `bool` | `std::vector<bool>` is the packed specialisation — no contiguous storage |
| wider than 64 bits (e.g. `__int128`) | the offset arithmetic is carried in `uint64_t`, so a wider key would be truncated |

The third one matters: some toolchains report `std::is_integral<__int128>`
as true, and without the check the truncated index overflowed the heap.
Signed and unsigned both work, including inputs containing `INT64_MIN`
and `INT64_MAX` simultaneously.

## Reuse

An instance owns scratch buffers and reuses them across calls, so sorting
many arrays with one instance avoids re-allocating:

```cpp
stratum::StratumSort<int64_t> sorter;
for (auto& batch : batches) sorter.sort(batch);
```

The memory is released when the instance is destroyed, not between calls:
an instance used once on a huge array keeps that memory alive.

## Guarantees

- **Deterministic.** No randomness; the same input always produces the
  same sequence of operations.
- **Strong exception safety** in a release build. Every allocation happens
  before the first write to your array, so if `sort()` throws, the input
  is untouched. (This does *not* hold if you define
  `STRATUM_ENABLE_METRICS` — that build records the join phase's timing
  after the join has run, and recording allocates.)
- **No global state.** Distinct instances are independent and may be used
  concurrently. A *single* instance is not thread-safe.
- **Not stable.** Unobservable for the integral keys accepted here, since
  equal elements are indistinguishable — but it does rule out a
  straightforward extension to key/value pairs.

## Tuning

Both constructor arguments are hints. They are clamped, never rejected:
`λ == 0` becomes the default, and `t < λ` is raised to `λ`. No combination
can produce undefined behaviour. Use the accessors to see what an instance
actually ended up with.

### λ — `targetElementsPerBin`, default 32

The target occupancy per bin, and the only parameter with a first-order
effect on running time. It trades two opposing costs:

- **Local sorting grows with λ.** Leaves hold about λ elements and are
  finished with insertion sort, so the expected comparisons per element
  are about `(λ+1)/4`.
- **Scattering grows as λ shrinks.** The distribution pass writes into
  `n/λ` output streams at once, each holding a cache line live, so the
  write working set is about `(n/λ)·64` bytes. When that approaches L2,
  throughput collapses.

**The useful lower bound therefore depends on your `n` and your cache**,
roughly `n·64/λ ≲ L2`. The default suits `n ≈ 10⁶` on a 4 MiB L2. At
`n ≈ 10⁷` the same condition puts the lower bound near 160, so raise λ
for much larger inputs.

Raising λ does not change the complexity class, but it does raise the
input size above which the linear regime applies — roughly
`λ · 2^(w/(D+1))`, which is about 1.8·10⁴ at λ = 32 and 5.8·10⁵ at
λ = 1024.

### t — `leafThreshold`, default 64

The size at which refinement stops. Precondition `t ≥ λ`, enforced by the
constructor.

**`t` is not a tuned value, it is a safe lower bound.** Its only job is to
sit above the upper tail of the occupancy distribution, so a bin is not
refined merely for landing slightly above average. With λ = 32,
`P(Poisson(32) > 64) ≈ 2·10⁻⁷`, and `t = 64`, 96 and 128 produce
byte-identical internal counters on every dataset. Once `t` clears the
tail its exact value is irrelevant.

Setting `t = λ` is legal and fuses the two parameters back into one, at
the cost of sending roughly half of all elements into refinement purely
because `P(X > λ) ≈ 0.5` for Poisson(λ) — by arithmetic, not because the
data needs it.

### A hazard worth knowing

Both parameters are `std::size_t` and adjacent, so swapping them at a call
site compiles silently. `StratumSort(64, 32)` is read as `(64, 64)`, not
`(32, 64)`, because `t` is clamped up to `λ`. Check with the accessors if
in doubt.

### Compile-time constants

`include/stratum/Config.hpp` holds the rest — the maximum refinement depth
`D` and the three local-sort dispatch thresholds — each with its
justification and, where a value has never been measured, an explicit note
saying so. One constraint is load-bearing: **`D ≥ 1`.** At `D = 0` there
is no refinement at all and the worst case becomes `Θ(n log n)`.

## Build configurations

| | metrics | assertions | use for |
|---|---|---|---|
| release | no | no | production; the only meaningful timings |
| test | no | **yes** | correctness; ~5% slower, never for timing |
| research | yes | yes | instrumentation; several times slower |

Define `STRATUM_ENABLE_METRICS` to compile in the counters — comparisons,
bins, subdivisions, depth, per-phase timing — reachable through
`sorter.metrics()`. Without it, none of that exists in the object code.
Never quote a timing from that build.

## Running the tests

```bash
make test         # three suites, assertions active
make sanitizers   # range-arithmetic limits under ASan/UBSan
make fuzz N=200000
make timings      # release timings against std::sort
```

Or with CMake:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build
```
