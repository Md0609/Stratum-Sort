#pragma once

#include "Config.hpp"

#ifdef DRS_ENABLE_METRICS
#include "DRSMetrics.hpp"
#endif

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace drs {

// ============================================================
// DynamicRangeSort
// ============================================================
// A distribution sort for integer keys. Instead of comparing elements
// against each other globally, it looks at how the values are spread,
// cuts the observed value range into equal-width intervals, refines the
// intervals that are still too crowded using ONLY that interval's own
// observed range, sorts each final interval locally, and concatenates.
//
//     analyse -> build intervals -> refine -> sort locally -> join
//
// The join is a plain concatenation, never a merge: the intervals are
// value-ordered by construction, so element i of interval j is <= every
// element of interval j+1.
//
// ---- Complexity ---------------------------------------------------
// Theta(n) time and Theta(n) auxiliary space, treating the key width w
// as a constant - the same sense in which radix sort is linear. This is
// NOT a bound in the comparison model and does not contradict the
// Omega(n log n) comparison lower bound: the algorithm does arithmetic on
// the keys, not only comparisons.
//
// The constant is bounded because the refinement depth is bounded by
// min(w, MAX_SUBDIVISION_DEPTH) and the largest range that can reach a
// comparison sort is bounded by lambda * 2^(w/D), a constant independent
// of n. See Config.hpp and the proofs next to refine().
//
// ---- Guarantees ---------------------------------------------------
// STABILITY: none. This sorter is NOT stable. For the integral key types
//   it accepts, equal elements are indistinguishable, so this is
//   unobservable - but it does mean the implementation must not be
//   generalised to key/value pairs without revisiting detectRun(), which
//   reverses runs in place.
//
// THREAD SAFETY: a single instance is NOT safe to use from more than one
//   thread; it owns mutable scratch buffers that are reused across the
//   whole recursion. Distinct instances are independent and may be used
//   concurrently. There is no shared global state.
//
// EXCEPTIONS: strong guarantee IN THE PRODUCTION CONFIGURATION. The only
//   operations that can throw are the internal allocations, and all of
//   them happen before the first write to the caller's array - the output
//   is only produced in the final join. If sort() throws, the input is
//   left exactly as it was.
//   This does NOT hold when DRS_ENABLE_METRICS is defined: the
//   instrumentation records the timing of the join phase *after* the join
//   has run, and that record allocates. A research build can therefore
//   throw with the output already written. The research build is a
//   measurement tool, not a product, and is not fixed for this - but the
//   difference is real and tests/api_contract.cpp pins it.
//
// MEMORY: sort() allocates about 3n * sizeof(T) bytes of scratch on first
//   use and REUSES it across subsequent calls on the same instance. The
//   memory is released when the instance is destroyed, not between calls;
//   an instance used once on a huge array keeps that memory alive.
//
// ---- Element type -------------------------------------------------
// Restricted to integral types: every formula (span, interval width, bin
// index) is defined over exact integer arithmetic, and the range
// arithmetic relies on two's-complement wrap-around being well defined.
// ============================================================
template <typename T>
class DynamicRangeSort {
    static_assert(std::is_integral<T>::value,
                  "DynamicRangeSort requires an integral element type");
    // std::is_integral<bool> is true, but std::vector<bool> is the packed
    // specialisation: it has no data() and its elements are not
    // addressable, so the block copies below cannot work on it. Rejecting
    // it here gives a readable message instead of a template error deep
    // inside memcpy.
    static_assert(!std::is_same<typename std::remove_cv<T>::type, bool>::value,
                  "DynamicRangeSort does not support bool: std::vector<bool> is a packed "
                  "specialisation with no contiguous storage");

public:
    // targetElementsPerBin (lambda) is the target occupancy per bin, and
    // leafThreshold (t) is the size at which refinement stops. See
    // Config.hpp for what each one controls and how to choose it.
    //
    // Both arguments are clamped rather than rejected, so that no
    // combination can produce undefined behaviour:
    //   - lambda == 0 is replaced by the default (a bin must hold at
    //     least one element);
    //   - t < lambda is raised to lambda (a bin of the target occupancy
    //     must be allowed to become a leaf).
    // The clamping is silent by design: these are tuning hints, not a
    // contract the caller can violate.
    //
    // NOTE: the two parameters are both std::size_t and adjacent, so
    // swapping them at a call site compiles. `DynamicRangeSort(64, 32)`
    // is silently read as (64, 64). A named-parameter struct would remove
    // the hazard; it is deliberately not introduced here to keep the
    // public interface unchanged.
    explicit DynamicRangeSort(std::size_t targetElementsPerBin = DEFAULT_TARGET_ELEMENTS_PER_BIN,
                              std::size_t leafThreshold = DEFAULT_LEAF_THRESHOLD);

    // Sorts 'data' in place into ascending order.
    void sort(std::vector<T>& data);

    // The effective parameters after clamping. Exposed because the
    // clamping is silent: this is the only way a caller can confirm what
    // the instance is actually doing.
    std::size_t targetElementsPerBin() const { return targetElementsPerBin_; }
    std::size_t leafThreshold() const { return leafThreshold_; }

#ifdef DRS_ENABLE_METRICS
    // Statistics from the last sort() call. Research build only.
    const DRSMetrics& metrics() const { return metrics_; }

    // ---- Research-only introspection --------------------------------
    // Lets analysis code inspect the partition DRS would produce without
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
    // long: long is 32 bits on Windows, which would silently break for
    // ranges above 2^31.
    using Index = std::ptrdiff_t;

    // ---- Phase 1: analysis ------------------------------------------
    struct ValueRange {
        T minimum{};
        T maximum{};
    };

    ValueRange analyze(const std::vector<T>& data) const;

    // ---- Interval formulas ------------------------------------------
    // The interval width is a magnitude, not a value of T: it is carried
    // as uint64_t because a width can exceed the representable range of
    // T. See the proof next to computeRangeParameters().
    struct IntervalGrid {
        std::size_t binCount = 1;
        uint64_t width = 1;
    };

    IntervalGrid computeRangeParameters(const ValueRange& range, std::size_t length) const;

    std::size_t computeBinIndex(T value, T rangeStart, uint64_t width) const;

    // ---- Phase 2 and 3: distribution --------------------------------
    void distribute(const std::vector<T>& data, T minimumValue, uint64_t width,
                    std::size_t binCount, std::vector<std::size_t>& outBucketStart,
                    std::vector<std::size_t>& outBucketSize);

    void countAndPlace(const std::vector<T>& src, std::size_t srcStart, std::size_t count,
                       std::vector<T>& dst, std::size_t dstStart, T rangeStart, uint64_t width,
                       std::size_t numBuckets, std::vector<std::size_t>& outBucketStart,
                       std::vector<std::size_t>& outBucketSize);

    // ---- Phase 4: recursive refinement ------------------------------
    struct RefinedRange {
        bool inBufferA = true;

        // Set by refine() when it discovers the bin's observed span is
        // zero, i.e. every element is identical. Such a bin is sorted by
        // definition; the flag lets the local sort skip it instead of
        // re-scanning the whole bin to rediscover the same fact.
        //
        // Only bins that actually entered refine() carry it: a bin that
        // returns because it is already at or below the leaf threshold
        // never computes its min and max, so it has nothing to certify.
        //
        // Placed next to inBufferA so it lands in existing padding.
        bool sorted = false;

        std::size_t start = 0;
        std::size_t count = 0;
        std::vector<RefinedRange> children;

        bool isLeaf() const { return children.empty(); }
    };

    RefinedRange refine(bool inBufferA, std::size_t start, std::size_t count, std::size_t depth);

#ifdef DRS_ENABLE_METRICS
    void flattenLeaves(const RefinedRange& node, std::vector<LeafView>& out) const;
#endif

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

    std::size_t targetElementsPerBin_;   // lambda: target occupancy
    std::size_t leafThreshold_;          // t: base-case size

    // The two cascading buffers. A bin at even refinement depth lives in
    // bufferA_, at odd depth in bufferB_; a split reads one and writes the
    // other at the SAME absolute offsets.
    std::vector<T> bufferA_;
    std::vector<T> bufferB_;

    // Reused by every countAndPlace() call. This is safe because the
    // recursion is strictly depth-first and single-threaded: a call has
    // finished reading both of these before any nested call reuses them.
    std::vector<std::size_t> bucketOfScratch_;
    std::vector<std::size_t> writeCursorScratch_;

#ifdef DRS_ENABLE_METRICS
    DRSMetrics metrics_;
#endif
};

} // namespace drs

#include "DynamicRangeSort.tpp"
