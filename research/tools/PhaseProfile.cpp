// ============================================================
// Per-phase profile and heap-allocation count
// ============================================================
// Answers two questions that the wall clock alone cannot:
//   1. where the time actually goes, phase by phase, per dataset;
//   2. how many heap allocations one sort() performs, and how many bytes.
//
// The allocation count works by replacing the global operator new and
// delete, so it needs no cooperation from the algorithm: it is switched
// on only around the call to sort().
//
// The phase timings come from the instrumented build and are therefore
// NOT release timings. Use them to find where to look, never to claim a
// speed-up.
#include "DatasetGenerator.hpp"
#include "stratum/StratumSort.hpp"
#include "SystemInfo.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <new>
#include <string>
#include <vector>

// ---- contador global de asignaciones --------------------------------------
namespace {
bool g_counting = false;
std::size_t g_allocs = 0;
std::size_t g_bytes = 0;
std::size_t g_frees = 0;
} // namespace

void* operator new(std::size_t sz) {
    if (sz == 0) sz = 1;
    if (g_counting) { ++g_allocs; g_bytes += sz; }
    void* p = std::malloc(sz);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t sz) { return operator new(sz); }
void operator delete(void* p) noexcept {
    if (g_counting && p) ++g_frees;
    std::free(p);
}
void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { operator delete(p); }

using stratum::StratumSort;
using stratum::testing::DataVector;
using stratum::testing::DatasetGenerator;

namespace {

std::size_t kTarget = 64;  // overridable from argv[1]
constexpr std::size_t kReps = 7;

const char* kPhases[] = {"analyze", "distribute", "refine", "localSort", "join"};
constexpr std::size_t kNumPhases = 5;

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
DataVector b9(DatasetGenerator& g, std::size_t n) { return g.adversarialPeeling(n, kTarget); }

const Case kCases[] = {
    {"RandomUniform", b0},   {"SortedAscending", b1},  {"SortedDescending", b2},
    {"ManyRepeated", b3},    {"NormalGaussian", b4},   {"Concentrated", b5},
    {"SmallRangeManyEl", b6},{"HugeRangeFewEl", b7},   {"FullRangeExtremes", b8},
    {"AdversarialPeeling", b9},
};

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) kTarget = static_cast<std::size_t>(std::atoi(argv[1]));
    std::cout << "================================================================\n";
    std::cout << " Stratum Sort - per-phase profile and allocation count\n";
    std::cout << "================================================================\n";
    stratum::SystemInfo::collect().print(std::cout);
    std::cout << "target=" << kTarget << "  repeticiones=" << kReps << "\n\n";

    const std::size_t n = 1000000;

    std::cout << "---- 1. Reparto de tiempo por fase (n=" << n << ", mediana de "
              << kReps << ") ----\n";
    std::cout << std::left << std::setw(20) << "Dataset" << std::right << std::setw(9) << "total";
    for (const char* p : kPhases) std::cout << std::setw(12) << p;
    std::cout << std::setw(9) << "suma%" << "\n";
    std::cout << std::string(98, '-') << "\n";

    for (const Case& c : kCases) {
        DatasetGenerator gen;
        const DataVector data = c.build(gen, n);

        std::vector<double> totals;
        std::vector<std::vector<double>> ph(kNumPhases);
        for (std::size_t r = 0; r < kReps; ++r) {
            DataVector copy = data;
            StratumSort<int64_t> s(kTarget);
            const auto t0 = std::chrono::steady_clock::now();
            s.sort(copy);
            const auto t1 = std::chrono::steady_clock::now();
            totals.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            for (std::size_t i = 0; i < kNumPhases; ++i)
                ph[i].push_back(s.metrics().phaseDurationMs(kPhases[i]));
        }
        const double tot = median(totals);
        std::cout << std::left << std::setw(20) << c.name << std::right << std::fixed
                  << std::setprecision(2) << std::setw(9) << tot;
        double sum = 0.0;
        for (std::size_t i = 0; i < kNumPhases; ++i) {
            const double m = median(ph[i]);
            sum += m;
            std::cout << std::setw(8) << std::setprecision(1) << (tot > 0 ? m / tot * 100.0 : 0.0)
                      << "%   ";
        }
        std::cout << std::setw(7) << std::setprecision(1) << (tot > 0 ? sum / tot * 100.0 : 0.0)
                  << "%\n";
    }

    std::cout << "\n---- 2. Asignaciones de heap por sort() (n=" << n << ") ----\n";
    std::cout << std::left << std::setw(20) << "Dataset" << std::right << std::setw(12) << "allocs"
              << std::setw(14) << "bytes ped." << std::setw(12) << "subdiv."
              << std::setw(14) << "allocs/subdiv" << std::setw(16) << "SortMetrics MB" << "\n";
    std::cout << std::string(88, '-') << "\n";

    for (const Case& c : kCases) {
        DatasetGenerator gen;
        const DataVector data = c.build(gen, n);
        DataVector copy = data;
        StratumSort<int64_t> s(kTarget);

        g_allocs = g_bytes = g_frees = 0;
        g_counting = true;
        s.sort(copy);
        g_counting = false;

        const std::size_t subdiv = s.metrics().subdivisions();
        std::cout << std::left << std::setw(20) << c.name << std::right << std::setw(12) << g_allocs
                  << std::setw(14) << g_bytes << std::setw(12) << subdiv << std::setw(14)
                  << std::fixed << std::setprecision(2)
                  << (subdiv ? static_cast<double>(g_allocs) / static_cast<double>(subdiv) : 0.0)
                  << std::setw(16) << std::setprecision(2)
                  << static_cast<double>(s.metrics().approxMemoryBytes()) / (1024.0 * 1024.0)
                  << "\n";
    }

    std::cout << "\n(Referencia: 2n*8 = " << (2.0 * n * 8 / (1024 * 1024))
              << " MB solo de bufferA_+bufferB_; n*8 = " << (1.0 * n * 8 / (1024 * 1024))
              << " MB de bucketOfScratch_)\n";
    return 0;
}
