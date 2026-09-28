// ============================================================
// RecordStrategies - how to sort records by an integer key
// ============================================================
// Sorting a record by its key can be done without ever moving the record
// through the distribution passes: sort (key, position) pairs, then gather
// the records into their final order. It trades the record's bytes moved
// per level for one random read per record at the end. Which wins depends
// on how many bytes a record is, so this measures both, alongside the
// standard library, for records of 16 to 256 bytes:
//
//   direct     stratum::sort_by_key / stable_sort_by_key: records move
//              through every distribution pass (AoS)
//   indirect   stratum::sorted_indices (stable) on (key, index) pairs,
//              then a gather into a second vector: every record moved once
//   std        std::sort / std::stable_sort with a key comparator
//
// Time is the median of alternating repetitions; memory is the peak at the
// allocator (the gather's destination vector counts: it is what the
// indirect strategy costs).
#include "AllocationTracker.hpp"
#include "stratum/StratumSort.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

template <std::size_t Bytes>
struct Record {
    uint64_t key;
    std::array<unsigned char, Bytes - sizeof(uint64_t)> payload;
};

double medianOf(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

template <std::size_t Bytes>
void run(std::size_t n, uint64_t keyRange) {
    using R = Record<Bytes>;
    std::mt19937_64 rng(Bytes * 7 + n);
    std::vector<R> input(n);
    for (std::size_t i = 0; i < n; ++i) {
        input[i].key = keyRange ? rng() % keyRange : rng();
        input[i].payload.fill(static_cast<unsigned char>(i));
    }
    auto key = [](const R& r) { return r.key; };
    auto byKey = [](const R& a, const R& b) { return a.key < b.key; };

    struct Strategy {
        const char* name;
        void (*run)(std::vector<R>&);
    };
    const Strategy strategies[] = {
        {"stratum sort_by_key", [](std::vector<R>& v) { stratum::sort_by_key(v, [](const R& r) { return r.key; }); }},
        {"stratum stable_sort_by_key", [](std::vector<R>& v) { stratum::stable_sort_by_key(v, [](const R& r) { return r.key; }); }},
        {"indirect (indices + gather)", [](std::vector<R>& v) {
             const std::vector<std::size_t> order =
                 stratum::sorted_indices(v.begin(), v.end(), [](const R& r) { return r.key; });
             std::vector<R> out(v.size());
             for (std::size_t i = 0; i < order.size(); ++i) out[i] = v[order[i]];
             v.swap(out);
         }},
        {"std::sort", [](std::vector<R>& v) { std::sort(v.begin(), v.end(), [](const R& a, const R& b) { return a.key < b.key; }); }},
        {"std::stable_sort", [](std::vector<R>& v) { std::stable_sort(v.begin(), v.end(), [](const R& a, const R& b) { return a.key < b.key; }); }},
    };
    constexpr std::size_t kStrategies = sizeof(strategies) / sizeof(strategies[0]);
    std::vector<double> times[kStrategies];
    std::size_t peak[kStrategies] = {};
    const int reps = n <= 100000 ? 15 : 7;
    for (int r = 0; r < reps; ++r) {
        for (std::size_t k = 0; k < kStrategies; ++k) {
            const std::size_t i = (k + static_cast<std::size_t>(r)) % kStrategies;
            std::vector<R> v = input;
            const auto t0 = Clock::now();
            strategies[i].run(v);
            times[i].push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
            if (!std::is_sorted(v.begin(), v.end(), byKey)) std::printf("  NOT SORTED: %s\n", strategies[i].name);
        }
    }
    for (std::size_t i = 0; i < kStrategies; ++i) {
        std::vector<R> v = input;
        stratum::bench::AllocationScope scope;
        strategies[i].run(v);
        peak[i] = scope.peakBytes();
    }
    (void)key;
    const double stdMs = medianOf(times[3]);
    std::printf("\n%zu-byte records, n = %zu, keys %s\n", Bytes, n, keyRange ? "with duplicates" : "distinct");
    for (std::size_t i = 0; i < kStrategies; ++i)
        std::printf("  %-28s %9.3f ms  %5.2fx std::sort   peak aux %6.2f x input\n", strategies[i].name,
                    medianOf(times[i]), medianOf(times[i]) / stdMs,
                    static_cast<double>(peak[i]) / static_cast<double>(n * sizeof(R)));
    std::fflush(stdout);
}

} // namespace

int main() {
    for (std::size_t n : {std::size_t{100000}, std::size_t{1000000}}) {
        run<16>(n, 0);
        run<32>(n, 0);
        run<64>(n, 0);
        run<128>(n, 0);
        run<256>(n, 0);
        run<64>(n, 1000);
    }
    return 0;
}
