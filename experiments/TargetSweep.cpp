// ============================================================
// docs/
// ============================================================
// El paso 4 midio que localSort() es el 60-64% del tiempo en los cuatro
// datasets sin redundancia. El coste de Insertion Sort sobre hojas de
// tamano medio lambda es n*lambda/4, es decir PROPORCIONAL a lambda, y
// lambda lo fija targetElementsPerBin.
//
// Este barrido NO toca el algoritmo: targetElementsPerBin es parametro
// del constructor desde v3. Es medicion pura.
//
// Nota importante sobre lo que este barrido PUEDE y NO PUEDE decir:
// hoy targetElementsPerBin desempena a la vez el papel de ocupacion
// objetivo (lambda, via initialBins y splits) y de umbral de hoja (t).
// Son el MISMO numero, asi que este barrido mueve los dos a la vez y solo
// puede encontrar el optimo de la variable fusionada. Separarlos exige un
// cambio de codigo y es la Fase B.
//
// Los targets se recorren DENTRO de cada repeticion, de modo que
// cualquier deriva del entorno afecta por igual a todos.
#include "BenchmarkRunner.hpp"
#include "DatasetGenerator.hpp"
#include "drs/DynamicRangeSort.hpp"
#include "SystemInfo.hpp"

#include <algorithm>
#include <chrono>
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
constexpr std::size_t kReps = 5;
const std::vector<std::size_t> kTargets = {8, 12, 16, 24, 32, 48, 64, 96, 128, 256};
constexpr std::size_t kBaseline = 64; // el valor por defecto actual

struct Case {
    const char* name;
    DataVector (*build)(DatasetGenerator&, std::size_t);
};

DataVector b0(DatasetGenerator& g, std::size_t n) { return g.randomUniform(n); }
DataVector b1(DatasetGenerator& g, std::size_t n) { return g.sortedAscending(n); }
DataVector b2(DatasetGenerator& g, std::size_t n) { return g.sortedDescending(n); }
DataVector b3(DatasetGenerator& g, std::size_t n) { return g.manyRepeated(n); }
DataVector b4(DatasetGenerator& g, std::size_t n) { return g.normalDistribution(n); }
DataVector b5(DatasetGenerator& g, std::size_t n) { return g.concentrated(n); }
DataVector b6(DatasetGenerator& g, std::size_t n) { return g.smallRangeManyElements(n); }
DataVector b7(DatasetGenerator& g, std::size_t n) { return g.hugeRangeFewElements(n); }
DataVector b8(DatasetGenerator& g, std::size_t n) { return g.fullRangeExtremes(n); }
DataVector b9(DatasetGenerator& g, std::size_t n) { return g.adversarialPeeling(n, 64); }

const Case kCases[] = {
    {"RandomUniform", b0},    {"SortedAscending", b1}, {"SortedDescending", b2},
    {"ManyRepeated", b3},     {"NormalGaussian", b4},  {"Concentrated", b5},
    {"SmallRangeManyEl", b6}, {"HugeRangeFewEl", b7},  {"FullRangeExtremes", b8},
    {"AdversarialPeeling", b9},
};

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}

} // namespace

int main() {
    std::cout << "================================================================\n";
    std::cout << " BARRIDO DE target (docs/
    std::cout << "================================================================\n";
    drs::SystemInfo::collect().print(std::cout);
    std::cout << "n=" << kN << "  repeticiones=" << kReps
              << "  (targets recorridos dentro de cada repeticion)\n\n";

    // ---- 1. Tiempo mediano por (dataset, target) --------------------------
    std::cout << "---- 1. Tiempo mediano (ms) ----\n";
    std::cout << std::left << std::setw(20) << "Dataset" << std::right;
    for (std::size_t t : kTargets) std::cout << std::setw(9) << t;
    std::cout << std::setw(12) << "mejor" << "\n";
    std::cout << std::string(20 + 9 * kTargets.size() + 12, '-') << "\n";

    std::vector<std::vector<double>> allMed;
    for (const Case& c : kCases) {
        DatasetGenerator gen;
        const DataVector data = c.build(gen, kN);

        std::vector<std::vector<double>> samples(kTargets.size());
        for (std::size_t r = 0; r < kReps; ++r) {
            for (std::size_t i = 0; i < kTargets.size(); ++i) {
                DataVector copy = data;
                DynamicRangeSort<int64_t> s(kTargets[i]);
                const auto t0 = std::chrono::steady_clock::now();
                s.sort(copy);
                const auto t1 = std::chrono::steady_clock::now();
                samples[i].push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            }
        }
        std::vector<double> med;
        for (auto& v : samples) med.push_back(median(v));
        allMed.push_back(med);

        std::size_t best = 0;
        for (std::size_t i = 1; i < med.size(); ++i)
            if (med[i] < med[best]) best = i;

        std::cout << std::left << std::setw(20) << c.name << std::right << std::fixed
                  << std::setprecision(2);
        for (double m : med) std::cout << std::setw(9) << m;
        std::cout << std::setw(12) << kTargets[best] << "\n";
    }

    // ---- 2. Variacion relativa frente a target=64 -------------------------
    std::cout << "\n---- 2. Variacion relativa frente a target=" << kBaseline
              << " (negativo = mas rapido) ----\n";
    std::size_t baseIdx = 0;
    for (std::size_t i = 0; i < kTargets.size(); ++i)
        if (kTargets[i] == kBaseline) baseIdx = i;

    std::cout << std::left << std::setw(20) << "Dataset" << std::right;
    for (std::size_t t : kTargets) std::cout << std::setw(9) << t;
    std::cout << "\n" << std::string(20 + 9 * kTargets.size(), '-') << "\n";
    for (std::size_t ci = 0; ci < std::size(kCases); ++ci) {
        std::cout << std::left << std::setw(20) << kCases[ci].name << std::right << std::fixed
                  << std::setprecision(1);
        const double base = allMed[ci][baseIdx];
        for (double m : allMed[ci])
            std::cout << std::setw(8) << (base > 0 ? (m / base - 1.0) * 100.0 : 0.0) << "%";
        std::cout << "\n";
    }

    // ---- 3. Contadores deterministas --------------------------------------
    std::cout << "\n---- 3. Contadores deterministas (una ejecucion por punto) ----\n";
    std::cout << std::left << std::setw(20) << "Dataset" << std::right << std::setw(8) << "target"
              << std::setw(14) << "comparac." << std::setw(11) << "bins" << std::setw(8) << "prof"
              << std::setw(11) << "maxHoja" << std::setw(9) << "Intro" << std::setw(9) << "Quick"
              << "\n";
    std::cout << std::string(90, '-') << "\n";
    for (const Case& c : kCases) {
        DatasetGenerator gen;
        const DataVector data = c.build(gen, kN);
        for (std::size_t t : kTargets) {
            DataVector copy = data;
            DynamicRangeSort<int64_t> s(t);
            s.sort(copy);
            const auto& m = s.metrics();
            const auto& u = m.algorithmUsage();
            auto uget = [&](const char* k) {
                auto it = u.find(k);
                return it == u.end() ? std::size_t(0) : it->second;
            };
            std::cout << std::left << std::setw(20) << c.name << std::right << std::setw(8) << t
                      << std::setw(14) << m.comparisons() << std::setw(11) << m.totalBins()
                      << std::setw(8) << m.maxSubdivisionDepth() << std::setw(11) << m.maxBinSize()
                      << std::setw(9) << uget("Introsort") << std::setw(9) << uget("QuickSort")
                      << "\n";
        }
    }
    return 0;
}
