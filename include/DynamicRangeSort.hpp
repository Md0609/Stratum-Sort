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
// DynamicRangeSort (DRS)
// ============================================================
// Custom sorting algorithm following the philosophy:
//
//   Analyze -> Build bins -> Refine bins -> Sort locally -> Merge
//
// This implementation only supports integral element types, since
// every binning formula in the specification (range, intervalSize,
// binIndex, observedRange) is defined over integer arithmetic.
//
// The class is split into one private method per phase of the
// specification, matching the document sections one-to-one:
//   analyze()            -> FASE 1 / ANALISIS
//   buildInitialBins()   -> FORMULAS / ESTRUCTURA DE CADA BIN
//   firstPass()          -> PRIMERA PASADA
//   subdivideBins()      -> SUBDIVISION
//   secondPass()         -> SEGUNDA PASADA
//   sortBinLocally()     -> ORDENACION LOCAL
//   mergeResults()       -> UNION FINAL
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
    std::vector<Bin<T>> buildInitialBins(const AnalysisResult& analysis, T& outIntervalSize) const;

    std::size_t computeBinIndex(T value, T rangeStart, T intervalSize, std::size_t binCount) const;

    // ---- PRIMERA PASADA --------------------------------------------------
    void firstPass(const std::vector<T>& data, std::vector<Bin<T>>& bins, T minimumValue,
                    T intervalSize) const;

    // ---- SUBDIVISION -------------------------------------------------------
    // Per-original-bin routing information used to place elements into
    // their final (post-subdivision) bin during the second pass.
    struct SubdivisionInfo {
        bool needsSubdivision = false;
        T observedMin{};
        T newIntervalSize{};
        std::size_t splits = 1;
        std::size_t finalBinOffset = 0; // offset into the flat finalBins vector
    };

    std::vector<SubdivisionInfo> subdivideBins(std::vector<Bin<T>>& bins,
                                                std::size_t& outFinalBinCount);

    // ---- SEGUNDA PASADA ------------------------------------------------------
    struct FinalBin {
        T lowerBound{};
        T upperBound{};
        std::vector<T> elements;
    };

    std::vector<FinalBin> secondPass(const std::vector<T>& data, const std::vector<Bin<T>>& originalBins,
                                      const std::vector<SubdivisionInfo>& subInfo, T minimumValue,
                                      T intervalSize, std::size_t finalBinCount) const;

    // ---- ORDENACION LOCAL -------------------------------------------------
    void sortBinLocally(FinalBin& bin);
    void insertionSort(std::vector<T>& arr);
    void quickSort(std::vector<T>& arr, long left, long right);
    void introSort(std::vector<T>& arr);
    void introSortImpl(std::vector<T>& arr, long left, long right, int depthLimit);
    void heapSort(std::vector<T>& arr, long left, long right);
    void siftDown(std::vector<T>& arr, long start, long end);
    long partition(std::vector<T>& arr, long left, long right);

    // ---- UNION FINAL --------------------------------------------------------
    void mergeResults(std::vector<T>& data, std::vector<FinalBin>& finalBins) const;

    std::size_t targetElementsPerBin_;
    DRSMetrics metrics_;
};

} // namespace drs

#include "DynamicRangeSort.tpp"
