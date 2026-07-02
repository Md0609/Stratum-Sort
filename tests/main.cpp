#include "../include/DynamicRangeSort.hpp"
#include "DatasetGenerator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using drs::DynamicRangeSort;
using drs::testing::DataVector;
using drs::testing::DatasetGenerator;

namespace {

struct BenchmarkResult {
    std::string datasetName;
    std::size_t n = 0;
    double drsTimeMs = 0.0;
    double stdSortTimeMs = 0.0;
    double stableSortTimeMs = 0.0;
    bool correct = false;
    std::size_t totalBins = 0;
    std::size_t emptyBins = 0;
    std::size_t subdivisions = 0;
    double avgBinSize = 0.0;
    std::size_t maxBinSize = 0;
    std::size_t comparisons = 0;
};

BenchmarkResult runBenchmark(const std::string& name, const DataVector& data) {
    DataVector forStdSort = data;
    DataVector forStableSort = data;
    DataVector forDrs = data;

    DynamicRangeSort<int64_t> sorter;

    const auto t0 = std::chrono::steady_clock::now();
    sorter.sort(forDrs);
    const auto t1 = std::chrono::steady_clock::now();

    const auto t2 = std::chrono::steady_clock::now();
    std::sort(forStdSort.begin(), forStdSort.end());
    const auto t3 = std::chrono::steady_clock::now();

    const auto t4 = std::chrono::steady_clock::now();
    std::stable_sort(forStableSort.begin(), forStableSort.end());
    const auto t5 = std::chrono::steady_clock::now();

    BenchmarkResult result;
    result.datasetName = name;
    result.n = data.size();
    result.drsTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    result.stdSortTimeMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
    result.stableSortTimeMs = std::chrono::duration<double, std::milli>(t5 - t4).count();
    result.correct = (forDrs == forStdSort) && (forDrs == forStableSort);
    result.totalBins = sorter.metrics().totalBins();
    result.emptyBins = sorter.metrics().emptyBins();
    result.subdivisions = sorter.metrics().subdivisions();
    result.avgBinSize = sorter.metrics().averageBinSize();
    result.maxBinSize = sorter.metrics().maxBinSize();
    result.comparisons = sorter.metrics().comparisons();
    return result;
}

void printHeader() {
    std::cout << std::left << std::setw(20) << "Dataset" << std::right << std::setw(10) << "n"
              << std::setw(13) << "DRS(ms)" << std::setw(15) << "std::sort(ms)" << std::setw(16)
              << "stable_sort(ms)" << std::setw(6) << "OK" << std::setw(9) << "Bins"
              << std::setw(9) << "Empty" << std::setw(9) << "Subdiv" << std::setw(11) << "AvgBin"
              << std::setw(9) << "MaxBin" << std::setw(15) << "Comparisons" << "\n";
    std::cout << std::string(142, '-') << "\n";
}

void printRow(const BenchmarkResult& r) {
    std::cout << std::left << std::setw(20) << r.datasetName << std::right << std::setw(10)
              << r.n << std::setw(13) << std::fixed << std::setprecision(3) << r.drsTimeMs
              << std::setw(15) << std::fixed << std::setprecision(3) << r.stdSortTimeMs
              << std::setw(16) << std::fixed << std::setprecision(3) << r.stableSortTimeMs
              << std::setw(6) << (r.correct ? "yes" : "NO") << std::setw(9) << r.totalBins
              << std::setw(9) << r.emptyBins << std::setw(9) << r.subdivisions << std::setw(11)
              << std::fixed << std::setprecision(2) << r.avgBinSize << std::setw(9)
              << r.maxBinSize << std::setw(15) << r.comparisons << "\n";
}

} // namespace

int main() {
    const std::vector<std::size_t> sizes = {100, 1000, 10000, 100000, 1000000};
    DatasetGenerator gen;

    std::vector<BenchmarkResult> allResults;
    printHeader();

    for (std::size_t n : sizes) {
        struct NamedDataset {
            std::string name;
            DataVector data;
        };

        std::vector<NamedDataset> datasets;
        datasets.push_back({"RandomUniform", gen.randomUniform(n)});
        datasets.push_back({"SortedAscending", gen.sortedAscending(n)});
        datasets.push_back({"SortedDescending", gen.sortedDescending(n)});
        datasets.push_back({"ManyRepeated", gen.manyRepeated(n)});
        datasets.push_back({"NormalGaussian", gen.normalDistribution(n)});
        datasets.push_back({"Concentrated", gen.concentrated(n)});
        datasets.push_back({"SmallRangeManyEl", gen.smallRangeManyElements(n)});
        datasets.push_back({"HugeRangeFewEl", gen.hugeRangeFewElements(n)});

        for (const auto& ds : datasets) {
            const BenchmarkResult r = runBenchmark(ds.name, ds.data);
            printRow(r);
            allResults.push_back(r);
        }
    }

    std::cout << "\n=== Empirical scaling on RandomUniform ===\n";
    std::cout << std::left << std::setw(12) << "n" << std::setw(14) << "time(ms)"
              << std::setw(18) << "time/n" << std::setw(18) << "time/(n log2 n)" << "\n";
    for (const auto& r : allResults) {
        if (r.datasetName != "RandomUniform") continue;
        const double logn = std::log2(static_cast<double>(r.n));
        std::cout << std::left << std::setw(12) << r.n << std::setw(14) << std::fixed
                  << std::setprecision(3) << r.drsTimeMs << std::setw(18) << std::scientific
                  << std::setprecision(3) << (r.drsTimeMs / static_cast<double>(r.n))
                  << std::setw(18) << std::scientific << std::setprecision(3)
                  << (r.drsTimeMs / (static_cast<double>(r.n) * logn)) << "\n";
    }

    const bool allCorrect = std::all_of(allResults.begin(), allResults.end(),
                                         [](const BenchmarkResult& r) { return r.correct; });
    std::cout << "\nAll results correctly sorted: " << (allCorrect ? "YES" : "NO") << "\n";

    return allCorrect ? 0 : 1;
}
