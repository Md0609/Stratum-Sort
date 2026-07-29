#pragma once

#include <cstddef>

// ============================================================
// Dynamic Range Sort - compile-time configuration
// ============================================================
// Every constant here is a tuning parameter, not a correctness
// requirement: the algorithm is correct for any value satisfying the
// stated preconditions. Where a value has a *measured* justification it
// is given; where it does not, that is said explicitly rather than
// implied.
//
// The rationale behind each value, and the experiments behind it, live in
// documentacion/. This file states the contract, not the history.
// ============================================================
namespace drs {

// ------------------------------------------------------------------
// DRS_ENABLE_METRICS - build configuration
// ------------------------------------------------------------------
// Undefined (production): DRSMetrics, the introspection API
// (debugPartitionOnly) and every instrumentation call site are compiled
// out entirely. The production binary contains no counters, timers or
// histograms - not disabled ones, none at all.
//
// Defined (research): the full metrics system is compiled in. It costs
// time and memory and must never be used to measure performance.

// ------------------------------------------------------------------
// PARTITIONING PARAMETERS
// ------------------------------------------------------------------

// lambda - target occupancy per bin.
//
// Controls how finely the value range is cut:
//     binCount = ceil(n / lambda)              (top level)
//     splits   = ceil(count / lambda)          (each refinement)
//
// It is the only parameter with a first-order effect on running time,
// through two opposing costs:
//
//   - Local sorting cost grows with lambda. Leaf sizes follow the bin
//     occupancy distribution, and Insertion Sort is quadratic in leaf
//     size, so the expected comparison count per element is
//     E[k^2] / (4 E[k]); for Poisson(lambda) occupancy that is
//     (lambda + 1) / 4.
//
//   - Scatter cost grows as lambda shrinks. The distribution pass writes
//     into n/lambda simultaneous output streams, each holding one cache
//     line live, so the write working set is (n / lambda) * 64 bytes.
//     Once that approaches the L2 capacity, throughput collapses.
//
// THE USEFUL LOWER BOUND THEREFORE DEPENDS ON n AND ON THE CACHE, NOT ON
// THE ALGORITHM: the condition is roughly
//
//     n * 64 / lambda  <~  L2 capacity
//
// The default below was measured as a practical optimum for n ~ 1e6 on a
// machine with a 4 MiB L2. It is NOT universal: for n ~ 1e7 the same
// condition would put the lower bound near 160. A caller sorting much
// larger inputs should raise it.
//
// Upper bound: see MAX_SUBDIVISION_DEPTH, which imposes a hard
// constraint (lambda >= 1024 reintroduces a superlinear worst case).
constexpr std::size_t DEFAULT_TARGET_ELEMENTS_PER_BIN = 32;

// t - leaf threshold. A bin holding at most t elements stops being
// refined and is handed to the local sort.
//
// Precondition: t >= lambda. A bin of the target occupancy must be
// allowed to become a leaf; the constructor enforces this.
//
// t is NOT a tuned value, it is a safe lower bound. Its only job is to
// sit above the upper tail of the occupancy distribution so that a bin
// of typical size is not refined merely because it landed slightly above
// average. Once t clears that tail, raising it further changes nothing:
// with lambda = 32, P(Poisson(32) > 64) ~ 2e-7, and t = 64, 96 and 128
// were measured to produce byte-identical counters on every dataset.
//
// Setting t = lambda is legal and reproduces pre-v10 behaviour, at the
// cost of sending roughly half of all elements into refinement purely
// because P(X > lambda) ~ 0.5 for Poisson(lambda) - by arithmetic, not
// because the data needs it.
constexpr std::size_t DEFAULT_LEAF_THRESHOLD = 64;

// D - maximum refinement depth.
//
// Refinement terminates on its own: each level strictly shrinks the bin's
// observed span (see the proof next to refine()), so the recursion cannot
// run forever. D is a bound on how much work is spent trying, after which
// the remainder is handed to the local sort whatever its size.
//
// D is what keeps the worst case linear, which is the opposite of what it
// looks like. Each level consumes log2(splits) bits of the bin's span,
// and a span has at most w bits, so a bin of size m can only survive D
// degenerate levels if
//
//     D * log2(m / lambda) <= w    <=>    m <= lambda * 2^(w/D)
//
// With lambda = 32, w = 64 and D = 6 that is m <= ~52000. The residual
// handed to a comparison sort is therefore bounded by a CONSTANT
// independent of n, and its aggregate cost is n * log2(52000) ~= 16n.
//
// HARD CONSTRAINT: the bound scales with lambda. Raising lambda to 1024
// makes m_max ~= 1.7e6, no longer small compared to a realistic n, and a
// Theta(n log n) term reappears. ANY change to lambda must re-check this.
// Raising the leaf threshold does not affect it.
//
// The value 6 itself has never been swept; it is known to work, not known
// to be optimal.
constexpr std::size_t MAX_SUBDIVISION_DEPTH = 6;

// ------------------------------------------------------------------
// LOCAL SORT PARAMETERS
// ------------------------------------------------------------------
// These describe the local sorting routines and NOTHING ELSE. They used
// to be derived from the leaf threshold, which meant that changing how
// the partitioning stops silently changed which sorting algorithm ran on
// the leaves - two unrelated concepts sharing one number. They are now
// independent.
//
// A leaf normally holds at most t elements, so only the first threshold
// is normally reached; the other two exist for leaves produced by
// exhausting MAX_SUBDIVISION_DEPTH, which can be much larger.
//
// NONE OF THESE THREE VALUES HAS BEEN MEASURED on this codebase. They are
// conventional values inherited from textbook implementations. Treat them
// as unverified.

// Ranges of at most this many elements are sorted with Insertion Sort.
constexpr std::size_t LOCAL_INSERTION_MAX_ELEMENTS = 64;

// Ranges of at most this many elements (and above the previous bound) use
// QuickSort. Larger ranges use Introsort, which falls back to HeapSort
// when its own recursion budget is exhausted.
constexpr std::size_t LOCAL_QUICKSORT_MAX_ELEMENTS = 384;

// Inside QuickSort and Introsort, ranges of at most this many elements
// are finished with Insertion Sort instead of being partitioned further.
constexpr std::size_t LOCAL_PARTITION_CUTOFF = 12;

// Introsort switches to HeapSort after this many levels of partitioning,
// expressed as a multiple of log2(range size). The classic value is 2.
constexpr std::size_t INTROSORT_DEPTH_FACTOR = 2;

} // namespace drs
