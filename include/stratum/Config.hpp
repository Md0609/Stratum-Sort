#pragma once

#include <cstddef>

// ============================================================
// Stratum Sort - compile-time configuration
// ============================================================
// Every constant here is a tuning parameter, not a correctness
// requirement: the algorithm is correct for any value satisfying the
// stated preconditions. Where a value has a *measured* justification it
// is given; where it does not, that is said explicitly rather than
// implied.
//
// Each value states its own justification. Where a value has never been
// measured on this codebase, that is said explicitly rather than implied.
// ============================================================
namespace stratum {

// ------------------------------------------------------------------
// STRATUM_ENABLE_METRICS - build configuration
// ------------------------------------------------------------------
// Undefined (production): SortMetrics, the introspection API
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
// Upper bound: see MAX_SUBDIVISION_DEPTH. Raising lambda does not change
// the complexity class, but it raises the input size above which the
// linear regime applies - roughly lambda * 2^(w/(D+1)), which is ~1.8e4
// at lambda = 32 and ~5.8e5 at lambda = 1024.
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
// Setting t = lambda is legal - it fuses the two parameters back into
// one, which is how the algorithm was originally written - at the
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
// D DOES NOT MAKE THE ALGORITHM LINEAR, which is the opposite of what a
// depth cap usually does. It is a constant-factor choice.
//
// Refinement terminates on its own and stays linear WITHOUT any cap: each
// level at least halves the bin's observed span, so the depth is bounded
// by w = 64 regardless, and the residual handed to a comparison sort
// would be just t. Uncapped costs O((w+1)*n); capped costs O((D+1)*n) but
// enlarges the residual. D trades passes for residual size:
//
//     uncapped   65 passes over the data, largest residual t = 64
//     D = 6       7 passes over the data, largest residual ~18000
//
// D = 6 was chosen by measurement, not by analysis: over a range of
// adversarial inputs the uncapped variant was never faster and cost up to
// +109%. Both settings are Theta(n); this is a constant-factor decision.
//
// ---- The residual bound ----
// A leaf with span > 0 produced by depth exhaustion has size at most
//
//     B(n) = min( n, (lambda^(D+1) * 2^w / n)^(1/D) )
//
// These are TWO DIFFERENT OBJECTS and the docs used to blur them:
//
//   B(n) is the bound AT A GIVEN n. It depends on n and, above
//        n = lambda*2^(w/(D+1)), it DECREASES like n^(-1/D), because the
//        top-level split spends log2(n/lambda) of the w-bit budget before
//        refinement starts. B(1e6) = 9268.2, B(1e7) = 6314.3.
//
//   M = sup over n of B(n) = lambda * 2^(w/(D+1)) = 18089.4 is the GLOBAL
//        SUPREMUM: a single constant, free of n, attained near n = M
//        itself. It is NOT the tightest bound at any particular size -
//        for realistic n it is 2-3x looser than B(n).
//
// Linearity needs only M < infinity. B(n) is what an adversary at a given
// size actually faces.
//
// Note the exponent is w/(D+1) and not w/D: the top-level split in
// distribute() already spends log2(n/lambda) of the w-bit budget before
// refinement runs, so it counts as one of the D+1 splits along any
// root-to-leaf path.
//
// ---- What raising lambda actually does ----
// Raising lambda does NOT reintroduce a Theta(n log n) term: for fixed w
// the residual is bounded by a constant whatever lambda is, so the
// algorithm stays Theta(n).
//
// What lambda moves is the input size at which the linear regime starts.
// The bound above is vacuous while lambda * 2^(w/(D+1)) exceeds n, since
// m <= n always - and in that range the whole array can end up in one
// comparison sort. So the threshold to watch is
//
//     n* = lambda * 2^(w/(D+1))       lambda = 32   -> n* ~ 1.8e4
//                                     lambda = 1024 -> n* ~ 5.8e5
//
// At lambda = 32 that is far below any realistic input, so the linear
// regime always applies. At lambda = 1024 it lands inside the range
// people actually sort, and inputs near it can degrade to Introsort over
// a large fraction of the array. Asymptotically still linear; practically
// a different algorithm. ANY change to lambda must re-check n*.
// Raising the leaf threshold does not affect it.
//
// All of the above assumes lambda, t and D are constants chosen
// independently of n (hypothesis H2 of the proof), AND that D >= 1. A
// caller who scales one of them with the input size - t = n, say - is
// outside the hypothesis, and the linear bound has to be re-derived.
//
// D = 0 is not merely outside the proof, it is genuinely superlinear:
// refine's first test is `count <= t || depth >= D`, so D = 0 fires it at
// depth 0 for every top-level bin and refinement never runs. A single bin
// can then hold Theta(n) elements and go straight to Introsort. Measured
// with D forced to 0: comparisons per element rise from 14.2 at n = 1e5
// to 17.6 at n = 1e6, which is the log n signature. Do not set D = 0.
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

} // namespace stratum
