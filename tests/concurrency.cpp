#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

// ============================================================
// Concurrency: one sorter, many threads, one workspace each
// ============================================================
// 0.11.0 separates a sorter's SETTINGS (immutable after construction) from
// its SCRATCH (a Workspace). sort(data, workspace) is const, so a single
// StratumSort may be used by any number of threads at once provided each
// passes its own workspace; the free functions allocate a workspace per
// call and are safe to call concurrently without any. No lock exists and
// none is needed, because nothing is shared.
//
// This test does it: eight threads share one const sorter, each sorting
// many inputs of different shapes and sizes with its own long-lived
// workspace (so stale scratch from a previous call would show), while
// four more call the free functions. Every output is checked. Built under
// ThreadSanitizer in `make tsan`, which is what turns "no output was
// wrong" into "no access raced".
#include "stratum/StratumSort.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

int main() {
    const stratum::StratumSort<int64_t> shared; // settings only
    std::atomic<int> failures{0};
    std::atomic<long> sorts{0};

    auto sharedSorterWorker = [&](unsigned seed) {
        stratum::Workspace<int64_t> ws; // this thread's scratch, reused
        std::mt19937_64 rng(seed);
        for (int round = 0; round < 40; ++round) {
            const std::size_t n = static_cast<std::size_t>(rng() % 60000);
            std::vector<int64_t> v(n);
            const int shape = static_cast<int>(rng() % 4);
            for (std::size_t i = 0; i < n; ++i) {
                switch (shape) {
                    case 0: v[i] = static_cast<int64_t>(rng()); break;
                    case 1: v[i] = static_cast<int64_t>(rng() % 7); break;
                    case 2: v[i] = static_cast<int64_t>(i); break;
                    default: v[i] = i < n / 2 ? static_cast<int64_t>(i) : static_cast<int64_t>(rng() % 1000); break;
                }
            }
            std::vector<int64_t> want = v;
            std::sort(want.begin(), want.end());
            shared.sort(v, ws);
            if (v != want) ++failures;
            ++sorts;
        }
    };

    auto freeFunctionWorker = [&](unsigned seed) {
        std::mt19937_64 rng(seed);
        for (int round = 0; round < 40; ++round) {
            struct Row { uint32_t key; uint32_t seq; };
            std::vector<Row> rows(static_cast<std::size_t>(rng() % 30000));
            for (std::size_t i = 0; i < rows.size(); ++i)
                rows[i] = {static_cast<uint32_t>(rng() % 50), static_cast<uint32_t>(i)};
            std::vector<Row> want = rows;
            std::stable_sort(want.begin(), want.end(), [](const Row& a, const Row& b) { return a.key < b.key; });
            stratum::stable_sort_by_key(rows, [](const Row& r) { return r.key; });
            bool same = rows.size() == want.size();
            for (std::size_t i = 0; same && i < rows.size(); ++i)
                same = rows[i].key == want[i].key && rows[i].seq == want[i].seq;
            if (!same) ++failures;
            std::vector<double> d(static_cast<std::size_t>(rng() % 20000));
            for (auto& x : d) x = static_cast<double>(static_cast<int64_t>(rng() % 100000) - 50000) / 7.0;
            std::vector<double> dw = d;
            std::sort(dw.begin(), dw.end());
            stratum::sort(d);
            if (d != dw) ++failures;
            sorts += 2;
        }
    };

    std::vector<std::thread> threads;
    for (unsigned t = 0; t < 8; ++t) threads.emplace_back(sharedSorterWorker, 1000 + t);
    for (unsigned t = 0; t < 4; ++t) threads.emplace_back(freeFunctionWorker, 2000 + t);
    for (auto& th : threads) th.join();

    std::printf("%ld concurrent sorts, %d wrong\n", sorts.load(), failures.load());
    if (failures != 0) {
        std::printf("FAILED\n");
        return 1;
    }
    std::printf("Concurrency test passed.\n");
    return 0;
}
