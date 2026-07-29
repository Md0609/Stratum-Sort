// ============================================================
//
// ============================================================
// The baseline benchmark battery over a wide, dense set of input sizes,
// plus (section 9 of the v7 brief) a direct comparison against
// std::sort and std::stable_sort on every standard dataset shape.
#include "BenchmarkRunner.hpp"
#include "drs/DynamicRangeSort.hpp"
#include "Statistics.hpp"
#include "SystemInfo.hpp"
#include "DatasetGenerator.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>

using drs::SystemInfo;
using drs::bench::BenchmarkReport;
using drs::bench::printComparisonTable;
using drs::bench::runBenchmark;
using drs::testing::DataVector;
using drs::testing::DatasetGenerator;

namespace {

constexpr std::size_t kRepetitions = 5;
constexpr std::size_t kBaselineTarget = 64;

struct NamedDataset {
    std::string name;
    DataVector data;
};

std::vector<NamedDataset> buildDatasets(DatasetGenerator& gen, std::size_t n) {
    std::vector<NamedDataset> datasets;
    datasets.push_back({"RandomUniform", gen.randomUniform(n)});
    datasets.push_back({"SortedAscending", gen.sortedAscending(n)});
    datasets.push_back({"SortedDescending", gen.sortedDescending(n)});
    datasets.push_back({"ManyRepeated", gen.manyRepeated(n)});
    datasets.push_back({"NormalGaussian", gen.normalDistribution(n)});
    datasets.push_back({"Concentrated", gen.concentrated(n)});
    datasets.push_back({"SmallRangeManyEl", gen.smallRangeManyElements(n)});
    datasets.push_back({"HugeRangeFewEl", gen.hugeRangeFewElements(n)});
    return datasets;
}

double medianStdSortMs(const DataVector& base, std::size_t reps) {
    std::vector<double> samples;
    for (std::size_t r = 0; r < reps; ++r) {
        DataVector data = base;
        const auto t0 = std::chrono::steady_clock::now();
        std::sort(data.begin(), data.end());
        const auto t1 = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

double medianStableSortMs(const DataVector& base, std::size_t reps) {
    std::vector<double> samples;
    for (std::size_t r = 0; r < reps; ++r) {
        DataVector data = base;
        const auto t0 = std::chrono::steady_clock::now();
        std::stable_sort(data.begin(), data.end());
        const auto t1 = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

// ---- Section 9: direct comparison against std::sort / std::stable_sort ----
void runStdSortComparison(const std::vector<std::size_t>& sizes) {
    std::cout << "\n================ DRS vs std::sort vs std::stable_sort "
                 "================\n";
    DatasetGenerator gen;
    for (std::size_t n : sizes) {
        std::cout << "-- n=" << n << " --\n";
        std::cout << std::left << std::setw(20) << "Dataset" << std::right << std::setw(14)
                  << "DRS(ms)" << std::setw(16) << "std::sort(ms)" << std::setw(18)
                  << "stable_sort(ms)" << std::setw(14) << "DRS vs sort" << "\n";
        std::cout << std::string(82, '-') << "\n";
        for (const NamedDataset& ds : buildDatasets(gen, n)) {
            const auto drsReport = runBenchmark(ds.name, ds.data, kRepetitions, kBaselineTarget);
            const double stdSortMs = medianStdSortMs(ds.data, kRepetitions);
            const double stableSortMs = medianStableSortMs(ds.data, kRepetitions);
            const double ratio = stdSortMs > 0.0 ? drsReport.timing.medianMs / stdSortMs : 0.0;

            std::cout << std::left << std::setw(20) << ds.name << std::right << std::setw(14)
                      << std::fixed << std::setprecision(3) << drsReport.timing.medianMs
                      << std::setw(16) << std::fixed << std::setprecision(3) << stdSortMs
                      << std::setw(18) << std::fixed << std::setprecision(3) << stableSortMs
                      << std::setw(13) << std::fixed << std::setprecision(2) << ratio << "x\n";
        }
    }
}

} // namespace

int main() {
    const SystemInfo sysInfo = SystemInfo::collect();
    std::cout << "================ Informacion del sistema ================\n";
    sysInfo.print(std::cout);

    // Section 6: wider, denser size list than v4's five points.
    const std::vector<std::size_t> sizes = {100,    500,    1000,   2500,    5000,    10000,  25000,
                                             50000,  100000, 250000, 500000,  1000000, 2000000, 5000000};

    std::cout << "\n================ Bateria de benchmarks (tamanos ampliados) "
                 "================\n";

    DatasetGenerator gen;
    std::vector<BenchmarkReport> reports;
    for (std::size_t n : sizes) {
        const std::vector<NamedDataset> datasets = buildDatasets(gen, n);
        std::vector<BenchmarkReport> sizeReports;
        for (const NamedDataset& ds : datasets) {
            sizeReports.push_back(runBenchmark(ds.name, ds.data, kRepetitions, kBaselineTarget));
        }
        printComparisonTable(sizeReports, std::cout);
        reports.insert(reports.end(), sizeReports.begin(), sizeReports.end());
    }

    const bool allCorrect =
        std::all_of(reports.begin(), reports.end(), [](const BenchmarkReport& r) { return r.correct; });
    std::cout << "\nAll benchmark results correctly sorted: " << (allCorrect ? "YES" : "NO") << "\n";

    runStdSortComparison({1000, 10000, 100000, 1000000});

    return allCorrect ? 0 : 1;
}
