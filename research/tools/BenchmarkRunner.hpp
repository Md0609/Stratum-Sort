#pragma once

#include "stratum/StratumSort.hpp"

#ifndef STRATUM_ENABLE_METRICS
#error "BenchmarkRunner.hpp requires the research build (-DDRS_ENABLE_METRICS); it reads sorter.metrics()."
#endif

#include "Statistics.hpp"
#include "SystemInfo.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace stratum::bench {

// ============================================================
// BenchmarkReport
// ============================================================
// Everything the research brief asked to see after a benchmark: the
// timing distribution across repetitions, a correctness check, and every
// SortMetrics figure from a representative run. Comparisons, subdivisions,
// bin counts, etc. are deterministic for a given input and configuration
// (Stratum Sort has no randomness of its own), so a single representative run's
// SortMetrics is exact for those fields - only wall-clock time needs
// repeating and aggregating to average out system noise.
// ============================================================
struct BenchmarkReport {
    std::string label;
    std::size_t n = 0;
    stratum::stats::RepetitionStats timing;
    bool correct = true;
    SortMetrics metrics; // from one representative run
};

// Runs 'repetitions' trials of StratumSort::sort() on fresh copies of
// 'baseline', verifying correctness on every trial and collecting timing
// statistics across all of them. This function requires STRATUM_ENABLE_METRICS
// (it reads sorter.metrics()), matching every binary that includes this
// header (benchmarks/, experiments/, analysis/ all build with it).
// targetElementsPerBin == 0 means "use the shipped defaults", which is
// what a report about the library should measure. Passing an explicit
// value is for parameter studies only.
template <typename T>
BenchmarkReport runBenchmark(const std::string& label, const std::vector<T>& baseline,
                              std::size_t repetitions, std::size_t targetElementsPerBin = 0) {
    BenchmarkReport report;
    report.label = label;
    report.n = baseline.size();

    std::vector<T> reference = baseline;
    std::sort(reference.begin(), reference.end());

    std::vector<double> samplesMs;
    samplesMs.reserve(repetitions);

    for (std::size_t r = 0; r < repetitions; ++r) {
        std::vector<T> data = baseline;
        StratumSort<T> sorter =
            targetElementsPerBin == 0 ? StratumSort<T>()
                                      : StratumSort<T>(targetElementsPerBin);

        const auto t0 = std::chrono::steady_clock::now();
        sorter.sort(data);
        const auto t1 = std::chrono::steady_clock::now();

        samplesMs.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());

        if (data != reference) report.correct = false;
        if (r == repetitions - 1) report.metrics = sorter.metrics(); // deterministic; last is fine
    }

    report.timing = stratum::stats::computeRepetitionStats(std::move(samplesMs));
    return report;
}

// Prints every field requested in the research brief for a single report.
inline void printFullReport(const BenchmarkReport& report, const SystemInfo& sysInfo,
                             std::ostream& os) {
    os << "=== " << report.label << " (n=" << report.n << ") ===\n";
    os << "-- System / build --\n";
    sysInfo.print(os);
    os << "-- Timing (" << report.timing.repetitions << " repetitions) --\n";
    os << "  Min:    " << std::fixed << std::setprecision(3) << report.timing.minMs << " ms\n";
    os << "  Max:    " << std::fixed << std::setprecision(3) << report.timing.maxMs << " ms\n";
    os << "  Mean:   " << std::fixed << std::setprecision(3) << report.timing.meanMs << " ms\n";
    os << "  Median: " << std::fixed << std::setprecision(3) << report.timing.medianMs << " ms\n";
    os << "  Stddev: " << std::fixed << std::setprecision(3) << report.timing.stddevMs << " ms\n";
    os << "-- Correctness --\n";
    os << "  Sorted correctly: " << (report.correct ? "YES" : "NO") << "\n";
    os << "-- Stratum Sort internal metrics --\n";
    report.metrics.print(os);
    os << "\n";
}

// Prints a compact one-line-per-report comparison table, used to line up
// several variants or several dataset shapes side by side.
inline void printComparisonTable(const std::vector<BenchmarkReport>& reports, std::ostream& os) {
    os << std::left << std::setw(28) << "Label" << std::right << std::setw(10) << "n"
       << std::setw(12) << "Median(ms)" << std::setw(10) << "Stddev" << std::setw(6) << "OK"
       << std::setw(10) << "Bins" << std::setw(12) << "Comparisons" << std::setw(12) << "MemApprox"
       << "\n";
    os << std::string(90, '-') << "\n";
    for (const BenchmarkReport& r : reports) {
        os << std::left << std::setw(28) << r.label << std::right << std::setw(10) << r.n
           << std::setw(12) << std::fixed << std::setprecision(3) << r.timing.medianMs
           << std::setw(10) << std::fixed << std::setprecision(3) << r.timing.stddevMs
           << std::setw(6) << (r.correct ? "yes" : "NO") << std::setw(10) << r.metrics.totalBins()
           << std::setw(12) << r.metrics.comparisons() << std::setw(12)
           << r.metrics.approxMemoryBytes() << "\n";
    }
}

} // namespace stratum::bench
