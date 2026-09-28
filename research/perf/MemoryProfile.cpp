// ============================================================
// MemoryProfile - peak auxiliary memory, v0.10.0 against the current header
// ============================================================
// Measured at the allocator (benchmarks/AllocationTracker.hpp), in the
// RELEASE configuration: the metrics build allocates for its own
// bookkeeping and would inflate every number here.
//
// For every (key width, n, lambda, input shape) it sorts the same input
// with the frozen 0.10.0 implementation and with the current one, each on
// a FRESH instance so that the scratch an instance allocates on first use
// is counted, and reports:
//
//   peak      highest live byte count above what was live before the call
//   B/elem    peak / n
//   x input   peak / (n * sizeof(key))
//
// Deterministic: fixed seeds, no clock. Run it twice and diff the output.
//
//   make research-perf && ./build/perf_MemoryProfile [--quick]
#include "AllocationTracker.hpp"

#include "DatasetGenerator.hpp"
#include "stratum/StratumSort.hpp"
#include "v0_10_0/StratumSort.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

template <typename T>
std::vector<T> makeInput(const std::string& shape, std::size_t n, std::size_t lambda) {
    std::mt19937_64 rng(20260928);
    std::vector<T> v(n);
    if (shape == "uniform") {
        for (auto& x : v) x = static_cast<T>(rng());
    } else if (shape == "duplicates") {
        for (auto& x : v) x = static_cast<T>(rng() % 5);
    } else if (shape == "binary") { // one bit of entropy per key
        for (auto& x : v) x = static_cast<T>(rng() & 1);
    } else if (shape == "ascending") {
        for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<T>(i);
    } else if (shape == "adversarial") {
        // The depth-exhausting generator, built against the lambda under test.
        stratum::testing::DatasetGenerator g(7);
        const auto a = g.adversarialPeeling(n, lambda);
        for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<T>(a[i]);
    }
    return v;
}

struct Peak {
    std::size_t bytes = 0;
    std::size_t allocations = 0;
};

template <typename Sorter, typename T>
Peak measure(const std::vector<T>& input, std::size_t lambda, std::size_t t, bool& ok,
             const std::vector<T>& expected) {
    std::vector<T> data = input;
    Peak p;
    {
        stratum::bench::AllocationScope scope;
        Sorter sorter(lambda, t);
        sorter.sort(data);
        p.bytes = scope.peakBytes();
        p.allocations = scope.allocations();
    }
    ok = ok && (data == expected);
    return p;
}

template <typename T>
void row(const char* typeName, const std::string& shape, std::size_t n, std::size_t lambda) {
    // t = 2*lambda, as the defaults (32, 64); lambda = 1 uses t = 1, the
    // configuration the 0.10.0 documentation quotes its 16.56x worst figure for.
    const std::size_t t = lambda == 1 ? 1 : 2 * lambda;
    const std::vector<T> input = makeInput<T>(shape, n, lambda);
    std::vector<T> expected = input;
    std::sort(expected.begin(), expected.end());
    bool ok = true;
    const Peak before =
        measure<stratum_v0_10_0::StratumSort<T>>(input, lambda, t, ok, expected);
    const Peak after = measure<stratum::StratumSort<T>>(input, lambda, t, ok, expected);
    const double inBytes = static_cast<double>(n * sizeof(T));
    std::printf("%-8s %-12s %9zu %6zu | %12zu %7.2f %6.2fx | %12zu %7.2f %6.2fx | %6.1f%% %s\n",
                typeName, shape.c_str(), n, lambda, before.bytes,
                static_cast<double>(before.bytes) / static_cast<double>(n),
                static_cast<double>(before.bytes) / inBytes, after.bytes,
                static_cast<double>(after.bytes) / static_cast<double>(n),
                static_cast<double>(after.bytes) / inBytes,
                before.bytes ? 100.0 * (1.0 - static_cast<double>(after.bytes) /
                                                  static_cast<double>(before.bytes))
                             : 0.0,
                ok ? "" : "  WRONG OUTPUT");
}

template <typename T>
void sweep(const char* typeName, const std::vector<std::size_t>& sizes,
           const std::vector<std::size_t>& lambdas, const std::vector<std::string>& shapes) {
    for (const std::string& shape : shapes)
        for (std::size_t n : sizes)
            for (std::size_t lambda : lambdas) row<T>(typeName, shape, n, lambda);
}

} // namespace

int main(int argc, char** argv) {
    const bool quick = argc > 1 && std::strcmp(argv[1], "--quick") == 0;
    std::printf("Peak auxiliary bytes per sort() on a fresh instance (t = 2*lambda, 1 at lambda = 1).\n");
    std::printf("%-8s %-12s %9s %6s | %28s | %28s | %7s\n", "key", "input", "n", "lambda",
                "v0.10.0  peak   B/elem  x in", "current  peak   B/elem  x in", "saved");
    std::printf("%s\n", std::string(116, '-').c_str());

    const std::vector<std::size_t> sizes =
        quick ? std::vector<std::size_t>{100000}
              : std::vector<std::size_t>{1000, 10000, 100000, 1000000, 10000000};
    const std::vector<std::size_t> lambdas =
        quick ? std::vector<std::size_t>{16} : std::vector<std::size_t>{1, 8, 16, 32, 128, 1024};
    const std::vector<std::string> shapes = {"uniform", "duplicates", "binary", "ascending",
                                             "adversarial"};

    sweep<uint8_t>("uint8", sizes, lambdas, {"uniform", "duplicates", "binary"});
    sweep<uint16_t>("uint16", sizes, lambdas, {"uniform", "duplicates"});
    sweep<uint32_t>("uint32", sizes, lambdas, {"uniform", "duplicates"});
    sweep<int64_t>("int64", sizes, lambdas, shapes);
    return 0;
}
