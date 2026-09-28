// ============================================================
// VersionTimings - 0.10.0 against the current header, against std::sort
// ============================================================
// Release configuration, one process, the three sorts ALTERNATED inside
// every repetition on identical copies of the same input, so that drift in
// machine state hits all three equally. This is the before/after instrument
// for every 0.11.0 change: comparing numbers taken in separate sessions
// produced a false 8.8% result in this project once already.
//
// Each Stratum sort runs on a FRESH instance, as a caller who writes
// `StratumSort<T>().sort(v)` would: the allocation and the first touch of
// the scratch are part of what that caller pays. The "reuse" column runs
// the current header with one workspace kept across repetitions.
//
//   make research-perf && ./build/perf_VersionTimings [n...] [--shapes a,b,c] [--type int64|uint32|uint8]
#include "BenchDatasets.hpp"

#include "stratum/StratumSort.hpp"
#include "v0_10_0/StratumSort.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double medianOf(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}

template <typename F>
double timeMs(F&& f) {
    const auto t0 = Clock::now();
    f();
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// The adversarial shapes are built against a lambda: an adversary built for
// one lambda is just another input for a different one. 0.10.0 defaults to
// lambda = 32, 0.11.0 to 16 (automatic), so each version is timed against
// the adversary built for ITS OWN default - the only fair comparison. std::sort
// gets the one built for the current default.
template <typename T>
std::vector<T> oldInputFor(const std::string& shape, std::size_t n, const std::vector<T>& current) {
    if (shape != "adversarial" && shape != "worst_case") return current;
    stratum::testing::DatasetGenerator g(42);
    const auto a = g.adversarialPeeling(n, stratum_v0_10_0::DEFAULT_TARGET_ELEMENTS_PER_BIN,
                                        shape == "adversarial" ? 0 : 4096);
    std::vector<T> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<T>(a[i]);
    return v;
}

template <typename T>
void runShape(const std::string& shape, std::size_t n, int reps) {
    const std::vector<T> input = stratum::bench::makeShape<T>(shape, n);
    std::vector<T> expected = input;
    std::sort(expected.begin(), expected.end());
    const std::vector<T> inputOld = oldInputFor(shape, n, input);
    std::vector<T> expectedOld = inputOld;
    std::sort(expectedOld.begin(), expectedOld.end());

    std::vector<double> tOld, tNew, tReuse, tStd;
    bool ok = true;
    stratum::Workspace<T> ws;
    const stratum::StratumSort<T> shared;
    for (int r = 0; r < reps; ++r) {
        {
            std::vector<T> d = inputOld;
            tOld.push_back(timeMs([&] { stratum_v0_10_0::StratumSort<T>().sort(d); }));
            ok = ok && d == expectedOld;
        }
        {
            std::vector<T> d = input;
            tNew.push_back(timeMs([&] { stratum::StratumSort<T>().sort(d); }));
            ok = ok && d == expected;
        }
        {
            std::vector<T> d = input;
            tReuse.push_back(timeMs([&] { shared.sort(d, ws); }));
            ok = ok && d == expected;
        }
        {
            std::vector<T> d = input;
            tStd.push_back(timeMs([&] { std::sort(d.begin(), d.end()); }));
        }
    }
    const double o = medianOf(tOld), nw = medianOf(tNew), ru = medianOf(tReuse), sd = medianOf(tStd);
    std::printf("%-15s %9zu | %9.3f %9.3f %9.3f %9.3f | %+7.1f%% %+7.1f%% | %6.2f %6.2f %s%s\n",
                shape.c_str(), n, o, nw, ru, sd, 100.0 * (nw / o - 1.0), 100.0 * (ru / o - 1.0),
                o / sd, nw / sd, ok ? "" : " WRONG",
                &inputOld == &input || inputOld == input ? "" : "  (each vs its own adversary)");
    std::fflush(stdout);
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) out.push_back(item);
    return out;
}

template <typename T>
void runAll(const std::vector<std::size_t>& sizes, const std::vector<std::string>& shapes) {
    for (std::size_t n : sizes) {
        const int reps = n <= 100000 ? 31 : n <= 1000000 ? 11 : 5;
        std::printf("\n%-15s %9s | %9s %9s %9s %9s | %8s %8s | %6s %6s\n", "shape", "n", "v0.10 ms",
                    "new ms", "reuse ms", "std ms", "new/old", "reuse", "old/std", "new/std");
        for (const std::string& s : shapes)
            if (stratum::bench::shapeApplies<T>(s)) runShape<T>(s, n, reps);
    }
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::size_t> sizes;
    std::vector<std::string> shapes = stratum::bench::allShapes();
    std::string type = "int64";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--shapes") == 0 && i + 1 < argc) shapes = split(argv[++i]);
        else if (std::strcmp(argv[i], "--type") == 0 && i + 1 < argc) type = argv[++i];
        else sizes.push_back(static_cast<std::size_t>(std::atoll(argv[i])));
    }
    if (sizes.empty()) sizes = {100000, 1000000};
    std::printf("Median ms; each Stratum sort on a fresh instance except 'reuse'. key = %s\n",
                type.c_str());
    if (type == "uint8") runAll<uint8_t>(sizes, shapes);
    else if (type == "uint16") runAll<uint16_t>(sizes, shapes);
    else if (type == "uint32") runAll<uint32_t>(sizes, shapes);
    else if (type == "int32") runAll<int32_t>(sizes, shapes);
    else runAll<int64_t>(sizes, shapes);
    return 0;
}
