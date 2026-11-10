// ============================================================
// Release-configuration timings against std::sort
// ============================================================
// This is the ONLY tool in the project whose timings may be quoted.
//
// `make baseline` cannot be: BenchmarkRunner.hpp reads sorter.metrics(),
// so it hard-requires STRATUM_ENABLE_METRICS, and a research binary carries
// counters, timers and per-bin bookkeeping that a caller never pays for.
// The distortion is not uniform - it lands on whichever phase happens to
// increment counters most - so a research table cannot be corrected by
// scaling it either. This project has already published one number that
// was an artefact of exactly that mistake.
//
// So: no instrumentation, and no counters printed - because it does not
// have any. What it loses in explanatory power it gains in being true.
//
// Stratum Sort and std::sort are alternated within each repetition, on
// identical copies of the same input, so drift in machine state hits both
// equally. The std::sort output doubles as the reference the Stratum Sort
// output is checked against, so correctness is verified on every
// repetition at no extra cost.
//
// The battery is objective.md section 4 (datasets/Battery.hpp): the ten
// current datasets, the eight new ones and the eight AdversaryBattery
// families, with the adversaries rebuilt against the lambda and depth cap
// of the headers this binary was compiled with. The A/B driver
// (research/tools/ab_compare.sh) compiles this same file against two
// commits' headers and runs the binaries alternately with --csv --reps 1.
//
// Usage: timings [--sizes LIST|all] [--types LIST|all] [--datasets LIST]
//                [--reps N] [--warmup N] [--seed S] [--csv] [--tag TEXT]
//   LIST is comma-separated. Sizes accept 1e5 notation; "all" is
//   1e3,1e4,1e5,1e6,1e7. Types: int64, uint32. Defaults: --sizes 1e5,1e6
//   --types int64 --reps 9, every dataset, human-readable table.
//   --csv prints one line per repetition, for the A/B and for spreadsheets.
//   --warmup N runs N untimed sorts of each kind on every input first: with
//   --reps 1 (one repetition per process, as the A/B runs it) the first
//   sort otherwise pays the process's cold page faults and allocator
//   growth. Measured on a 4-core x86 VM, one A/B of the same code each
//   way: the median per-repetition |B/A - 1| at n = 1e4 went from 12.7 %
//   without warm-up to 6.5 % with it; at 1e5 and 1e6 it did not change.
// Exit status: 0 if every sort matched std::sort, 1 otherwise, 2 on bad usage.
#include "Battery.hpp"
#include "SystemInfo.hpp"
#include "stratum/StratumSort.hpp"

#ifdef STRATUM_ENABLE_METRICS
#error "ReleaseTimings must be built WITHOUT metrics; its whole purpose is to time the shipped configuration."
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using stratum::testing::AdversaryTarget;
using stratum::testing::battery;
using stratum::testing::batteryDataset;
using stratum::testing::batteryIndex;

namespace {

struct Options {
    std::vector<std::size_t> sizes{100000, 1000000};
    std::vector<std::string> types{"int64"};
    std::vector<std::size_t> datasets; // indices into battery(); empty = all
    std::size_t reps = 9;
    std::size_t warmup = 0;
    uint64_t seed = 42;
    bool csv = false;
    std::string tag = "-";
};

std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ','))
        if (!item.empty()) out.push_back(item);
    return out;
}

[[noreturn]] void usage(const std::string& why) {
    std::cerr << "timings: " << why << "\n"
              << "usage: timings [--sizes LIST|all] [--types int64,uint32|all] [--datasets LIST]\n"
              << "               [--reps N] [--warmup N] [--seed S] [--csv] [--tag TEXT]\n"
              << "datasets:";
    for (const auto& e : battery()) std::cerr << " " << e.name;
    std::cerr << "\n";
    std::exit(2);
}

// "1e5", "100000" and "1E6" are all accepted; anything else is an error.
std::size_t parseSize(const std::string& s) {
    const std::size_t e = s.find_first_of("eE");
    char* end = nullptr;
    if (e == std::string::npos) {
        const unsigned long long v = std::strtoull(s.c_str(), &end, 10);
        if (*end != '\0' || v == 0) usage("bad size '" + s + "'");
        return static_cast<std::size_t>(v);
    }
    const unsigned long long mant = std::strtoull(s.substr(0, e).c_str(), &end, 10);
    if (*end != '\0') usage("bad size '" + s + "'");
    const unsigned long long ex = std::strtoull(s.substr(e + 1).c_str(), &end, 10);
    if (*end != '\0' || ex > 9 || mant == 0) usage("bad size '" + s + "'");
    unsigned long long v = mant;
    for (unsigned long long i = 0; i < ex; ++i) v *= 10;
    return static_cast<std::size_t>(v);
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) usage("missing value for " + a);
            return argv[++i];
        };
        if (a == "--sizes") {
            const std::string v = value();
            o.sizes.clear();
            for (const auto& s : splitList(v == "all" ? "1e3,1e4,1e5,1e6,1e7" : v))
                o.sizes.push_back(parseSize(s));
        } else if (a == "--types") {
            const std::string v = value();
            o.types = splitList(v == "all" ? "int64,uint32" : v);
            for (const auto& t : o.types)
                if (t != "int64" && t != "uint32") usage("unknown type '" + t + "'");
        } else if (a == "--datasets") {
            for (const auto& d : splitList(value())) {
                const std::size_t idx = batteryIndex(d);
                if (idx == battery().size()) usage("unknown dataset '" + d + "'");
                o.datasets.push_back(idx);
            }
        } else if (a == "--reps") {
            o.reps = static_cast<std::size_t>(std::strtoull(value().c_str(), nullptr, 10));
            if (o.reps == 0) usage("--reps must be >= 1");
        } else if (a == "--warmup") {
            o.warmup = static_cast<std::size_t>(std::strtoull(value().c_str(), nullptr, 10));
        } else if (a == "--seed") {
            o.seed = std::strtoull(value().c_str(), nullptr, 10);
        } else if (a == "--csv") {
            o.csv = true;
        } else if (a == "--tag") {
            o.tag = value();
        } else if (a == "--help" || a == "-h") {
            usage("help");
        } else {
            usage("unknown option '" + a + "'");
        }
    }
    if (o.datasets.empty())
        for (std::size_t i = 0; i < battery().size(); ++i) o.datasets.push_back(i);
    return o;
}

double medianOf(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

bool g_allCorrect = true;

template <typename T>
void runType(const Options& o, const char* typeName) {
    // The configuration being measured: the lambda a default-constructed
    // sorter really uses, and the depth cap of these headers. The
    // adversaries are rebuilt against exactly this.
    const AdversaryTarget target{stratum::StratumSort<T>().targetElementsPerBin(),
                                 stratum::MAX_SUBDIVISION_DEPTH};

    for (const std::size_t n : o.sizes) {
        if (!o.csv) {
            std::cout << "\n---------------- " << typeName << "  n = " << n
                      << " ----------------\n"
                      << std::left << std::setw(22) << "Dataset" << std::right << std::setw(10)
                      << "n_real" << std::setw(12) << "stratum(ms)" << std::setw(11) << "std(ms)"
                      << std::setw(8) << "ratio" << std::setw(15) << "range" << std::setw(5)
                      << "OK" << "\n"
                      << std::string(83, '-') << "\n";
        }
        for (const std::size_t idx : o.datasets) {
            const char* name = battery()[idx].name;
            const std::vector<T> input = batteryDataset<T>(idx, n, target, o.seed);
            if (input.empty()) {
                // Not constructible at this size (adversary families 7/8).
                if (o.csv) {
                    std::cout << o.tag << "," << typeName << "," << name << "," << n
                              << ",0,0,,,na\n";
                } else {
                    std::cout << std::left << std::setw(22) << name << std::right
                              << std::setw(10) << "n/a" << "   (family not constructible at this n)\n";
                }
                continue;
            }

            for (std::size_t w = 0; w < o.warmup; ++w) {
                std::vector<T> data = input;
                stratum::StratumSort<T>().sort(data);
                std::vector<T> reference = input;
                std::sort(reference.begin(), reference.end());
            }

            std::vector<double> sortMs, stdMs, ratios;
            bool correct = true;
            for (std::size_t r = 0; r < o.reps; ++r) {
                std::vector<T> data = input;
                stratum::StratumSort<T> sorter;
                const auto t0 = std::chrono::steady_clock::now();
                sorter.sort(data);
                const auto t1 = std::chrono::steady_clock::now();

                std::vector<T> reference = input;
                const auto t2 = std::chrono::steady_clock::now();
                std::sort(reference.begin(), reference.end());
                const auto t3 = std::chrono::steady_clock::now();

                const double a = std::chrono::duration<double, std::milli>(t1 - t0).count();
                const double b = std::chrono::duration<double, std::milli>(t3 - t2).count();
                const bool ok = data == reference;
                correct = correct && ok;
                sortMs.push_back(a);
                stdMs.push_back(b);
                ratios.push_back(b > 0.0 ? a / b : 0.0);
                if (o.csv) {
                    std::cout << o.tag << "," << typeName << "," << name << "," << n << ","
                              << input.size() << "," << (r + 1) << "," << std::fixed
                              << std::setprecision(6) << a << "," << b << ","
                              << (ok ? "ok" : "MISMATCH") << "\n";
                }
            }
            if (!correct) g_allCorrect = false;
            if (!o.csv) {
                const auto mm = std::minmax_element(ratios.begin(), ratios.end());
                std::ostringstream range;
                range << std::fixed << std::setprecision(2) << *mm.first << "-" << *mm.second;
                const double sm = medianOf(sortMs), dm = medianOf(stdMs);
                std::cout << std::left << std::setw(22) << name << std::right << std::setw(10)
                          << input.size() << std::fixed << std::setprecision(3) << std::setw(12)
                          << sm << std::setw(11) << dm << std::setprecision(2) << std::setw(8)
                          << medianOf(ratios) << std::setw(15) << range.str() << std::setw(5)
                          << (correct ? "si" : "NO") << "\n";
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    const Options o = parse(argc, argv);

    if (o.csv) {
        std::ostringstream info;
        stratum::SystemInfo::collect().print(info);
        std::string line;
        std::istringstream lines(info.str());
        while (std::getline(lines, line)) std::cout << "# " << line << "\n";
        std::cout << "# lambda=" << stratum::DEFAULT_TARGET_ELEMENTS_PER_BIN
                  << " t=" << stratum::DEFAULT_LEAF_THRESHOLD
                  << " D=" << stratum::MAX_SUBDIVISION_DEPTH << " reps=" << o.reps
                  << " warmup=" << o.warmup
                  << " seed=" << o.seed << "\n"
                  << "tag,type,dataset,n,n_real,rep,stratum_ms,std_ms,status\n";
    } else {
        std::cout << "================================================================\n"
                  << " Stratum Sort - RELEASE timings (no instrumentation)\n"
                  << "================================================================\n";
        stratum::SystemInfo::collect().print(std::cout);
        std::cout << "Repetitions per point: " << o.reps << "   lambda/t = defaults ("
                  << stratum::DEFAULT_TARGET_ELEMENTS_PER_BIN << "/"
                  << stratum::DEFAULT_LEAF_THRESHOLD << "), D = "
                  << stratum::MAX_SUBDIVISION_DEPTH << "\n"
                  << "ratio = median over repetitions of (Stratum Sort / std::sort) on the same\n"
                  << "input; range = min-max of those per-repetition ratios.\n";
    }

    for (const auto& t : o.types) {
        if (t == "int64") runType<int64_t>(o, "int64");
        else if (t == "uint32") runType<uint32_t>(o, "uint32");
    }

    if (!o.csv) {
        std::cout << "\nCounters are deliberately absent: this binary has none, which is why\n"
                     "its clock is the only one worth quoting. The deterministic counters\n"
                     "live in the instrumented build; never quote a timing from that one.\n"
                     "For a verdict (gana/empate/pierde) use the alternating A/B: make ab.\n";
    }
    if (!g_allCorrect) {
        std::cerr << "timings: Stratum Sort DISAGREED with std::sort on at least one input\n";
        return 1;
    }
    return 0;
}
