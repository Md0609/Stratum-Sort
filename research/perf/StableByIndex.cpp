// ============================================================
// StableByIndex - candidate C of the memory study (prototype, not adopted)
// ============================================================
// The stable sort of records through (key, index) pairs: 16 bytes per
// element whatever the record size, sorted UNSTABLY - in place, within a
// 16 MiB budget - by key, then every run of equal keys by index (indices
// are distinct, so the order inside a run is forced and the result is the
// stable order), then the permutation applied to the records in place by
// cycles. Compared with stable_sort_by_key (the partner buffer, n records)
// and std::stable_sort. Built only from the public API.
// research/history/V11_memoria.md section 10.3 has the verdict.
//
//   make research-perf && ./build/perf_StableByIndex
#include "stratum/StratumSort.hpp"
#include "BenchDatasets.hpp"
#include "AllocationTracker.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>

template <std::size_t P> struct Rec { uint64_t key; unsigned char pay[P]; };
struct Pair { uint64_t key; uint64_t index; };

template <typename E>
void stableByIndex(std::vector<E>& v, std::size_t pairBudget) {
    const std::size_t n = v.size();
    std::vector<Pair> p(n);
    for (std::size_t i = 0; i < n; ++i) p[i] = {v[i].key, i};
    stratum::Workspace<Pair> ws(pairBudget);
    stratum::sort_by_key(p, [](const Pair& x) { return x.key; }, ws);
    std::size_t i = 0;
    while (i < n) {
        std::size_t j = i + 1;
        while (j < n && p[j].key == p[i].key) ++j;
        if (j - i > 1) stratum::sort_by_key(p.data() + i, p.data() + j, [](const Pair& x) { return x.index; }, ws);
        i = j;
    }
    // out[j] = in[p[j].index], by cycles; p[j].index = j marks j done.
    for (std::size_t s = 0; s < n; ++s) {
        if (p[s].index == s) continue;
        E tmp = v[s];
        std::size_t j = s;
        for (;;) {
            const std::size_t src = p[j].index;
            p[j].index = j;
            if (src == s) { v[j] = tmp; break; }
            v[j] = v[src];
            j = src;
        }
    }
}

template <typename E>
void run(const char* tn, const char* sh, std::size_t n, int reps) {
    auto keys = stratum::bench::makeShape<uint64_t>(sh, n, 42);
    std::vector<E> in(n);
    for (std::size_t i = 0; i < n; ++i) { in[i].key = keys[i]; std::memset(in[i].pay, int(i), sizeof in[i].pay); std::memcpy(in[i].pay, &i, sizeof i); }
    std::vector<E> want = in;
    std::stable_sort(want.begin(), want.end(), [](const E& a, const E& b) { return a.key < b.key; });
    double t[3][16]; std::size_t peak[3] = {0, 0, 0};
    for (int r = 0; r < reps; ++r)
        for (int m = 0; m < 3; ++m) {
            const int k = (m + r) % 3;
            auto d = in;
            stratum::bench::AllocationScope scope;
            auto t0 = std::chrono::steady_clock::now();
            if (k == 0) { stratum::Workspace<E> ws(stratum::Workspace<E>::kUnlimited); stratum::stable_sort_by_key(d, [](const E& x) { return x.key; }, ws); }
            else if (k == 1) stableByIndex(d, 16u << 20);
            else std::stable_sort(d.begin(), d.end(), [](const E& a, const E& b) { return a.key < b.key; });
            t[k][r] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            peak[k] = scope.peakBytes();
            if (std::memcmp(d.data(), want.data(), n * sizeof(E)) != 0) std::printf("WRONG k=%d\n", k);
        }
    double med[3];
    for (int k = 0; k < 3; ++k) { std::sort(t[k], t[k] + reps); med[k] = t[k][reps / 2]; }
    std::printf("%-7s %-14s %9zu | partner %9.1f ms %6.1f B/e | pairs %+6.0f%% %6.1f B/e | std::stable_sort %9.1f ms %6.1f B/e | pairs vs std %.2fx\n",
                tn, sh, n, med[0], peak[0] / double(n), 100 * (med[1] / med[0] - 1), peak[1] / double(n), med[2], peak[2] / double(n), med[1] / med[2]);
    std::fflush(stdout);
}

int main() {
    for (std::size_t n : {std::size_t(1000000), std::size_t(10000000)}) {
        const int reps = n > 1000000 ? 3 : 7;
        for (const char* sh : {"random", "nearly_sorted", "duplicates", "sorted_tail"}) {
            run<Rec<8>>("rec16", sh, n, reps);
            run<Rec<64>>("rec72", sh, n, reps);
            if (n <= 1000000 || std::strcmp(sh, "random") == 0) run<Rec<256>>("rec264", sh, n, reps);
        }
    }
}
