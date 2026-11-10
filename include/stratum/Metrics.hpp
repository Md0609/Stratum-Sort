#pragma once

// Only included when STRATUM_ENABLE_METRICS is defined (see
// StratumSort.hpp). It is research-only and never part of a release
// build, so none of its methods needs an internal runtime toggle: simply
// not including this file removes all of it.
//
// Nothing here is on any correctness path. A release build must behave
// identically without it.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

namespace stratum {

// ============================================================
// SortMetrics (research build only)
// ============================================================
// Collects internal statistics while StratumSort executes:
// comparisons, subdivisions, subdivision quality, bin sizes, per-phase
// timing, and which local sorting algorithm was used in each bin.
// ============================================================
class SortMetrics {
public:
    // One record per actual subdivision.
    // reductionPct = 1 - (largest child size / original size): how much
    // the worst-case remaining subproblem shrank. A subdivision that
    // barely splits its elements (one dominant child) has reductionPct
    // near 0; a well-balanced split has it near 1.
    struct SubdivisionRecord {
        std::size_t originalSize = 0;
        std::size_t childCount = 0;
        std::size_t maxChildSize = 0;
        std::size_t depth = 0;
        double reductionPct = 0.0;
    };

    enum class SubdivisionQuality { Useful, Marginal, Useless };

    // ---- Total work (objective.md A2) ---------------------------------
    // A deterministic count of everything the algorithm does to elements,
    // in EVERY phase - not only the comparisons of the local sorts, which
    // is all comparisons() sees. Exists so that a superlinearity hidden in
    // the distribution or the refinement cannot stay invisible.
    //
    // The cost model, stated once so the numbers mean something:
    //   reads        an element value loaded from an array. An element-vs-
    //                element comparison reads 2; a comparison against a
    //                value held in a local (insertion key, pivot) reads 1;
    //                a swap reads 2 and writes 2.
    //   writes       an element value stored into an array.
    //   comparisons  every comparison involving an element value: the
    //                min/max tests of analyze and scanRange (2 per element
    //                after the first) PLUS every comparison of the local
    //                sorts (exactly what comparisons() counts).
    //   bucketOps    one unit per bucket per distribution step: the
    //                histogram, the prefix sum and the child bookkeeping
    //                are all O(buckets), and a bucket count that outgrew n
    //                would show up here and nowhere else.
    //   total()      reads + writes + comparisons + bucketOps.
    //
    // Per-pass costs (analyze, scanRange, distribution, join) are exact
    // closed forms recorded once per pass; the local sorts record each
    // read and write as it happens. Nothing here depends on the clock.
    enum class WorkPhase : std::size_t {
        Analyze, ScanRange, Distribute, Refine, LocalSort, Join, Count
    };
    static constexpr std::size_t kWorkPhases = static_cast<std::size_t>(WorkPhase::Count);

    static const char* workPhaseName(WorkPhase p) {
        static const char* const names[kWorkPhases] = {"analyze", "scanRange", "distribute",
                                                       "refine",  "localSort", "join"};
        return names[static_cast<std::size_t>(p)];
    }

    struct WorkCounts {
        std::size_t reads = 0;
        std::size_t writes = 0;
        std::size_t comparisons = 0;
        std::size_t bucketOps = 0;
        std::size_t total() const { return reads + writes + comparisons + bucketOps; }
        WorkCounts& operator+=(const WorkCounts& o) {
            reads += o.reads;
            writes += o.writes;
            comparisons += o.comparisons;
            bucketOps += o.bucketOps;
            return *this;
        }
    };

    // Thresholds behind SubdivisionQuality: "Useful" requires at least
    // halving the worst-case remaining work; "Useless" means the largest
    // child kept at least 90% of the original problem. These cutoffs come
    // from the measured distribution of reductionPct on this codebase, not
    // from taste.
    static constexpr double kUsefulReductionThreshold = 0.5;
    static constexpr double kUselessReductionThreshold = 0.1;

    static SubdivisionQuality classifyReduction(double reductionPct) {
        if (reductionPct >= kUsefulReductionThreshold) return SubdivisionQuality::Useful;
        if (reductionPct >= kUselessReductionThreshold) return SubdivisionQuality::Marginal;
        return SubdivisionQuality::Useless;
    }

    void reset() {
        comparisons_ = 0;
        subdivisions_ = 0;
        maxSubdivisionDepth_ = 0;
        totalBins_ = 0;
        emptyBins_ = 0;
        approxMemoryBytes_ = 0;
        binSizes_.clear();
        algorithmUsage_.clear();
        phaseDurationsMs_.clear();
        phaseStartTimes_.clear();
        subdivisionRecords_.clear();
        workByDepth_.assign(1, 0);
        work_.fill(WorkCounts{});
        unknownWorkPhase_ = false;
    }

    // Comparisons are only ever reported by the local sorts, so they also
    // land in the LocalSort row of the total-work counter.
    void recordComparisons(std::size_t n) {
        comparisons_ += n;
        work_[static_cast<std::size_t>(WorkPhase::LocalSort)].comparisons += n;
    }

    // One whole pass with a known closed-form cost. 'phase' is one of the
    // names returned by workPhaseName(); called O(bins) times per sort, so
    // the string lookup is not on any per-element path.
    //
    // "distribute" ALSO books the analyze() pass that always precedes it
    // over the same n elements (n = 'writes': the top-level distribution
    // writes every element exactly once): n reads and 2n comparisons. It
    // is booked here, on the metrics side, so that the release build needs
    // no wrapper call next to analyze(); an empty call there perturbed
    // GCC's register allocation, and the release object code is kept
    // identical (BENCHMARKS.md).
    void recordPassWork(const char* phase, std::size_t reads, std::size_t writes,
                        std::size_t comparisons, std::size_t bucketOps) {
        if (std::strcmp(phase, "distribute") == 0) {
            WorkCounts& a = work_[static_cast<std::size_t>(WorkPhase::Analyze)];
            a.reads += writes;
            a.comparisons += 2 * writes;
        }
        for (std::size_t i = 0; i < kWorkPhases; ++i) {
            if (std::strcmp(phase, workPhaseName(static_cast<WorkPhase>(i))) == 0) {
                WorkCounts& w = work_[i];
                w.reads += reads;
                w.writes += writes;
                w.comparisons += comparisons;
                w.bucketOps += bucketOps;
                return;
            }
        }
        unknownWorkPhase_ = true; // surfaced by hasUnknownWorkPhase(); a test pins it false
    }

    // Individual reads and writes inside the local sorts.
    void recordLocalSortMoves(std::size_t reads, std::size_t writes) {
        WorkCounts& w = work_[static_cast<std::size_t>(WorkPhase::LocalSort)];
        w.reads += reads;
        w.writes += writes;
    }

    // Called every time a bin is split during recursive refinement.
    // depthReached is the depth of the children produced by this split
    // (i.e. current depth + 1), so maxSubdivisionDepth_ always reflects
    // the deepest level of refinement actually used.
    void recordSubdivision(std::size_t depthReached) {
        ++subdivisions_;
        maxSubdivisionDepth_ = std::max(maxSubdivisionDepth_, depthReached);
    }

    // Full quality record for one subdivision, kept per event rather than
    // aggregated: the interesting question was never the mean reduction
    // but the shape of its distribution, and a mean cannot be
    // un-aggregated afterwards. Also accumulates the elements processed
    // per depth level, which is what shows whether a level earns its cost.
    void recordSubdivisionQuality(std::size_t originalSize, std::size_t childCount,
                                   std::size_t maxChildSize, std::size_t depth) {
        SubdivisionRecord rec;
        rec.originalSize = originalSize;
        rec.childCount = childCount;
        rec.maxChildSize = maxChildSize;
        rec.depth = depth;
        rec.reductionPct = originalSize == 0
                                ? 0.0
                                : 1.0 - static_cast<double>(maxChildSize) / static_cast<double>(originalSize);
        subdivisionRecords_.push_back(rec);

        if (workByDepth_.size() <= depth) workByDepth_.resize(depth + 1, 0);
        workByDepth_[depth] += originalSize;
    }

    void recordBin(std::size_t size, bool isEmptyBin) {
        ++totalBins_;
        if (isEmptyBin) ++emptyBins_;
        binSizes_.push_back(size);
    }

    void recordAlgorithmUsage(const std::string& algorithmName) { ++algorithmUsage_[algorithmName]; }

    // Adds 'bytes' to the running approximate memory usage figure. This is
    // an instrumented estimate (the code explicitly reports every buffer
    // and bookkeeping structure it allocates), not a value obtained from an
    // allocator hook or OS-level RSS sampling, so it is documented as
    // approximate rather than exact.
    void addApproxMemory(std::size_t bytes) { approxMemoryBytes_ += bytes; }

    void startPhase(const std::string& phaseName) {
        phaseStartTimes_[phaseName] = std::chrono::steady_clock::now();
    }

    void endPhase(const std::string& phaseName) {
        auto it = phaseStartTimes_.find(phaseName);
        if (it != phaseStartTimes_.end()) {
            const double elapsedMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - it->second)
                    .count();
            phaseDurationsMs_[phaseName] += elapsedMs;
        }
    }

    // ---- accessors ----------------------------------------------------
    const WorkCounts& work(WorkPhase p) const { return work_[static_cast<std::size_t>(p)]; }
    WorkCounts totalWork() const {
        WorkCounts sum;
        for (const WorkCounts& w : work_) sum += w;
        return sum;
    }
    bool hasUnknownWorkPhase() const { return unknownWorkPhase_; }

    std::size_t comparisons() const { return comparisons_; }
    std::size_t subdivisions() const { return subdivisions_; }
    std::size_t maxSubdivisionDepth() const { return maxSubdivisionDepth_; }
    std::size_t totalBins() const { return totalBins_; }
    std::size_t emptyBins() const { return emptyBins_; }

    double averageBinSize() const {
        if (binSizes_.empty()) return 0.0;
        const double sum = std::accumulate(binSizes_.begin(), binSizes_.end(), 0.0);
        return sum / static_cast<double>(binSizes_.size());
    }

    std::size_t maxBinSize() const {
        if (binSizes_.empty()) return 0;
        return *std::max_element(binSizes_.begin(), binSizes_.end());
    }

    // Raw per-leaf-bin sizes (including empty bins, size 0), in the order
    // they were recorded. Exists so that external code can study the whole
    // occupancy distribution; averageBinSize() and maxBinSize() above
    // remain the cheap summary for routine reporting.
    const std::vector<std::size_t>& binSizes() const { return binSizes_; }

    double phaseDurationMs(const std::string& phaseName) const {
        const auto it = phaseDurationsMs_.find(phaseName);
        return it == phaseDurationsMs_.end() ? 0.0 : it->second;
    }

    const std::unordered_map<std::string, std::size_t>& algorithmUsage() const { return algorithmUsage_; }

    std::size_t approxMemoryBytes() const { return approxMemoryBytes_; }

    const std::vector<SubdivisionRecord>& subdivisionRecords() const { return subdivisionRecords_; }
    const std::vector<std::size_t>& workByDepth() const { return workByDepth_; }

    // "Subdivision Efficiency": the mean reductionPct across every
    // subdivision performed - a single number summarizing how much
    // useful work the refinement step is doing on average.
    double subdivisionEfficiency() const {
        if (subdivisionRecords_.empty()) return 0.0;
        double sum = 0.0;
        for (const auto& rec : subdivisionRecords_) sum += rec.reductionPct;
        return sum / static_cast<double>(subdivisionRecords_.size());
    }

    // Counts of Useful / Marginal / Useless subdivisions.
    struct QualityBreakdown {
        std::size_t useful = 0;
        std::size_t marginal = 0;
        std::size_t useless = 0;
        std::size_t total = 0;
    };
    QualityBreakdown subdivisionQualityBreakdown() const {
        QualityBreakdown b;
        for (const auto& rec : subdivisionRecords_) {
            switch (classifyReduction(rec.reductionPct)) {
                case SubdivisionQuality::Useful: ++b.useful; break;
                case SubdivisionQuality::Marginal: ++b.marginal; break;
                case SubdivisionQuality::Useless: ++b.useless; break;
            }
        }
        b.total = subdivisionRecords_.size();
        return b;
    }

    void print(std::ostream& os) const {
        os << "  Comparisons:          " << comparisons_ << "\n";
        os << "  Subdivisions:         " << subdivisions_ << "\n";
        os << "  Max subdivision depth:" << maxSubdivisionDepth_ << "\n";
        os << "  Total bins:           " << totalBins_ << "\n";
        os << "  Empty bins:           " << emptyBins_ << "\n";
        os << "  Average bin size:     " << std::fixed << std::setprecision(2) << averageBinSize() << "\n";
        os << "  Max bin size:         " << maxBinSize() << "\n";
        os << "  Subdivision efficiency (mean reduction): " << std::fixed << std::setprecision(4)
           << subdivisionEfficiency() << "\n";
        os << "  Approx. memory used:  " << approxMemoryBytes_ << " bytes\n";
        const WorkCounts tw = totalWork();
        os << "  Total work:           " << tw.total() << " (reads " << tw.reads << ", writes "
           << tw.writes << ", comparisons " << tw.comparisons << ", bucketOps " << tw.bucketOps
           << ")\n";
        for (std::size_t i = 0; i < kWorkPhases; ++i) {
            os << "  Work [" << workPhaseName(static_cast<WorkPhase>(i))
               << "]: " << work_[i].total() << "\n";
        }
        for (const auto& [phase, ms] : phaseDurationsMs_) {
            os << "  Phase [" << phase << "]: " << std::fixed << std::setprecision(3) << ms << " ms\n";
        }
        for (const auto& [algo, count] : algorithmUsage_) {
            os << "  Local algorithm [" << algo << "]: " << count << " bins\n";
        }
    }

private:
    std::size_t comparisons_ = 0;
    std::size_t subdivisions_ = 0;
    std::size_t maxSubdivisionDepth_ = 0;
    std::size_t totalBins_ = 0;
    std::size_t emptyBins_ = 0;
    std::size_t approxMemoryBytes_ = 0;
    std::vector<std::size_t> binSizes_;
    std::vector<SubdivisionRecord> subdivisionRecords_;
    std::vector<std::size_t> workByDepth_{0};
    std::array<WorkCounts, kWorkPhases> work_{};
    bool unknownWorkPhase_ = false;
    std::unordered_map<std::string, std::size_t> algorithmUsage_;
    std::unordered_map<std::string, double> phaseDurationsMs_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> phaseStartTimes_;
};

} // namespace stratum
