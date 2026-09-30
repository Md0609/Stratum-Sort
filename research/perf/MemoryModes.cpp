// ============================================================
// MemoryModes - time of each memory policy, on the same inputs
// ============================================================
// The companion of MemoryAudit.cpp: that one measures the bytes, this one
// what each byte saved costs. For every (type, shape, n) the modes below
// and std::sort run alternately on identical copies of the input, in a
// rotating order, each with a FRESH workspace per call (allocation and
// first touch included, as a caller that sorts once pays them); the table
// reports medians.
//
//   unl     Workspace::kUnlimited, the default: the partner buffer at
//           every size (also the behaviour before the memory study)
//   cur     Workspace::kAutomatic (the default during the study, opt-in
//           since): partner up to 16 MiB, bounded to 16 MiB above
//   b<N>    an explicit budget of N bytes (k/m suffixes): e.g. b1m, b600k,
//           b0 (the floor: the counter arena alone, every pass a flag pass)
//   std     std::sort (std::stable_sort for the stable rows)
//
// A trailing 's' on the type runs the stable sort (stable_sort /
// stable_sort_by_key).
//
//   make research-perf
//   ./build/perf_MemoryModes [--n 1e6,1e7] [--types u64,rec72] [--shapes random,organ_pipe]
//                            [--modes unl,cur,b1m,b0,std] [--reps 7]
#include "BenchDatasets.hpp"

#include "stratum/StratumSort.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

template <std::size_t P>
struct Rec {
    uint64_t key;
    unsigned char payload[P];
};
template <typename T>
struct IsRecord : std::false_type {};
template <std::size_t P>
struct IsRecord<Rec<P>> : std::true_type {};

template <typename T>
uint64_t keyOf(const T& x) {
    if constexpr (IsRecord<T>::value) return x.key;
    else return stratum::OrderedKey<T>::key(x);
}

template <typename T>
std::vector<T> makeInput(const std::string& shape, std::size_t n) {
    if constexpr (IsRecord<T>::value) {
        const std::vector<uint64_t> keys = stratum::bench::makeShape<uint64_t>(shape, n, 42);
        std::vector<T> v(n);
        for (std::size_t i = 0; i < n; ++i) {
            v[i].key = keys[i];
            std::memset(v[i].payload, static_cast<int>(i & 0xFF), sizeof v[i].payload);
        }
        return v;
    } else if constexpr (std::is_floating_point<T>::value) {
        return stratum::bench::makeFloatShape<T>(shape, n, 42);
    } else {
        return stratum::bench::makeShape<T>(shape, n, 42);
    }
}

// The budget a mode name stands for; false for "std".
template <typename T>
bool budgetOf(const std::string& mode, std::size_t& budget) {
    if (mode == "std") return false;
    if (mode == "unl") budget = stratum::Workspace<T>::kUnlimited;
    else if (mode == "cur") budget = stratum::Workspace<T>::kAutomatic;
    else {
        char* end = nullptr;
        budget = static_cast<std::size_t>(std::strtod(mode.c_str() + 1, &end));
        if (*end == 'k') budget <<= 10;
        if (*end == 'm') budget <<= 20;
    }
    return true;
}

// Runs one mode; returns false when it refuses (a stable sort of records
// under a budget below n records). 'bytes' receives the workspace's size.
template <typename T>
bool runMode(const std::string& mode, bool stable, std::vector<T>& d, std::size_t& bytes) {
    auto less = [](const T& a, const T& b) { return keyOf(a) < keyOf(b); };
    std::size_t budget = 0;
    if (!budgetOf<T>(mode, budget)) {
        if (stable) std::stable_sort(d.begin(), d.end(), less);
        else std::sort(d.begin(), d.end(), less);
        bytes = 0;
        return true;
    }
    stratum::Workspace<T> ws(budget);
    try {
        if constexpr (IsRecord<T>::value) {
            auto key = [](const T& r) { return r.key; };
            if (stable) stratum::stable_sort_by_key(d, key, ws);
            else stratum::sort_by_key(d, key, ws);
        } else {
            if (stable) stratum::stable_sort(d, ws);
            else stratum::sort(d, ws);
        }
    } catch (const std::length_error&) {
        return false;
    }
    bytes = ws.bytes();
    return true;
}

template <typename T>
void run(const std::string& typeName, bool stable, const std::string& shape, std::size_t n,
         const std::vector<std::string>& modes, int reps) {
    const std::vector<T> input = makeInput<T>(shape, n);
    const std::size_t M = modes.size();
    std::vector<std::vector<double>> times(M);
    std::vector<std::size_t> bytes(M, 0);
    std::vector<bool> refused(M, false);
    bool wrong = false;
    for (int r = 0; r < reps; ++r) {
        for (std::size_t i = 0; i < M; ++i) {
            const std::size_t k = (i + static_cast<std::size_t>(r)) % M;
            if (refused[k]) continue;
            std::vector<T> d = input;
            const auto t0 = std::chrono::steady_clock::now();
            const bool ran = runMode(modes[k], stable, d, bytes[k]);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (!ran) {
                refused[k] = true;
                continue;
            }
            times[k].push_back(ms);
            for (std::size_t j = 1; j < n; ++j)
                if (keyOf(d[j]) < keyOf(d[j - 1])) {
                    wrong = true;
                    break;
                }
        }
    }
    std::vector<double> med(M, 0);
    for (std::size_t k = 0; k < M; ++k) {
        if (refused[k]) continue;
        std::sort(times[k].begin(), times[k].end());
        med[k] = times[k][times[k].size() / 2];
    }
    // CSV: one line per mode, the first mode is the reference.
    for (std::size_t k = 0; k < M; ++k) {
        if (refused[k]) {
            std::printf("%s%s,%s,%zu,%s,refused,,,,\n", typeName.c_str(), stable ? "s" : "", shape.c_str(), n,
                        modes[k].c_str());
            continue;
        }
        std::printf("%s%s,%s,%zu,%s,%.3f,%.4f,%.4f,%zu,%s\n", typeName.c_str(), stable ? "s" : "", shape.c_str(), n,
                    modes[k].c_str(), med[k], med[0] > 0 ? med[k] / med[0] : 0.0,
                    static_cast<double>(bytes[k]) / static_cast<double>(n), bytes[k], wrong ? "WRONG" : "ok");
    }
    std::fflush(stdout);
}

template <typename T>
void runAll(const std::string& typeName, bool stable, const std::vector<std::string>& shapes, std::size_t n,
            const std::vector<std::string>& modes, int reps) {
    for (const auto& sh : shapes) {
        if (sh == "qsort_killer" && n > 1000000) continue; // its generator is quadratic in n
        run<T>(typeName, stable, sh, n, modes, reps);
    }
}

void runType(const std::string& t, const std::vector<std::string>& shapes, std::size_t n,
             const std::vector<std::string>& modes, int reps) {
    const bool stable = !t.empty() && t.back() == 's';
    const std::string base = stable ? t.substr(0, t.size() - 1) : t;
    if (base == "u8") runAll<uint8_t>(base, stable, shapes, n, modes, reps);
    else if (base == "u16") runAll<uint16_t>(base, stable, shapes, n, modes, reps);
    else if (base == "u32") runAll<uint32_t>(base, stable, shapes, n, modes, reps);
    else if (base == "u64") runAll<uint64_t>(base, stable, shapes, n, modes, reps);
    else if (base == "f32") runAll<float>(base, stable, shapes, n, modes, reps);
    else if (base == "f64") runAll<double>(base, stable, shapes, n, modes, reps);
    else if (base == "rec16") runAll<Rec<8>>(base, stable, shapes, n, modes, reps);
    else if (base == "rec72") runAll<Rec<64>>(base, stable, shapes, n, modes, reps);
    else if (base == "rec264") runAll<Rec<256>>(base, stable, shapes, n, modes, reps);
    else std::fprintf(stderr, "unknown type %s\n", t.c_str());
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) out.push_back(item);
    return out;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> sizes = {"1e6"};
    std::vector<std::string> types = {"u64"};
    std::vector<std::string> shapes = stratum::bench::allShapes();
    std::vector<std::string> modes = {"unl", "cur", "b1m", "b600k", "b0", "std"};
    int reps = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string k = argv[i], v = argv[i + 1];
        if (k == "--n") sizes = split(v);
        else if (k == "--types") types = split(v);
        else if (k == "--shapes") shapes = split(v);
        else if (k == "--modes") modes = split(v);
        else if (k == "--reps") reps = std::atoi(v.c_str());
    }
    std::printf("type,shape,n,mode,ms,ratio_to_first,aux_bytes_per_elem,aux_bytes,ok\n");
    for (const auto& ns : sizes) {
        const std::size_t n = static_cast<std::size_t>(std::strtod(ns.c_str(), nullptr));
        const int r = reps > 0 ? reps : (n >= 50000000 ? 3 : n >= 10000000 ? 5 : n >= 1000000 ? 9 : 21);
        for (const auto& t : types) runType(t, shapes, n, modes, r);
    }
    return 0;
}
