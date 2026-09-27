// ============================================================
// Reference baseline over the full dataset battery
// ============================================================
// Times Stratum Sort against std::sort on every dataset and prints the
// deterministic counters next to the times, so a change can be judged by
// the work it does and not only by the clock - the counters are exact and
// reproducible, the clock is not.
//
// It also runs two targeted checks that the eight historical datasets
// never exercised: an input spanning the entire key universe, and an
// adversarial input built to exhaust the refinement depth.
#include "BenchmarkRunner.hpp"
#include "DatasetGenerator.hpp"
#include "stratum/StratumSort.hpp"
#include "SystemInfo.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using stratum::StratumSort;
using stratum::bench::runBenchmark;
using stratum::testing::DataVector;
using stratum::testing::DatasetGenerator;

namespace {

constexpr std::size_t kRepetitions = 7;
// 0 means "the shipped defaults": a baseline report must measure what
// the library actually ships, not a configuration chosen by the tool.
constexpr std::size_t kTarget = 0;

struct Case {
    std::string name;
    DataVector (*build)(DatasetGenerator&, std::size_t);
};

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
    return g.adversarialPeeling(n, stratum::DEFAULT_TARGET_ELEMENTS_PER_BIN);
}

const std::vector<Case>& allCases() {
    static const std::vector<Case> cases = {
        {"RandomUniform", mkRandomUniform},     {"SortedAscending", mkSortedAsc},
        {"SortedDescending", mkSortedDesc},     {"ManyRepeated", mkManyRepeated},
        {"NormalGaussian", mkNormal},           {"Concentrated", mkConcentrated},
        {"SmallRangeManyEl", mkSmallRange},     {"HugeRangeFewEl", mkHugeRange},
        // Stress datasets: whole-universe span, and depth exhaustion.
        {"FullRangeExtremes", mkFullRange},     {"AdversarialPeeling", mkAdversarial},
    };
    return cases;
}

std::size_t usageOf(const stratum::SortMetrics& m, const std::string& algo) {
    const auto& u = m.algorithmUsage();
    const auto it = u.find(algo);
    return it == u.end() ? 0 : it->second;
}

double medianStdSortMs(const DataVector& baseline, std::size_t reps) {
    std::vector<double> samples;
    samples.reserve(reps);
    for (std::size_t r = 0; r < reps; ++r) {
        DataVector copy = baseline;
        const auto t0 = std::chrono::steady_clock::now();
        std::sort(copy.begin(), copy.end());
        const auto t1 = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

} // namespace

int main() {
    std::cout << "================================================================\n";
    std::cout << " Stratum Sort - reference baseline\n";
    std::cout << "================================================================\n";
    stratum::SystemInfo::collect().print(std::cout);
    std::cout << "Repetitions per point: " << kRepetitions << "   target=default(" << stratum::DEFAULT_TARGET_ELEMENTS_PER_BIN << "/" << stratum::DEFAULT_LEAF_THRESHOLD << ")" << "\n\n";

    const std::vector<std::size_t> sizes = {100000, 1000000};

    for (std::size_t n : sizes) {
        std::cout << "---------------- n = " << n << " ----------------\n";
        std::cout << std::left << std::setw(20) << "Dataset" << std::right << std::setw(10)
                  << "Stratum Sort(ms)" << std::setw(9) << "sd" << std::setw(10) << "std(ms)"
                  << std::setw(8) << "ratio" << std::setw(6) << "OK" << std::setw(9) << "prof"
                  << std::setw(11) << "maxHoja" << std::setw(9) << "Intro" << std::setw(9)
                  << "Quick" << std::setw(14) << "Comparac." << std::setw(11) << "bins"
                  << std::setw(11) << "vacios" << "\n";
        std::cout << std::string(137, '-') << "\n";

        for (const Case& c : allCases()) {
            DatasetGenerator gen; // semilla fija: mismo dataset en cada ejecucion
            const DataVector data = c.build(gen, n);
            const auto rep = runBenchmark(c.name, data, kRepetitions, kTarget);
            const double stdMs = medianStdSortMs(data, kRepetitions);
            const stratum::SortMetrics& m = rep.metrics;

            std::cout << std::left << std::setw(20) << c.name << std::right << std::fixed
                      << std::setprecision(2) << std::setw(10) << rep.timing.medianMs
                      << std::setw(9) << rep.timing.stddevMs << std::setw(10) << stdMs
                      << std::setw(8) << (stdMs > 0 ? rep.timing.medianMs / stdMs : 0.0)
                      << std::setw(6) << (rep.correct ? "si" : "NO") << std::setw(9)
                      << m.maxSubdivisionDepth() << std::setw(11) << m.maxBinSize()
                      << std::setw(9) << usageOf(m, "Introsort") << std::setw(9)
                      << usageOf(m, "QuickSort") << std::setw(14) << m.comparisons()
                      << std::setw(11) << m.totalBins() << std::setw(11) << m.emptyBins() << "\n";
        }
        std::cout << "\n";
    }

    // ------------------------------------------------------------------
    // Two targeted checks the eight historical datasets never exercised.
    // ------------------------------------------------------------------
    std::cout << "================================================================\n";
    std::cout << " Targeted checks\n";
    std::cout << "================================================================\n";

    const std::size_t n = 1000000;

    {
        // Comparacion PAREADA. Las dos entradas son uniformes y dispersas sobre
        // un rango enorme; la unica diferencia relevante es que la segunda tiene
        // span = 2^64-1, que hace desbordar tanto (max-min+1) como el
        // (rango + splits - 1) de la division con techo. La primera, con
        // span ~ 2^63, no desborda ninguno de los dos. Cualquier diferencia
        // sistematica entre ambas es atribuible al desbordamiento.
        struct Probe {
            std::string label;
            stratum::SortMetrics metrics;
            double medianMs = 0.0;
        };
        auto probe = [&](const std::string& label, const DataVector& data) {
            Probe p;
            p.label = label;
            const auto rep = runBenchmark(label, data, kRepetitions, kTarget);
            p.metrics = rep.metrics;
            p.medianMs = rep.timing.medianMs;
            return p;
        };

        DatasetGenerator g1, g2;
        const Probe control = probe("HugeRangeFewEl", g1.hugeRangeFewElements(n));
        const Probe test = probe("FullRangeExtremes", g2.fullRangeExtremes(n));

        std::cout << "\n[D1] Whole-universe span - paired comparison\n";
        std::cout << "     Both: uniform and sparse over a huge range, n=" << n << "\n";
        std::cout << "     Control (span ~ 2^63) vs test (span = 2^64-1)\n\n";
        std::cout << std::left << std::setw(24) << "" << std::right << std::setw(14) << "control"
                  << std::setw(14) << "test" << "\n";
        auto row = [&](const std::string& k, double a, double b) {
            std::cout << std::left << std::setw(24) << k << std::right << std::fixed
                      << std::setprecision(2) << std::setw(14) << a << std::setw(14) << b << "\n";
        };
        row("median time (ms)", control.medianMs, test.medianMs);
        row("comparisons (M)", control.metrics.comparisons() / 1e6,
            test.metrics.comparisons() / 1e6);
        row("max depth", static_cast<double>(control.metrics.maxSubdivisionDepth()),
            static_cast<double>(test.metrics.maxSubdivisionDepth()));
        row("subdivisions", static_cast<double>(control.metrics.subdivisions()),
            static_cast<double>(test.metrics.subdivisions()));
        row("largest leaf", static_cast<double>(control.metrics.maxBinSize()),
            static_cast<double>(test.metrics.maxBinSize()));

        std::cout << "\n     elements reprocessed per level (workByDepth):\n";
        auto printWork = [&](const Probe& p) {
            std::cout << "       " << std::left << std::setw(20) << p.label << std::right;
            const auto& w = p.metrics.workByDepth();
            for (std::size_t d = 0; d < w.size(); ++d) {
                std::cout << "  d" << d << "=" << w[d];
            }
            std::cout << "\n";
        };
        printWork(control);
        printWork(test);

        // Predicado del defecto: el desbordamiento NO produce una hoja gigante
        // (la revision demostro que el colapso no puede encadenarse). Lo que
        // produce son niveles degenerados de mas: profundidad estrictamente
        // mayor que el control y tiempo sensiblemente peor, a igualdad de
        // comparaciones. Sin desbordamiento, ambas entradas deben comportarse
        // igual porque tienen la misma forma.
        const bool deeper =
            test.metrics.maxSubdivisionDepth() > control.metrics.maxSubdivisionDepth();
        const bool slower = test.medianMs > control.medianMs * 1.15;
        const double pct =
            control.medianMs > 0 ? (test.medianMs / control.medianMs - 1.0) * 100.0 : 0.0;
        std::cout << "\n     => DEFECT " << ((deeper && slower) ? "PRESENTE" : "AUSENTE") << ": "
                  << std::setprecision(0) << (pct >= 0 ? "+" : "") << pct
                  << "% de tiempo y profundidad " << test.metrics.maxSubdivisionDepth() << " vs "
                  << control.metrics.maxSubdivisionDepth() << " del control.\n";
        std::cout << "        PRESENT  => the range is held as max-min+1 and overflows:\n";
        std::cout << "                    el nivel 0 colapsa a un solo bin y un nivel de\n";
        std::cout << "                    refine() degenera (~2 pasadas O(n) desperdiciadas).\n";
        std::cout << "        ABSENT   => span arithmetic in force.\n";
    }

    {
        DatasetGenerator gen;
        const DataVector data = gen.adversarialPeeling(n, kTarget);
        StratumSort<int64_t> sorter;
        DataVector copy = data;
        sorter.sort(copy);
        const stratum::SortMetrics& m = sorter.metrics();
        const bool capped = m.maxSubdivisionDepth() >= stratum::MAX_SUBDIVISION_DEPTH;
        const bool fellBack = usageOf(m, "Introsort") + usageOf(m, "QuickSort") > 0;
        std::cout << "\n[D2] Depth exhaustion\n";
        std::cout << "     dataset AdversarialPeeling, n=" << n << ", target=" << kTarget << "\n";
        std::cout << "     total bins:          " << m.totalBins() << "\n";
        std::cout << "     subdivisions:       " << m.subdivisions() << "\n";
        std::cout << "     largest leaf:     " << m.maxBinSize() << "\n";
        std::cout << "     max depth:  " << m.maxSubdivisionDepth() << "  (tope="
                  << stratum::MAX_SUBDIVISION_DEPTH << ")\n";
        std::cout << "     leaves to Introsort:   " << usageOf(m, "Introsort") << "\n";
        std::cout << "     leaves to QuickSort:   " << usageOf(m, "QuickSort") << "\n";
        std::cout << "     sorted correctly:     " << (std::is_sorted(copy.begin(), copy.end()) ? "si" : "NO")
                  << "\n";
        std::cout << "     => depth cap " << (capped ? "REACHED" : "not reached")
                  << "; fell back to a comparison sort: " << (fellBack ? "SI" : "no") << "\n";
    }

    std::cout << "\n";
    return 0;
}
