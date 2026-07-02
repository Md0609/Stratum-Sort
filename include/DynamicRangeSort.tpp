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
// Single O(n) pass. Per element: O(1) comparisons/updates only.
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
    result.isSorted = true;

    for (std::size_t i = 0; i < data.size(); ++i) {
        const T value = data[i];
        if (value < result.minimumValue) result.minimumValue = value;
        if (value > result.maximumValue) result.maximumValue = value;
        if (i > 0 && data[i] < data[i - 1]) result.isSorted = false;
    }
    return result;
}

// ============================================================
// FORMULAS: range, initialBins, intervalSize, binIndex
// ============================================================
// initialBins is capped at the observed value range: a bin narrower than
// a single value can never be reached (computeBinIndex could never
// produce that index), so allocating and later iterating it is pure
// waste. Capping it here means the number of bins DRS ever materializes
// is proportional to min(length / targetElementsPerBin, range) - the
// direct fix for the "many empty bins" bottleneck observed with narrow
// value ranges or heavily duplicated data.
template <typename T>
std::vector<Bin<T>> DynamicRangeSort<T>::buildInitialBins(const AnalysisResult& analysis,
                                                            T& outIntervalSize) const {
    if (analysis.length == 0) {
        outIntervalSize = static_cast<T>(1);
        return {};
    }

    // range = maximumValue - minimumValue + 1
    const uint64_t range = static_cast<uint64_t>(analysis.maximumValue) -
                            static_cast<uint64_t>(analysis.minimumValue) + 1ULL;

    // initialBins = ceil(length / targetElementsPerBin), capped by range.
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
    outIntervalSize = static_cast<T>(intervalSize);

    std::vector<Bin<T>> bins(initialBins);
    const uint64_t minAsU64 = static_cast<uint64_t>(analysis.minimumValue);
    for (std::size_t i = 0; i < initialBins; ++i) {
        const uint64_t lower = minAsU64 + static_cast<uint64_t>(i) * intervalSize;
        const uint64_t upper = lower + intervalSize - 1;
        bins[i].lowerBound = static_cast<T>(lower);
        bins[i].upperBound = static_cast<T>(upper);
    }
    return bins;
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
// PRIMERA PASADA
// ============================================================
// Each element updates count / observedMin / observedMax for its bin,
// and its bin index is cached in outBinIndices so the grouping step
// that follows never recomputes the division.
template <typename T>
void DynamicRangeSort<T>::firstPass(const std::vector<T>& data, std::vector<Bin<T>>& bins,
                                     T minimumValue, T intervalSize,
                                     std::vector<std::size_t>& outBinIndices) const {
    const std::size_t binCount = bins.size();
    for (std::size_t i = 0; i < data.size(); ++i) {
        const T value = data[i];
        const std::size_t idx = computeBinIndex(value, minimumValue, intervalSize, binCount);
        bins[idx].updateObserved(value);
        outBinIndices[i] = idx;
    }
}

// ============================================================
// REFINAMIENTO RECURSIVO (generalización de SUBDIVISION)
// ============================================================
// Splits 'elements' using only its own observed range (never the
// original array's range), exactly as the base rule specifies, but
// allows the resulting children to be refined again - bounded by
// MAX_SUBDIVISION_DEPTH - instead of being forced into a comparison
// sort just because one refinement pass was not enough.
//
// Two fast paths keep this close to O(n) in practice:
//   - A bin whose observed range is a single value is, by definition,
//     already sorted: it becomes a leaf immediately, with no further
//     splitting and no comparisons.
//   - A bin at or below targetElementsPerBin also becomes a leaf.
template <typename T>
typename DynamicRangeSort<T>::RefinedBin DynamicRangeSort<T>::refine(std::vector<T>&& elements,
                                                                      std::size_t depth) {
    RefinedBin node;
    const std::size_t count = elements.size();

    if (count == 0) {
        metrics_.recordBin(0, true);
        return node;
    }

    if (count <= targetElementsPerBin_ || depth >= MAX_SUBDIVISION_DEPTH) {
        metrics_.recordBin(count, false);
        node.elements = std::move(elements);
        return node;
    }

    T observedMin = elements[0];
    T observedMax = elements[0];
    for (const T v : elements) {
        if (v < observedMin) observedMin = v;
        if (v > observedMax) observedMax = v;
    }

    if (observedMin == observedMax) {
        // Every element in this bin is identical: trivially sorted.
        metrics_.recordBin(count, false);
        node.elements = std::move(elements);
        return node;
    }

    // splits = ceil(count / targetElementsPerBin)
    const std::size_t splits = (count + targetElementsPerBin_ - 1) / targetElementsPerBin_;

    // observedRange = observedMax - observedMin + 1 (observed range only)
    const uint64_t observedRange =
        static_cast<uint64_t>(observedMax) - static_cast<uint64_t>(observedMin) + 1ULL;
    uint64_t newIntervalSizeU64 =
        (observedRange + static_cast<uint64_t>(splits) - 1) / static_cast<uint64_t>(splits);
    if (newIntervalSizeU64 == 0) newIntervalSizeU64 = 1;
    const T newIntervalSize = static_cast<T>(newIntervalSizeU64);

    metrics_.recordSubdivision(depth + 1);

    std::vector<std::vector<T>> buckets(splits);
    for (const T v : elements) {
        const std::size_t idx = computeBinIndex(v, observedMin, newIntervalSize, splits);
        buckets[idx].push_back(v);
    }
    // Elements have been redistributed into buckets; release the parent
    // vector's storage before recursing so peak memory stays close to a
    // single extra copy of the data, not one copy per refinement level.
    elements.clear();
    elements.shrink_to_fit();

    node.children.reserve(splits);
    for (auto& bucket : buckets) {
        node.children.push_back(refine(std::move(bucket), depth + 1));
    }
    return node;
}

// ============================================================
// ORDENACION LOCAL
// ============================================================
// O(k) scan that recognizes a bin that is already fully ascending or
// fully descending, so it can be finished without a comparison sort at
// all (ascending: no-op) or with a single O(k) reversal (descending).
// This directly targets Insertion Sort's worst case: a reversed bin.
template <typename T>
typename DynamicRangeSort<T>::RunShape DynamicRangeSort<T>::detectRun(const std::vector<T>& arr) {
    if (arr.size() < 2) return RunShape::Ascending;

    bool ascending = true;
    bool descending = true;
    for (std::size_t i = 1; i < arr.size(); ++i) {
        metrics_.recordComparison();
        if (arr[i] < arr[i - 1]) ascending = false;
        if (arr[i] > arr[i - 1]) descending = false;
        if (!ascending && !descending) return RunShape::Unsorted;
    }
    return ascending ? RunShape::Ascending : RunShape::Descending;
}

template <typename T>
void DynamicRangeSort<T>::sortLeaf(std::vector<T>& arr) {
    if (arr.size() < 2) return;

    switch (detectRun(arr)) {
        case RunShape::Ascending:
            metrics_.recordAlgorithmUsage("AlreadySorted");
            return;
        case RunShape::Descending:
            std::reverse(arr.begin(), arr.end());
            metrics_.recordAlgorithmUsage("ReversedRun");
            return;
        case RunShape::Unsorted:
            break;
    }

    const std::size_t n = arr.size();
    if (n <= INSERTION_SORT_THRESHOLD) {
        insertionSort(arr);
        metrics_.recordAlgorithmUsage("InsertionSort");
    } else if (n <= QUICKSORT_THRESHOLD) {
        quickSort(arr, 0, static_cast<long>(n) - 1);
        metrics_.recordAlgorithmUsage("QuickSort");
    } else {
        introSort(arr);
        metrics_.recordAlgorithmUsage("Introsort");
    }
}

template <typename T>
void DynamicRangeSort<T>::sortRefined(RefinedBin& node) {
    if (node.isLeaf()) {
        sortLeaf(node.elements);
        return;
    }
    for (RefinedBin& child : node.children) {
        sortRefined(child);
    }
}

template <typename T>
void DynamicRangeSort<T>::insertionSort(std::vector<T>& arr) {
    for (std::size_t i = 1; i < arr.size(); ++i) {
        const T key = arr[i];
        long j = static_cast<long>(i) - 1;
        while (j >= 0) {
            metrics_.recordComparison();
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

    metrics_.recordComparisons(3);
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
            metrics_.recordComparison();
        } while (arr[i] < pivot);
        do {
            --j;
            metrics_.recordComparison();
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
    for (long i = left + 1; i <= right; ++i) {
        const T key = arr[i];
        long j = i - 1;
        while (j >= left) {
            metrics_.recordComparison();
            if (arr[j] <= key) break;
            arr[j + 1] = arr[j];
            --j;
        }
        arr[j + 1] = key;
    }
}

template <typename T>
void DynamicRangeSort<T>::introSort(std::vector<T>& arr) {
    if (arr.size() < 2) return;
    const int depthLimit = static_cast<int>(2.0 * std::log2(static_cast<double>(arr.size())));
    introSortImpl(arr, 0, static_cast<long>(arr.size()) - 1, depthLimit);
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
    for (long i = left + 1; i <= right; ++i) {
        const T key = arr[i];
        long j = i - 1;
        while (j >= left) {
            metrics_.recordComparison();
            if (arr[j] <= key) break;
            arr[j + 1] = arr[j];
            --j;
        }
        arr[j + 1] = key;
    }
}

template <typename T>
void DynamicRangeSort<T>::siftDown(std::vector<T>& arr, long start, long end) {
    long root = start;
    while (2 * (root - start) + 1 <= end - start) {
        const long child = start + 2 * (root - start) + 1;
        long swapIdx = root;

        metrics_.recordComparison();
        if (arr[swapIdx] < arr[child]) swapIdx = child;

        if (child + 1 <= end) {
            metrics_.recordComparison();
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
// Children of a RefinedBin are always produced and visited in
// ascending value order, so a plain in-order traversal that appends
// each leaf's (now sorted) elements yields the fully sorted array.
template <typename T>
void DynamicRangeSort<T>::mergeRefined(const RefinedBin& node, std::vector<T>& out,
                                        std::size_t& pos) const {
    if (node.isLeaf()) {
        for (const T value : node.elements) {
            out[pos++] = value;
        }
        return;
    }
    for (const RefinedBin& child : node.children) {
        mergeRefined(child, out, pos);
    }
}

// ============================================================
// Orchestration
// ============================================================
template <typename T>
void DynamicRangeSort<T>::sort(std::vector<T>& data) {
    metrics_.reset();
    if (data.size() < 2) return;

    metrics_.startPhase("analysis");
    const AnalysisResult analysis = analyze(data);
    metrics_.endPhase("analysis");

    T intervalSize{};
    metrics_.startPhase("buildInitialBins");
    std::vector<Bin<T>> bins = buildInitialBins(analysis, intervalSize);
    metrics_.endPhase("buildInitialBins");

    metrics_.startPhase("firstPass");
    std::vector<std::size_t> binIndices(data.size());
    firstPass(data, bins, analysis.minimumValue, intervalSize, binIndices);
    metrics_.endPhase("firstPass");

    metrics_.startPhase("groupElements");
    std::vector<std::vector<T>> groups(bins.size());
    for (std::size_t i = 0; i < bins.size(); ++i) {
        groups[i].reserve(bins[i].count);
    }
    for (std::size_t i = 0; i < data.size(); ++i) {
        groups[binIndices[i]].push_back(data[i]);
    }
    metrics_.endPhase("groupElements");

    metrics_.startPhase("refine");
    std::vector<RefinedBin> roots;
    roots.reserve(bins.size());
    for (std::size_t i = 0; i < bins.size(); ++i) {
        roots.push_back(refine(std::move(groups[i]), 0));
    }
    metrics_.endPhase("refine");

    metrics_.startPhase("localSort");
    for (RefinedBin& root : roots) {
        sortRefined(root);
    }
    metrics_.endPhase("localSort");

    metrics_.startPhase("merge");
    std::size_t pos = 0;
    for (const RefinedBin& root : roots) {
        mergeRefined(root, data, pos);
    }
    metrics_.endPhase("merge");
}

} // namespace drs
