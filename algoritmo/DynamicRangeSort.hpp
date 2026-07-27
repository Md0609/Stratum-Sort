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
// DynamicRangeSort (DRS) - v7
// ============================================================
// Custom sorting algorithm following the philosophy:
//
//   Analyze -> Build bins -> Refine bins -> Sort locally -> Merge
//
// v7 is a pure performance pass, not a new heuristic. Three mechanisms
// investigated in v6 (micro-histogram splits, density-aware initial
// bins, Difficulty-Score-based subdivision skipping) were all measured
// as net-negative and are gone from this class entirely - not disabled,
// removed. Their code still exists, isolated, in
// algoritmo/versions/DRSv6_experimental.hpp for archival comparison
// only; it is never included here. See ANALYSIS_v6.md for why each one
// was removed and ANALYSIS_v7.md for the cleanup.
//
// The other v7 change is the production/research split: every
// DRSMetrics call site, the metrics() accessor, and the debug-only
// introspection API (debugPartitionOnly) are now compiled out entirely
// unless DRS_ENABLE_METRICS is defined. A production build (the
// Makefile's default) produces a binary with zero instrumentation
// code - not disabled counters, no counters at all. See
// ANALYSIS_v7.md, "2/3", for the measured production-vs-research
// overhead this replaces.
//
// The five-step philosophy, and the rule that any subdivision only ever
// uses a bin's own observed range, are unchanged from the original
// specification.
//
// This implementation only supports integral element types, since every
// binning formula (range, intervalSize, binIndex, observedRange) is
// defined over integer arithmetic.
// ============================================================
template <typename T>
class DynamicRangeSort {
    static_assert(std::is_integral<T>::value,
                  "DynamicRangeSort requires an integral element type");

public:
    explicit DynamicRangeSort(std::size_t targetElementsPerBin = DEFAULT_TARGET_ELEMENTS_PER_BIN);

    // Sorts 'data' in place following the DRS specification.
    void sort(std::vector<T>& data);

#ifdef DRS_ENABLE_METRICS
    // Read-only access to the metrics gathered during the last sort()
    // call. Only exists in the research build.
    const DRSMetrics& metrics() const { return metrics_; }

    // ---- Research-only introspection --------------------------------------
    // Exists purely so external analysis code (see experimentos/) can
    // inspect the bins DRS would produce, without sorting or merging
    // them. sort() never calls any of this.
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
    // ---- FASE 1: ANALISIS ----------------------------------------------
    struct AnalysisResult {
        T minimumValue{};
        T maximumValue{};
        std::size_t length = 0;
    };

    AnalysisResult analyze(const std::vector<T>& data) const;

    // ---- FORMULAS ---------------------------------------------------------
    // The interval width is a magnitude, not a value of T: it is carried as
    // uint64_t so it can represent widths that do not fit in T. See the
    // comment on computeRangeParameters() in the .tpp for the span-based
    // formulation and the two properties it relies on.
    void computeRangeParameters(const AnalysisResult& analysis, std::size_t& outBinCount,
                                 uint64_t& outIntervalSize) const;

    std::size_t computeBinIndex(T value, T rangeStart, uint64_t intervalSize) const;

    // ---- DISTRIBUCION (primera+segunda pasada fusionadas) ------------------
    void distribute(const std::vector<T>& data, T minimumValue, uint64_t intervalSize,
                     std::size_t binCount, std::vector<std::size_t>& outBucketStart,
                     std::vector<std::size_t>& outBucketSize);

    // Shared counting-sort-style distribution step used by distribute()
    // and every refine() split: counts how many elements of
    // src[srcStart, srcStart+count) fall into each of 'numBuckets'
    // equal-width buckets, then places them into dst starting at
    // dstStart. Reuses scratch member vectors across calls instead of
    // allocating fresh ones each time (see ANALYSIS_v7.md, "4/7").
    void countAndPlace(const std::vector<T>& src, std::size_t srcStart, std::size_t count,
                        std::vector<T>& dst, std::size_t dstStart, T rangeStart,
                        uint64_t intervalSize, std::size_t numBuckets,
                        std::vector<std::size_t>& outBucketStart,
                        std::vector<std::size_t>& outBucketSize);

    // ---- REFINAMIENTO RECURSIVO (SUBDIVISION generalizada) ------------------
    struct RefinedRange {
        bool inBufferA = true;
        // Certificado de ordenado. refine() lo pone cuando descubre que el
        // rango observado del bin es degenerado (observedMin == observedMax):
        // en ese momento YA SABE que el bin esta ordenado, porque todos sus
        // elementos son identicos. Sin el certificado, sortLeaf() vuelve a
        // recorrer el bin entero con detectRun() para redescubrirlo.
        //
        // Solo lo llevan las hojas que llegaron a refine() con
        // count > targetElementsPerBin_; las que salen por tamano retornan
        // antes de calcular min/max y no tienen nada que certificar.
        //
        // Va junto a inBufferA a proposito: cae en el relleno que ya existia,
        // asi que sizeof(RefinedRange) no cambia.
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

    // ---- ORDENACION LOCAL -------------------------------------------------
    enum class RunShape { Ascending, Descending, Unsorted };

    RunShape detectRun(const std::vector<T>& buf, std::size_t start, std::size_t count);
    void sortLeaf(std::vector<T>& buf, std::size_t start, std::size_t count);
    void sortRefined(RefinedRange& node);

    void insertionSort(std::vector<T>& arr, long left, long right);
    void quickSort(std::vector<T>& arr, long left, long right);
    void introSort(std::vector<T>& arr, long left, long right);
    void introSortImpl(std::vector<T>& arr, long left, long right, int depthLimit);
    void heapSort(std::vector<T>& arr, long left, long right);
    void siftDown(std::vector<T>& arr, long start, long end);
    long partition(std::vector<T>& arr, long left, long right);

    // ---- UNION FINAL --------------------------------------------------------
    void mergeRefined(const RefinedRange& node, std::vector<T>& out, std::size_t& pos) const;

    std::size_t targetElementsPerBin_;

    // Scratch buffers shared by distribute() and refine() for the
    // duration of a single sort() call.
    std::vector<T> bufferA_;
    std::vector<T> bufferB_;

    // Reused across every countAndPlace() call (top-level and every
    // recursive split) to avoid a heap allocation per call - recursion
    // is strictly depth-first and single-threaded, so each call fully
    // consumes these before any nested call reuses them.
    std::vector<std::size_t> bucketOfScratch_;
    std::vector<std::size_t> bucketSizeScratch_;
    std::vector<std::size_t> writeCursorScratch_;

#ifdef DRS_ENABLE_METRICS
    DRSMetrics metrics_;
#endif
};

} // namespace drs

#include "DynamicRangeSort.tpp"
