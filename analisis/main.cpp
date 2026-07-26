// ============================================================
// DRS v5 - analisis/main.cpp
// ============================================================
// Section 7 of the research brief: refits the five complexity models
// against the expanded size list from section 6, for EVERY dataset
// shape (not just RandomUniform as in v4), reporting R^2, SSE,
// coefficients and 95% confidence intervals for each coefficient.
#include "BenchmarkRunner.hpp"
#include "DynamicRangeSort.hpp"
#include "Statistics.hpp"
#include "SystemInfo.hpp"
#include "DatasetGenerator.hpp"

#include <iostream>
#include <vector>

using drs::bench::runBenchmark;
using drs::testing::DataVector;
using drs::testing::DatasetGenerator;

namespace {

constexpr std::size_t kRepetitions = 3;
constexpr std::size_t kBaselineTarget = 64;

DataVector buildDataset(DatasetGenerator& gen, const std::string& name, std::size_t n) {
    if (name == "RandomUniform") return gen.randomUniform(n);
    if (name == "SortedAscending") return gen.sortedAscending(n);
    if (name == "SortedDescending") return gen.sortedDescending(n);
    if (name == "ManyRepeated") return gen.manyRepeated(n);
    if (name == "NormalGaussian") return gen.normalDistribution(n);
    if (name == "Concentrated") return gen.concentrated(n);
    if (name == "SmallRangeManyEl") return gen.smallRangeManyElements(n);
    return gen.hugeRangeFewElements(n);
}

} // namespace

int main() {
    const std::vector<std::string> datasetNames = {"RandomUniform",      "SortedAscending",
                                                     "SortedDescending",  "ManyRepeated",
                                                     "NormalGaussian",    "Concentrated",
                                                     "SmallRangeManyEl",  "HugeRangeFewEl"};

    const std::vector<std::size_t> sizes = {100,    500,    1000,   2500,    5000,    10000,  25000,
                                             50000,  100000, 250000, 500000,  1000000, 2000000, 5000000};

    std::cout << "================ 7. Ajuste matematico de complejidad (todos los "
                 "datasets) ================\n";
    std::cout << "Tamanos usados: ";
    for (std::size_t n : sizes) std::cout << n << " ";
    std::cout << "\nRepeticiones por punto: " << kRepetitions << "\n\n";

    DatasetGenerator gen;
    for (const std::string& name : datasetNames) {
        std::vector<drs::stats::DataPoint> points;
        for (std::size_t n : sizes) {
            const DataVector data = buildDataset(gen, name, n);
            const auto report = runBenchmark(name, data, kRepetitions, kBaselineTarget);
            points.push_back({static_cast<double>(n), report.timing.medianMs});
        }

        std::cout << "--- " << name << " ---\n";
        const auto fits = drs::stats::fitAllModels(points);
        drs::stats::printModelComparison(fits, std::cout);
        std::cout << "\n";
    }

    return 0;
}
