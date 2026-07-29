// ============================================================
// Release-configuration timings against std::sort
// ============================================================
// This is the ONLY tool in the project whose timings may be quoted.
//
// `make baseline` cannot be: BenchmarkRunner.hpp reads sorter.metrics(),
// so it hard-requires DRS_ENABLE_METRICS, and a research binary carries
// counters, timers and per-bin bookkeeping that a caller never pays for.
// The distortion is not uniform - it lands on whichever phase happens to
// increment counters most - so a research table cannot be corrected by
// scaling it either. This project has already published one number that
// was an artefact of exactly that mistake.
//
// So: same datasets, same repetition count, same defaults as the baseline
// tool, no instrumentation, and no counters printed - because it does not
// have any. What it loses in explanatory power it gains in being true.
//
// DRS and std::sort are alternated within each repetition, on identical
// copies of the same input, so drift in machine state hits both equally.
#include "DatasetGenerator.hpp"
#include "SystemInfo.hpp"
#include "drs/DynamicRangeSort.hpp"

#ifdef DRS_ENABLE_METRICS
#error "ReleaseTimings must be built WITHOUT metrics; its whole purpose is to time the shipped configuration."
#endif

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using drs::testing::DataVector;
using drs::testing::DatasetGenerator;

namespace {

constexpr std::size_t kRepetitions = 9;

double medianOf(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}

struct Case {
    std::string name;
    DataVector (*build)(DatasetGenerator&, std::size_t);
};

// Identical to the dataset list in ReferenceBaseline.cpp. The adversary is
// built against the shipped lambda on purpose: an adversary built for a
// different lambda is not an adversary, it is just another random input.
DataVector mkRandomUniform(DatasetGenerator& g, std::size_t n) { return g.randomUniform(n); }
DataVector mkSortedAsc(DatasetGenerator& g, std::size_t n) { return g.sortedAscending(n); }
DataVector mkSortedDesc(DatasetGenerator& g, std::size_t n) { return g.sortedDescending(n); }
DataVector mkManyRepeated(DatasetGenerator& g, std::size_t n) { return g.manyRepeated(n); }
DataVector mkNormal(DatasetGenerator& g, std::size_t n) { return g.normalDistribution(n); }
DataVector mkConcentrated(DatasetGenerator& g, std::size_t n) { return g.concentrated(n); }
DataVector mkSmallRange(DatasetGenerator& g, std::size_t n) { return g.smallRangeManyElements(n); }
DataVector mkHugeRange(DatasetGenerator& g, std::size_t n) { return g.hugeRangeFewElements(n); }
DataVector mkFullRange(DatasetGenerator& g, std::size_t n) { return g.fullRangeExtremes(n); }
DataVector mkAdversarial(DatasetGenerator& g, std::size_t n) {
    return g.adversarialPeeling(n, drs::DEFAULT_TARGET_ELEMENTS_PER_BIN);
}

} // namespace

int main() {
    const std::vector<Case> cases = {
        {"RandomUniform", mkRandomUniform},     {"SortedAscending", mkSortedAsc},
        {"SortedDescending", mkSortedDesc},     {"ManyRepeated", mkManyRepeated},
        {"NormalGaussian", mkNormal},           {"Concentrated", mkConcentrated},
        {"SmallRangeManyEl", mkSmallRange},     {"HugeRangeFewEl", mkHugeRange},
        {"FullRangeExtremes", mkFullRange},     {"AdversarialPeeling", mkAdversarial},
    };

    std::cout << "================================================================\n"
              << " Dynamic Range Sort - RELEASE timings (no instrumentation)\n"
              << "================================================================\n";
    drs::SystemInfo::collect().print(std::cout);
    std::cout << "Repetitions per point: " << kRepetitions << "   lambda/t = defaults\n";

    for (const std::size_t n : {std::size_t{100000}, std::size_t{1000000}}) {
        std::cout << "\n---------------- n = " << n << " ----------------\n";
        std::cout << std::left << std::setw(22) << "Dataset" << std::right << std::setw(10)
                  << "DRS(ms)" << std::setw(10) << "std(ms)" << std::setw(9) << "ratio"
                  << std::setw(6) << "OK" << "\n";
        std::cout << std::string(57, '-') << "\n";

        for (const Case& c : cases) {
            DatasetGenerator gen(42);
            const DataVector input = c.build(gen, n);

            DataVector reference = input;
            std::sort(reference.begin(), reference.end());

            std::vector<double> drsMs, stdMs;
            bool correct = true;
            for (std::size_t r = 0; r < kRepetitions; ++r) {
                {
                    DataVector data = input;
                    drs::DynamicRangeSort<int64_t> sorter;
                    const auto t0 = std::chrono::steady_clock::now();
                    sorter.sort(data);
                    const auto t1 = std::chrono::steady_clock::now();
                    drsMs.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
                    if (data != reference) correct = false;
                }
                {
                    DataVector data = input;
                    const auto t0 = std::chrono::steady_clock::now();
                    std::sort(data.begin(), data.end());
                    const auto t1 = std::chrono::steady_clock::now();
                    stdMs.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
                }
            }

            const double drsMedian = medianOf(drsMs);
            const double stdMedian = medianOf(stdMs);
            std::cout << std::left << std::setw(22) << c.name << std::right << std::setw(10)
                      << std::fixed << std::setprecision(2) << drsMedian << std::setw(10)
                      << stdMedian << std::setw(9) << std::setprecision(2)
                      << (stdMedian > 0.0 ? drsMedian / stdMedian : 0.0) << std::setw(6)
                      << (correct ? "si" : "NO") << "\n";
        }
    }
    std::cout << "\nCounters are deliberately absent: this binary has none. For the\n"
                 "deterministic counters use `make baseline`, and do not quote its times.\n";
}
