#pragma once

#include "Bin.hpp"
#include "Config.hpp"
#include "DRSMetrics.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace drs {

// ============================================================
// DynamicRangeSort (DRS) - v2
// ============================================================
// Custom sorting algorithm following the philosophy:
//
//   Analyze -> Build bins -> Refine bins -> Sort locally -> Merge
//
// This version generalizes "refine bins" into a bounded recursive
// step: a bin that is still too large after being split, and whose
// own observed range still spans more than one value, is refined
// again against its own data (not the original array), up to
// MAX_SUBDIVISION_DEPTH levels. Elements that are exact duplicates
// of one another collapse into a single already-sorted leaf without
// ever being compared, and any bin that turns out to already be
// sorted (ascending or descending) is detected and finished in
// O(k) instead of being handed to a comparison sort.
//
// This implementation only supports integral element types, since
// every binning formula (range, intervalSize, binIndex, observedRange)
// is defined over integer arithmetic.
// ============================================================
template <typename T>
class DynamicRangeSort {
    static_assert(std::is_integral<T>::value,
                  "DynamicRangeSort requires an integral element type");

public:
    explicit DynamicRangeSort(std::size_t targetElementsPerBin = DEFAULT_TARGET_ELEMENTS_PER_BIN);

    // Sorts 'data' in place following the DRS specification.
    void sort(std::vector<T>& data);

    // Read-only access to the metrics gathered during the last sort() call.
    const DRSMetrics& metrics() const { return metrics_; }

private:
    // ---- FASE 1: ANALISIS ----------------------------------------------
    struct AnalysisResult {
        T minimumValue{};
        T maximumValue{};
        std::size_t length = 0;
        bool isSorted = true; // collected as an optional statistic only
    };

    AnalysisResult analyze(const std::vector<T>& data) const;

    // ---- FORMULAS / ESTRUCTURA DE CADA BIN ------------------------------
    // Builds the coarse, first-level bins. The bin count is capped at the
    // observed value range (a bin narrower than one value is meaningless),
    // which keeps the number of bins - and therefore the number of empty
    // bins ever materialized - proportional to min(n, range) instead of
    // always n / targetElementsPerBin.
    std::vector<Bin<T>> buildInitialBins(const AnalysisResult& analysis, T& outIntervalSize) const;

    std::size_t computeBinIndex(T value, T rangeStart, T intervalSize, std::size_t binCount) const;

    // ---- PRIMERA PASADA --------------------------------------------------
    // Single O(n) pass that both updates each bin's statistics AND caches
    // the computed bin index for every element, so the grouping step
    // below never has to recompute the division.
    void firstPass(const std::vector<T>& data, std::vector<Bin<T>>& bins, T minimumValue,
                    T intervalSize, std::vector<std::size_t>& outBinIndices) const;

    // ---- REFINAMIENTO RECURSIVO (SUBDIVISION generalizada) ------------------
    // A refined bin is a small recursive tree: a leaf holds the elements
    // that will be handed to a local sort; an internal node holds the
    // children produced by splitting its elements according to their own
    // observed range.
    struct RefinedBin {
        std::vector<T> elements;          // meaningful only when children is empty (leaf)
        std::vector<RefinedBin> children; // meaningful only when non-empty (internal node)

        bool isLeaf() const { return children.empty(); }
    };

    // Takes ownership of a bin's element bucket and recursively refines it.
    RefinedBin refine(std::vector<T>&& elements, std::size_t depth);

    // ---- ORDENACION LOCAL -------------------------------------------------
    enum class RunShape { Ascending, Descending, Unsorted };

    RunShape detectRun(const std::vector<T>& arr);
    void sortLeaf(std::vector<T>& arr);
    void sortRefined(RefinedBin& node);

    void insertionSort(std::vector<T>& arr);
    void quickSort(std::vector<T>& arr, long left, long right);
    void introSort(std::vector<T>& arr);
    void introSortImpl(std::vector<T>& arr, long left, long right, int depthLimit);
    void heapSort(std::vector<T>& arr, long left, long right);
    void siftDown(std::vector<T>& arr, long start, long end);
    long partition(std::vector<T>& arr, long left, long right);

    // ---- UNION FINAL --------------------------------------------------------
    void mergeRefined(const RefinedBin& node, std::vector<T>& out, std::size_t& pos) const;

    std::size_t targetElementsPerBin_;
    DRSMetrics metrics_;
};

} // namespace drs

#include "DynamicRangeSort.tpp"
