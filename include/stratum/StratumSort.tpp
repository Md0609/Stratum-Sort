#pragma once

// Included at the bottom of StratumSort.hpp; not meant to be
// included directly.

#include <algorithm>
#include <cassert>
#include <cstring>
#include <utility>

namespace stratum {
inline namespace STRATUM_ABI_NAMESPACE {

// ============================================================
// Instrumentation wrappers
// ============================================================
// The only place in the algorithm where STRATUM_ENABLE_METRICS appears, so
// the phases below read as algorithm rather than as a mixture of
// algorithm and measurement.
//
// Three separate statements, worth not conflating:
//   - SEMANTICS: in a release build each wrapper has an empty body, so it
//     has no observable effect. That much is guaranteed.
//   - ARGUMENTS: the expressions passed to a wrapper are still part of the
//     program. Most are already-computed scalars, but noteSplit() is given
//     *std::max_element(bucketSize...), which is O(bucket count).
//   - OPTIMISATION: a compiler MAY discard an empty call and its pure
//     arguments under the as-if rule, and at -O3 it does. It is not
//     obliged to, and this is an expectation rather than a guarantee.
// Complexity does not depend on the third point: even if nothing is
// elided, that max_element costs O(n / lambda) per level, so the algorithm
// stays Theta(n) either way.

#ifdef STRATUM_ENABLE_METRICS
template <typename T>
void StratumSort<T>::beginPhase(const char* name) {
    metrics_.startPhase(name);
}
template <typename T>
void StratumSort<T>::endPhase(const char* name) {
    metrics_.endPhase(name);
}
template <typename T>
void StratumSort<T>::noteComparisons(std::size_t howMany) {
    metrics_.recordComparisons(howMany);
}
template <typename T>
void StratumSort<T>::noteLeaf(std::size_t count, bool isEmpty) {
    metrics_.recordBin(count, isEmpty);
}
template <typename T>
void StratumSort<T>::noteSplit(std::size_t originalSize, std::size_t childCount,
                                    std::size_t maxChildSize, std::size_t depth) {
    metrics_.recordSubdivision(depth);
    metrics_.recordSubdivisionQuality(originalSize, childCount, maxChildSize, depth);
}
template <typename T>
void StratumSort<T>::noteLocalAlgorithm(const char* name) {
    metrics_.recordAlgorithmUsage(name);
}
template <typename T>
void StratumSort<T>::noteMemory(std::size_t bytes) {
    metrics_.addApproxMemory(bytes);
}
#else
template <typename T>
void StratumSort<T>::beginPhase(const char*) {}
template <typename T>
void StratumSort<T>::endPhase(const char*) {}
template <typename T>
void StratumSort<T>::noteComparisons(std::size_t) {}
template <typename T>
void StratumSort<T>::noteLeaf(std::size_t, bool) {}
template <typename T>
void StratumSort<T>::noteSplit(std::size_t, std::size_t, std::size_t, std::size_t) {}
template <typename T>
void StratumSort<T>::noteLocalAlgorithm(const char*) {}
template <typename T>
void StratumSort<T>::noteMemory(std::size_t) {}
#endif

// ============================================================
// Construction
// ============================================================
template <typename T>
StratumSort<T>::StratumSort(std::size_t targetElementsPerBin, std::size_t leafThreshold)
    : targetElementsPerBin_(targetElementsPerBin == 0 ? DEFAULT_TARGET_ELEMENTS_PER_BIN
                            : targetElementsPerBin > MAX_TARGET_ELEMENTS_PER_BIN
                                ? MAX_TARGET_ELEMENTS_PER_BIN
                                : targetElementsPerBin),
      leafThreshold_(leafThreshold < targetElementsPerBin_ ? targetElementsPerBin_
                     : leafThreshold > MAX_LEAF_THRESHOLD  ? MAX_LEAF_THRESHOLD
                                                           : leafThreshold) {
    assert(targetElementsPerBin_ >= 1 && targetElementsPerBin_ <= MAX_TARGET_ELEMENTS_PER_BIN);
    assert(leafThreshold_ >= targetElementsPerBin_ && leafThreshold_ <= MAX_LEAF_THRESHOLD);
}

// ============================================================
// Phase 1: analysis
// ============================================================
// One pass computing the minimum and maximum.
//
// WHY IT CANNOT BE FUSED WITH ANYTHING. Every later formula - span,
// interval width, bucket index - is defined relative to the observed
// range, and the range is not known until the last element has been seen.
// So this pass has to complete before the next one can start.
//
// Precondition: data is non-empty. sort() returns earlier for n < 2.
template <typename T>
typename StratumSort<T>::ValueRange StratumSort<T>::analyze(
    const std::vector<T>& data) const {
    assert(!data.empty());

    ValueRange range;
    range.minimum = data[0];
    range.maximum = data[0];
    for (const T value : data) {
        if (value < range.minimum) range.minimum = value;
        if (value > range.maximum) range.maximum = value;
    }
    return range;
}

// Same scan, restricted to one slice of a buffer. Used by refine() to get
// a bin's OWN range - never the whole array's, which is the rule that
// makes the partition adapt to local density.
template <typename T>
typename StratumSort<T>::ValueRange StratumSort<T>::scanRange(const std::vector<T>& buf,
                                                                       std::size_t start,
                                                                       std::size_t count) const {
    assert(count > 0);

    ValueRange range;
    range.minimum = buf[start];
    range.maximum = buf[start];
    for (std::size_t i = start + 1; i < start + count; ++i) {
        if (buf[i] < range.minimum) range.minimum = buf[i];
        if (buf[i] > range.maximum) range.maximum = buf[i];
    }
    return range;
}

// ============================================================
// Interval formulas
// ============================================================
// A range is always described by its SPAN, max - min, never by the number
// of distinct values max - min + 1.
//
// WHY. The +1 form needs w+1 bits. For T = int64_t and an input holding
// both INT64_MIN and INT64_MAX it is exactly 2^64, which wraps to zero in
// 64-bit arithmetic and collapses the entire partition into one bucket.
// The span is at most 2^64 - 1 and is always representable.
//
// Two properties everything below depends on:
//
//   PROPERTY 1 (equivalence). width = span/s + 1 equals ceil((span+1)/s).
//     Write span = q*s + r with 0 <= r < s. Then
//       ceil((span+1)/s) = q + ceil((r+1)/s) = q + 1
//     because 1 <= r+1 <= s. So this is the natural "range cut into s
//     equal parts, rounded up", computed in a form that cannot overflow.
//
//   PROPERTY 2 (the index is always in range). No clamping is needed:
//       s*width = s*floor(span/s) + s = (span - r) + s > span
//     hence span/width < s, so floor(offset/width) <= s-1 for every
//     offset in [0, span].
//
// Property 2 holds for any s >= 1 provided width is computed exactly.
// Width can only overflow for s == 1, which planPartition() handles
// explicitly below.

// Chooses the top-level grid: how many buckets, and how wide.
template <typename T>
typename StratumSort<T>::Partitioning StratumSort<T>::planPartition(
    const ValueRange& range, std::size_t length) const {
    assert(length > 0);

    const uint64_t span =
        static_cast<uint64_t>(range.maximum) - static_cast<uint64_t>(range.minimum);

    Partitioning grid;
    grid.origin = range.minimum;
    grid.binCount = (length + targetElementsPerBin_ - 1) / targetElementsPerBin_;
    if (grid.binCount == 0) grid.binCount = 1;

    // Cap by the number of distinct values the range can hold. A bucket
    // narrower than one value is unreachable - no element can map into it
    // - so allocating, zeroing, prefix-summing and iterating it is waste.
    //
    // The cap cannot change the partition. It only binds when
    // span < binCount, and then the width is span/binCount + 1 = 1 both
    // before and after capping. Identical widths mean an identical
    // value-to-bucket assignment; only the unreachable tail disappears.
    //
    // span + 1 cannot overflow here: the branch requires span < binCount.
    if (span < static_cast<uint64_t>(grid.binCount)) {
        grid.binCount = static_cast<std::size_t>(span + 1ULL);
    }

    // With a single bucket the width carries no information - every value
    // maps to bucket 0 - and its exact value, span + 1, may not be
    // representable: it is exactly 2^64 when the input spans the whole
    // universe, which wraps to 0. Normalising to 1 makes the
    // postcondition "width >= 1" hold unconditionally, so no wrapped value
    // can escape this function. Relying instead on countAndPlace's
    // short-circuit of the single-bucket case would make correctness an
    // accident of call order rather than a property of this function.
    grid.width =
        (grid.binCount == 1) ? 1ULL : (span / static_cast<uint64_t>(grid.binCount) + 1ULL);

    assert(grid.binCount >= 1);
    assert(grid.width >= 1);
    return grid;
}

// Chooses the grid for one refinement step, from the bin's OWN observed
// range. Identical rule to planPartition, expressed over the bin instead
// of the whole array.
//
// Precondition: the bin is not degenerate (observed.minimum <
// observed.maximum) and count > leafThreshold_. Both are checked by the
// caller before this is reached.
template <typename T>
typename StratumSort<T>::Partitioning StratumSort<T>::planRefinement(
    const ValueRange& observed, std::size_t count) const {
    assert(observed.minimum < observed.maximum);

    const uint64_t span =
        static_cast<uint64_t>(observed.maximum) - static_cast<uint64_t>(observed.minimum);
    assert(span >= 1);

    Partitioning grid;
    grid.origin = observed.minimum;
    grid.binCount = (count + targetElementsPerBin_ - 1) / targetElementsPerBin_;
    if (span < static_cast<uint64_t>(grid.binCount)) {
        grid.binCount = static_cast<std::size_t>(span + 1ULL);
    }

    // Always at least two buckets here: count > leafThreshold_ >= lambda
    // gives at least 2, and when the cap binds it becomes span + 1 >= 2.
    // That is what guarantees a refinement always makes progress.
    assert(grid.binCount >= 2);

    grid.width = span / static_cast<uint64_t>(grid.binCount) + 1ULL;
    assert(grid.width >= 1);
    return grid;
}

// Bucket index of a value. By Property 2 the result is always valid, so
// there is no clamp: a clamp here would silently absorb an arithmetic bug
// instead of exposing it.
//
// The subtraction is done in unsigned arithmetic on purpose. For a signed
// T with a range straddling zero, max - min overflows in signed
// arithmetic (undefined behaviour) but is exact modulo 2^w in unsigned -
// and since min <= value, the true offset is what the wrap produces.
//
// Monotone in 'value' for a fixed origin and width, which is the property
// the tiling invariant in the join phase rests on.
template <typename T>
std::size_t StratumSort<T>::bucketOf(T value, T origin, uint64_t width) {
    const uint64_t offset = static_cast<uint64_t>(value) - static_cast<uint64_t>(origin);
    return static_cast<std::size_t>(offset / width);
}

// ============================================================
// countAndPlace - the shared distribution step
// ============================================================
// Counts how many elements of 'src' fall into each bucket of 'grid', then
// writes them into 'dst' grouped by bucket, in ascending bucket order.
// This is the only place elements ever move, and it is used identically
// by the top-level distribution and by every refinement split.
//
// Guarantees on exit:
//   - outBucketStart[b] is where bucket b begins in dst, and the buckets
//     tile [dst.start, dst.start + src.count) with no gaps, in order;
//   - within a bucket, elements keep their relative order in src.
//
// WHY TWO PASSES. The destination offset of an element depends on how
// many elements precede it in earlier buckets, which is not known until
// every element has been classified. Counting first is what allows the
// second pass to write each element straight to its final place, with no
// insertion and no reallocation.
//
// The two scratch vectors are members reused by every call. That is safe
// because the recursion is strictly depth-first and single-threaded: this
// function is finished with both before it returns, and nothing nested
// runs while it is using them. outBucketStart and outBucketSize CANNOT be
// shared that way - the caller keeps reading them while it recurses into
// the children it just created.
template <typename T>
void StratumSort<T>::countAndPlace(SourceSlice src, TargetSlice dst, const Partitioning& grid,
                                        std::vector<std::size_t>& outBucketStart,
                                        std::vector<std::size_t>& outBucketSize) {
    assert(grid.binCount >= 1);
    assert(grid.width >= 1);
    // src and dst must be distinct objects: the single-bucket path uses
    // memcpy, which is undefined for overlapping regions. Every call site
    // satisfies this - the top level reads the caller's array and writes
    // bufferA_, a refinement reads one buffer and writes the other - but
    // nothing in the signature says so.
    assert(static_cast<const void*>(&src.buffer) != static_cast<const void*>(&dst.buffer));

    // Degenerate grid: one bucket takes everything, so there is no index
    // to compute. Isolating it is what allows bucketOf() to have no
    // clamp, since this is the only case where the exact width may not be
    // representable. Reachable two ways: an input small enough to fit one
    // bin, and an input whose span is zero, which planPartition()
    // collapses to a single bin.
    if (grid.binCount == 1) {
        outBucketStart.assign(1, dst.start);
        outBucketSize.assign(1, src.count);
        if (src.count > 0) {
            std::memcpy(dst.buffer.data() + dst.start, src.buffer.data() + src.start,
                        src.count * sizeof(T));
        }
        return;
    }

    // Everything the two loops below touch per element is hoisted into a
    // local first. The grid arrives by const reference and the slices hold
    // references, so without this the compiler must reload origin, width
    // and the buffer address on every iteration - it cannot prove they are
    // not aliased by the writes. Measured: 6-15% slower without it,
    // depending on the dataset. The parameter structs are for the reader;
    // these locals are for the optimiser.
    const T origin = grid.origin;
    const uint64_t width = grid.width;
    const std::size_t numBuckets = grid.binCount;
    const std::size_t count = src.count;
    const T* const in = src.buffer.data() + src.start;
    T* const out = dst.buffer.data();

    // Pass 1: classify every element and build the histogram.
    outBucketSize.assign(numBuckets, 0);
    if (bucketOfScratch_.size() < count) bucketOfScratch_.resize(count);
    std::size_t* const bucketOfIt = bucketOfScratch_.data();
    std::size_t* const sizes = outBucketSize.data();
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t idx = bucketOf(in[i], origin, width);
        assert(idx < numBuckets); // Property 2
        bucketOfIt[i] = idx;
        ++sizes[idx];
    }

    // Prefix sum: turn the histogram into one write cursor per bucket.
    outBucketStart.assign(numBuckets, 0);
    if (writeCursorScratch_.size() < numBuckets) writeCursorScratch_.resize(numBuckets);
    std::size_t* const starts = outBucketStart.data();
    std::size_t* const cursors = writeCursorScratch_.data();
    std::size_t cursor = dst.start;
    for (std::size_t b = 0; b < numBuckets; ++b) {
        starts[b] = cursor;
        cursors[b] = cursor;
        cursor += sizes[b];
    }
    assert(cursor == dst.start + count);

    // Pass 2: place. Advancing each cursor as we go is what preserves the
    // relative order of elements landing in the same bucket.
    for (std::size_t i = 0; i < count; ++i) {
        out[cursors[bucketOfIt[i]]++] = in[i];
    }
}

// ============================================================
// Phases 2 and 3: top-level distribution
// ============================================================
template <typename T>
void StratumSort<T>::distribute(const std::vector<T>& data, const Partitioning& grid,
                                     std::vector<std::size_t>& outBucketStart,
                                     std::vector<std::size_t>& outBucketSize) {
    // Both buffers must exist at full size before any refinement runs: a
    // split writes into the other buffer at the same absolute offsets, so
    // both are indexed over [0, n) from the first level onwards.
    bufferA_.assign(data.size(), T{});
    bufferB_.assign(data.size(), T{});

    countAndPlace(SourceSlice{data, 0, data.size()}, TargetSlice{bufferA_, 0}, grid, outBucketStart,
                  outBucketSize);

    noteMemory(bufferA_.size() * sizeof(T) + bufferB_.size() * sizeof(T));
}

// ============================================================
// Phase 4: recursive refinement
// ============================================================
// Splits [start, start+count) of whichever buffer holds it, using ONLY
// that bin's own observed range, writing the children into the other
// buffer at the same absolute offsets.
//
// WHY THE BIN'S OWN RANGE. The top level cuts the global range into
// equal widths, which is the wrong resolution wherever the data is dense.
// Re-deriving the width from the bin's own extremes makes the partition
// adapt to local density without ever looking at the rest of the array -
// and it is what lets a tight cluster hidden inside a wide interval be
// resolved immediately instead of after log(width) levels.
//
// WHY IT TERMINATES. Let span be the bin's observed span and s >= 2 the
// number of buckets. Every child occupies an interval of exactly 'width'
// values, so its own span is at most width - 1 = floor(span/s) < span for
// span >= 1. The span therefore decreases strictly at every level, and it
// is a non-negative integer, so the recursion terminates on its own.
// MAX_SUBDIVISION_DEPTH bounds the effort spent, not the termination.
//
// EXIT CONDITIONS. A bin becomes a leaf when it is empty, when it holds
// at most leafThreshold_ elements, when the depth cap is reached, or when
// its observed span is zero. There is no other way out.
template <typename T>
typename StratumSort<T>::RefinedRange StratumSort<T>::refine(bool inBufferA,
                                                                      std::size_t start,
                                                                      std::size_t count,
                                                                      std::size_t depth) {
    RefinedRange node;
    node.inBufferA = inBufferA;
    node.start = start;
    node.count = count;

    if (count == 0) {
        noteLeaf(0, true);
        return node;
    }

    // Base case by size, or by exhausted depth. Note this returns BEFORE
    // scanning for the range: a bin that stops here never pays for a
    // range it does not need.
    if (count <= leafThreshold_ || depth >= MAX_SUBDIVISION_DEPTH) {
        noteLeaf(count, false);
        return node;
    }

    const std::vector<T>& current = inBufferA ? bufferA_ : bufferB_;
    const ValueRange observed = scanRange(current, start, count);

    if (observed.minimum == observed.maximum) {
        // Degenerate range: every element is identical, so the bin is
        // sorted by definition and the data does not need to move.
        // Recording the certificate saves the local sort a full O(count)
        // rescan to rediscover exactly this.
        node.sorted = true;
        noteLeaf(count, false);
        return node;
    }

    const Partitioning grid = planRefinement(observed, count);

    std::vector<T>& other = inBufferA ? bufferB_ : bufferA_;
    std::vector<std::size_t> bucketStart;
    std::vector<std::size_t> bucketSize;
    countAndPlace(SourceSlice{current, start, count}, TargetSlice{other, start}, grid, bucketStart,
                  bucketSize);

    noteSplit(count, grid.binCount,
              bucketSize.empty() ? 0 : *std::max_element(bucketSize.begin(), bucketSize.end()),
              depth + 1);
    noteMemory(grid.binCount * sizeof(RefinedRange));

    node.children.reserve(grid.binCount);
    for (std::size_t b = 0; b < grid.binCount; ++b) {
        node.children.push_back(refine(!inBufferA, bucketStart[b], bucketSize[b], depth + 1));
    }
    return node;
}

// ============================================================
// Phase 5: local sorting
// ============================================================
// One O(k) scan recognising a range that is already fully ascending or
// fully descending, so it can be finished without a comparison sort:
// ascending is a no-op, descending is a single reversal.
//
// NOTE this is why the sorter is not stable - reversing a descending run
// swaps elements that would compare equal under a weaker ordering. That
// is harmless for the integral keys this class accepts, and it is the
// reason it must not be generalised to key/value pairs as written.
template <typename T>
typename StratumSort<T>::RunShape StratumSort<T>::detectRun(const std::vector<T>& buf,
                                                                     std::size_t start,
                                                                     std::size_t count) {
    if (count < 2) return RunShape::Ascending;

    bool ascending = true;
    bool descending = true;
    for (std::size_t i = start + 1; i < start + count; ++i) {
        // Two comparisons per step, both counted: the counter reflects
        // work done, not source lines.
        noteComparisons(2);
        if (buf[i] < buf[i - 1]) ascending = false;
        if (buf[i] > buf[i - 1]) descending = false;
        if (!ascending && !descending) return RunShape::Unsorted;
    }
    return ascending ? RunShape::Ascending : RunShape::Descending;
}

// Sorts one leaf in place.
//
// The dispatch thresholds describe the LOCAL SORTS and nothing else. They
// are deliberately independent of leafThreshold_: which algorithm suits a
// range of k elements is a property of the algorithms, not of the rule
// that decided to stop refining. Tying the two together, as earlier
// versions did, meant that changing where refinement stopped silently
// changed which sort ran.
//
// A leaf normally holds at most leafThreshold_ elements. Larger ones only
// arise from exhausting MAX_SUBDIVISION_DEPTH, which is what the second
// and third branches are for.
template <typename T>
void StratumSort<T>::sortLeaf(std::vector<T>& buf, std::size_t start, std::size_t count) {
    if (count < 2) return;

    switch (detectRun(buf, start, count)) {
        case RunShape::Ascending:
            noteLocalAlgorithm("AlreadySorted");
            return;
        case RunShape::Descending:
            std::reverse(buf.begin() + static_cast<Index>(start),
                         buf.begin() + static_cast<Index>(start + count));
            noteLocalAlgorithm("ReversedRun");
            return;
        case RunShape::Unsorted:
            break;
    }

    const Index left = static_cast<Index>(start);
    const Index right = static_cast<Index>(start + count - 1);
    if (count <= LOCAL_INSERTION_MAX_ELEMENTS) {
        insertionSort(buf, left, right);
        noteLocalAlgorithm("InsertionSort");
    } else if (count <= LOCAL_QUICKSORT_MAX_ELEMENTS) {
        quickSort(buf, left, right);
        noteLocalAlgorithm("QuickSort");
    } else {
        introSort(buf, left, right);
        noteLocalAlgorithm("Introsort");
    }
}

template <typename T>
void StratumSort<T>::sortRefined(RefinedRange& node) {
    if (node.isLeaf()) {
        // A certified bin needs nothing: refine() already established
        // that every element is identical. Without the certificate this
        // would cost a full scan to reach the same conclusion.
        if (node.sorted) {
            noteLocalAlgorithm("AlreadySorted");
            return;
        }
        std::vector<T>& buf = node.inBufferA ? bufferA_ : bufferB_;
        sortLeaf(buf, node.start, node.count);
        return;
    }
    for (RefinedRange& child : node.children) {
        sortRefined(child);
    }
}

template <typename T>
void StratumSort<T>::insertionSort(std::vector<T>& arr, Index left, Index right) {
    for (Index i = left + 1; i <= right; ++i) {
        const T key = arr[i];
        Index j = i - 1;
        while (j >= left) {
            noteComparisons(1);
            if (arr[j] <= key) break;
            arr[j + 1] = arr[j];
            --j;
        }
        arr[j + 1] = key;
    }
}

// Median-of-three Hoare partition, shared by quickSort and introSortImpl.
//
// WHY THE UNGUARDED SCANS ARE SAFE. The median-of-three step leaves
// arr[left] <= pivot <= arr[right], and parks the pivot at right-1. Those
// two elements act as sentinels: the ascending scan cannot run past
// 'right' because arr[right] >= pivot stops it, and the descending scan
// cannot run past 'left' because arr[left] <= pivot stops it. That is why
// neither loop tests its bound - and why the precondition below is not
// optional.
template <typename T>
typename StratumSort<T>::Index StratumSort<T>::partition(std::vector<T>& arr, Index left,
                                                                   Index right) {
    assert(right - left >= 2 && "partition needs at least three elements for the sentinels");

    const Index mid = left + (right - left) / 2;

    noteComparisons(3);
    if (arr[mid] < arr[left]) std::swap(arr[mid], arr[left]);
    if (arr[right] < arr[left]) std::swap(arr[right], arr[left]);
    if (arr[right] < arr[mid]) std::swap(arr[right], arr[mid]);

    const T pivot = arr[mid];
    std::swap(arr[mid], arr[right - 1]);

    Index i = left;
    Index j = right - 1;
    while (true) {
        do {
            ++i;
            noteComparisons(1);
        } while (arr[i] < pivot);
        do {
            --j;
            noteComparisons(1);
        } while (arr[j] > pivot);
        if (i >= j) break;
        std::swap(arr[i], arr[j]);
    }
    std::swap(arr[i], arr[right - 1]);
    return i;
}

// QuickSort that recurses into the smaller side and loops on the larger,
// which bounds the stack depth at O(log k) instead of O(k).
template <typename T>
void StratumSort<T>::quickSort(std::vector<T>& arr, Index left, Index right) {
    while (right - left > static_cast<Index>(LOCAL_PARTITION_CUTOFF)) {
        const Index p = partition(arr, left, right);
        if (p - left < right - p) {
            quickSort(arr, left, p - 1);
            left = p + 1;
        } else {
            quickSort(arr, p + 1, right);
            right = p - 1;
        }
    }
    insertionSort(arr, left, right);
}

namespace detail {

// floor(log2(v)) for v >= 1, by integer arithmetic. Deliberately not
// std::log2: this class is otherwise entirely integer, and using floating
// point for a value that controls control flow invites platform-dependent
// rounding.
inline std::size_t floorLog2(std::size_t v) {
    std::size_t r = 0;
    while (v > 1) {
        v >>= 1;
        ++r;
    }
    return r;
}

} // namespace detail

template <typename T>
void StratumSort<T>::introSort(std::vector<T>& arr, Index left, Index right) {
    if (right <= left) return;
    const std::size_t n = static_cast<std::size_t>(right - left + 1);
    introSortImpl(arr, left, right, INTROSORT_DEPTH_FACTOR * detail::floorLog2(n));
}

// QuickSort with a partitioning budget; when the budget runs out the rest
// is finished with HeapSort. That is what turns QuickSort's O(k^2) worst
// case into a guaranteed O(k log k).
template <typename T>
void StratumSort<T>::introSortImpl(std::vector<T>& arr, Index left, Index right,
                                        std::size_t depthLimit) {
    while (right - left > static_cast<Index>(LOCAL_PARTITION_CUTOFF)) {
        if (depthLimit == 0) {
            heapSort(arr, left, right);
            return;
        }
        --depthLimit;
        const Index p = partition(arr, left, right);
        if (p - left < right - p) {
            introSortImpl(arr, left, p - 1, depthLimit);
            left = p + 1;
        } else {
            introSortImpl(arr, p + 1, right, depthLimit);
            right = p - 1;
        }
    }
    insertionSort(arr, left, right);
}

// Restores the max-heap property at 'root', within the heap that occupies
// [base, end]. All three are absolute indices into arr.
//
// 'base' and 'root' are separate on purpose. The heap embedding is defined
// relative to the heap's FIRST slot: arr[base + k] holds heap node k, whose
// children are nodes 2k+1 and 2k+2, at arr[base + 2k + 1] and arr[base + 2k + 2].
// A node's children are therefore a property of where the heap begins, not of
// where the node happens to sit. Folding the two together - taking the node
// being repaired as the origin of the indexing - only agrees with the real
// embedding when the node IS the heap's first slot, which is true for the
// extraction loop below and false for every step of the heapify loop but the
// last. That is a heap that is never actually built.
template <typename T>
void StratumSort<T>::siftDown(std::vector<T>& arr, Index base, Index root, Index end) {
    // Heap node index of the last slot. Node k has a left child iff
    // 2k + 1 <= last; the guard is written as k <= (last - 1) / 2 so that
    // 2k + 1 is only ever formed once it is known to be within the heap.
    const Index last = end - base;
    if (last < 1) return; // a one-element heap has no interior node

    while (root - base <= (last - 1) / 2) {
        const Index child = base + 2 * (root - base) + 1;
        Index swapIdx = root;

        noteComparisons(1);
        if (arr[swapIdx] < arr[child]) swapIdx = child;

        if (child + 1 <= end) {
            noteComparisons(1);
            if (arr[swapIdx] < arr[child + 1]) swapIdx = child + 1;
        }

        if (swapIdx == root) return;
        std::swap(arr[root], arr[swapIdx]);
        root = swapIdx;
    }
}

template <typename T>
void StratumSort<T>::heapSort(std::vector<T>& arr, Index left, Index right) {
    const Index n = right - left + 1;
    if (n < 2) return;

    // Reached only when introSortImpl exhausts its partitioning budget, which
    // takes adversarial data and is therefore easy to leave untested by
    // accident - and was: this fallback shipped in 0.9.0 without sorting
    // correctly, because nothing observable distinguished "introsort finished"
    // from "introsort fell back". Recording it makes the path assertable.
    // Compiles to nothing unless STRATUM_ENABLE_METRICS is defined.
    noteLocalAlgorithm("HeapSort");

    // Heapify: repair every interior node, deepest first. The heap is
    // [left, right] throughout, so 'left' is the base of every one of these
    // calls; only the node under repair moves.
    for (Index start = left + (n - 2) / 2; start >= left; --start) {
        siftDown(arr, left, start, right);
    }
    // Extract: swap the maximum to the end and shrink the heap by one.
    for (Index end = right; end > left; --end) {
        std::swap(arr[left], arr[end]);
        siftDown(arr, left, left, end - 1);
    }
}

// ============================================================
// Phase 6: join
// ============================================================
// THE TILING INVARIANT, and why the join is not a merge.
//
// countAndPlace writes the children of a bin at consecutive offsets
// starting at the parent's own start, in ascending bucket order, and
// bucketOf() is monotone in the value. So the children tile the parent's
// range in ascending value order, and by induction over the depth the
// leaves tile [0, n) in ascending value order.
//
// The consequence used here: EVERY LEAF ALREADY OCCUPIES ITS FINAL
// POSITION. There is nothing to merge and not even a write cursor to
// maintain - each leaf is copied to exactly its own offset. The only open
// question per leaf is which of the two buffers it currently lives in.
//
// memcpy is valid with no runtime check:
//   - 'out' is the caller's vector and 'buf' is bufferA_ or bufferB_ -
//     three distinct objects, so they cannot overlap;
//   - T is integral by the class-level static_assert, hence trivially
//     copyable;
//   - the destination range is exactly the leaf's own range, by the
//     invariant above.
template <typename T>
void StratumSort<T>::appendLeaves(const RefinedRange& node, std::vector<T>& out) const {
    if (node.isLeaf()) {
        if (node.count > 0) {
            assert(node.start + node.count <= out.size());
            const std::vector<T>& buf = node.inBufferA ? bufferA_ : bufferB_;
            std::memcpy(out.data() + node.start, buf.data() + node.start, node.count * sizeof(T));
        }
        return;
    }
    for (const RefinedRange& child : node.children) {
        appendLeaves(child, out);
    }
}

#ifndef NDEBUG
namespace detail {

// Debug-only check of the tiling invariant: walks the leaves in order and
// verifies they cover [0, n) exactly once, no gap and no overlap. The
// join depends on this property, so it is worth stating executably rather
// than only in a comment.
template <typename Node>
bool verifyTiling(const Node& node, std::size_t& expectedStart) {
    if (node.isLeaf()) {
        if (node.start != expectedStart) return false;
        expectedStart += node.count;
        return true;
    }
    for (const auto& child : node.children) {
        if (!verifyTiling(child, expectedStart)) return false;
    }
    return true;
}

} // namespace detail
#endif

// ============================================================
// Orchestration
// ============================================================
template <typename T>
void StratumSort<T>::sort(std::vector<T>& data) {
#ifdef STRATUM_ENABLE_METRICS
    metrics_.reset();
#endif
    if (data.size() < 2) return;

    beginPhase("analyze");
    const ValueRange range = analyze(data);
    endPhase("analyze");

    const Partitioning grid = planPartition(range, data.size());

    beginPhase("distribute");
    std::vector<std::size_t> bucketStart;
    std::vector<std::size_t> bucketSize;
    distribute(data, grid, bucketStart, bucketSize);
    endPhase("distribute");

    beginPhase("refine");
    std::vector<RefinedRange> roots;
    roots.reserve(grid.binCount);
    for (std::size_t b = 0; b < grid.binCount; ++b) {
        // distribute() always leaves the data in bufferA_.
        roots.push_back(refine(true, bucketStart[b], bucketSize[b], 0));
    }
    endPhase("refine");

#ifndef NDEBUG
    {
        std::size_t expectedStart = 0;
        for (const RefinedRange& root : roots) {
            assert(detail::verifyTiling(root, expectedStart) &&
                   "leaves do not tile [0, n) in order");
        }
        assert(expectedStart == data.size() && "leaves do not cover [0, n)");
    }
#endif

    beginPhase("localSort");
    for (RefinedRange& root : roots) {
        sortRefined(root);
    }
    endPhase("localSort");

    // Only from here on is the caller's array modified. Everything above
    // reads 'data' but never writes it, which is what gives sort() the
    // strong exception guarantee documented in the header: any allocation
    // failure happens before the first write.
    beginPhase("join");
    for (const RefinedRange& root : roots) {
        appendLeaves(root, data);
    }
    endPhase("join");

    noteMemory(bucketOfScratch_.capacity() * sizeof(std::size_t) +
               writeCursorScratch_.capacity() * sizeof(std::size_t));
}

#ifdef STRATUM_ENABLE_METRICS
// ============================================================
// Research-only introspection
// ============================================================
template <typename T>
void StratumSort<T>::flattenLeaves(const RefinedRange& node, std::vector<LeafView>& out) const {
    if (node.isLeaf()) {
        if (node.count > 0) out.push_back({node.inBufferA, node.start, node.count});
        return;
    }
    for (const RefinedRange& child : node.children) {
        flattenLeaves(child, out);
    }
}

// Runs the partitioning phases only, without sorting or joining, and
// returns the resulting leaves.
//
// NOTE this deliberately mirrors the first three phases of sort(). If
// sort() ever changes shape, this must change with it, or the
// introspection will describe a partition sort() no longer produces.
template <typename T>
std::vector<typename StratumSort<T>::LeafView> StratumSort<T>::debugPartitionOnly(
    const std::vector<T>& data) {
    metrics_.reset();
    std::vector<LeafView> leaves;
    if (data.size() < 2) {
        if (data.size() == 1) leaves.push_back({true, 0, 1});
        bufferA_ = data;
        bufferB_.assign(data.size(), T{});
        return leaves;
    }

    const ValueRange range = analyze(data);
    const Partitioning grid = planPartition(range, data.size());

    std::vector<std::size_t> bucketStart;
    std::vector<std::size_t> bucketSize;
    distribute(data, grid, bucketStart, bucketSize);

    for (std::size_t b = 0; b < grid.binCount; ++b) {
        const RefinedRange root = refine(true, bucketStart[b], bucketSize[b], 0);
        flattenLeaves(root, leaves);
    }
    return leaves;
}
#endif

} // inline namespace STRATUM_ABI_NAMESPACE
} // namespace stratum
