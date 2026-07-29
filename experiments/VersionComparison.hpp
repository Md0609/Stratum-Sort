#pragma once

#include "drs/DynamicRangeSort.hpp"
#include "legacy/DRSv1.hpp"
#include "legacy/DRSv2.hpp"
#include "legacy/DRSv3.hpp"
#include "legacy/DRSv6_experimental.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <ostream>
#include <string>
#include <vector>

namespace drs::experimental {

struct VersionResult {
    std::string version;
    double medianMs = 0.0;
    bool correct = true;
};

// Runs 'repetitions' trials of a sort function (any callable taking
// std::vector<T>& and sorting it in place) and returns the median time
// plus a correctness check against std::sort.
template <typename T, typename SortFn>
VersionResult benchmarkVersion(const std::string& label, SortFn&& sortFn, const std::vector<T>& baseline,
                                std::size_t repetitions) {
    std::vector<T> reference = baseline;
    std::sort(reference.begin(), reference.end());

    std::vector<double> samples;
    samples.reserve(repetitions);
    bool correct = true;
    for (std::size_t r = 0; r < repetitions; ++r) {
        std::vector<T> data = baseline;
        const auto t0 = std::chrono::steady_clock::now();
        sortFn(data);
        const auto t1 = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        if (data != reference) correct = false;
    }
    std::sort(samples.begin(), samples.end());

    VersionResult result;
    result.version = label;
    result.medianMs = samples[samples.size() / 2];
    result.correct = correct;
    return result;
}

template <typename T>
std::vector<VersionResult> compareAllVersions(const std::vector<T>& baseline, std::size_t repetitions) {
    std::vector<VersionResult> results;

    results.push_back(benchmarkVersion<T>(
        "v1 (subdivision unica, target=16)",
        [](std::vector<T>& d) { drs::v1::DynamicRangeSortV1<T> s; s.sort(d); }, baseline, repetitions));

    results.push_back(benchmarkVersion<T>(
        "v2 (recursivo, vector<vector>, target=16)",
        [](std::vector<T>& d) { drs::v2::DynamicRangeSortV2<T> s; s.sort(d); }, baseline, repetitions));

    results.push_back(benchmarkVersion<T>(
        "v3 (pasadas fusionadas, target=64)",
        [](std::vector<T>& d) { drs::v3::DynamicRangeSortV3<T> s; s.sort(d); }, baseline, repetitions));

    results.push_back(benchmarkVersion<T>(
        "v6 (config. por defecto, historico)",
        [](std::vector<T>& d) { drs::v6::DynamicRangeSortV6<T> s; s.sort(d); }, baseline, repetitions));

    // NOTE: this binary is compiled with DRS_ENABLE_METRICS (required by
    // BenchmarkRunner.hpp elsewhere in this program), so the v7 entry
    // below includes DRSMetrics instrumentation overhead - it is not the
    // pure production configuration. `make overhead` (benchmarks/
    // ProductionVsResearch.cpp) is the authoritative production-vs-
    // research comparison; this entry exists so v1/v2/v3/v6/v7 can be
    // seen side by side in one table, not to measure v7 in isolation.
    results.push_back(benchmarkVersion<T>(
        "v7 (este binario, con metricas activas)",
        [](std::vector<T>& d) { drs::DynamicRangeSort<T> s; s.sort(d); }, baseline, repetitions));

    return results;
}

inline void printVersionComparison(const std::vector<VersionResult>& results, std::ostream& os) {
    os << std::left << std::setw(42) << "Version" << std::right << std::setw(14) << "Median(ms)"
       << std::setw(16) << "Speedup vs v1" << std::setw(8) << "OK" << "\n";
    os << std::string(80, '-') << "\n";
    const double v1Time = results.empty() ? 1.0 : results.front().medianMs;
    for (const VersionResult& r : results) {
        const double speedup = r.medianMs > 0.0 ? v1Time / r.medianMs : 0.0;
        os << std::left << std::setw(42) << r.version << std::right << std::setw(14) << std::fixed
           << std::setprecision(3) << r.medianMs << std::setw(15) << std::fixed << std::setprecision(2)
           << speedup << "x" << std::setw(8) << (r.correct ? "yes" : "NO") << "\n";
    }
}

} // namespace drs::experimental
