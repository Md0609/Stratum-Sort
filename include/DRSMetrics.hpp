#pragma once

#include "Config.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

namespace drs {

// ============================================================
// DRSMetrics
// ============================================================
// Collects internal statistics while DynamicRangeSort executes:
// comparisons, subdivisions, maximum subdivision depth, bin sizes,
// per-phase timing and which local sorting algorithm was used in
// each bin.
//
// Every recording method below is guarded internally by the
// DEBUG_METRICS constant (Config.hpp). When DEBUG_METRICS is 0,
// all method bodies become empty, so calling them from the hot
// sorting path costs (after optimization) essentially nothing.
// This is the "single constant" toggle requested for the whole
// metrics system.
// ============================================================
class DRSMetrics {
public:
    void reset() {
#if DEBUG_METRICS
        comparisons_ = 0;
        subdivisions_ = 0;
        maxSubdivisionDepth_ = 0;
        totalBins_ = 0;
        emptyBins_ = 0;
        binSizes_.clear();
        algorithmUsage_.clear();
        phaseDurationsMs_.clear();
        phaseStartTimes_.clear();
#endif
    }

    void recordComparison() {
#if DEBUG_METRICS
        ++comparisons_;
#endif
    }

    void recordComparisons(std::size_t n) {
#if DEBUG_METRICS
        comparisons_ += n;
#else
        (void)n;
#endif
    }

    // The specification subdivides each over-sized bin exactly once
    // (subdivision uses the *observed* range, not a recursive scheme),
    // so the maximum depth reached by this implementation is always
    // 0 (no bin needed subdivision) or 1 (at least one bin was split).
    // The field is kept general so a future recursive-subdivision
    // variant (see IMPROVEMENTS at the end of ANALYSIS.md) can reuse it.
    void recordSubdivision() {
#if DEBUG_METRICS
        ++subdivisions_;
        maxSubdivisionDepth_ = std::max<std::size_t>(maxSubdivisionDepth_, 1);
#endif
    }

    void recordBin(std::size_t size, bool isEmptyBin) {
#if DEBUG_METRICS
        ++totalBins_;
        if (isEmptyBin) ++emptyBins_;
        binSizes_.push_back(size);
#else
        (void)size;
        (void)isEmptyBin;
#endif
    }

    void recordAlgorithmUsage(const std::string& algorithmName) {
#if DEBUG_METRICS
        ++algorithmUsage_[algorithmName];
#else
        (void)algorithmName;
#endif
    }

    void startPhase(const std::string& phaseName) {
#if DEBUG_METRICS
        phaseStartTimes_[phaseName] = std::chrono::steady_clock::now();
#else
        (void)phaseName;
#endif
    }

    void endPhase(const std::string& phaseName) {
#if DEBUG_METRICS
        auto it = phaseStartTimes_.find(phaseName);
        if (it != phaseStartTimes_.end()) {
            const double elapsedMs = std::chrono::duration<double, std::milli>(
                                          std::chrono::steady_clock::now() - it->second)
                                          .count();
            phaseDurationsMs_[phaseName] += elapsedMs;
        }
#else
        (void)phaseName;
#endif
    }

    // ---- accessors ----------------------------------------------------
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

    double phaseDurationMs(const std::string& phaseName) const {
        const auto it = phaseDurationsMs_.find(phaseName);
        return it == phaseDurationsMs_.end() ? 0.0 : it->second;
    }

    const std::unordered_map<std::string, std::size_t>& algorithmUsage() const {
        return algorithmUsage_;
    }

    void print(std::ostream& os) const {
#if DEBUG_METRICS
        os << "  Comparisons:          " << comparisons_ << "\n";
        os << "  Subdivisions:         " << subdivisions_ << "\n";
        os << "  Max subdivision depth:" << maxSubdivisionDepth_ << "\n";
        os << "  Total bins:           " << totalBins_ << "\n";
        os << "  Empty bins:           " << emptyBins_ << "\n";
        os << "  Average bin size:     " << std::fixed << std::setprecision(2)
           << averageBinSize() << "\n";
        os << "  Max bin size:         " << maxBinSize() << "\n";
        for (const auto& [phase, ms] : phaseDurationsMs_) {
            os << "  Phase [" << phase << "]: " << std::fixed << std::setprecision(3) << ms
               << " ms\n";
        }
        for (const auto& [algo, count] : algorithmUsage_) {
            os << "  Local algorithm [" << algo << "]: " << count << " bins\n";
        }
#else
        os << "  (metrics disabled: DEBUG_METRICS == 0)\n";
#endif
    }

private:
    std::size_t comparisons_ = 0;
    std::size_t subdivisions_ = 0;
    std::size_t maxSubdivisionDepth_ = 0;
    std::size_t totalBins_ = 0;
    std::size_t emptyBins_ = 0;
    std::vector<std::size_t> binSizes_;
    std::unordered_map<std::string, std::size_t> algorithmUsage_;
    std::unordered_map<std::string, double> phaseDurationsMs_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> phaseStartTimes_;
};

} // namespace drs
