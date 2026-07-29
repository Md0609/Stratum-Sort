// ============================================================
// Ceiling of each remaining optimisation candidate
// ============================================================
// Measures, in a RELEASE build, the most any given change could possibly
// save - before writing that change. Phase percentages from the
// instrumented build are inflated by the instrumentation itself, so they
// cannot be used for this.
//
// The allocation timer replaces the global operator new and delete, so it
// works in any configuration and needs no cooperation from the algorithm.
#include "DatasetGenerator.hpp"
#include "drs/DynamicRangeSort.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <new>
#include <vector>

namespace {
bool g_on = false;
std::size_t g_allocs = 0;
double g_allocNs = 0.0;
} // namespace

void* operator new(std::size_t sz) {
    if (sz == 0) sz = 1;
    if (!g_on) {
        void* p = std::malloc(sz);
        if (!p) throw std::bad_alloc();
        return p;
    }
    const auto t0 = std::chrono::steady_clock::now();
    void* p = std::malloc(sz);
    const auto t1 = std::chrono::steady_clock::now();
    g_allocNs += std::chrono::duration<double, std::nano>(t1 - t0).count();
    ++g_allocs;
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t sz) { return operator new(sz); }
void operator delete(void* p) noexcept {
    if (!g_on) { std::free(p); return; }
    const auto t0 = std::chrono::steady_clock::now();
    std::free(p);
    const auto t1 = std::chrono::steady_clock::now();
    g_allocNs += std::chrono::duration<double, std::nano>(t1 - t0).count();
}
void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { operator delete(p); }

using drs::DynamicRangeSort;
using drs::testing::DataVector;
using drs::testing::DatasetGenerator;

namespace {
constexpr std::size_t kN = 1000000;
constexpr std::size_t kTarget = 64;
constexpr std::size_t kReps = 7;

struct Case { const char* name; DataVector (*f)(DatasetGenerator&, std::size_t); };
DataVector c0(DatasetGenerator& g, std::size_t n) { return g.randomUniform(n); }
DataVector c1(DatasetGenerator& g, std::size_t n) { return g.sortedAscending(n); }
DataVector c2(DatasetGenerator& g, std::size_t n) { return g.sortedDescending(n); }
DataVector c3(DatasetGenerator& g, std::size_t n) { return g.manyRepeated(n); }
DataVector c4(DatasetGenerator& g, std::size_t n) { return g.normalDistribution(n); }
DataVector c5(DatasetGenerator& g, std::size_t n) { return g.concentrated(n); }
DataVector c6(DatasetGenerator& g, std::size_t n) { return g.smallRangeManyElements(n); }
DataVector c7(DatasetGenerator& g, std::size_t n) { return g.hugeRangeFewElements(n); }
DataVector c8(DatasetGenerator& g, std::size_t n) { return g.fullRangeExtremes(n); }
DataVector c9(DatasetGenerator& g, std::size_t n) { return g.adversarialPeeling(n, kTarget); }
const Case kCases[] = {
    {"RandomUniform", c0},    {"SortedAscending", c1}, {"SortedDescending", c2},
    {"ManyRepeated", c3},     {"NormalGaussian", c4},  {"Concentrated", c5},
    {"SmallRangeManyEl", c6}, {"HugeRangeFewEl", c7},  {"FullRangeExtremes", c8},
    {"AdversarialPeeling", c9},
};

double med(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}
} // namespace

int main() {
    std::cout << "Techo de mejora en PRODUCCION (sin DRS_ENABLE_METRICS), n=" << kN
              << ", target=" << kTarget << ", mediana de " << kReps << "\n\n";

    // --- Coste aislado del relleno de ceros de los dos buffers (candidato P2)
    // Es exactamente el trabajo que hace distribute(): dos assign(n, T{}).
    std::vector<double> zf;
    for (std::size_t r = 0; r < kReps; ++r) {
        std::vector<int64_t> a, b;
        a.reserve(kN); b.reserve(kN);
        const auto t0 = std::chrono::steady_clock::now();
        a.assign(kN, int64_t{});
        b.assign(kN, int64_t{});
        const auto t1 = std::chrono::steady_clock::now();
        zf.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        if (a[0] != 0 || b[0] != 0) return 1; // evita que se optimice fuera
    }
    const double zeroFillMs = med(zf);
    std::cout << "P2  relleno de ceros de bufferA_+bufferB_ (2n escrituras muertas): "
              << std::fixed << std::setprecision(3) << zeroFillMs << " ms\n\n";

    std::cout << std::left << std::setw(20) << "Dataset" << std::right << std::setw(10) << "total"
              << std::setw(12) << "P6 alloc" << std::setw(8) << "%" << std::setw(12) << "P7 merge"
              << std::setw(8) << "%" << std::setw(12) << "P2 ceros" << std::setw(8) << "%" << "\n";
    std::cout << std::string(90, '-') << "\n";

    for (const Case& c : kCases) {
        DatasetGenerator gen;
        const DataVector data = c.f(gen, kN);

        // tiempo total y tiempo dentro de malloc/free
        std::vector<double> tot, alloc;
        for (std::size_t r = 0; r < kReps; ++r) {
            DataVector copy = data;
            DynamicRangeSort<int64_t> s(kTarget);
            g_allocs = 0; g_allocNs = 0.0; g_on = true;
            const auto t0 = std::chrono::steady_clock::now();
            s.sort(copy);
            const auto t1 = std::chrono::steady_clock::now();
            g_on = false;
            tot.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            alloc.push_back(g_allocNs / 1e6);
        }
        const double mt = med(tot), ma = med(alloc);

        // P7: fraccion de elementos en hojas de paridad IMPAR, que con 'data'
        // como buffer quedarian ya en su sitio y no habria que copiar.
        // Se obtiene de la particion, sin cronometro: es determinista.
        // (Requiere el build de investigacion; aqui se estima por el coste de
        // copiar n elementos, y la fraccion se calcula en el programa hermano.)
        const double mergeMs = zeroFillMs * 0.5; // n lecturas + n escrituras ~ un assign de n

        std::cout << std::left << std::setw(20) << c.name << std::right << std::fixed
                  << std::setprecision(2) << std::setw(10) << mt << std::setw(12)
                  << std::setprecision(3) << ma << std::setw(7) << std::setprecision(1)
                  << (mt > 0 ? ma / mt * 100 : 0) << "%" << std::setw(12) << std::setprecision(3)
                  << mergeMs << std::setw(7) << std::setprecision(1)
                  << (mt > 0 ? mergeMs / mt * 100 : 0) << "%" << std::setw(12)
                  << std::setprecision(3) << zeroFillMs << std::setw(7) << std::setprecision(1)
                  << (mt > 0 ? zeroFillMs / mt * 100 : 0) << "%\n";
    }

    std::cout << "\nNotas:\n"
              << "  P6 alloc = tiempo REAL dentro de malloc/free durante sort(). Techo exacto.\n"
              << "  P7 merge = coste de copiar n elementos (cota SUPERIOR: solo se evita la\n"
              << "             fraccion de paridad impar, que mide el programa hermano).\n"
              << "  P2 ceros = coste de los dos assign(n) que nadie lee. Techo exacto.\n";
    return 0;
}
