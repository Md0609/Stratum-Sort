// ============================================================
// Resolucion de la observacion O8 (STEP3_terminal_refinement.md)
// ============================================================
// El paso 3 midio +111% en AdversarialPeeling al quitar el tope de
// profundidad. Pero ese dataset usa grupos de ~107 elementos, que con
// target=64 dan splits=2, es decir UN bit de span consumido por nivel: el
// regimen que maximiza el numero de niveles degenerados y por tanto el
// peor caso para la version SIN capar.
//
// El regimen contrario -nucleo grande, splits grande, pocos bits
// disponibles por nivel- es el que produce el termino Theta(n log n) que
// el tope provocaba: pocos niveles degenerados y un residuo ENORME
// entregado a Introsort. Ese regimen no estaba medido.
//
// Este programa barre el tamano de nucleo y compara las dos versiones.
// Se compila DOS VECES, contra el algoritmo capado y contra el sin capar,
// y los dos binarios se ejecutan alternados desde fuera.
#include "BenchmarkRunner.hpp"
#include "DatasetGenerator.hpp"
#include "DynamicRangeSort.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using drs::bench::runBenchmark;
using drs::testing::DataVector;
using drs::testing::DatasetGenerator;

namespace {
constexpr std::size_t kN = 1000000;
constexpr std::size_t kTarget = 64;
constexpr std::size_t kReps = 5;

std::size_t usageOf(const drs::DRSMetrics& m, const std::string& a) {
    const auto& u = m.algorithmUsage();
    const auto it = u.find(a);
    return it == u.end() ? 0 : it->second;
}
} // namespace

int main(int argc, char** argv) {
    const std::string tag = argc > 1 ? argv[1] : "?";

    std::cout << "# variante=" << tag << "  n=" << kN << "  target=" << kTarget
              << "  reps=" << kReps << "\n";
    std::cout << "# core tiempo_ms prof maxHoja intro quick comparaciones bins ok\n";

    for (std::size_t core : {64u, 128u, 256u, 1024u, 4096u, 16384u, 65536u}) {
        DatasetGenerator gen;
        const DataVector data = gen.adversarialPeeling(kN, kTarget, core);
        const auto rep = runBenchmark("adv", data, kReps, kTarget);
        const drs::DRSMetrics& m = rep.metrics;
        std::cout << core << " " << std::fixed << std::setprecision(3) << rep.timing.medianMs << " "
                  << m.maxSubdivisionDepth() << " " << m.maxBinSize() << " "
                  << usageOf(m, "Introsort") << " " << usageOf(m, "QuickSort") << " "
                  << m.comparisons() << " " << m.totalBins() << " " << (rep.correct ? 1 : 0)
                  << "\n";
        std::cout.flush();
    }
    return 0;
}
