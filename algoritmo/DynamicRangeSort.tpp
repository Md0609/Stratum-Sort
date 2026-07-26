#pragma once

// This file is included at the bottom of DynamicRangeSort.hpp; it is not
// meant to be included directly.

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace drs {

// ============================================================
// Construction
// ============================================================
template <typename T>
DynamicRangeSort<T>::DynamicRangeSort(std::size_t targetElementsPerBin)
    : targetElementsPerBin_(targetElementsPerBin == 0 ? DEFAULT_TARGET_ELEMENTS_PER_BIN
                                                       : targetElementsPerBin) {}

// ============================================================
// FASE 1: ANALISIS
// ============================================================
// Single O(n) pass. Per element: O(1) comparisons/updates only. This is
// the one pass that cannot be fused with anything else: every later
// formula (range, intervalSize, binIndex) depends on the minimum and
// maximum values found here.
template <typename T>
typename DynamicRangeSort<T>::AnalysisResult DynamicRangeSort<T>::analyze(
    const std::vector<T>& data) const {
    AnalysisResult result;
    result.length = data.size();
    if (data.empty()) {
        return result;
    }

    result.minimumValue = data[0];
    result.maximumValue = data[0];
    for (const T value : data) {
        if (value < result.minimumValue) result.minimumValue = value;
        if (value > result.maximumValue) result.maximumValue = value;
    }
    return result;
}

// ============================================================
// FORMULAS: range, initialBins, intervalSize, binIndex
// ============================================================
template <typename T>
void DynamicRangeSort<T>::computeRangeParameters(const AnalysisResult& analysis,
                                                  std::size_t& outBinCount,
                                                  T& outIntervalSize) const {
    if (analysis.length == 0) {
        outBinCount = 0;
        outIntervalSize = static_cast<T>(1);
        return;
    }

    // range = maximumValue - minimumValue + 1
    const uint64_t range = static_cast<uint64_t>(analysis.maximumValue) -
                            static_cast<uint64_t>(analysis.minimumValue) + 1ULL;

    // initialBins = ceil(length / targetElementsPerBin), capped by range: a
    // bin narrower than one value can never be reached, so allocating and
    // iterating it is pure waste.
    std::size_t initialBins =
        (analysis.length + targetElementsPerBin_ - 1) / targetElementsPerBin_;
    if (initialBins == 0) initialBins = 1;

    constexpr uint64_t kMaxSize = std::numeric_limits<std::size_t>::max();
    const std::size_t rangeAsSize = range > kMaxSize ? std::numeric_limits<std::size_t>::max()
                                                       : static_cast<std::size_t>(range);
    initialBins = std::min(initialBins, rangeAsSize);
    if (initialBins == 0) initialBins = 1;

    // intervalSize = ceil(range / initialBins)
    uint64_t intervalSize = (range + static_cast<uint64_t>(initialBins) - 1) /
                             static_cast<uint64_t>(initialBins);
    if (intervalSize == 0) intervalSize = 1;

    outBinCount = initialBins;
    outIntervalSize = static_cast<T>(intervalSize);
}

// binIndex = floor((value - rangeStart) / intervalSize), clamped defensively
// to [0, binCount - 1] to absorb rounding at the very last bin.
template <typename T>
std::size_t DynamicRangeSort<T>::computeBinIndex(T value, T rangeStart, T intervalSize,
                                                   std::size_t binCount) const {
    const uint64_t offset = static_cast<uint64_t>(value) - static_cast<uint64_t>(rangeStart);
    std::size_t index = static_cast<std::size_t>(offset / static_cast<uint64_t>(intervalSize));
    if (index >= binCount) index = binCount - 1;
    return index;
}

// ============================================================
// countAndPlace: shared counting-sort-style distribution step
// ============================================================
// Counts how many elements of src[srcStart, srcStart+count) fall into
// each of 'numBuckets' equal-width buckets (O(count)), then places them
// into dst at precomputed offsets (O(count)). Used by both distribute()
// (the top-level split) and every refine() split.
//
// bucketOfScratch_ and writeCursorScratch_ are member-level scratch
// space reused across every call (see the header comment on those
// fields for why this is safe under DRS's strictly depth-first,
// single-threaded recursion): resize() only reallocates when a call
// needs more capacity than any previous call has used, which in
// practice means only the first few (largest) calls in the recursion
// tree ever trigger an allocation. outBucketStart/outBucketSize remain
// ordinary caller-owned vectors, because their contents must stay valid
// for as long as the caller is iterating over and recursing into the
// resulting children - unlike bucketOf/writeCursor, they cannot be
// reused scratch.
template <typename T>
void DynamicRangeSort<T>::countAndPlace(const std::vector<T>& src, std::size_t srcStart,
                                         std::size_t count, std::vector<T>& dst,
                                         std::size_t dstStart, T rangeStart, T intervalSize,
                                         std::size_t numBuckets,
                                         std::vector<std::size_t>& outBucketStart,
                                         std::vector<std::size_t>& outBucketSize) {
    outBucketSize.assign(numBuckets, 0);
    if (bucketOfScratch_.size() < count) bucketOfScratch_.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t idx = computeBinIndex(src[srcStart + i], rangeStart, intervalSize, numBuckets);
        bucketOfScratch_[i] = idx;
        ++outBucketSize[idx];
    }

    outBucketStart.assign(numBuckets, 0);
    if (writeCursorScratch_.size() < numBuckets) writeCursorScratch_.resize(numBuckets);
    std::size_t cursor = dstStart;
    for (std::size_t b = 0; b < numBuckets; ++b) {
        outBucketStart[b] = cursor;
        writeCursorScratch_[b] = cursor;
        cursor += outBucketSize[b];
    }

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t b = bucketOfScratch_[i];
        dst[writeCursorScratch_[b]++] = src[srcStart + i];
    }
}

// ============================================================
// DISTRIBUCION (primera+segunda pasada fusionadas)
// ============================================================
template <typename T>
void DynamicRangeSort<T>::distribute(const std::vector<T>& data, T minimumValue, T intervalSize,
                                      std::size_t binCount, std::vector<std::size_t>& outBucketStart,
                                      std::vector<std::size_t>& outBucketSize) {
    bufferA_.assign(data.size(), T{});
    bufferB_.assign(data.size(), T{});
    countAndPlace(data, 0, data.size(), bufferA_, 0, minimumValue, intervalSize, binCount,
                  outBucketStart, outBucketSize);
#ifdef DRS_ENABLE_METRICS
    metrics_.addApproxMemory(bufferA_.size() * sizeof(T) + bufferB_.size() * sizeof(T));
#endif
}

// ============================================================
// REFINAMIENTO RECURSIVO (generalizacion de SUBDIVISION)
// ============================================================
// Splits the range [start, start+count) of whichever buffer currently
// holds it, using only its own observed range (never the original
// array's range), into the *other* buffer (the cascading double-buffer
// scheme). Bounded by MAX_SUBDIVISION_DEPTH.
//
// Two fast paths keep this close to O(n) in practice:
//   - A bin whose observed range is a single value is, by definition,
//     already sorted: it becomes a leaf immediately, with no further
//     splitting, no comparisons, and no data movement at all.
//   - A bin at or below targetElementsPerBin also becomes a leaf.
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

    std::vector<T>& cur = inBufferA ? bufferA_ : bufferB_;

    if (count <= targetElementsPerBin_ || depth >= MAX_SUBDIVISION_DEPTH) {
#ifdef DRS_ENABLE_METRICS
        metrics_.recordBin(count, false);
#endif
        return node;
    }

    T observedMin = cur[start];
    T observedMax = cur[start];
    for (std::size_t i = start + 1; i < start + count; ++i) {
        if (cur[i] < observedMin) observedMin = cur[i];
        if (cur[i] > observedMax) observedMax = cur[i];
    }

    if (observedMin == observedMax) {
        // Every element in this bin is identical: trivially sorted, and
        // the data does not even need to move.
#ifdef DRS_ENABLE_METRICS
        metrics_.recordBin(count, false);
#endif
        return node;
    }

    // splits = ceil(count / targetElementsPerBin)
    const std::size_t splits = (count + targetElementsPerBin_ - 1) / targetElementsPerBin_;

    // observedRange = observedMax - observedMin + 1 (observed range only)
    const uint64_t observedRange =
        static_cast<uint64_t>(observedMax) - static_cast<uint64_t>(observedMin) + 1ULL;
    uint64_t newIntervalSizeU64 = (observedRange + splits - 1) / splits;
    if (newIntervalSizeU64 == 0) newIntervalSizeU64 = 1;
    const T newIntervalSize = static_cast<T>(newIntervalSizeU64);

#ifdef DRS_ENABLE_METRICS
    metrics_.recordSubdivision(depth + 1);
#endif

    std::vector<T>& other = inBufferA ? bufferB_ : bufferA_;
    std::vector<std::size_t> bucketStart;
    std::vector<std::size_t> bucketSize;
    countAndPlace(cur, start, count, other, start, observedMin, newIntervalSize, splits, bucketStart,
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
// ORDENACION LOCAL
// ============================================================
// O(k) scan that recognizes a bin that is already fully ascending or
// fully descending, so it can be finished without a comparison sort at
// all (ascending: no-op) or with a single O(k) reversal (descending).
template <typename T>
typename DynamicRangeSort<T>::RunShape DynamicRangeSort<T>::detectRun(const std::vector<T>& buf,
                                                                       std::size_t start,
                                                                       std::size_t count) {
    if (count < 2) return RunShape::Ascending;

    bool ascending = true;
    bool descending = true;
    for (std::size_t i = start + 1; i < start + count; ++i) {
#ifdef DRS_ENABLE_METRICS
        metrics_.recordComparison();
#endif
        if (buf[i] < buf[i - 1]) ascending = false;
        if (buf[i] > buf[i - 1]) descending = false;
        if (!ascending && !descending) return RunShape::Unsorted;
    }
    return ascending ? RunShape::Ascending : RunShape::Descending;
}

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
            std::reverse(buf.begin() + static_cast<long>(start),
                         buf.begin() + static_cast<long>(start + count));
#ifdef DRS_ENABLE_METRICS
            metrics_.recordAlgorithmUsage("ReversedRun");
#endif
            return;
        case RunShape::Unsorted:
            break;
    }

    const long left = static_cast<long>(start);
    const long right = static_cast<long>(start + count - 1);
    if (count <= INSERTION_SORT_THRESHOLD) {
        insertionSort(buf, left, right);
#ifdef DRS_ENABLE_METRICS
        metrics_.recordAlgorithmUsage("InsertionSort");
#endif
    } else if (count <= QUICKSORT_THRESHOLD) {
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
        std::vector<T>& buf = node.inBufferA ? bufferA_ : bufferB_;
        sortLeaf(buf, node.start, node.count);
        return;
    }
    for (RefinedRange& child : node.children) {
        sortRefined(child);
    }
}

template <typename T>
void DynamicRangeSort<T>::insertionSort(std::vector<T>& arr, long left, long right) {
    for (long i = left + 1; i <= right; ++i) {
        const T key = arr[i];
        long j = i - 1;
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

// Median-of-three Hoare-style partition, shared by quickSort() and
// introSortImpl(). Assumes right - left >= 2.
template <typename T>
long DynamicRangeSort<T>::partition(std::vector<T>& arr, long left, long right) {
    const long mid = left + (right - left) / 2;

#ifdef DRS_ENABLE_METRICS
    metrics_.recordComparisons(3);
#endif
    if (arr[mid] < arr[left]) std::swap(arr[mid], arr[left]);
    if (arr[right] < arr[left]) std::swap(arr[right], arr[left]);
    if (arr[right] < arr[mid]) std::swap(arr[right], arr[mid]);

    const T pivot = arr[mid];
    std::swap(arr[mid], arr[right - 1]);

    long i = left;
    long j = right - 1;
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

// Iterative-recursive hybrid QuickSort (tail call on the larger partition
// turned into a loop to bound stack depth), finishing small ranges with
// Insertion Sort.
template <typename T>
void DynamicRangeSort<T>::quickSort(std::vector<T>& arr, long left, long right) {
    while (right - left > 12) {
        const long p = partition(arr, left, right);
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

template <typename T>
void DynamicRangeSort<T>::introSort(std::vector<T>& arr, long left, long right) {
    if (right - left < 1) return;
    const std::size_t n = static_cast<std::size_t>(right - left + 1);
    const int depthLimit = static_cast<int>(2.0 * std::log2(static_cast<double>(n)));
    introSortImpl(arr, left, right, depthLimit);
}

// QuickSort with a recursion-depth limit; once the limit is exhausted the
// remaining range is finished with HeapSort (classic Introsort behavior,
// guarding against QuickSort's O(n^2) worst case).
template <typename T>
void DynamicRangeSort<T>::introSortImpl(std::vector<T>& arr, long left, long right,
                                          int depthLimit) {
    while (right - left > 12) {
        if (depthLimit == 0) {
            heapSort(arr, left, right);
            return;
        }
        --depthLimit;
        const long p = partition(arr, left, right);
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

template <typename T>
void DynamicRangeSort<T>::siftDown(std::vector<T>& arr, long start, long end) {
    long root = start;
    while (2 * (root - start) + 1 <= end - start) {
        const long child = start + 2 * (root - start) + 1;
        long swapIdx = root;

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
void DynamicRangeSort<T>::heapSort(std::vector<T>& arr, long left, long right) {
    const long n = right - left + 1;
    if (n < 2) return;

    for (long start = left + (n - 2) / 2; start >= left; --start) {
        siftDown(arr, start, right);
    }
    for (long end = right; end > left; --end) {
        std::swap(arr[left], arr[end]);
        siftDown(arr, left, end - 1);
    }
}

// ============================================================
// UNION FINAL
// ============================================================
// Children of a RefinedRange are always produced and visited in
// ascending value order, so a plain in-order traversal that appends
// each leaf's (now sorted) elements - read from whichever buffer that
// leaf currently lives in - yields the fully sorted array.
template <typename T>
void DynamicRangeSort<T>::mergeRefined(const RefinedRange& node, std::vector<T>& out,
                                        std::size_t& pos) const {
    if (node.isLeaf()) {
        const std::vector<T>& buf = node.inBufferA ? bufferA_ : bufferB_;
        for (std::size_t i = node.start; i < node.start + node.count; ++i) {
            out[pos++] = buf[i];
        }
        return;
    }
    for (const RefinedRange& child : node.children) {
        mergeRefined(child, out, pos);
    }
}

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
    const AnalysisResult analysis = analyze(data);
#ifdef DRS_ENABLE_METRICS
    metrics_.endPhase("analyze");
#endif

    std::size_t binCount = 0;
    T intervalSize{};
    computeRangeParameters(analysis, binCount, intervalSize);

#ifdef DRS_ENABLE_METRICS
    metrics_.startPhase("distribute");
#endif
    std::vector<std::size_t> bucketStart;
    std::vector<std::size_t> bucketSize;
    distribute(data, analysis.minimumValue, intervalSize, binCount, bucketStart, bucketSize);
#ifdef DRS_ENABLE_METRICS
    metrics_.endPhase("distribute");
#endif

#ifdef DRS_ENABLE_METRICS
    metrics_.startPhase("refine");
#endif
    std::vector<RefinedRange> roots;
    roots.reserve(binCount);
    for (std::size_t i = 0; i < binCount; ++i) {
        // Data placed by distribute() always starts out in bufferA_.
        roots.push_back(refine(true, bucketStart[i], bucketSize[i], 0));
    }
#ifdef DRS_ENABLE_METRICS
    metrics_.endPhase("refine");
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

#ifdef DRS_ENABLE_METRICS
    metrics_.startPhase("merge");
#endif
    std::size_t pos = 0;
    for (const RefinedRange& root : roots) {
        mergeRefined(root, data, pos);
    }
#ifdef DRS_ENABLE_METRICS
    metrics_.endPhase("merge");
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

    const AnalysisResult analysis = analyze(data);

    std::size_t binCount = 0;
    T intervalSize{};
    computeRangeParameters(analysis, binCount, intervalSize);

    std::vector<std::size_t> bucketStart;
    std::vector<std::size_t> bucketSize;
    distribute(data, analysis.minimumValue, intervalSize, binCount, bucketStart, bucketSize);

    for (std::size_t i = 0; i < binCount; ++i) {
        const RefinedRange root = refine(true, bucketStart[i], bucketSize[i], 0);
        flattenLeaves(root, leaves);
    }
    return leaves;
}
#endif

} // namespace drs
