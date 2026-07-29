// ============================================================
// Research experiments driver
// ============================================================
// v7 removed three mechanisms from the production algorithm (see
// ANALYSIS_v6.md and ANALYSIS_v7.md): micro-histogram splits,
// density-aware initial bins, and Difficulty-Score-based subdivision
// skipping. The sections of this file that existed only to study those
// mechanisms (microhistogram cost, predictor comparison) are removed
// along with them - there is nothing left to measure once the code is
// gone. Everything else from v5/v6 that is still relevant to a
// performance-focused DRS remains: target strategy comparison, bin size
// distribution, disorder metrics, the locality experiment, subdivision
// quality, and the version comparison (now including v6's isolated
// historical reconstruction and current v7).
#include "BenchmarkRunner.hpp"
#include "BinSizeHistogram.hpp"
#include "DisorderMetrics.hpp"
#include "drs/DynamicRangeSort.hpp"
#include "LocalityExperiment.hpp"
#include "Statistics.hpp"
#include "SubdivisionQualityAnalysis.hpp"
#include "SystemInfo.hpp"
#include "TargetStrategies.hpp"
#include "VersionComparison.hpp"
#include "DatasetGenerator.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using drs::bench::BenchmarkReport;
using drs::bench::printComparisonTable;
using drs::bench::runBenchmark;
using drs::testing::DataVector;
using drs::testing::DatasetGenerator;

namespace {

constexpr std::size_t kRepetitions = 3;
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

uint64_t observedRange(const DataVector& data) {
    if (data.empty()) return 1;
    const auto [minIt, maxIt] = std::minmax_element(data.begin(), data.end());
    return static_cast<uint64_t>(*maxIt) - static_cast<uint64_t>(*minIt) + 1ULL;
}

// ---- Target strategy comparison, all datasets --------------------------------
void runFullTargetStudy(const std::vector<std::size_t>& sizes) {
    std::cout << "\n================ TARGET: estudio completo (todos los datasets) "
                 "================\n";
    DatasetGenerator gen;
    const auto strategies = drs::experimental::allTargetStrategies();

    for (std::size_t n : sizes) {
        for (const NamedDataset& ds : buildDatasets(gen, n)) {
            const uint64_t range = observedRange(ds.data);
            std::cout << "-- " << ds.name << ", n=" << n << " (range=" << range << ") --\n";
            std::vector<BenchmarkReport> reports;
            for (const auto& strategy : strategies) {
                const std::size_t target = strategy.compute(n, range);
                std::string label = strategy.name + " (t=" + std::to_string(target) + ")";
                reports.push_back(runBenchmark(label, ds.data, kRepetitions, target));
            }
            printComparisonTable(reports, std::cout);
        }
    }
}

// ---- Bin size histogram --------------------------------------------------------
void runBinSizeHistogramStudy(std::size_t n) {
    std::cout << "\n================ Histograma de tamanos de bins (n=" << n
              << ") ================\n";
    DatasetGenerator gen;
    for (const NamedDataset& ds : buildDatasets(gen, n)) {
        std::cout << "--- " << ds.name << " ---\n";
        drs::DynamicRangeSort<int64_t> sorter(kBaselineTarget);
        std::vector<int64_t> data = ds.data;
        sorter.sort(data);
        drs::experimental::printBinSizeHistogram(sorter.metrics().binSizes(), std::cout);
        std::cout << "\n";
    }
}

// ---- Disorder metrics ------------------------------------------------------------
void runDisorderMetricsStudy(std::size_t n) {
    std::cout << "\n================ Metricas de desorden (n=" << n << ") ================\n";
    DatasetGenerator gen;
    for (const NamedDataset& ds : buildDatasets(gen, n)) {
        std::cout << "--- " << ds.name << " ---\n";
        drs::DynamicRangeSort<int64_t> sorter(kBaselineTarget);
        const auto stats = drs::experimental::measureDisorder(sorter, ds.data);
        drs::experimental::printDisorderStats(stats, std::cout);
        std::cout << "\n";
    }
}

// ---- Locality experiment --------------------------------------------------------
void runLocalityExperiment(const std::vector<std::size_t>& sizes) {
    std::cout << "\n================ Estudio de localidad (distribute() por bloques) "
                 "================\n";
    DatasetGenerator gen;
    const std::vector<std::size_t> blockSizes = {4, 8, 16, 32, 64};
    constexpr std::size_t kLocalityReps = 9;

    std::cout << std::left << std::setw(12) << "n" << std::setw(16) << "unblocked(ms)";
    for (std::size_t b : blockSizes) std::cout << std::setw(16) << ("block=" + std::to_string(b));
    std::cout << "\n" << std::string(12 + 16 + 16 * blockSizes.size(), '-') << "\n";

    for (std::size_t n : sizes) {
        DataVector data = gen.randomUniform(n);
        const int64_t minV = *std::min_element(data.begin(), data.end());
        const int64_t maxV = *std::max_element(data.begin(), data.end());
        const uint64_t range = static_cast<uint64_t>(maxV) - static_cast<uint64_t>(minV) + 1ULL;
        const std::size_t numBuckets = std::min<std::size_t>((n / 64) + 1, range);
        const int64_t intervalSize =
            static_cast<int64_t>((range + numBuckets - 1) / numBuckets == 0
                                      ? 1
                                      : (range + numBuckets - 1) / numBuckets);

        std::vector<int64_t> dst;
        const double unblocked = drs::experimental::medianTimeMs(
            [&]() { drs::experimental::distributeUnblocked(data, minV, intervalSize, numBuckets, dst); },
            kLocalityReps);

        std::cout << std::left << std::setw(12) << n << std::setw(16) << std::fixed
                  << std::setprecision(3) << unblocked;

        for (std::size_t blockSize : blockSizes) {
            const double blocked = drs::experimental::medianTimeMs(
                [&]() {
                    drs::experimental::distributeBlocked(data, minV, intervalSize, numBuckets, blockSize,
                                                          dst);
                },
                kLocalityReps);
            std::cout << std::setw(16) << std::fixed << std::setprecision(3) << blocked;
        }
        std::cout << "\n";
    }
    std::cout << "\n(Solo se comparan tiempos; no se extraen conclusiones sin datos "
                 "adicionales.)\n";
}

// ---- Version comparison -----------------------------------------------------------
void runVersionComparison(const std::vector<std::size_t>& sizes) {
    std::cout << "\n================ Comparacion con versiones anteriores de DRS "
                 "================\n";
    DatasetGenerator gen;
    constexpr std::size_t kVersionReps = 7;

    for (std::size_t n : sizes) {
        DataVector data = gen.randomUniform(n);
        std::cout << "-- RandomUniform, n=" << n << " --\n";
        const auto results = drs::experimental::compareAllVersions(data, kVersionReps);
        drs::experimental::printVersionComparison(results, std::cout);
    }
}

// ---- Subdivision quality, work-by-depth, correlations ------------------------------
void runSubdivisionQualityStudy(std::size_t n) {
    std::cout << "\n================ Calidad de subdivision y correlaciones (n=" << n
              << ") ================\n";
    DatasetGenerator gen;
    for (const NamedDataset& ds : buildDatasets(gen, n)) {
        std::cout << "--- " << ds.name << " ---\n";
        drs::DynamicRangeSort<int64_t> sorter(kBaselineTarget);
        std::vector<int64_t> data = ds.data;
        sorter.sort(data);
        drs::experimental::printSubdivisionQualityReport(sorter.metrics(), std::cout);
        drs::experimental::printWorkByDepthReport(sorter.metrics(), std::cout);
        drs::experimental::printCorrelationAnalysis(sorter.metrics(), std::cout);
        std::cout << "\n";
    }
}

} // namespace

int main() {
    const std::vector<std::size_t> targetStudySizes = {1000, 10000, 100000, 1000000};
    const std::vector<std::size_t> localitySizes = {10000, 100000, 1000000};
    const std::vector<std::size_t> versionSizes = {1000, 10000, 100000, 1000000};
    constexpr std::size_t kHistogramAndDisorderSize = 1000000;

    runFullTargetStudy(targetStudySizes);
    runBinSizeHistogramStudy(kHistogramAndDisorderSize);
    runDisorderMetricsStudy(kHistogramAndDisorderSize);
    runLocalityExperiment(localitySizes);
    runVersionComparison(versionSizes);
    runSubdivisionQualityStudy(kHistogramAndDisorderSize);

    return 0;
}
