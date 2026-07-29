#pragma once

// Included at the bottom of DynamicRangeSort.hpp; not meant to be
// included directly.

#include <algorithm>
#include <cassert>
#include <cstring>
#include <utility>

namespace drs {

// ============================================================
// Construction
// ============================================================
template <typename T>
DynamicRangeSort<T>::DynamicRangeSort(std::size_t targetElementsPerBin, std::size_t leafThreshold)
    : targetElementsPerBin_(targetElementsPerBin == 0 ? DEFAULT_TARGET_ELEMENTS_PER_BIN
                                                      : targetElementsPerBin),
      leafThreshold_(std::max(leafThreshold, targetElementsPerBin_)) {
    // Both invariants the rest of the file relies on.
    assert(targetElementsPerBin_ >= 1);
    assert(leafThreshold_ >= targetElementsPerBin_);
}

// ============================================================
// Phase 1: analysis
// ============================================================
// One pass computing the minimum and maximum. This is the only pass that
// cannot be fused with anything else: every later formula (span, interval
// width, bin index) is defined relative to the observed range, which is
// not known until the last element has been seen.
//
// Precondition: data is non-empty (sort() returns earlier for n < 2).
template <typename T>
typename DynamicRangeSort<T>::ValueRange DynamicRangeSort<T>::analyze(
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

// ============================================================
// Interval formulas
// ============================================================
// The observed range is carried as its SPAN, max - min, never as the
// number of distinct values max - min + 1.
//
// WHY: the +1 form needs w+1 bits. For T = int64_t and an input holding
// both INT64_MIN and INT64_MAX it is exactly 2^64, which wraps to zero in
// 64-bit arithmetic and collapses the whole partition. The span is at
// most 2^64 - 1 and always representable.
//
// The two properties every caller of these formulas depends on:
//
//   PROPERTY 1 (equivalence). width = span/s + 1 equals ceil((span+1)/s).
//     Write span = q*s + r with 0 <= r < s. Then
//       ceil((span+1)/s) = q + ceil((r+1)/s) = q + 1
//     because 1 <= r+1 <= s. So this matches the natural "range divided
//     into s equal parts, rounded up" wherever that form is computable.
//
//   PROPERTY 2 (the index is always in range). No clamping is needed:
//       s*width = s*floor(span/s) + s = (span - r) + s > span
//     hence span/width < s, so floor(offset/width) <= s-1 for every
//     offset in [0, span].
//
// Property 2 holds for any s >= 1 provided width is computed exactly.
// Width can only overflow when s == 1 and span == 2^64 - 1; countAndPlace
// handles s == 1 without computing an index at all, so that case never
// reaches the division.
template <typename T>
typename DynamicRangeSort<T>::IntervalGrid DynamicRangeSort<T>::computeRangeParameters(
    const ValueRange& range, std::size_t length) const {
    assert(length > 0);

    const uint64_t span =
        static_cast<uint64_t>(range.maximum) - static_cast<uint64_t>(range.minimum);

    IntervalGrid grid;
    grid.binCount = (length + targetElementsPerBin_ - 1) / targetElementsPerBin_;
    if (grid.binCount == 0) grid.binCount = 1;

    // Cap by the number of distinct values the range can hold. A bin
    // narrower than one value is unreachable: no element can map into it,
    // so allocating, zeroing, prefix-summing and iterating it is waste.
    //
    // The cap cannot change the partition. It only binds when
    // span < binCount, and then the width is span/binCount + 1 = 1 both
    // before and after capping - identical widths mean an identical
    // value-to-bucket assignment. Only the unreachable tail disappears.
    //
    // span + 1 cannot overflow here: the branch is only taken when
    // span < binCount <= length.
    if (span < static_cast<uint64_t>(grid.binCount)) {
        grid.binCount = static_cast<std::size_t>(span + 1ULL);
    }

    // With a single bucket the width carries no information - every value
    // maps to bucket 0 regardless - and its exact value, span + 1, may not
    // be representable: it is exactly 2^64 when the input spans the whole
    // universe (both INT64_MIN and INT64_MAX present), which wraps to 0.
    //
    // Normalising it to 1 makes the postcondition "width >= 1" hold
    // unconditionally, so no overflowed value can escape this function.
    // Before v10 the wrapped 0 did escape, and the code was correct only
    // because countAndPlace happens to short-circuit numBuckets == 1
    // before dividing. That was luck, not a contract.
    grid.width =
        (grid.binCount == 1) ? 1ULL : (span / static_cast<uint64_t>(grid.binCount) + 1ULL);

    assert(grid.binCount >= 1);
    assert(grid.width >= 1);
    return grid;
}

// Index of 'value' within a grid of equal-width intervals starting at
// rangeStart. By Property 2 the result is always a valid bucket index, so
// there is no clamping: a clamp here would silently absorb an arithmetic
// bug instead of exposing it.
template <typename T>
std::size_t DynamicRangeSort<T>::computeBinIndex(T value, T rangeStart, uint64_t width) const {
    const uint64_t offset = static_cast<uint64_t>(value) - static_cast<uint64_t>(rangeStart);
    return static_cast<std::size_t>(offset / width);
}

// ============================================================
// countAndPlace - the shared distribution step
// ============================================================
// Counts how many of src[srcStart, srcStart+count) fall into each of
// numBuckets equal-width intervals, then places them into
// dst[dstStart, dstStart+count) grouped by bucket and in ascending bucket
// order. Used both by the top-level distribution and by every refinement
// split.
//
// Guarantees on exit:
//   - outBucketStart[b] is the offset of bucket b in dst, and the buckets
//     tile [dstStart, dstStart+count) with no gaps, in ascending order;
//   - within a bucket, elements keep their relative order in src.
//
// bucketOfScratch_ and writeCursorScratch_ are member scratch reused by
// every call. That is safe because the recursion is strictly depth-first
// and single-threaded: this function has finished with both before it
// returns, and nothing nested runs while it is using them.
// outBucketStart and outBucketSize CANNOT be shared this way - the caller
// keeps reading them while it recurses into the children.
template <typename T>
void DynamicRangeSort<T>::countAndPlace(const std::vector<T>& src, std::size_t srcStart,
                                        std::size_t count, std::vector<T>& dst,
                                        std::size_t dstStart, T rangeStart, uint64_t width,
                                        std::size_t numBuckets,
                                        std::vector<std::size_t>& outBucketStart,
                                        std::vector<std::size_t>& outBucketSize) {
    assert(numBuckets >= 1);
    assert(width >= 1);
    // src and dst must be distinct objects: the single-bucket path below
    // uses memcpy, which is undefined for overlapping regions. Every call
    // site satisfies this (the top level reads the caller's array and
    // writes bufferA_; a refinement reads one buffer and writes the
    // other), but nothing in the signature says so.
    assert(static_cast<const void*>(&src) != static_cast<const void*>(&dst));

    // Degenerate partition: one bucket takes everything, so there is no
    // index to compute. Isolating this case is what allows
    // computeBinIndex() to have no clamp - it is the only case where the
    // exact width (span + 1) may not be representable.
    //
    // Reachable in two ways: an input small enough to fit one bin, and an
    // input whose span is zero (every element identical), which the cap
    // in computeRangeParameters() collapses to a single bin.
    if (numBuckets == 1) {
        outBucketStart.assign(1, dstStart);
        outBucketSize.assign(1, count);
        if (count > 0) {
            std::memcpy(dst.data() + dstStart, src.data() + srcStart, count * sizeof(T));
        }
        return;
    }

    // Pass 1: bucket index of every element, and the histogram.
    outBucketSize.assign(numBuckets, 0);
    if (bucketOfScratch_.size() < count) bucketOfScratch_.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t idx = computeBinIndex(src[srcStart + i], rangeStart, width);
        assert(idx < numBuckets); // Property 2
        bucketOfScratch_[i] = idx;
        ++outBucketSize[idx];
    }

    // Prefix sum: turn the histogram into write cursors.
    outBucketStart.assign(numBuckets, 0);
    if (writeCursorScratch_.size() < numBuckets) writeCursorScratch_.resize(numBuckets);
    std::size_t cursor = dstStart;
    for (std::size_t b = 0; b < numBuckets; ++b) {
        outBucketStart[b] = cursor;
        writeCursorScratch_[b] = cursor;
        cursor += outBucketSize[b];
    }
    assert(cursor == dstStart + count);

    // Pass 2: place. Advancing the cursor as we go is what preserves the
    // relative order of equal-bucket elements.
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t b = bucketOfScratch_[i];
        dst[writeCursorScratch_[b]++] = src[srcStart + i];
    }
}

// ============================================================
// Phases 2 and 3: top-level distribution
// ============================================================
template <typename T>
void DynamicRangeSort<T>::distribute(const std::vector<T>& data, T minimumValue, uint64_t width,
                                     std::size_t binCount,
                                     std::vector<std::size_t>& outBucketStart,
                                     std::vector<std::size_t>& outBucketSize) {
    // Both buffers must exist at full size before any refinement runs: a
    // split writes into the other buffer at the same absolute offsets, so
    // both are indexed over [0, n) from the first level onwards.
    bufferA_.assign(data.size(), T{});
    bufferB_.assign(data.size(), T{});
    countAndPlace(data, 0, data.size(), bufferA_, 0, minimumValue, width, binCount, outBucketStart,
                  outBucketSize);
#ifdef DRS_ENABLE_METRICS
    metrics_.addApproxMemory(bufferA_.size() * sizeof(T) + bufferB_.size() * sizeof(T));
#endif
}

// ============================================================
// Phase 4: recursive refinement
// ============================================================
// Splits [start, start+count) of whichever buffer currently holds it,
// using ONLY that bin's own observed range, writing the children into the
// other buffer at the same absolute offsets.
//
// TERMINATION. Let span be the bin's observed span and s >= 2 the number
// of splits. Every child occupies an interval of exactly width values, so
// its own span is at most width - 1 = floor(span/s) < span for span >= 1.
// The span therefore decreases strictly at every level and is a
// non-negative integer, so the recursion terminates even with no depth
// cap. MAX_SUBDIVISION_DEPTH bounds the effort spent, not the
// termination; see Config.hpp for why it also bounds the worst case.
//
// EXIT CONDITIONS. A bin becomes a leaf when it is empty, when it holds
// at most leafThreshold_ elements, when the depth cap is reached, or when
// its observed span is zero. There is no other way out.
template <typename T>
typename DynamicRangeSort<T>::RefinedRange DynamicRangeSort<T>::refine(bool inBufferA,
                                                                      std::size_t start,
                                                                      std::size_t count,
                                                                      std::size_t depth) {
    RefinedRange node;
    node.inBufferA = inBufferA;
    node.start = start;
    node.count = count;

    if (count == 0) {
#ifdef DRS_ENABLE_METRICS
        metrics_.recordBin(0, true);
#endif
        return node;
    }

    // Base case by size, or by exhausted depth. Note this returns BEFORE
    // scanning for min and max: a bin that stops here never pays for a
    // range it does not need.
    if (count <= leafThreshold_ || depth >= MAX_SUBDIVISION_DEPTH) {
#ifdef DRS_ENABLE_METRICS
        metrics_.recordBin(count, false);
#endif
        return node;
    }

    const std::vector<T>& cur = inBufferA ? bufferA_ : bufferB_;

    T observedMin = cur[start];
    T observedMax = cur[start];
    for (std::size_t i = start + 1; i < start + count; ++i) {
        if (cur[i] < observedMin) observedMin = cur[i];
        if (cur[i] > observedMax) observedMax = cur[i];
    }

    if (observedMin == observedMax) {
        // Degenerate range: every element is identical, so the bin is
        // sorted by definition and the data does not need to move.
        // Recording the certificate saves the local sort a full O(count)
        // rescan to rediscover the same fact.
        node.sorted = true;
#ifdef DRS_ENABLE_METRICS
        metrics_.recordBin(count, false);
#endif
        return node;
    }

    // Never overflows, and is >= 1 here because the equal case returned.
    const uint64_t observedSpan =
        static_cast<uint64_t>(observedMax) - static_cast<uint64_t>(observedMin);

    // Same rule as the top level: aim for lambda elements per child, but
    // never create more intervals than there are distinct values. See
    // computeRangeParameters() for why the cap cannot change the
    // partition.
    //
    // splits stays >= 2: count > leafThreshold_ >= targetElementsPerBin_
    // gives at least 2, and when the cap binds it becomes
    // observedSpan + 1 >= 2.
    std::size_t splits = (count + targetElementsPerBin_ - 1) / targetElementsPerBin_;
    if (observedSpan < static_cast<uint64_t>(splits)) {
        splits = static_cast<std::size_t>(observedSpan + 1ULL);
    }
    assert(splits >= 2);

    const uint64_t newWidth = observedSpan / static_cast<uint64_t>(splits) + 1ULL;

#ifdef DRS_ENABLE_METRICS
    metrics_.recordSubdivision(depth + 1);
#endif

    std::vector<T>& other = inBufferA ? bufferB_ : bufferA_;
    std::vector<std::size_t> bucketStart;
    std::vector<std::size_t> bucketSize;
    countAndPlace(cur, start, count, other, start, observedMin, newWidth, splits, bucketStart,
                  bucketSize);

#ifdef DRS_ENABLE_METRICS
    const std::size_t maxChildSize =
        bucketSize.empty() ? 0 : *std::max_element(bucketSize.begin(), bucketSize.end());
    metrics_.recordSubdivisionQuality(count, splits, maxChildSize, depth + 1);
    metrics_.addApproxMemory(splits * sizeof(RefinedRange));
#endif

    node.children.reserve(splits);
    for (std::size_t s = 0; s < splits; ++s) {
        node.children.push_back(refine(!inBufferA, bucketStart[s], bucketSize[s], depth + 1));
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
// NOTE this is why the sorter is not stable: reversing a descending run
// swaps elements that compare equal under a weaker ordering. Harmless for
// the integral keys this class accepts, but it is the reason the class
// must not be generalised to key/value pairs as written.
template <typename T>
typename DynamicRangeSort<T>::RunShape DynamicRangeSort<T>::detectRun(const std::vector<T>& buf,
                                                                     std::size_t start,
                                                                     std::size_t count) {
    if (count < 2) return RunShape::Ascending;

    bool ascending = true;
    bool descending = true;
    for (std::size_t i = start + 1; i < start + count; ++i) {
#ifdef DRS_ENABLE_METRICS
        // Two comparisons per step, both counted: the counter is meant to
        // reflect work done, not source lines.
        metrics_.recordComparisons(2);
#endif
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
// that decided to stop refining. Tying them together meant that changing
// the leaf threshold silently changed which sort ran.
//
// A leaf normally holds at most leafThreshold_ elements. Larger ones only
// arise from exhausting MAX_SUBDIVISION_DEPTH, which is what the second
// and third branches exist for.
template <typename T>
void DynamicRangeSort<T>::sortLeaf(std::vector<T>& buf, std::size_t start, std::size_t count) {
    if (count < 2) return;

    switch (detectRun(buf, start, count)) {
        case RunShape::Ascending:
#ifdef DRS_ENABLE_METRICS
            metrics_.recordAlgorithmUsage("AlreadySorted");
#endif
            return;
        case RunShape::Descending:
            std::reverse(buf.begin() + static_cast<Index>(start),
                         buf.begin() + static_cast<Index>(start + count));
#ifdef DRS_ENABLE_METRICS
            metrics_.recordAlgorithmUsage("ReversedRun");
#endif
            return;
        case RunShape::Unsorted:
            break;
    }

    const Index left = static_cast<Index>(start);
    const Index right = static_cast<Index>(start + count - 1);
    if (count <= LOCAL_INSERTION_MAX_ELEMENTS) {
        insertionSort(buf, left, right);
#ifdef DRS_ENABLE_METRICS
        metrics_.recordAlgorithmUsage("InsertionSort");
#endif
    } else if (count <= LOCAL_QUICKSORT_MAX_ELEMENTS) {
        quickSort(buf, left, right);
#ifdef DRS_ENABLE_METRICS
        metrics_.recordAlgorithmUsage("QuickSort");
#endif
    } else {
        introSort(buf, left, right);
#ifdef DRS_ENABLE_METRICS
        metrics_.recordAlgorithmUsage("Introsort");
#endif
    }
}

template <typename T>
void DynamicRangeSort<T>::sortRefined(RefinedRange& node) {
    if (node.isLeaf()) {
        // A certified bin needs nothing: refine() already established that
        // every element is identical. Without the certificate this would
        // cost a full O(count) scan to reach the same conclusion.
        if (node.sorted) {
#ifdef DRS_ENABLE_METRICS
            metrics_.recordAlgorithmUsage("AlreadySorted");
#endif
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
void DynamicRangeSort<T>::insertionSort(std::vector<T>& arr, Index left, Index right) {
    for (Index i = left + 1; i <= right; ++i) {
        const T key = arr[i];
        Index j = i - 1;
        while (j >= left) {
#ifdef DRS_ENABLE_METRICS
            metrics_.recordComparison();
#endif
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
// arr[left] <= pivot <= arr[right], and the pivot itself is parked at
// right-1. Those two elements are sentinels: the ascending scan cannot
// run past 'right' because arr[right] >= pivot stops it, and the
// descending scan cannot run past 'left' because arr[left] <= pivot stops
// it. This is the reason neither loop tests its bound, and the reason the
// precondition below is not optional.
template <typename T>
typename DynamicRangeSort<T>::Index DynamicRangeSort<T>::partition(std::vector<T>& arr, Index left,
                                                                   Index right) {
    assert(right - left >= 2 && "partition needs at least three elements for the sentinels");

    const Index mid = left + (right - left) / 2;

#ifdef DRS_ENABLE_METRICS
    metrics_.recordComparisons(3);
#endif
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
#ifdef DRS_ENABLE_METRICS
            metrics_.recordComparison();
#endif
        } while (arr[i] < pivot);
        do {
            --j;
#ifdef DRS_ENABLE_METRICS
            metrics_.recordComparison();
#endif
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
void DynamicRangeSort<T>::quickSort(std::vector<T>& arr, Index left, Index right) {
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

// floor(log2(v)) for v >= 1, by integer arithmetic. Replaces a
// std::log2 call: this class is otherwise entirely integer, and pulling
// in floating point for a depth bound risks platform-dependent rounding
// in a value that controls control flow.
inline std::size_t floorLog2(std::size_t v) {
    std::size_t r = 0;
    while (v > 1) {
        v >>= 1;
        ++r;
    }
    return r;
}

template <typename T>
void DynamicRangeSort<T>::introSort(std::vector<T>& arr, Index left, Index right) {
    if (right <= left) return;
    const std::size_t n = static_cast<std::size_t>(right - left + 1);
    introSortImpl(arr, left, right, INTROSORT_DEPTH_FACTOR * floorLog2(n));
}

// QuickSort with a partitioning budget; once it is exhausted the rest is
// finished with HeapSort. This is what turns QuickSort's O(k^2) worst case
// into a guaranteed O(k log k).
template <typename T>
void DynamicRangeSort<T>::introSortImpl(std::vector<T>& arr, Index left, Index right,
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

// Restores the max-heap property at 'start' within the heap rooted at
// 'start' and ending at 'end', both absolute indices into arr.
template <typename T>
void DynamicRangeSort<T>::siftDown(std::vector<T>& arr, Index start, Index end) {
    Index root = start;
    while (2 * (root - start) + 1 <= end - start) {
        const Index child = start + 2 * (root - start) + 1;
        Index swapIdx = root;

#ifdef DRS_ENABLE_METRICS
        metrics_.recordComparison();
#endif
        if (arr[swapIdx] < arr[child]) swapIdx = child;

        if (child + 1 <= end) {
#ifdef DRS_ENABLE_METRICS
            metrics_.recordComparison();
#endif
            if (arr[swapIdx] < arr[child + 1]) swapIdx = child + 1;
        }

        if (swapIdx == root) return;
        std::swap(arr[root], arr[swapIdx]);
        root = swapIdx;
    }
}

template <typename T>
void DynamicRangeSort<T>::heapSort(std::vector<T>& arr, Index left, Index right) {
    const Index n = right - left + 1;
    if (n < 2) return;

    for (Index start = left + (n - 2) / 2; start >= left; --start) {
        siftDown(arr, start, right);
    }
    for (Index end = right; end > left; --end) {
        std::swap(arr[left], arr[end]);
        siftDown(arr, left, end - 1);
    }
}

// ============================================================
// Phase 6: join
// ============================================================
// THE TILING INVARIANT. countAndPlace writes the children of a bin at
// consecutive offsets starting at the parent's own start, in ascending
// bucket order, and computeBinIndex is monotone in the value. So the
// children tile the parent's range in ascending value order, and by
// induction over the depth the leaves tile [0, n) in ascending value
// order.
//
// The consequence used here: EVERY LEAF ALREADY OCCUPIES ITS FINAL
// POSITION. The join is not a merge and does not even need a write
// cursor - each leaf is copied to exactly its own offset. The only open
// question per leaf is which of the two buffers it currently lives in.
//
// memcpy is valid without any runtime check:
//   - 'out' is the caller's vector and 'buf' is bufferA_ or bufferB_:
//     three distinct objects, so they cannot overlap;
//   - T is integral by the class-level static_assert, hence trivially
//     copyable;
//   - the destination range is exactly the leaf's own range, by the
//     invariant above.
template <typename T>
void DynamicRangeSort<T>::appendLeaves(const RefinedRange& node, std::vector<T>& out) const {
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
// Debug-only check of the tiling invariant: walks the leaves in order and
// verifies they cover [0, n) exactly once, with no gap and no overlap.
// This is the property the join relies on, so it is worth stating
// executably rather than only in a comment.
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
#endif

// ============================================================
// Orchestration
// ============================================================
template <typename T>
void DynamicRangeSort<T>::sort(std::vector<T>& data) {
#ifdef DRS_ENABLE_METRICS
    metrics_.reset();
#endif
    if (data.size() < 2) return;

#ifdef DRS_ENABLE_METRICS
    metrics_.startPhase("analyze");
#endif
    const ValueRange range = analyze(data);
#ifdef DRS_ENABLE_METRICS
    metrics_.endPhase("analyze");
#endif

    const IntervalGrid grid = computeRangeParameters(range, data.size());

#ifdef DRS_ENABLE_METRICS
    metrics_.startPhase("distribute");
#endif
    std::vector<std::size_t> bucketStart;
    std::vector<std::size_t> bucketSize;
    distribute(data, range.minimum, grid.width, grid.binCount, bucketStart, bucketSize);
#ifdef DRS_ENABLE_METRICS
    metrics_.endPhase("distribute");
#endif

#ifdef DRS_ENABLE_METRICS
    metrics_.startPhase("refine");
#endif
    std::vector<RefinedRange> roots;
    roots.reserve(grid.binCount);
    for (std::size_t i = 0; i < grid.binCount; ++i) {
        // distribute() always leaves the data in bufferA_.
        roots.push_back(refine(true, bucketStart[i], bucketSize[i], 0));
    }
#ifdef DRS_ENABLE_METRICS
    metrics_.endPhase("refine");
#endif

#ifndef NDEBUG
    {
        std::size_t expectedStart = 0;
        for (const RefinedRange& root : roots) {
            assert(verifyTiling(root, expectedStart) && "leaves do not tile [0, n) in order");
        }
        assert(expectedStart == data.size() && "leaves do not cover [0, n)");
    }
#endif

#ifdef DRS_ENABLE_METRICS
    metrics_.startPhase("localSort");
#endif
    for (RefinedRange& root : roots) {
        sortRefined(root);
    }
#ifdef DRS_ENABLE_METRICS
    metrics_.endPhase("localSort");
#endif

    // Only from here on is the caller's array modified. Everything above
    // reads 'data' but never writes it, which is what gives sort() the
    // strong exception guarantee documented in the header: any allocation
    // failure happens before the first write.
#ifdef DRS_ENABLE_METRICS
    metrics_.startPhase("merge");
#endif
    for (const RefinedRange& root : roots) {
        appendLeaves(root, data);
    }
#ifdef DRS_ENABLE_METRICS
    metrics_.endPhase("merge");
#endif

#ifdef DRS_ENABLE_METRICS
    metrics_.addApproxMemory(bucketOfScratch_.capacity() * sizeof(std::size_t) +
                             writeCursorScratch_.capacity() * sizeof(std::size_t));
#endif
}

#ifdef DRS_ENABLE_METRICS
// ============================================================
// Research-only introspection
// ============================================================
template <typename T>
void DynamicRangeSort<T>::flattenLeaves(const RefinedRange& node, std::vector<LeafView>& out) const {
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
// introspection will describe a partition that sort() no longer produces.
template <typename T>
std::vector<typename DynamicRangeSort<T>::LeafView> DynamicRangeSort<T>::debugPartitionOnly(
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
    const IntervalGrid grid = computeRangeParameters(range, data.size());

    std::vector<std::size_t> bucketStart;
    std::vector<std::size_t> bucketSize;
    distribute(data, range.minimum, grid.width, grid.binCount, bucketStart, bucketSize);

    for (std::size_t i = 0; i < grid.binCount; ++i) {
        const RefinedRange root = refine(true, bucketStart[i], bucketSize[i], 0);
        flattenLeaves(root, leaves);
    }
    return leaves;
}
#endif

} // namespace drs
