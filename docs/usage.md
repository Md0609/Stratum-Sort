# Using Stratum Sort

Header-only, C++17, no dependencies. This document is the API reference
and the tuning guide: everything needed to use the library is here.

The derivation of the complexity guarantee, the measurement methodology
and the design record are kept in the project repository, under
`research/`. They are deliberately not part of this package — you never
need them to use the library.

## Installing

Copy `include/stratum/` anywhere on your include path, or with CMake:

```cmake
add_subdirectory(stratumsort)
target_link_libraries(your_target PRIVATE StratumSort::stratumsort)
```

There is nothing to compile or link — `stratumsort` is an INTERFACE
target that only sets an include directory and `cxx_std_17`. One include
gives you everything:

```cpp
#include "stratum/StratumSort.hpp"
```

## Which function do I want?

| you have | call | stable | extra memory (default) |
|---|---|---|---|
| integers, enums, `char` types | `stratum::sort(v)` | equal keys are identical | ≤ min(≈ `n·sizeof(T)`, 16 MiB) |
| `float` / `double` | `stratum::sort(v)` — IEEE-754 `totalOrder` | idem | ≤ min(≈ `n·sizeof(T)`, 16 MiB) |
| records with a numeric key, order of equal keys irrelevant | `stratum::sort_by_key(v, key)` | no | ≤ min(≈ `n·sizeof(record)`, 16 MiB) |
| records with a numeric key, order of equal keys must be kept | `stratum::stable_sort_by_key(v, key)` | **yes** | ≈ `n·sizeof(record)` |
| records that are not trivially copyable, or very large | `stratum::sorted_indices(first, last, key)` | **yes** | `n` (key, index) pairs |
| 0.10.0 code | `stratum::StratumSort<T>().sort(v)` — unchanged | — | ≤ min(≈ `n·sizeof(T)`, 16 MiB) |

"≈" means `n·sizeof(T)` plus `(2⌈n/λ⌉ + 2)` counters of 4 bytes — 1.06×
the input for an 8-byte key — and **less** on several common inputs. The
16 MiB cap is the default memory policy; a `Workspace` can set any other
budget, down to a floor of a few tens of KB (see "Memory" below).

## The free functions

```cpp
namespace stratum {

// Keys that are their own value: integral (not bool), enum, float, double.
template <typename T> void sort(std::vector<T>& v);
template <typename T> void sort(T* first, T* last);
template <typename T> void stable_sort(std::vector<T>& v);
template <typename T> void stable_sort(T* first, T* last);

// Records moved as a whole, ordered by key(record). The record must be
// trivially copyable; key must return an integral, enum, float or double.
template <typename E, typename KeyFn> void sort_by_key(std::vector<E>& v, KeyFn key);
template <typename E, typename KeyFn> void stable_sort_by_key(std::vector<E>& v, KeyFn key);
// (and the (E* first, E* last, KeyFn key) forms)

// The permutation that sorts [first, last) by key, stably. Any element type.
template <typename It, typename KeyFn>
std::vector<std::size_t> sorted_indices(It first, It last, KeyFn key);

}
```

Every `sort*` function also has a form taking a `Workspace<E>&` and a
`Parameters` (see below): `stratum::sort(v, workspace, parameters)`.

```cpp
std::vector<double> prices = /* ... */;
stratum::sort(prices);                                   // totalOrder

struct Order { uint64_t customer; uint32_t amount; uint32_t day; };
std::vector<Order> orders = /* ... */;
stratum::stable_sort_by_key(orders, [](const Order& o) { return o.customer; });
// orders of one customer keep their previous relative order

std::vector<std::string> names = /* ... */;
std::vector<std::size_t> byLength =
    stratum::sorted_indices(names.begin(), names.end(),
                            [](const std::string& s) { return s.size(); });
```

### Key types

| key | order | notes |
|---|---|---|
| signed / unsigned integers, 8–64 bits | numeric | `INT64_MIN` and `INT64_MAX` together are fine |
| `char`, `wchar_t`, `char16_t`, `char32_t` (and `char8_t` in C++20) | numeric value of the code unit | |
| enums, scoped or not | the underlying integer | negative enumerators work |
| `float`, `double` | IEEE 754 `totalOrder` | see below |
| `bool`, `__int128`, `long double`, strings, pointers | **rejected at compile time** | |

**Floating point.** `float` and `double` are sorted by IEEE 754-2008
`totalOrder`:

```
-NaN < -inf < … < -0.0 < +0.0 < … < +inf < +NaN
```

Without NaN, the result is also a valid result of `std::sort(v.begin(),
v.end())` — the only difference is that `-0.0` is placed before `+0.0`
instead of in unspecified order. With NaN, `std::sort` with `operator<` is
undefined behaviour; `stratum::sort` puts negative NaNs first and positive
ones last, deterministically. The output is a permutation of the input's
exact bit patterns: `-0.0` stays `-0.0` and NaN payloads survive. The key
mapping is proved exhaustively for all 2³² `float` patterns by
`tests/float_keys.cpp`. `long double` is not supported: its width and
layout differ between platforms.

**The key function** is called several times per element (once per pass),
must return the same key for the same record every time, must not modify
the record, and must not throw — the strong exception guarantee relies on
nothing after the first write being able to fail. A lambda returning a
member is the intended use.

### Stability — exactly what is promised

- `stable_sort`, `stable_sort_by_key` and `sorted_indices` keep elements
  with equal keys in their input order, **for every input and every
  parameter**. `tests/stability.cpp` checks this against
  `std::stable_sort` with an identifying payload on every record,
  including inputs with thousands of copies of each key and adversarial
  inputs that force the internal merge sort.
- `sort` and `sort_by_key` are deterministic but make **no** promise about
  equal keys: a different λ, or a different `n` under automatic parameters,
  can order them differently. For `sort` on integers, enums and floats the
  difference is unobservable (equal keys are equal bit patterns).
- Cost: on the record benchmarks `stable_sort_by_key` runs at the same
  speed as `sort_by_key` (±3%; the merge sort only runs on leaves above 64
  elements), and it uses the same memory.

## The class — 0.10.0's interface, unchanged

```cpp
namespace stratum {

template <typename T>                 // integral, not bool, <= 64 bits
class StratumSort {
public:
    StratumSort();                                          // automatic λ, t
    explicit StratumSort(std::size_t targetElementsPerBin,
                         std::size_t leafThreshold = 32);   // fixed λ, t
    explicit StratumSort(const Parameters& parameters);     // named

    void sort(std::vector<T>& data);                        // own workspace
    void sort(T* first, T* last);
    void sort(std::vector<T>& data, Workspace<T>& ws) const; // caller's workspace
    void sort(T* first, T* last, Workspace<T>& ws) const;

    std::size_t targetElementsPerBin() const;
    std::size_t leafThreshold() const;
    bool        automaticParameters() const;
    Parameters  effectiveParameters(std::size_t n) const;   // what a sort of n uses

    std::size_t scratchBytes() const;                       // held by the instance
    void        releaseScratch() noexcept;
};

}
```

Every 0.10.0 call site compiles and behaves as before: explicit
`(λ, t)` are clamped exactly as in 0.10.0 and used for every `n`. Two
things changed underneath: the default constructor now chooses λ per call
(below), and an instance allocates about a third of the memory.

## Workspaces, reuse and threads

A `Workspace<E>` holds the scratch of a sort. It grows to the largest
sort it has served and is kept until `release()` or destruction, so
reusing one avoids allocating on every call:

```cpp
stratum::Workspace<int64_t> ws;
for (auto& batch : batches) stratum::sort(batch, ws);
```

It is movable and not copyable, and `bytes()` reports what it holds.

**Memory budget.** A workspace also bounds how much a sort may hold:

```cpp
stratum::Workspace<int64_t> ws(1 << 20);   // at most 1 MiB of scratch
stratum::sort(v, ws);                       // any n
ws.setBudget(stratum::Workspace<int64_t>::kUnlimited);

stratum::StratumSort<int64_t> sorter;
sorter.setMemoryBudget(0);                  // the floor: ~44 KB for 64-bit keys
```

| budget | what a sort holds |
|---|---|
| `Workspace<E>::kAutomatic` (the default) | the partner buffer while it needs at most 16 MiB (`AUTOMATIC_MEMORY_LIMIT`); at most 16 MiB above that |
| `Workspace<E>::kUnlimited` | always the partner buffer: `n·sizeof(E)` plus the counters |
| a number of bytes `M` | at most `max(M, floor)`; the floor is the in-place engine's counters, 43 856 bytes for 64-bit keys, 30 736 for 32-bit, 20 896 for 8-bit |

Below the partner buffer the sort switches to an in-place distribution
that produces the same partition in passes of at most 1 024 groups, still
`Θ(n)` in the worst case (research/ALGORITHM.md §14). It is not stable,
so it serves `sort`, `sort_by_key`, `StratumSort<T>`, and `stable_sort` of
a type that is its own key. **`stable_sort_by_key` and `sorted_indices`
need their `n`-record (or `n`-pair) buffer**: given an explicit budget
below it they throw `std::length_error` before touching the input, rather
than fall back to a slower algorithm; the automatic policy does not bound
them.

**Threads.** Nothing in the library is shared or global. The rules:

| call | safe from several threads at once? |
|---|---|
| `stratum::sort(v)` and the other free functions without a workspace | yes — each call builds its own |
| free functions with a workspace | yes, **one workspace per thread** |
| `sorter.sort(v, ws) const` on **one shared** `const StratumSort<T>` | yes, one workspace per thread |
| `sorter.sort(v)` (the instance's own workspace) | **no** on one instance, as in 0.10.0 |

```cpp
const stratum::StratumSort<int64_t> sorter;             // shared, read-only
std::vector<std::thread> pool;
for (auto& chunk : chunks)
    pool.emplace_back([&sorter, &chunk] {
        stratum::Workspace<int64_t> ws;                 // one per thread
        sorter.sort(chunk, ws);
    });
for (auto& t : pool) t.join();
```

No lock is taken and none is needed. `tests/concurrency.cpp` runs this
pattern under ThreadSanitizer (`make tsan`).

## Memory

Everything a sort holds besides your array lives in its workspace, and
nothing in it depends on the input's shape except where it is less.
Peak auxiliary bytes measured at the allocator, `int64_t`, random keys:

| `n` | 0.10.0 | unlimited (partner buffer) | **automatic (default)** | 600 KiB budget | budget 0 (floor) | `std::sort` |
|---|---|---|---|---|---|---|
| 10⁵ | 2.6 MB | 850 KB | 850 KB | 600 KiB | 44 KB | 0 |
| 10⁶ | 26 MB | 8.5 MB | 8.5 MB | 600 KiB | 44 KB | 0 |
| 10⁷ | 263 MB | 82.5 MB | **16 MiB** | 600 KiB | 44 KB | 0 |
| 10⁸ | — | 825 MB | **16 MiB** | 600 KiB | 44 KB | 0 |

`std::sort` uses no heap and `O(log n)` stack (0.5–1 KB measured); the
in-place engine's stack is `O(w)` frames, 1–7 KB measured. With the
automatic policy the 16 MiB are reserved but mostly untouched on random
input (resident: ~1 MB at 10⁷); the sorted-prefix merge and deep
refinements are what use them.

What each budget costs in time, `n = 10⁷` `int64_t`, relative to the
unlimited partner buffer (one Xeon, GCC 13; research/history/V11_memoria.md
has every shape, type and size):

| input | automatic (16 MiB) | 600 KiB | floor (44 KB) | floor vs `std::sort` |
|---|---|---|---|---|
| random | −31% | −36% | +23% | 0.58× |
| nearly sorted | −26% | −24% | +8% | 0.80× |
| sorted + random tail | +12% | +36% | +1409% | 0.82× |
| organ pipe | −3% | +153% | +489% | 0.34× |
| adversarial | −43% | −45% | +10% | 0.64× |
| low entropy | −15% | −4% | +42% | 1.04× |

On large arrays the partner buffer's one-pass scatter over the whole
array is slower than passes of 1 024 groups — from about 4·10⁶ 8-byte keys
on most inputs, −58% at 10⁸, and 2–4× for 72- to 264-byte records at
10⁷ — so bounding the memory is also faster there. At the floor every pass is an American-flag
permutation (no block buffers): slower than the partner buffer, never more
than 1.04× `std::sort` at 10⁷.

Paths that need less than the table, whatever the budget:

- already ascending or non-increasing input: nothing;
- a key span smaller than `n/λ` (8-bit keys, few distinct values): only
  the counters — about 1 KB for `uint8_t`;
- at most `λ` elements: nothing.

## Parameters and the automatic default

Two numbers steer the algorithm; both are **hints**, clamped rather than
rejected, so no combination can produce undefined behaviour or break the
complexity bound.

```cpp
struct Parameters {
    std::size_t targetElementsPerBin = 0; // λ; 0 = automatic
    std::size_t leafThreshold        = 0; // t; 0 = automatic (2λ)
};
```

**Automatic (the default everywhere).** λ is chosen per call from `n`:

| `n` | λ | t |
|---|---|---|
| `≤ 2²²` (4 194 304) | 16 | 32 |
| `> 2²²` | 32 | 64 |

This is a deterministic function of `n` only. It deliberately does **not**
read the cache size or the core count: `sort_by_key` orders equal keys
according to λ, and a machine-dependent λ would make that order change
between your laptop and your server. The rule was chosen by measuring
every λ from 8 to 1024 on four environments (Linux/GCC on two x86_64
machines, Apple M1/AppleClang, Windows/MSVC on AMD EPYC): on every
measured row the automatic choice costs at most about 1.2× the time of
that row's best λ, usually within 1.1×, where 0.10.0's fixed λ = 32
reached 1.40×.
`effectiveParameters(n)` tells you what a sort of `n` elements will use.

**Explicit.** Name the fields, so they cannot be swapped:

```cpp
stratum::Parameters p;
p.targetElementsPerBin = 32;
p.leafThreshold = 64;
stratum::sort(v, ws, p);
stratum::StratumSort<int64_t> s(p);
```

Clamping: λ = 0 means automatic; λ and t above 10 000 are lowered to
10 000; t below λ is raised to λ. The ceiling is part of the complexity
guarantee: it keeps λ and t independent of `n` whatever is passed —
`data.size()` included — so the worst case stays `Θ(n)`.

### What λ and t do, if you tune them

- **λ, the target elements per bin**, trades local sorting (leaves hold
  about λ elements; insertion-sort cost grows like `(λ+1)/4` comparisons
  per element up to λ = 64) against scattering (the distribution writes
  into `n/λ` streams at once). Smaller λ also lowers the largest leaf a
  comparison sort can ever receive, `λ·2^(w/7)`: about 9 000 at λ = 16.
  There is rarely a reason to leave the automatic value; if you measure,
  alternate the candidates inside one process on your own data.
- **t, the leaf threshold**, is a safe lower bound, not a tuned value: it
  only needs to sit above the upper tail of the occupancy of a bin.
  `t = 2λ` was re-measured against `t/λ` from 1 to 8 and is optimal or
  within 4% everywhere; `t = λ` sends about half the elements into an
  extra refinement pass for no reason.

**A hazard, still present for compatibility:** `StratumSort(64, 32)` is
read as `(64, 64)`, not `(32, 64)`, because t is raised to λ. The
`Parameters` form removes it.

### Compile-time constants

`include/stratum/Config.hpp` holds the rest — the maximum refinement depth
`D = 6`, the local-sort dispatch thresholds, the automatic-parameter table
— each with its justification. One constraint is load-bearing: **`D ≥ 1`**,
enforced with a `static_assert`.

## Performance — what to expect

Measured by CI on GitHub's runners with `benchmarks/stratum_bench.cpp`,
`n = 10⁶`, time relative to `std::sort` on the same input (below 1 is
faster). Full tables per platform are in the README.

| input | Linux x86_64, GCC | macOS arm64, AppleClang | Windows x86_64, MSVC |
|---|---|---|---|
| random `int64_t` | 0.33× | 0.57× | 0.37× |
| already sorted / reversed | 0.05× / 0.13× | 0.39× / 0.39× | 0.05× / 0.05× |
| nearly sorted (1% swaps) | 0.70× | 0.76× | 1.23× |
| few distinct values | 0.13× | 0.25× | 0.17× |
| random `uint8_t` | 0.04× | 0.08× | 0.04× |
| random `double` | 0.34× | 0.63× | 0.50× |
| 16-byte records, stable vs `std::stable_sort` | 0.26× | 0.25× | 0.41× |

Where it loses: inputs built against its own partition (1.2–1.7× on
macOS and Windows), low-entropy keys on Apple M1 (1.7×), nearly sorted
input under MSVC (1.2×), and nearly sorted floating point on every
platform (1.1–2.9×). The README lists every loss with its cause.

## Guarantees

- **Deterministic.** No randomness; the same input and the same `n`
  always produce the same sequence of operations and the same output.
- **`Θ(n)` worst case**, for every input and every argument, treating the
  key width as a constant (the sense in which radix sort is linear).
- **Strong exception safety** in a release build. Every allocation happens
  before the first write to your array, so if a sort throws
  (`std::bad_alloc`), the input is untouched. This does *not* hold with
  `STRATUM_ENABLE_METRICS` (the instrumentation allocates while
  recording), nor if a key function throws.
- **Bounded memory.** At most 16 MiB of scratch by default, whatever `n`
  is, and at most `max(budget, floor)` bytes under an explicit
  `Workspace` budget — except the stable sorts of records, which need
  their `n`-record buffer and refuse a smaller explicit budget
  (`std::length_error`, input untouched). See "Memory".
- **No global state.** See "Workspaces, reuse and threads".

## Build configurations

| | metrics | assertions | use for |
|---|---|---|---|
| release | no | no | production; the only meaningful timings |
| test | no | **yes** | correctness; slower, never for timing |
| research | yes | yes | instrumentation; several times slower |

Define `STRATUM_ENABLE_METRICS` to compile in the counters — comparisons,
bins, subdivisions, depth, per-phase timing — reachable through
`sorter.metrics()` or `workspace.metrics()`. Without it, none of that
exists in the object code. Never quote a timing from that build.

> **Define it for the whole program or not at all.** The macro adds
> members to `StratumSort<T>` and `Workspace<E>`, so they have a different
> size and layout in the two configurations. That mismatch is made safe
> rather than left to chance: the classes live in an inline namespace
> tagged by the macro, so the two configurations are **different types**.
> You still write `stratum::StratumSort<T>`. But if two translation units
> disagree and the type crosses between them, the build fails at link time
> with an undefined symbol mentioning `stratum::abi_v1` or
> `stratum::abi_metrics_v1` — instead of linking quietly and corrupting
> memory later.

## Running the tests and the benchmark

```bash
make test         # all suites, assertions active
make sanitizers   # ASan/UBSan
make tsan         # ThreadSanitizer: shared sorter, one workspace per thread
make fuzz N=200000
make bench        # benchmarks/stratum_bench against std::sort, SUITE=quick|ci|types|lambda|full
```

Or with CMake:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build
cmake -B build -DSTRATUMSORT_BUILD_BENCHMARKS=ON && cmake --build build --target stratum_bench --config Release
```

`stratum_bench` prints the compiler, standard, flags, CPU, caches and core
count with every table, times Stratum and `std::sort` alternately on
identical copies of each input, and reports medians with the p10–p90
spread, ns/element, elements/s and the peak auxiliary memory measured at
the allocator. Run it on your own machine before relying on any number
above.
