// ============================================================
// DRS v7 - benchmarks/ProductionVsResearch.cpp
// ============================================================
// This single source file is compiled TWICE by the Makefile - once
// without DRS_ENABLE_METRICS (build/drs_overhead_production) and once
// with it (build/drs_overhead_research) - so the only difference between
// the two binaries is exactly the macro this project uses to separate
// production from research. Comparing their output directly answers
// section 3 of the research brief: how much do all the metrics actually
// cost.
#include "DynamicRangeSort.hpp"
#include "DatasetGenerator.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

using drs::DynamicRangeSort;
using drs::testing::DatasetGenerator;

namespace {

constexpr std::size_t kRepetitions = 9;

double medianTimeMs(const std::vector<int64_t>& base) {
    std::vector<double> samples;
    samples.reserve(kRepetitions);
    for (std::size_t r = 0; r < kRepetitions; ++r) {
        std::vector<int64_t> data = base;
        DynamicRangeSort<int64_t> sorter;
        const auto t0 = std::chrono::steady_clock::now();
        sorter.sort(data);
        const auto t1 = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

} // namespace

int main() {
#ifdef DRS_ENABLE_METRICS
    std::cout << "=== Build: RESEARCH (DRS_ENABLE_METRICS defined) ===\n";
#else
    std::cout << "=== Build: PRODUCTION (no instrumentation compiled in) ===\n";
#endif

    std::cout << "sizeof(DynamicRangeSort<int64_t>) = " << sizeof(DynamicRangeSort<int64_t>)
              << " bytes\n\n";

    DatasetGenerator gen;
    const std::vector<std::size_t> sizes = {1000, 10000, 100000, 1000000};

    std::cout << "n            median(ms)\n";
    for (std::size_t n : sizes) {
        const auto data = gen.randomUniform(n);
        const double ms = medianTimeMs(data);
        std::cout << n << "            " << ms << "\n";
    }

#ifdef DRS_ENABLE_METRICS
    // Extra memory the research build accumulates DURING a sort() call
    // that production never allocates at all (subdivision records and
    // per-bin sizes growing inside DRSMetrics).
    const auto data = gen.randomUniform(1000000);
    std::vector<int64_t> copy = data;
    DynamicRangeSort<int64_t> sorter;
    sorter.sort(copy);
    const std::size_t metricsBytes =
        sorter.metrics().subdivisionRecords().size() * sizeof(drs::DRSMetrics::SubdivisionRecord) +
        sorter.metrics().binSizes().size() * sizeof(std::size_t);
    std::cout << "\nMemoria adicional acumulada en DRSMetrics durante sort() (n=1,000,000): "
              << metricsBytes << " bytes (" << sorter.metrics().subdivisionRecords().size()
              << " subdivision records, " << sorter.metrics().binSizes().size() << " bin sizes)\n";
#endif

    return 0;
}
