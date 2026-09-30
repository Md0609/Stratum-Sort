// ============================================================
// InPlaceTuning - the in-place engine's constants, on the machine at hand
// ============================================================
// INPLACE_RADIX_BITS (L) and INPLACE_BLOCK_BYTES are compile-time
// constants, so each configuration is a separate build of this file
// against a copy of include/ with the two constants edited (the
// `tune` job of .github/workflows/bench.yml does exactly that on the CI
// runners). Each run prints one table: for every input, the partner buffer
// (unlimited budget) and three bounded strategies, alternated on identical
// copies, medians of the repetitions:
//
//   blocks     arena + block buffers exactly: every pass by blocks, no
//              partner buffer at all
//   floor      budget 0: the arena alone, every pass an American flag
//   automatic  the default policy (partner up to 16 MiB, 16 MiB above)
//
// Why it exists: the block buffers are (2^L + 3) blocks, 526 KB at the
// defaults, and the memory study measured them fast on a Xeon with a large
// L2 and slow on the CI's EPYC 7763 (L2 512 KB). Which (L, block) is best
// depends on the cache; this measures it where it matters.
//
//   ./perf_InPlaceTuning [--n 1e6,1e7] [--reps 5]
#include "BenchDatasets.hpp"
#include "SystemInfo.hpp"

#include "stratum/StratumSort.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace {

using E = uint64_t;

double timeOne(std::vector<E>& d, std::size_t budget, bool useStd) {
    const auto t0 = std::chrono::steady_clock::now();
    if (useStd) {
        std::sort(d.begin(), d.end());
    } else {
        stratum::Workspace<E> ws(budget);
        stratum::sort(d, ws);
    }
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::size_t> sizes = {1000000, 10000000};
    int reps = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string k = argv[i], v = argv[i + 1];
        if (k == "--n") {
            sizes.clear();
            std::stringstream ss(v);
            std::string item;
            while (std::getline(ss, item, ',')) sizes.push_back(static_cast<std::size_t>(std::strtod(item.c_str(), nullptr)));
        } else if (k == "--reps") {
            reps = std::atoi(v.c_str());
        }
    }
    const auto info = stratum::SystemInfo::collect();
    const std::size_t radix = std::size_t{1} << stratum::INPLACE_RADIX_BITS;
    const std::size_t block = stratum::INPLACE_BLOCK_BYTES / sizeof(E);
    const std::size_t arenaBytes = stratum::detail::inPlaceArenaFor(64) * sizeof(uint32_t);
    const std::size_t blockBytes = (radix + 3) * block * sizeof(E);
    // Exactly the arena and the blocks: nothing left for a partner buffer.
    const std::size_t blocksOnly = arenaBytes + blockBytes + 16;
    const std::size_t budgets[4] = {stratum::Workspace<E>::kUnlimited, blocksOnly, 0,
                                    stratum::Workspace<E>::kAutomatic};

    std::printf("\n### L = %u, block = %zu B (block buffers %zu KB) - %s, %s, %s\n\n",
                stratum::INPLACE_RADIX_BITS, stratum::INPLACE_BLOCK_BYTES, blockBytes / 1024,
                info.cpuModel.c_str(), info.caches.c_str(), info.compilerName.c_str());
    std::printf("| n | input | partner ms | blocks | floor | automatic | std::sort ms | blocks vs std |\n");
    std::printf("|---|---|---|---|---|---|---|---|\n");
    for (std::size_t n : sizes) {
        const int r = reps > 0 ? reps : (n >= 10000000 ? 5 : 9);
        for (const char* shape : {"random", "nearly_sorted", "adversarial", "few_outliers", "low_entropy"}) {
            const std::vector<E> input = stratum::bench::makeShape<E>(shape, n, 42);
            std::vector<double> t[5];
            for (int rep = 0; rep < r; ++rep) {
                for (int m = 0; m < 5; ++m) {
                    const int k = (m + rep) % 5;
                    std::vector<E> d = input;
                    t[k].push_back(timeOne(d, k < 4 ? budgets[k] : 0, k == 4));
                    if (!std::is_sorted(d.begin(), d.end())) {
                        std::printf("WRONG\n");
                        return 1;
                    }
                }
            }
            double med[5];
            for (int k = 0; k < 5; ++k) {
                std::sort(t[k].begin(), t[k].end());
                med[k] = t[k][t[k].size() / 2];
            }
            std::printf("| %zu | %s | %.2f | %+.0f%% | %+.0f%% | %+.0f%% | %.2f | %.2fx |\n", n,
                        shape, med[0], 100 * (med[1] / med[0] - 1), 100 * (med[2] / med[0] - 1),
                        100 * (med[3] / med[0] - 1), med[4], med[1] / med[4]);
            std::fflush(stdout);
        }
    }
    return 0;
}
