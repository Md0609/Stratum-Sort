#pragma once

#include "Config.hpp"

#ifdef STRATUM_ENABLE_METRICS
#include "Metrics.hpp"
#endif

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace stratum {

// ============================================================
// StratumSort
// ============================================================
// A distribution sort for integer keys.
//
// Rather than comparing elements against each other globally, it looks at
// how the values are spread out, cuts the observed value range into
// equal-width intervals, refines the intervals that are still too crowded
// using only that interval's own observed range, sorts each final
// interval locally, and concatenates the result.
//
//     analyse -> build intervals -> refine -> sort locally -> join
//
// The join is a plain concatenation, never a merge: intervals are
// value-ordered by construction, so every element of interval j is <=
// every element of interval j+1.
//
// ---- Complexity ---------------------------------------------------
// Theta(n) time and Theta(n) auxiliary space, treating the key width w as
// a constant - the same sense in which radix sort is linear. This is NOT
// a bound in the comparison model and does not contradict the
// Omega(n log n) comparison lower bound: the algorithm does arithmetic on
// the keys, not only comparisons.
//
// The constant is bounded because the refinement depth is bounded by
// min(w, MAX_SUBDIVISION_DEPTH), and a leaf reaching a comparison sort
// with a non-zero span has size at most
//
//     B(n) = min( n, (lambda^(D+1) * 2^w / n)^(1/D) )
//
// which for large n DECREASES like n^(-1/D), and whose supremum over all
// n is the CONSTANT lambda * 2^(w/(D+1)) ~= 18000. The bound depends on
// n; its supremum does not, and that supremum is what makes the total
// linear. Full proof, with its four hypotheses and five lemmas, in
// docs/ALGORITHM.md 8.
//
// ---- Guarantees ---------------------------------------------------
// STABILITY: none. This sorter is NOT stable. For the integral key types
//   it accepts, equal elements are indistinguishable, so this is
//   unobservable - but it does mean the implementation must not be
//   generalised to key/value pairs without revisiting detectRun(), which
//   reverses runs in place.
//
// THREAD SAFETY: a single instance is NOT safe to use from more than one
//   thread; it owns mutable scratch buffers reused across the whole
//   recursion. Distinct instances are independent and may be used
//   concurrently. There is no shared global state.
//
// EXCEPTIONS: strong guarantee in a release build. The only operations
//   that can throw are the internal allocations, and all of them happen
//   before the first write to the caller's array - the output is produced
//   only in the final join. If sort() throws, the input is unchanged.
//   This does NOT hold when STRATUM_ENABLE_METRICS is defined: the
//   instrumentation records the timing of the join phase after the join
//   has run, and recording it allocates. A research build can therefore
//   throw with the output already written. That build is a measurement
//   tool, not a product; tests/api_contract.cpp pins the difference.
//
// MEMORY: sort() allocates
//
//       2n * sizeof(T)        the two cascading buffers
//     +  n * sizeof(size_t)   one bucket index per element
//     +  O(n / lambda)        write cursors and the refinement tree
//
//   NOTE the middle term does not scale with T: it is one size_t per
//   element whatever the key type. For an 8-byte key the total is about
//   3.1x the input, but for a 1-byte key it is about 10x. Sorting narrow
//   keys is where this sorter is least economical with memory.
//
//   The scratch is allocated on first use and reused across later calls
//   on the same instance. It is released when the instance is destroyed,
//   not between calls: an instance used once on a huge array keeps that
//   memory alive.
//
// ---- Element type -------------------------------------------------
// Integral types only: every formula (span, interval width, bin index) is
// defined over exact integer arithmetic, and the range arithmetic relies
// on unsigned wrap-around being well defined.
// ============================================================
template <typename T>
class StratumSort {
    static_assert(std::is_integral<T>::value,
                  "StratumSort requires an integral element type");
    // std::is_integral<bool> is true, but std::vector<bool> is the packed
    // specialisation: no data(), elements not individually addressable, so
    // the block copies below cannot work on it. Rejecting it here gives a
    // readable message instead of a template error deep inside memcpy.
    static_assert(!std::is_same<typename std::remove_cv<T>::type, bool>::value,
                  "StratumSort does not support bool: std::vector<bool> is a packed "
                  "specialisation with no contiguous storage");
    // The span arithmetic is carried in uint64_t throughout, so a key wider
    // than 64 bits would have its offset truncated in bucketOf() and could
    // produce an out-of-range bucket index. That is not a wrong answer, it
    // is a heap overflow: verified with __int128, which some toolchains
    // report as integral. The proof of linearity also assumes w is bounded.
    // Rejecting the type is the honest option; widening the arithmetic
    // would be a different algorithm with a different cost model.
    static_assert(sizeof(T) <= sizeof(uint64_t),
                  "StratumSort supports keys of at most 64 bits: the range arithmetic "
                  "is carried in uint64_t");

public:
    // targetElementsPerBin (lambda) is the target occupancy per bin;
    // leafThreshold (t) is the size at which refinement stops. Config.hpp
    // explains what each controls and how to choose it.
    //
    // Both arguments are clamped rather than rejected, so no combination
    // can produce undefined behaviour:
    //   - lambda == 0 becomes the default (a bin must hold >= 1 element);
    //   - t < lambda is raised to lambda (a bin of the target occupancy
    //     must be allowed to become a leaf).
    // The clamping is silent by design: these are tuning hints, not a
    // contract the caller can violate. Use the accessors below to see
    // what an instance actually ended up with.
    //
    // KNOWN HAZARD: both parameters are std::size_t and adjacent, so
    // swapping them at a call site compiles. StratumSort(64, 32) is
    // read as (64, 64), not (32, 64). A named-parameter struct would
    // remove the hazard at the cost of breaking source compatibility; it
    // is deliberately not introduced.
    explicit StratumSort(std::size_t targetElementsPerBin = DEFAULT_TARGET_ELEMENTS_PER_BIN,
                              std::size_t leafThreshold = DEFAULT_LEAF_THRESHOLD);

    // Sorts 'data' in place into ascending order.
    void sort(std::vector<T>& data);

    std::size_t targetElementsPerBin() const { return targetElementsPerBin_; }
    std::size_t leafThreshold() const { return leafThreshold_; }

#ifdef STRATUM_ENABLE_METRICS
    // Statistics from the last sort() call. Research build only.
    const SortMetrics& metrics() const { return metrics_; }

    // Lets analysis code inspect the partition Stratum Sort would produce, without
    // sorting or joining it. sort() never calls any of this.
    struct LeafView {
        bool inBufferA;
        std::size_t start;
        std::size_t count;
    };

    std::vector<LeafView> debugPartitionOnly(const std::vector<T>& data);

    const std::vector<T>& debugBufferA() const { return bufferA_; }
    const std::vector<T>& debugBufferB() const { return bufferB_; }
#endif

private:
    // Signed index type for the local sorts. std::ptrdiff_t rather than
    // long, which is 32 bits on Windows and would silently break for
    // ranges above 2^31.
    using Index = std::ptrdiff_t;

    // ---- Value-space types ------------------------------------------

    // The observed minimum and maximum of a range of elements.
    struct ValueRange {
        T minimum{};
        T maximum{};
    };

    // A grid of equal-width value intervals: the description of one
    // splitting step, shared by the top level and by every refinement.
    //
    // 'width' is a magnitude, not a value of T: it is carried as uint64_t
    // because an interval can be wider than T can represent.
    struct Partitioning {
        T origin{};                // value mapped to bucket 0
        uint64_t width = 1;        // values per bucket
        std::size_t binCount = 1;  // number of buckets
    };

    // ---- Element-space types ----------------------------------------
    // These exist to keep countAndPlace() readable. It used to take ten
    // loose parameters, five of which were (buffer, offset, length)
    // triples that only mean anything together.

    struct SourceSlice {
        const std::vector<T>& buffer;
        std::size_t start;
        std::size_t count;
    };

    struct TargetSlice {
        std::vector<T>& buffer;
        std::size_t start;
    };

    // ---- Phase 1: analysis ------------------------------------------
    ValueRange analyze(const std::vector<T>& data) const;

    // ---- Interval formulas ------------------------------------------
    Partitioning planPartition(const ValueRange& range, std::size_t length) const;

    // The value-to-bucket map. Takes the grid fields as scalars rather
    // than the Partitioning, because the distribution loop calls it per
    // element and must keep them in registers - see countAndPlace().
    static std::size_t bucketOf(T value, T origin, uint64_t width);

    // ---- Phases 2 and 3: distribution -------------------------------
    void distribute(const std::vector<T>& data, const Partitioning& grid,
                    std::vector<std::size_t>& outBucketStart,
                    std::vector<std::size_t>& outBucketSize);

    void countAndPlace(SourceSlice src, TargetSlice dst, const Partitioning& grid,
                       std::vector<std::size_t>& outBucketStart,
                       std::vector<std::size_t>& outBucketSize);

    // ---- Phase 4: recursive refinement ------------------------------
    struct RefinedRange {
        bool inBufferA = true;

        // Set by refine() when it finds the bin's observed span is zero,
        // i.e. every element is identical. Such a bin is sorted by
        // definition; the flag lets the local sort skip it instead of
        // re-scanning to rediscover the same fact.
        //
        // Only bins that actually entered refine() carry it: a bin that
        // returns because it is already small enough never computes its
        // min and max, so it has nothing to certify.
        //
        // Placed next to inBufferA so it lands in existing padding.
        bool sorted = false;

        std::size_t start = 0;
        std::size_t count = 0;
        std::vector<RefinedRange> children;

        bool isLeaf() const { return children.empty(); }
    };

    RefinedRange refine(bool inBufferA, std::size_t start, std::size_t count, std::size_t depth);

    ValueRange scanRange(const std::vector<T>& buf, std::size_t start, std::size_t count) const;
    Partitioning planRefinement(const ValueRange& observed, std::size_t count) const;

    // ---- Phase 5: local sorting -------------------------------------
    enum class RunShape { Ascending, Descending, Unsorted };

    RunShape detectRun(const std::vector<T>& buf, std::size_t start, std::size_t count);
    void sortLeaf(std::vector<T>& buf, std::size_t start, std::size_t count);
    void sortRefined(RefinedRange& node);

    void insertionSort(std::vector<T>& arr, Index left, Index right);
    void quickSort(std::vector<T>& arr, Index left, Index right);
    void introSort(std::vector<T>& arr, Index left, Index right);
    void introSortImpl(std::vector<T>& arr, Index left, Index right, std::size_t depthLimit);
    void heapSort(std::vector<T>& arr, Index left, Index right);
    void siftDown(std::vector<T>& arr, Index start, Index end);
    Index partition(std::vector<T>& arr, Index left, Index right);

    // ---- Phase 6: join ----------------------------------------------
    void appendLeaves(const RefinedRange& node, std::vector<T>& out) const;

#ifdef STRATUM_ENABLE_METRICS
    void flattenLeaves(const RefinedRange& node, std::vector<LeafView>& out) const;
#endif

    // ---- Instrumentation --------------------------------------------
    // Thin wrappers so the algorithm itself contains no preprocessor
    // conditionals. In a release build every one of these is an empty
    // inline function that the compiler removes entirely, exactly as the
    // #ifdef blocks they replace did - but the algorithm reads as
    // algorithm rather than as instrumentation.
    void beginPhase(const char* name);
    void endPhase(const char* name);
    void noteComparisons(std::size_t howMany);
    void noteLeaf(std::size_t count, bool isEmpty);
    void noteSplit(std::size_t originalSize, std::size_t childCount, std::size_t maxChildSize,
                   std::size_t depth);
    void noteLocalAlgorithm(const char* name);
    void noteMemory(std::size_t bytes);

    std::size_t targetElementsPerBin_;   // lambda: target occupancy
    std::size_t leafThreshold_;          // t: base-case size

    // The two cascading buffers. A bin at even refinement depth lives in
    // bufferA_, at odd depth in bufferB_; a split reads one and writes the
    // other at the SAME absolute offsets.
    std::vector<T> bufferA_;
    std::vector<T> bufferB_;

    // Reused by every countAndPlace() call. Safe because the recursion is
    // strictly depth-first and single-threaded: a call is finished with
    // both before any nested call reuses them.
    std::vector<std::size_t> bucketOfScratch_;
    std::vector<std::size_t> writeCursorScratch_;

#ifdef STRATUM_ENABLE_METRICS
    SortMetrics metrics_;
#endif
};

} // namespace stratum

#include "StratumSort.tpp"
