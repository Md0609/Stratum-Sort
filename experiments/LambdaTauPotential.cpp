// ============================================================
// Predicted effect of separating lambda from t, measured before
// ============================================================
// Intento de FALSACION. La hipotesis de CC-F dice que con lambda=32 y
// t=64 la fase refine() se vacia, porque P(Poisson(32) > 64) ~ 1e-8.
// Eso es solo la mitad de la historia: al subir el umbral de hoja de 32 a
// 64, las hojas dejan de partirse y CRECEN, y el coste de Insertion Sort
// es cuadratico en el tamano de hoja.
//
// Este programa replica exactamente la particion de nivel 0 que haria
// DRS con un lambda dado -las mismas formulas de computeRangeParameters,
// con aritmetica de span- y cuenta la ocupacion real de cada bin. De ahi
// salen, SIN ejecutar el algoritmo:
//
//   - cuantos bins superarian el umbral t (o sea, cuanto refine() queda)
//   - sum(k-1)      -> comparaciones de detectRun
//   - sum(k(k-1)/4) -> comparaciones esperadas de Insertion Sort
//
// Comparando (lambda=t=64, la configuracion actual) contra
// (lambda=32, t=64, la propuesta) se puede predecir el signo del cambio
// antes de implementarlo.
#include "DatasetGenerator.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

using drs::testing::DataVector;
using drs::testing::DatasetGenerator;

namespace {
constexpr std::size_t kN = 1000000;

struct Stats {
    std::size_t bins = 0;
    std::size_t binsOverT = 0;
    std::size_t elemsOverT = 0;
    double detectRunCmp = 0;
    double insertionCmp = 0;
};

// Replica exacta de computeRangeParameters() + computeBinIndex() de v9.
Stats occupancy(const DataVector& v, std::size_t lambda, std::size_t t) {
    int64_t mn = v[0], mx = v[0];
    for (int64_t x : v) { if (x < mn) mn = x; if (x > mx) mx = x; }
    const uint64_t span = static_cast<uint64_t>(mx) - static_cast<uint64_t>(mn);

    std::size_t bins = (v.size() + lambda - 1) / lambda;
    if (bins == 0) bins = 1;
    if (span < static_cast<uint64_t>(bins)) bins = static_cast<std::size_t>(span + 1ULL);
    const uint64_t W = span / static_cast<uint64_t>(bins) + 1ULL;

    std::vector<std::size_t> occ(bins, 0);
    for (int64_t x : v) ++occ[static_cast<std::size_t>((static_cast<uint64_t>(x) -
                                                         static_cast<uint64_t>(mn)) / W)];
    Stats s;
    s.bins = bins;
    for (std::size_t k : occ) {
        if (k > t) { ++s.binsOverT; s.elemsOverT += k; }
        if (k >= 2) {
            s.detectRunCmp += static_cast<double>(k - 1);
            // Insertion Sort sobre k elementos aleatorios: ~k(k-1)/4 comparaciones.
            // Solo se aplica a los bins que quedan como hoja (k <= t).
            if (k <= t) s.insertionCmp += static_cast<double>(k) * (k - 1) / 4.0;
        }
    }
    return s;
}

struct Case { const char* name; DataVector (*f)(DatasetGenerator&, std::size_t); };
DataVector a0(DatasetGenerator& g, std::size_t n) { return g.randomUniform(n); }
DataVector a1(DatasetGenerator& g, std::size_t n) { return g.normalDistribution(n); }
DataVector a2(DatasetGenerator& g, std::size_t n) { return g.hugeRangeFewElements(n); }
DataVector a3(DatasetGenerator& g, std::size_t n) { return g.fullRangeExtremes(n); }
DataVector a4(DatasetGenerator& g, std::size_t n) { return g.sortedAscending(n); }
DataVector a5(DatasetGenerator& g, std::size_t n) { return g.concentrated(n); }
DataVector a6(DatasetGenerator& g, std::size_t n) { return g.manyRepeated(n); }
DataVector a7(DatasetGenerator& g, std::size_t n) { return g.smallRangeManyElements(n); }
const Case kCases[] = {
    {"RandomUniform", a0},  {"NormalGaussian", a1}, {"HugeRangeFewEl", a2},
    {"FullRangeExtremes", a3}, {"SortedAscending", a4}, {"Concentrated", a5},
    {"ManyRepeated", a6},   {"SmallRangeManyEl", a7},
};
} // namespace

int main() {
    std::cout << "Prediccion del efecto de separar lambda de t (n=" << kN << ")\n";
    std::cout << "Actual: lambda=t=64.   Propuesta: lambda=32, t=64.\n\n";
    std::cout << std::left << std::setw(20) << "Dataset" << std::right << std::setw(10) << "config"
              << std::setw(10) << "bins" << std::setw(11) << "bins>t" << std::setw(13) << "elem>t"
              << std::setw(9) << "%refina" << std::setw(14) << "cmp insercion" << "\n";
    std::cout << std::string(87, '-') << "\n";

    for (const Case& c : kCases) {
        DatasetGenerator gen;
        const DataVector v = c.f(gen, kN);
        const Stats now = occupancy(v, 64, 64);
        const Stats prop = occupancy(v, 32, 64);
        auto row = [&](const char* tag, const Stats& s) {
            std::cout << std::left << std::setw(20) << (tag == std::string("actual") ? c.name : "")
                      << std::right << std::setw(10) << tag << std::setw(10) << s.bins
                      << std::setw(11) << s.binsOverT << std::setw(13) << s.elemsOverT
                      << std::setw(8) << std::fixed << std::setprecision(1)
                      << 100.0 * static_cast<double>(s.elemsOverT) / kN << "%" << std::setw(14)
                      << std::setprecision(0) << s.insertionCmp << "\n";
        };
        row("actual", now);
        row("lambda=32", prop);
        const double dc = prop.insertionCmp - now.insertionCmp;
        std::cout << std::left << std::setw(20) << "" << std::right << std::setw(10) << "->"
                  << std::setw(34) << "" << std::setw(9) << "" << std::setw(13)
                  << std::showpos << std::setprecision(0) << dc << std::noshowpos
                  << "  comparaciones de insercion\n";
    }
    return 0;
}
