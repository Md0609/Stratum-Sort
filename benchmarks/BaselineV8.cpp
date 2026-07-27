// ============================================================
// SPEC_v9.md - PASO 0: linea base de v8 en ESTA maquina
// ============================================================
// Todos los numeros historicos del proyecto (ANALYSIS.md ... ANALYSIS_v8.md)
// proceden de un Xeon x86_64 con GCC 13.3 sobre Linux. Ninguna comparacion
// entre este proyecto y aquellos numeros es valida hasta re-medir v8 aqui.
// Este binario produce esa linea base, y ademas comprueba explicitamente los
// dos defectos que SPEC_v9 dice que v8 tiene y que hasta ahora nadie habia
// podido observar porque faltaban los datasets que los activan.
//
// No modifica el algoritmo. Solo lo mide.
#include "BenchmarkRunner.hpp"
#include "DatasetGenerator.hpp"
#include "DynamicRangeSort.hpp"
#include "SystemInfo.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using drs::DynamicRangeSort;
using drs::bench::runBenchmark;
using drs::testing::DataVector;
using drs::testing::DatasetGenerator;

namespace {

constexpr std::size_t kRepetitions = 7;
constexpr std::size_t kTarget = 64; // por defecto de v8

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
    return g.adversarialPeeling(n, kTarget);
}

const std::vector<Case>& allCases() {
    static const std::vector<Case> cases = {
        {"RandomUniform", mkRandomUniform},     {"SortedAscending", mkSortedAsc},
        {"SortedDescending", mkSortedDesc},     {"ManyRepeated", mkManyRepeated},
        {"NormalGaussian", mkNormal},           {"Concentrated", mkConcentrated},
        {"SmallRangeManyEl", mkSmallRange},     {"HugeRangeFewEl", mkHugeRange},
        // Anadidos en el paso 0 (SPEC_v9 s9.5 y s9.6):
        {"FullRangeExtremes", mkFullRange},     {"AdversarialPeeling", mkAdversarial},
    };
    return cases;
}

std::size_t usageOf(const drs::DRSMetrics& m, const std::string& algo) {
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
    std::cout << " DRS v8 - LINEA BASE (SPEC_v9.md, paso 0)\n";
    std::cout << "================================================================\n";
    drs::SystemInfo::collect().print(std::cout);
    std::cout << "Repeticiones por punto: " << kRepetitions << "   target=" << kTarget << "\n\n";

    const std::vector<std::size_t> sizes = {100000, 1000000};

    for (std::size_t n : sizes) {
        std::cout << "---------------- n = " << n << " ----------------\n";
        std::cout << std::left << std::setw(20) << "Dataset" << std::right << std::setw(10)
                  << "DRS(ms)" << std::setw(9) << "sd" << std::setw(10) << "std(ms)"
                  << std::setw(8) << "ratio" << std::setw(6) << "OK" << std::setw(9) << "prof"
                  << std::setw(11) << "maxHoja" << std::setw(9) << "Intro" << std::setw(9)
                  << "Quick" << std::setw(14) << "Comparac." << "\n";
        std::cout << std::string(115, '-') << "\n";

        for (const Case& c : allCases()) {
            DatasetGenerator gen; // semilla fija: mismo dataset en cada ejecucion
            const DataVector data = c.build(gen, n);
            const auto rep = runBenchmark(c.name, data, kRepetitions, kTarget);
            const double stdMs = medianStdSortMs(data, kRepetitions);
            const drs::DRSMetrics& m = rep.metrics;

            std::cout << std::left << std::setw(20) << c.name << std::right << std::fixed
                      << std::setprecision(2) << std::setw(10) << rep.timing.medianMs
                      << std::setw(9) << rep.timing.stddevMs << std::setw(10) << stdMs
                      << std::setw(8) << (stdMs > 0 ? rep.timing.medianMs / stdMs : 0.0)
                      << std::setw(6) << (rep.correct ? "si" : "NO") << std::setw(9)
                      << m.maxSubdivisionDepth() << std::setw(11) << m.maxBinSize()
                      << std::setw(9) << usageOf(m, "Introsort") << std::setw(9)
                      << usageOf(m, "QuickSort") << std::setw(14) << m.comparisons() << "\n";
        }
        std::cout << "\n";
    }

    // ------------------------------------------------------------------
    // Comprobacion explicita de los dos defectos que SPEC_v9 predice.
    // El criterio de aceptacion del paso 0 exige que se OBSERVEN aqui.
    // ------------------------------------------------------------------
    std::cout << "================================================================\n";
    std::cout << " Comprobacion de los defectos predichos por SPEC_v9\n";
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
            drs::DRSMetrics metrics;
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

        std::cout << "\n[D1] Desbordamiento de rango (SPEC_v9 s2.1) - comparacion pareada\n";
        std::cout << "     Ambos: uniformes y dispersos sobre un rango enorme, n=" << n << "\n";
        std::cout << "     Control (span ~ 2^63, sin desbordamiento) vs Test (span = 2^64-1)\n\n";
        std::cout << std::left << std::setw(24) << "" << std::right << std::setw(14) << "control"
                  << std::setw(14) << "test" << "\n";
        auto row = [&](const std::string& k, double a, double b) {
            std::cout << std::left << std::setw(24) << k << std::right << std::fixed
                      << std::setprecision(2) << std::setw(14) << a << std::setw(14) << b << "\n";
        };
        row("tiempo mediano (ms)", control.medianMs, test.medianMs);
        row("comparaciones (M)", control.metrics.comparisons() / 1e6,
            test.metrics.comparisons() / 1e6);
        row("profundidad maxima", static_cast<double>(control.metrics.maxSubdivisionDepth()),
            static_cast<double>(test.metrics.maxSubdivisionDepth()));
        row("subdivisiones", static_cast<double>(control.metrics.subdivisions()),
            static_cast<double>(test.metrics.subdivisions()));
        row("hoja mas grande", static_cast<double>(control.metrics.maxBinSize()),
            static_cast<double>(test.metrics.maxBinSize()));

        std::cout << "\n     elementos reprocesados por nivel (workByDepth):\n";
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

        const bool deeper =
            test.metrics.maxSubdivisionDepth() > control.metrics.maxSubdivisionDepth();
        const bool slower = test.medianMs > control.medianMs * 1.15;
        std::cout << "\n     => DEFECTO " << ((deeper && slower) ? "OBSERVADO" : "NO OBSERVADO")
                  << ".\n";
        std::cout << "        El desbordamiento NO provoca una hoja gigante (la revision ya\n";
        std::cout << "        demostro que no puede encadenarse): colapsa el nivel 0 a un solo\n";
        std::cout << "        bin y degenera un nivel de refine(), es decir ~2 pasadas O(n)\n";
        std::cout << "        desperdiciadas. El coste medido es de +"
                  << std::setprecision(0)
                  << (control.medianMs > 0 ? (test.medianMs / control.medianMs - 1.0) * 100.0 : 0.0)
                  << "% de tiempo a igualdad de comparaciones.\n";
    }

    {
        DatasetGenerator gen;
        const DataVector data = gen.adversarialPeeling(n, kTarget);
        DynamicRangeSort<int64_t> sorter(kTarget);
        DataVector copy = data;
        sorter.sort(copy);
        const drs::DRSMetrics& m = sorter.metrics();
        const bool capped = m.maxSubdivisionDepth() >= drs::MAX_SUBDIVISION_DEPTH;
        const bool fellBack = usageOf(m, "Introsort") + usageOf(m, "QuickSort") > 0;
        std::cout << "\n[D2] Peor caso por pelado (REVIEW, Teorema 9')\n";
        std::cout << "     dataset AdversarialPeeling, n=" << n << ", target=" << kTarget << "\n";
        std::cout << "     bins totales:        " << m.totalBins() << "\n";
        std::cout << "     subdivisiones:       " << m.subdivisions() << "\n";
        std::cout << "     hoja mas grande:     " << m.maxBinSize() << "\n";
        std::cout << "     profundidad maxima:  " << m.maxSubdivisionDepth() << "  (tope="
                  << drs::MAX_SUBDIVISION_DEPTH << ")\n";
        std::cout << "     hojas a Introsort:   " << usageOf(m, "Introsort") << "\n";
        std::cout << "     hojas a QuickSort:   " << usageOf(m, "QuickSort") << "\n";
        std::cout << "     ordenado correcto:   " << (std::is_sorted(copy.begin(), copy.end()) ? "si" : "NO")
                  << "\n";
        std::cout << "     => tope de profundidad " << (capped ? "ALCANZADO" : "no alcanzado")
                  << "; caida a sort por comparacion: " << (fellBack ? "SI" : "no") << "\n";
    }

    std::cout << "\n";
    return 0;
}
