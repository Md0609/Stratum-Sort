// ============================================================
// LambdaSweep - which lambda is fastest, as a function of n and key width
// ============================================================
// 0.10.0 shipped lambda = 32 with the caveat that it was tuned for
// n ~ 1e6 on a 4 MiB L2, and that "much larger inputs want a larger
// lambda" - leaving the choice, and the cache arithmetic behind it, to the
// caller. This sweep measures the question 0.11.0's automatic default has
// to answer: for each n and key width, where is the minimum, and how flat
// is the curve around it?
//
// t = 2 * lambda throughout, the ratio of the shipped (32, 64).
// Lambdas are ALTERNATED inside every repetition, so machine drift is
// spread across all of them. Output: median ms per lambda, normalised to
// the best lambda of the row.
//
//   ./build/perf_LambdaSweep [--type int64|uint32|uint16] [--shape random|normal|...] n...
#include "BenchDatasets.hpp"
#include "stratum/StratumSort.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double medianOf(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

template <typename T>
void sweep(const std::string& shape, std::size_t n, const std::vector<std::size_t>& lambdas) {
    const std::vector<T> input = stratum::bench::makeShape<T>(shape, n);
    std::vector<std::vector<double>> times(lambdas.size());
    const int reps = n <= 100000 ? 41 : n <= 1000000 ? 15 : n <= 10000000 ? 7 : 5;
    stratum::Workspace<T> ws; // reused: this measures the algorithm, not malloc
    for (int r = 0; r < reps; ++r) {
        for (std::size_t li = 0; li < lambdas.size(); ++li) {
            const std::size_t idx = (li + static_cast<std::size_t>(r)) % lambdas.size();
            const stratum::StratumSort<T> s(lambdas[idx], 2 * lambdas[idx]);
            std::vector<T> d = input;
            const auto t0 = Clock::now();
            s.sort(d, ws);
            times[idx].push_back(
                std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
        }
    }
    std::vector<double> med(lambdas.size());
    for (std::size_t i = 0; i < lambdas.size(); ++i) med[i] = medianOf(times[i]);
    const std::size_t best =
        static_cast<std::size_t>(std::min_element(med.begin(), med.end()) - med.begin());
    std::printf("%-8s %-10s %9zu | best l=%-5zu %8.3f ms |", sizeof(T) == 8 ? "int64" : sizeof(T) == 4 ? "uint32" : sizeof(T) == 2 ? "uint16" : "uint8",
                shape.c_str(), n, lambdas[best], med[best]);
    for (std::size_t i = 0; i < lambdas.size(); ++i) std::printf(" %5.2f", med[i] / med[best]);
    std::printf("\n");
    std::fflush(stdout);
}

// The same question for t at a fixed lambda: t/lambda in {1, 1.5, 2, 3, 4, 8}.
template <typename T>
void tSweep(const std::string& shape, std::size_t n, std::size_t lambda) {
    const std::vector<std::size_t> ratiosTimes2 = {2, 3, 4, 6, 8, 16};
    const std::vector<T> input = stratum::bench::makeShape<T>(shape, n);
    std::vector<std::vector<double>> times(ratiosTimes2.size());
    const int reps = n <= 100000 ? 41 : n <= 1000000 ? 15 : 7;
    stratum::Workspace<T> ws;
    for (int r = 0; r < reps; ++r) {
        for (std::size_t k = 0; k < ratiosTimes2.size(); ++k) {
            const std::size_t i = (k + static_cast<std::size_t>(r)) % ratiosTimes2.size();
            const stratum::StratumSort<T> s(lambda, lambda * ratiosTimes2[i] / 2);
            std::vector<T> d = input;
            const auto t0 = Clock::now();
            s.sort(d, ws);
            times[i].push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
        }
    }
    std::vector<double> med(ratiosTimes2.size());
    for (std::size_t i = 0; i < med.size(); ++i) med[i] = medianOf(times[i]);
    const double best = *std::min_element(med.begin(), med.end());
    std::printf("t-sweep l=%-4zu %-14s %9zu |", lambda, shape.c_str(), n);
    for (std::size_t i = 0; i < med.size(); ++i) std::printf(" t/l=%-4.1f %5.2f", ratiosTimes2[i] / 2.0, med[i] / best);
    std::printf("\n");
    std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--t-sweep") == 0) {
        for (std::size_t lambda : {std::size_t{16}, std::size_t{32}})
            for (const char* shape : {"random", "normal", "nearly_sorted", "adversarial", "clustered"})
                for (std::size_t n : {std::size_t{100000}, std::size_t{1000000}}) tSweep<int64_t>(shape, n, lambda);
        return 0;
    }
    std::string type = "int64", shape = "random";
    std::vector<std::size_t> sizes;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--type") && i + 1 < argc) type = argv[++i];
        else if (!std::strcmp(argv[i], "--shape") && i + 1 < argc) shape = argv[++i];
        else sizes.push_back(static_cast<std::size_t>(std::atoll(argv[i])));
    }
    if (sizes.empty()) sizes = {10000, 100000, 1000000, 10000000};
    const std::vector<std::size_t> lambdas = {8, 16, 24, 32, 48, 64, 96, 128, 192, 256, 512, 1024};
    std::printf("time / time(best lambda), t = 2*lambda\n%-8s %-10s %9s | %-26s|", "key", "shape", "n", "");
    for (std::size_t l : lambdas) std::printf(" %5zu", l);
    std::printf("\n");
    for (std::size_t n : sizes) {
        if (type == "uint32") sweep<uint32_t>(shape, n, lambdas);
        else if (type == "uint16") sweep<uint16_t>(shape, n, lambdas);
        else if (type == "uint8") sweep<uint8_t>(shape, n, lambdas);
        else sweep<int64_t>(shape, n, lambdas);
    }
    return 0;
}
