// ============================================================
// Complexity review: raw data for an external log-log fit
// ============================================================
// Emite datos crudos para un ajuste log-log externo. Corrige los cuatro
// defectos que ANALYSIS_v9_propuesta.md 3 identifico en el ajuste de
// complejidad historico (analisis/main.cpp):
//
//   3.1  aquel usaba SOLO tiempo de pared, teniendo al lado contadores
//        deterministas -> aqui se emiten ambos
//   3.2  aquel se compilaba con DRS_ENABLE_METRICS, cuyo overhead decrece
//        con n y sesga el exponente a la baja -> el tiempo se mide aqui en
//        una pasada de PRODUCCION y los contadores en otra de
//        investigacion, sin mezclarlos
//   3.3  aquel usaba randomUniform, con rango FIJO mientras n varia cuatro
//        ordenes de magnitud -> se anade randomUniformScaled, de densidad
//        constante
//   3.4  aquel elegia entre seis exponentes prefijados por R^2 -> aqui se
//        emite la nube de puntos y el exponente se ESTIMA con su intervalo
//        de confianza
//
// Salida: lineas "dataset n tiempo_ms comparaciones bins subdiv prof".
// El tiempo se toma con el binario de produccion (contadores a 0) y los
// contadores con el de investigacion (tiempo ignorado).
#include "DatasetGenerator.hpp"
#include "drs/DynamicRangeSort.hpp"

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
constexpr std::size_t kTarget = 64;

struct Case { const char* name; DataVector (*f)(DatasetGenerator&, std::size_t); };
DataVector d0(DatasetGenerator& g, std::size_t n) { return g.randomUniformScaled(n); }
DataVector d1(DatasetGenerator& g, std::size_t n) { return g.randomUniform(n); }
DataVector d2(DatasetGenerator& g, std::size_t n) { return g.sortedAscending(n); }
DataVector d3(DatasetGenerator& g, std::size_t n) { return g.manyRepeated(n); }
DataVector d4(DatasetGenerator& g, std::size_t n) { return g.normalDistribution(n); }
DataVector d5(DatasetGenerator& g, std::size_t n) { return g.concentrated(n); }
DataVector d6(DatasetGenerator& g, std::size_t n) { return g.hugeRangeFewElements(n); }
DataVector d7(DatasetGenerator& g, std::size_t n) { return g.adversarialPeeling(n, kTarget); }

const Case kCases[] = {
    {"RandomUniformScaled", d0}, {"RandomUniform", d1}, {"SortedAscending", d2},
    {"ManyRepeated", d3},        {"NormalGaussian", d4}, {"Concentrated", d5},
    {"HugeRangeFewEl", d6},      {"AdversarialPeeling", d7},
};

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}
} // namespace

int main() {
    const std::vector<std::size_t> sizes = {1000,   3000,   10000,   30000,   100000,
                                             300000, 1000000, 2000000, 5000000};
#ifdef DRS_ENABLE_METRICS
    std::cout << "# modo=investigacion (contadores validos, tiempo NO)\n";
#else
    std::cout << "# modo=produccion (tiempo valido, contadores a 0)\n";
#endif
    std::cout << "# dataset n tiempo_ms comparaciones bins subdiv prof\n";

    for (const Case& c : kCases) {
        for (std::size_t n : sizes) {
            DatasetGenerator gen;
            const DataVector data = c.f(gen, n);
            // mas repeticiones en los tamanos pequenos, donde el reloj resuelve peor
            const std::size_t reps = n <= 10000 ? 51 : (n <= 300000 ? 15 : 7);

            std::vector<double> t;
            std::size_t cmp = 0, bins = 0, sub = 0, prof = 0;
            for (std::size_t r = 0; r < reps; ++r) {
                DataVector copy = data;
                DynamicRangeSort<int64_t> s(kTarget);
                const auto t0 = std::chrono::steady_clock::now();
                s.sort(copy);
                const auto t1 = std::chrono::steady_clock::now();
                t.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
#ifdef DRS_ENABLE_METRICS
                if (r == 0) {
                    cmp = s.metrics().comparisons();
                    bins = s.metrics().totalBins();
                    sub = s.metrics().subdivisions();
                    prof = s.metrics().maxSubdivisionDepth();
                }
#endif
            }
            std::cout << c.name << " " << n << " " << std::fixed << std::setprecision(6)
                      << median(t) << " " << cmp << " " << bins << " " << sub << " " << prof
                      << "\n";
            std::cout.flush();
        }
    }
    return 0;
}
