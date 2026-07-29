// ============================================================
// Techo del certificado de ordenado - docs/history/STEP5_sorted_certificate.md
// ============================================================
// Un umbral numerico sobre una magnitud
// cuya procedencia no se ha medido es una adivinanza. Antes de tocar
// nada, este programa descompone de donde salen las comparaciones y
// cuantas son EXACTAMENTE eliminables por el certificado de ordenado.
//
// El certificado solo puede ayudar a las hojas que refine() reconocio
// como monovaluadas, es decir a las que llegaron a refine() con
// count > target y resultaron tener span 0. Las hojas que salen por
// count <= target NO tienen certificado, porque refine() retorna antes
// de calcular min/max.
//
// Usa debugPartitionOnly() + debugBufferA/B, la API de introspeccion de
// solo lectura que existe desde v4. No toca el algoritmo.
#include "DatasetGenerator.hpp"
#include "drs/DynamicRangeSort.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using drs::DynamicRangeSort;
using drs::testing::DataVector;
using drs::testing::DatasetGenerator;

namespace {
constexpr std::size_t kN = 1000000;
constexpr std::size_t kTarget = 64;

struct Case { const char* name; DataVector (*build)(DatasetGenerator&, std::size_t); };
DataVector b0(DatasetGenerator& g, std::size_t n) { return g.randomUniform(n); }
DataVector b1(DatasetGenerator& g, std::size_t n) { return g.sortedAscending(n); }
DataVector b2(DatasetGenerator& g, std::size_t n) { return g.sortedDescending(n); }
DataVector b3(DatasetGenerator& g, std::size_t n) { return g.manyRepeated(n); }
DataVector b4(DatasetGenerator& g, std::size_t n) { return g.normalDistribution(n); }
DataVector b5(DatasetGenerator& g, std::size_t n) { return g.concentrated(n); }
DataVector b6(DatasetGenerator& g, std::size_t n) { return g.smallRangeManyElements(n); }
DataVector b7(DatasetGenerator& g, std::size_t n) { return g.hugeRangeFewElements(n); }
DataVector b8(DatasetGenerator& g, std::size_t n) { return g.fullRangeExtremes(n); }
DataVector b9(DatasetGenerator& g, std::size_t n) { return g.adversarialPeeling(n, kTarget); }
const Case kCases[] = {
    {"RandomUniform", b0},    {"SortedAscending", b1}, {"SortedDescending", b2},
    {"ManyRepeated", b3},     {"NormalGaussian", b4},  {"Concentrated", b5},
    {"SmallRangeManyEl", b6}, {"HugeRangeFewEl", b7},  {"FullRangeExtremes", b8},
    {"AdversarialPeeling", b9},
};
} // namespace

int main() {
    std::cout << "Potencial del certificado de ordenado (n=" << kN << ", target=" << kTarget
              << ")\n\n";
    std::cout << std::left << std::setw(20) << "Dataset" << std::right << std::setw(14)
              << "comparac." << std::setw(14) << "detectRun" << std::setw(14) << "eliminables"
              << std::setw(10) << "% de tot" << std::setw(12) << "hojas>t" << std::setw(12)
              << "monoval." << "\n";
    std::cout << std::string(96, '-') << "\n";

    for (const Case& c : kCases) {
        DatasetGenerator gen;
        const DataVector data = c.build(gen, kN);

        // 1) comparaciones totales de una ordenacion real
        DataVector copy = data;
        DynamicRangeSort<int64_t> sorter(kTarget);
        sorter.sort(copy);
        const std::size_t totalCmp = sorter.metrics().comparisons();

        // 2) particion sin ordenar, para clasificar cada hoja
        DynamicRangeSort<int64_t> insp(kTarget);
        const auto leaves = insp.debugPartitionOnly(data);
        const auto& bufA = insp.debugBufferA();
        const auto& bufB = insp.debugBufferB();

        std::size_t detectRunCmp = 0, removable = 0, bigLeaves = 0, monoBig = 0;
        for (const auto& L : leaves) {
            if (L.count < 2) continue;
            detectRunCmp += L.count - 1; // detectRun recorre la hoja entera
            const auto& buf = L.inBufferA ? bufA : bufB;
            const bool mono =
                std::all_of(buf.begin() + static_cast<long>(L.start),
                            buf.begin() + static_cast<long>(L.start + L.count),
                            [&](int64_t v) { return v == buf[L.start]; });
            if (L.count > kTarget) {
                ++bigLeaves;
                if (mono) {
                    ++monoBig;
                    // Solo estas tienen certificado: llegaron a refine() con
                    // count > target y resultaron monovaluadas.
                    removable += L.count - 1;
                }
            }
        }

        std::cout << std::left << std::setw(20) << c.name << std::right << std::setw(14) << totalCmp
                  << std::setw(14) << detectRunCmp << std::setw(14) << removable << std::setw(9)
                  << std::fixed << std::setprecision(1)
                  << (totalCmp ? 100.0 * static_cast<double>(removable) /
                                     static_cast<double>(totalCmp)
                               : 0.0)
                  << "%" << std::setw(12) << bigLeaves << std::setw(12) << monoBig << "\n";
    }
    return 0;
}
