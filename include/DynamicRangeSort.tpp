#pragma once

// This file is included at the bottom of DynamicRangeSort.hpp; it is not
// meant to be included directly.

#include <cmath>
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

    // initialBins = ceil(length / targetElementsPerBin)
    std::size_t initialBins =
        (analysis.length + targetElementsPerBin_ - 1) / targetElementsPerBin_;
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
// Each element only updates count / observedMin / observedMax.
// Nothing is sorted or stored yet.
template <typename T>
void DynamicRangeSort<T>::firstPass(const std::vector<T>& data, std::vector<Bin<T>>& bins,
                                     T minimumValue, T intervalSize) const {
    const std::size_t binCount = bins.size();
    for (const T value : data) {
        const std::size_t idx = computeBinIndex(value, minimumValue, intervalSize, binCount);
        bins[idx].updateObserved(value);
    }
}

// ============================================================
// SUBDIVISION
// ============================================================
// splits = ceil(count / targetElementsPerBin)
// observedRange = observedMax - observedMin + 1   (NOT the original range)
// newIntervalSize = ceil(observedRange / splits)
template <typename T>
std::vector<typename DynamicRangeSort<T>::SubdivisionInfo> DynamicRangeSort<T>::subdivideBins(
    std::vector<Bin<T>>& bins, std::size_t& outFinalBinCount) {
    std::vector<SubdivisionInfo> infos(bins.size());
    std::size_t finalBinIndex = 0;

    for (std::size_t i = 0; i < bins.size(); ++i) {
        Bin<T>& bin = bins[i];
        SubdivisionInfo& info = infos[i];
        info.finalBinOffset = finalBinIndex;

        if (bin.isEmpty() || bin.count <= targetElementsPerBin_) {
            bin.needsSubdivision = false;
            info.needsSubdivision = false;
            info.splits = 1;
            finalBinIndex += 1;
            continue;
        }

        bin.needsSubdivision = true;
        info.needsSubdivision = true;

        const std::size_t splits =
            (bin.count + targetElementsPerBin_ - 1) / targetElementsPerBin_;

        const uint64_t observedRange = static_cast<uint64_t>(bin.observedMax) -
                                        static_cast<uint64_t>(bin.observedMin) + 1ULL;
        uint64_t newIntervalSize =
            (observedRange + static_cast<uint64_t>(splits) - 1) / static_cast<uint64_t>(splits);
        if (newIntervalSize == 0) newIntervalSize = 1;

        info.observedMin = bin.observedMin;
        info.newIntervalSize = static_cast<T>(newIntervalSize);
        info.splits = splits;

        finalBinIndex += splits;
        metrics_.recordSubdivision();
    }

    outFinalBinCount = finalBinIndex;
    return infos;
}

// ============================================================
// SEGUNDA PASADA
// ============================================================
// Every element is routed directly into its definitive bin:
//   1. Locate its original bin (same formula as Phase 1).
//   2. If that bin was subdivided, locate its sub-bin using the
//      bin's own observedMin / newIntervalSize.
template <typename T>
std::vector<typename DynamicRangeSort<T>::FinalBin> DynamicRangeSort<T>::secondPass(
    const std::vector<T>& data, const std::vector<Bin<T>>& originalBins,
    const std::vector<SubdivisionInfo>& subInfo, T minimumValue, T intervalSize,
    std::size_t finalBinCount) const {
    std::vector<FinalBin> finalBins(finalBinCount);

    // Pre-fill bounds for reporting purposes and to keep the final bins
    // in ascending order (required for a correct UNION FINAL / merge).
    for (std::size_t i = 0; i < originalBins.size(); ++i) {
        const SubdivisionInfo& info = subInfo[i];
        if (!info.needsSubdivision) {
            FinalBin& fb = finalBins[info.finalBinOffset];
            fb.lowerBound = originalBins[i].lowerBound;
            fb.upperBound = originalBins[i].upperBound;
            continue;
        }
        const uint64_t obsMinU64 = static_cast<uint64_t>(info.observedMin);
        const uint64_t stepU64 = static_cast<uint64_t>(info.newIntervalSize);
        for (std::size_t s = 0; s < info.splits; ++s) {
            FinalBin& fb = finalBins[info.finalBinOffset + s];
            const uint64_t lower = obsMinU64 + static_cast<uint64_t>(s) * stepU64;
            const uint64_t upper = lower + stepU64 - 1;
            fb.lowerBound = static_cast<T>(lower);
            fb.upperBound = static_cast<T>(upper);
        }
    }

    const std::size_t originalBinCount = originalBins.size();
    for (const T value : data) {
        const std::size_t originalIdx =
            computeBinIndex(value, minimumValue, intervalSize, originalBinCount);
        const SubdivisionInfo& info = subInfo[originalIdx];

        std::size_t finalIdx;
        if (!info.needsSubdivision) {
            finalIdx = info.finalBinOffset;
        } else {
            const std::size_t subIdx =
                computeBinIndex(value, info.observedMin, info.newIntervalSize, info.splits);
            finalIdx = info.finalBinOffset + subIdx;
        }
        finalBins[finalIdx].elements.push_back(value);
    }

    return finalBins;
}

// ============================================================
// ORDENACION LOCAL
// ============================================================
template <typename T>
void DynamicRangeSort<T>::sortBinLocally(FinalBin& bin) {
    const std::size_t n = bin.elements.size();
    if (n <= INSERTION_SORT_THRESHOLD) {
        insertionSort(bin.elements);
        metrics_.recordAlgorithmUsage("InsertionSort");
    } else if (n <= QUICKSORT_THRESHOLD) {
        quickSort(bin.elements, 0, static_cast<long>(n) - 1);
        metrics_.recordAlgorithmUsage("QuickSort");
    } else {
        introSort(bin.elements);
        metrics_.recordAlgorithmUsage("Introsort");
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
// Final bins were built in ascending value order (both across original
// bins and across their sub-bins), so a straight concatenation of their
// locally-sorted elements yields the fully sorted array.
template <typename T>
void DynamicRangeSort<T>::mergeResults(std::vector<T>& data,
                                        std::vector<FinalBin>& finalBins) const {
    std::size_t pos = 0;
    for (const FinalBin& bin : finalBins) {
        for (const T value : bin.elements) {
            data[pos++] = value;
        }
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
    firstPass(data, bins, analysis.minimumValue, intervalSize);
    metrics_.endPhase("firstPass");

    metrics_.startPhase("subdivision");
    std::size_t finalBinCount = 0;
    std::vector<SubdivisionInfo> subInfo = subdivideBins(bins, finalBinCount);
    metrics_.endPhase("subdivision");

    metrics_.startPhase("secondPass");
    std::vector<FinalBin> finalBins =
        secondPass(data, bins, subInfo, analysis.minimumValue, intervalSize, finalBinCount);
    metrics_.endPhase("secondPass");

    for (const FinalBin& fb : finalBins) {
        metrics_.recordBin(fb.elements.size(), fb.elements.empty());
    }

    metrics_.startPhase("localSort");
    for (FinalBin& bin : finalBins) {
        sortBinLocally(bin);
    }
    metrics_.endPhase("localSort");

    metrics_.startPhase("merge");
    mergeResults(data, finalBins);
    metrics_.endPhase("merge");
}

} // namespace drs
