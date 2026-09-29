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
// ------------------------------------------------------------------
// ABI TAG - why StratumSort lives in an inline namespace
// ------------------------------------------------------------------
// STRATUM_ENABLE_METRICS adds a member to StratumSort, so the class has a
// different size and layout in the two configurations. Without a tag, two
// translation units that disagree about the macro would each define a
// different class under the SAME name: an ODR violation that links
// silently and misbehaves at run time.
//
// Putting the class in an inline namespace whose NAME depends on the macro
// makes the two configurations distinct types with distinct mangled names.
// Callers still write stratum::StratumSort<T> - an inline namespace is
// transparent to name lookup - but a mismatch between translation units
// now fails at link time with an undefined symbol instead of corrupting
// memory. A build that is internally consistent is unaffected.
#ifdef STRATUM_ENABLE_METRICS
#define STRATUM_ABI_NAMESPACE abi_metrics_v1
#else
#define STRATUM_ABI_NAMESPACE abi_v1
#endif

namespace stratum_v0_11_pre {

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
//   - Scatter cost grows as lambda shrinks: the distribution pass keeps
//     n/lambda counters and writes into n/lambda output streams.
//
// WHY 16 (0.11.0). Up to 0.10.0 the default was 32 with the caveat that it
// was tuned to one cache - "n * 64 / lambda <~ L2", "much larger inputs
// want a larger lambda". The 64 was the bytes each bin cost: a 48-byte tree
// node plus its histogram entries. 0.11.0 keeps no tree and one 4-byte
// counter per bucket (detail/Engine.hpp), which moved the point where the
// fan-out saturates the cache by more than an order of magnitude, and with
// it the optimum. Re-measured on four environments (the table is under
// AUTOMATIC PARAMETERS below): up to a few million elements 16 is within
// 10-17% of each machine's best, where 32 was up to 40% off. Above that,
// larger inputs do still want a larger lambda on three of the four, which
// is what the automatic parameters below implement - as a function of n,
// never of the cache.
//
// Upper bound: see MAX_SUBDIVISION_DEPTH. lambda enters the worst-case
// leaf bound as M = lambda * 2^(w/(D+1)): 9045 at lambda = 16 (18090 at
// 0.10.0's 32), so the smaller default also halves the largest leaf an
// adversary can force.
constexpr std::size_t DEFAULT_TARGET_ELEMENTS_PER_BIN = 16;

// t - leaf threshold. A bin holding at most t elements stops being
// refined and is handed to the local sort.
//
// Precondition: t >= lambda. A bin of the target occupancy must be
// allowed to become a leaf; the constructor enforces this.
//
// t is NOT a tuned value, it is a safe lower bound. Its only job is to
// sit above the upper tail of the occupancy distribution so that a bin
// of typical size is not refined merely because it landed slightly above
// average: with lambda = 16, P(Poisson(16) > 32) ~ 1e-4. t = 2 * lambda
// was re-measured in 0.11.0 against t/lambda in {1, 1.5, 3, 4, 8}
// (research/perf/LambdaSweep.cpp --t-sweep): on every non-adversarial
// shape it is at the optimum or within 4% of it, at lambda 16 and 32.
//
// Setting t = lambda is legal - it fuses the two parameters back into
// one, which is how the algorithm was originally written - at the
// cost of sending roughly half of all elements into refinement purely
// because P(X > lambda) ~ 0.5 for Poisson(lambda) - by arithmetic, not
// because the data needs it. Measured at lambda = 16: 20-22% slower on
// random input, 75-85% on nearly sorted input.
constexpr std::size_t DEFAULT_LEAF_THRESHOLD = 32;

// Ceilings on lambda and t. They are what makes hypothesis H2 a property
// of the code instead of an assumption about the caller: the constructor
// clamps lambda into [1, MAX_TARGET_ELEMENTS_PER_BIN] and t into
// [lambda, MAX_LEAF_THRESHOLD], so neither can grow with n whatever the
// caller passes - data.size() included. Without them, lambda = t = n put
// the whole array in one leaf and the worst case was Theta(n log n).
//
// 10000 is the smallest value that changes no configuration the project
// itself uses: the contract tests pass t = 10000 and lambda up to 4385.
// A larger ceiling could not lower the worst-case constant, which is a
// maximum over every admissible (lambda, t) - a larger ceiling only adds
// configurations - and it raises the leaf bound ceiling * 2^(w/(D+1)).
// Its only gain would be for callers who want leaves much larger than the
// defaults, which no measurement here supports.
constexpr std::size_t MAX_TARGET_ELEMENTS_PER_BIN = 10000;
constexpr std::size_t MAX_LEAF_THRESHOLD = 10000;
static_assert(MAX_LEAF_THRESHOLD >= MAX_TARGET_ELEMENTS_PER_BIN,
              "t >= lambda must remain satisfiable at the ceiling");
static_assert(DEFAULT_TARGET_ELEMENTS_PER_BIN <= MAX_TARGET_ELEMENTS_PER_BIN &&
                  DEFAULT_LEAF_THRESHOLD <= MAX_LEAF_THRESHOLD,
              "the defaults must lie below the ceilings");

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
//     uncapped   65 passes over the data, largest residual t = 32
//     D = 6       7 passes over the data, largest residual ~9000
//                 (~18000 at 0.10.0's lambda = 32)
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
//        refinement starts. At the default lambda = 16:
//        B(1e6) = 4128.5, B(1e7) = 2812.7 (9268.2 and 6314.3 at 32).
//
//   M = sup over n of B(n) = lambda * 2^(w/(D+1)) = 9044.7 is the GLOBAL
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
//     n* = lambda * 2^(w/(D+1))       lambda = 16   -> n* ~ 9.0e3
//                                     lambda = 32   -> n* ~ 1.8e4
//                                     lambda = 1024 -> n* ~ 5.8e5
//
// At lambda = 16 that is far below any realistic input, so the linear
// regime always applies. At lambda = 1024 it lands inside the range
// people actually sort, and inputs near it can degrade to Introsort over
// a large fraction of the array. Asymptotically still linear; practically
// a different algorithm. ANY change to lambda must re-check n*.
// Raising the leaf threshold does not affect it.
//
// All of the above needs lambda, t and D to be constants independent of n,
// AND D >= 1 (hypothesis H2 of the proof). The code guarantees both: the
// constructor clamps lambda and t to the ceilings above, so a caller who
// passes t = n gets at most MAX_LEAF_THRESHOLD, and D >= 1 is asserted at
// compile time below.
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
static_assert(MAX_SUBDIVISION_DEPTH >= 1, "D = 0 disables refinement: the worst case becomes Theta(n log n)");

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

// ------------------------------------------------------------------
// AUTOMATIC PARAMETERS (0.11.0)
// ------------------------------------------------------------------
// A default-constructed StratumSort, and every free function called
// without explicit Parameters, chooses lambda from n alone:
//
//     n <= AUTOMATIC_LARGE_INPUT (2^22)   lambda = 16, t = 32
//     n >  AUTOMATIC_LARGE_INPUT          lambda = 32, t = 64
//
// WHY A STEP IN n, MEASURED ON FOUR ENVIRONMENTS. Worst slowdown against
// each row's own best lambda (research/perf/LambdaSweep.cpp and the lambda
// suite of benchmarks/stratum_bench.cpp, shapes random, normal, nearly
// sorted, whole universe, duplicates, 64- and 32-bit keys):
//
//                                  n <= 1e6          n = 1e7
//                               lambda 16  32     lambda 16  32
//     Xeon, Linux, GCC             1.10  1.29        1.12  1.00
//     x86_64 runner, Linux, GCC    1.09  1.39        1.09  1.17
//     Apple M1, AppleClang         1.17  1.40        1.17  1.10
//     EPYC runner, Windows, MSVC   1.05  1.15        1.27  1.04
//
// No single lambda is best everywhere; 16 up to about 4 million elements
// and 32 above is within 17% of every row's best on every machine. On the
// Xeon the crossover lies between 2^22 (16 best) and 2^23 (24-32 best),
// hence the threshold; it is not resolved more finely than that.
//
// WHY NOT THE CACHE SIZE. It would have to be queried per platform, and
// the partition - hence the order of equal keys in the UNSTABLE sorts -
// would then differ between machines. A function of n alone is
// deterministic and reproducible everywhere.
//
// WHY THE PROOF DOES NOT CARE. Hypothesis H2 needs lambda and t to be
// constants independent of n. A lambda that takes one of two fixed values
// depending on n satisfies it: both lie below the ceilings, so every bound
// of research/ALGORITHM.md holds with the constant of the larger one.
constexpr std::size_t AUTOMATIC_LARGE_INPUT = std::size_t{1} << 22;
constexpr std::size_t LARGE_INPUT_TARGET_ELEMENTS_PER_BIN = 32;
constexpr std::size_t LARGE_INPUT_LEAF_THRESHOLD = 64;
static_assert(LARGE_INPUT_TARGET_ELEMENTS_PER_BIN <= MAX_TARGET_ELEMENTS_PER_BIN &&
                  LARGE_INPUT_LEAF_THRESHOLD <= MAX_LEAF_THRESHOLD &&
                  LARGE_INPUT_LEAF_THRESHOLD >= LARGE_INPUT_TARGET_ELEMENTS_PER_BIN,
              "the automatic parameters must be admissible");

// ------------------------------------------------------------------
// RUN-TIME PARAMETERS, NAMED
// ------------------------------------------------------------------
// lambda and t as one aggregate with named fields, accepted by
// StratumSort's constructor and by every free function (Sort.hpp):
//
//     stratum::Parameters p;            // both 0: automatic, as above
//     p.targetElementsPerBin = 64;      // fixed lambda
//     p.leafThreshold = 128;            // fixed t
//
// StratumSort(64, 32) compiles and means (64, 64); with named fields the
// two cannot be swapped by accident. A field left at 0 is chosen
// automatically - lambda from n as above, t as 2 * lambda. A field set is
// clamped exactly like the positional constructor: lambda into
// [1, MAX_TARGET_ELEMENTS_PER_BIN], t into [lambda, MAX_LEAF_THRESHOLD].
struct Parameters {
    std::size_t targetElementsPerBin = 0; // lambda; 0 = automatic
    std::size_t leafThreshold = 0;        // t;      0 = automatic (2 * lambda)
};

// The parameters a sort of n elements uses when nothing was fixed.
inline Parameters automaticParameters(std::size_t n) {
    if (n > AUTOMATIC_LARGE_INPUT)
        return {LARGE_INPUT_TARGET_ELEMENTS_PER_BIN, LARGE_INPUT_LEAF_THRESHOLD};
    return {DEFAULT_TARGET_ELEMENTS_PER_BIN, DEFAULT_LEAF_THRESHOLD};
}

// Resolves a Parameters for a sort of n elements: 0 fields chosen
// automatically, set fields clamped.
inline Parameters resolveParameters(const Parameters& p, std::size_t n) {
    const Parameters automatic = automaticParameters(n);
    std::size_t lambda = p.targetElementsPerBin == 0 ? automatic.targetElementsPerBin
                                                     : p.targetElementsPerBin;
    if (lambda > MAX_TARGET_ELEMENTS_PER_BIN) lambda = MAX_TARGET_ELEMENTS_PER_BIN;
    std::size_t t = p.leafThreshold == 0 ? 2 * lambda : p.leafThreshold;
    if (t < lambda) t = lambda;
    if (t > MAX_LEAF_THRESHOLD) t = MAX_LEAF_THRESHOLD;
    return {lambda, t};
}

} // namespace stratum_v0_11_pre
