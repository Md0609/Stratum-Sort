// ============================================================
// stratum_bench - reproducible cross-platform benchmark
// ============================================================
// The one benchmark whose numbers the documentation may quote, and the one
// CI runs on Linux x86_64, macOS arm64 and Windows x86_64
// (.github/workflows/bench.yml). It exists because 0.10.0's timings came
// from one machine, and this project has repeatedly seen timing
// conclusions invert between machines.
//
// WHAT IT MEASURES, for every (key type, n, input shape):
//   - every contender, ALTERNATED inside each repetition on identical
//     copies of the input, in a rotating order, so drift hits all equally;
//   - the distribution of the wall clock, not one number: min, p10,
//     median, p90, max - a mean hides a regression in the tail;
//   - ns per element and million elements per second, from the median;
//   - the ratio to std::sort on the same input in the same session;
//   - peak auxiliary bytes, measured at the allocator on an untimed run;
//   - comparisons, for the comparison sorts, on an untimed run;
//   - correctness: every timed output is checked against std::sort.
// and it prints the compiler, its version, the language standard, the
// build flags and configuration, the CPU, its caches, the OS and the
// architecture, so a number can always be traced to where it came from.
//
// Stratum Sort runs two ways: on a FRESH instance per call (what
// `StratumSort<T>().sort(v)` costs, allocation and first touch included)
// and with one workspace REUSED across calls (the steady state of a
// program that sorts repeatedly).
//
// SUITES
//   --suite quick    int64, n = 1e6, every shape                (~1 min)
//   --suite ci       int64/uint32/uint8, n = 1e5 and 1e6, every shape,
//                    n = 1e7 on four shapes, plus the lambda sweep and the
//                    memory policies (~12 min)
//   --suite types    float, double and key/value records alone
//   --suite lambda   the lambda sweep alone
//   --suite memory   the memory policies alone (also part of ci and full):
//                    unlimited / automatic / 600 KB / floor budgets
//   --suite full     the ci suite at more sizes and repetitions
// Options: --md FILE --csv FILE --reps K --sizes a,b --types a,b
//          --shapes a,b --comparisons
//
// Build: `make bench` (Makefile) or the stratum_bench target (CMake,
// -DSTRATUMSORT_BUILD_BENCHMARKS=ON). Release configuration only.
// ============================================================
#include "AllocationTracker.hpp"
#include "BenchDatasets.hpp"
#include "SystemInfo.hpp"
#include "stratum/StratumSort.hpp"

#ifdef STRATUM_ENABLE_METRICS
#error "stratum_bench must be built WITHOUT metrics: it times the shipped configuration."
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// ---- Options -----------------------------------------------------------
struct Options {
    std::string suite = "quick";
    std::string mdPath;
    std::string csvPath;
    int reps = 0; // 0: suite default
    std::vector<std::size_t> sizes;
    std::vector<std::string> types;
    std::vector<std::string> shapes;
    bool comparisons = false;
};

std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) if (!item.empty()) out.push_back(item);
    return out;
}

// ---- Output sinks --------------------------------------------------------
struct Report {
    std::ostringstream md;
    std::ofstream csv;
    void line(const std::string& s) {
        std::cout << s << "\n";
        md << s << "\n";
    }
};

// ---- Statistics ----------------------------------------------------------
struct Stats {
    double min = 0, p10 = 0, median = 0, p90 = 0, max = 0;
};

Stats summarise(std::vector<double> v) {
    Stats s;
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    auto at = [&](double q) {
        const double pos = q * static_cast<double>(v.size() - 1);
        const std::size_t lo = static_cast<std::size_t>(std::floor(pos));
        const std::size_t hi = std::min(lo + 1, v.size() - 1);
        return v[lo] + (v[hi] - v[lo]) * (pos - static_cast<double>(lo));
    };
    s.min = v.front();
    s.p10 = at(0.10);
    s.median = at(0.50);
    s.p90 = at(0.90);
    s.max = v.back();
    return s;
}

template <typename F>
double timeMs(F&& f) {
    const auto t0 = Clock::now();
    f();
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

template <typename T>
const char* typeName() {
    if (std::is_same<T, int64_t>::value) return "int64";
    if (std::is_same<T, uint64_t>::value) return "uint64";
    if (std::is_same<T, int32_t>::value) return "int32";
    if (std::is_same<T, uint32_t>::value) return "uint32";
    if (std::is_same<T, int16_t>::value) return "int16";
    if (std::is_same<T, uint16_t>::value) return "uint16";
    if (std::is_same<T, int8_t>::value) return "int8";
    if (std::is_same<T, uint8_t>::value) return "uint8";
    return "?";
}

// ---- Contenders ----------------------------------------------------------
template <typename T>
struct Contender {
    std::string name;
    std::function<void(std::vector<T>&)> run;
    std::function<std::size_t(std::vector<T>&)> countComparisons; // empty: n/a
};

template <typename T>
std::vector<Contender<T>> contenders(stratum::Workspace<T>& ws, const stratum::StratumSort<T>& shared) {
    std::vector<Contender<T>> c;
    c.push_back({"stratum", [](std::vector<T>& v) { stratum::StratumSort<T>().sort(v); }, {}});
    c.push_back({"stratum_ws", [&](std::vector<T>& v) { shared.sort(v, ws); }, {}});
    c.push_back({"std::sort", [](std::vector<T>& v) { std::sort(v.begin(), v.end()); },
                 [](std::vector<T>& v) {
                     std::size_t k = 0;
                     std::sort(v.begin(), v.end(), [&](const T& a, const T& b) { ++k; return a < b; });
                     return k;
                 }});
    c.push_back({"std::stable_sort", [](std::vector<T>& v) { std::stable_sort(v.begin(), v.end()); },
                 [](std::vector<T>& v) {
                     std::size_t k = 0;
                     std::stable_sort(v.begin(), v.end(), [&](const T& a, const T& b) { ++k; return a < b; });
                     return k;
                 }});
    return c;
}

// ---- One case ---------------------------------------------------------------
struct CaseResult {
    std::string algorithm;
    Stats ms;
    std::size_t peakBytes = 0;
    std::size_t comparisons = 0;
    bool haveComparisons = false;
    bool correct = true;
};

template <typename T>
std::vector<CaseResult> runCase(const std::string& shape, std::size_t n, int reps, bool withComparisons) {
    const std::vector<T> input = stratum::bench::makeShape<T>(shape, n);
    std::vector<T> expected = input;
    std::sort(expected.begin(), expected.end());

    stratum::Workspace<T> ws;
    const stratum::StratumSort<T> shared;
    std::vector<Contender<T>> cs = contenders<T>(ws, shared);
    std::vector<std::vector<double>> times(cs.size());
    std::vector<CaseResult> results(cs.size());
    for (std::size_t i = 0; i < cs.size(); ++i) results[i].algorithm = cs[i].name;

    // One untimed warm-up of each, which also fills the reused workspace:
    // the "stratum_ws" row is the steady state by definition.
    for (auto& c : cs) {
        std::vector<T> d = input;
        c.run(d);
    }
    for (int r = 0; r < reps; ++r) {
        for (std::size_t k = 0; k < cs.size(); ++k) {
            const std::size_t i = (k + static_cast<std::size_t>(r)) % cs.size();
            std::vector<T> d = input;
            times[i].push_back(timeMs([&] { cs[i].run(d); }));
            if (d != expected) results[i].correct = false;
        }
    }
    for (std::size_t i = 0; i < cs.size(); ++i) {
        results[i].ms = summarise(times[i]);
        {
            // Peak auxiliary memory, untimed. The reused workspace is
            // released first so that its row shows what one sort needs,
            // not what an earlier, larger sort left behind.
            if (cs[i].name == "stratum_ws") ws.release();
            std::vector<T> d = input;
            stratum::bench::AllocationScope scope;
            cs[i].run(d);
            results[i].peakBytes = scope.peakBytes();
        }
        if (withComparisons && cs[i].countComparisons) {
            std::vector<T> d = input;
            results[i].comparisons = cs[i].countComparisons(d);
            results[i].haveComparisons = true;
        }
    }
    return results;
}

std::string fmt(const char* f, double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

template <typename T>
void runTable(Report& rep, const std::vector<std::string>& shapes, std::size_t n, int reps,
              bool withComparisons) {
    rep.line("");
    rep.line(std::string("### ") + typeName<T>() + ", n = " + std::to_string(n) + ", " +
             std::to_string(reps) + " repetitions");
    rep.line("");
    rep.line("| shape | Stratum median ms (p10-p90) | ns/elem | Melem/s | vs std::sort | reused ws vs std | "
             "std::sort ms | std::stable_sort ms | Stratum aux B/elem | ok |");
    rep.line("|---|---|---|---|---|---|---|---|---|---|");
    for (const std::string& shape : shapes) {
        if (!stratum::bench::shapeApplies<T>(shape)) continue;
        const std::vector<CaseResult> r = runCase<T>(shape, n, reps, withComparisons);
        const CaseResult& st = r[0];
        const CaseResult& ws = r[1];
        const CaseResult& sd = r[2];
        const CaseResult& ss = r[3];
        const double nsPer = st.ms.median * 1e6 / static_cast<double>(n);
        const bool ok = st.correct && ws.correct && sd.correct && ss.correct;
        rep.line("| " + shape + " | " + fmt("%.3f", st.ms.median) + " (" + fmt("%.3f", st.ms.p10) + "-" +
                 fmt("%.3f", st.ms.p90) + ") | " + fmt("%.2f", nsPer) + " | " +
                 fmt("%.1f", static_cast<double>(n) / (st.ms.median * 1e3)) + " | " +
                 fmt("%.2fx", st.ms.median / sd.ms.median) + " | " +
                 fmt("%.2fx", ws.ms.median / sd.ms.median) + " | " + fmt("%.3f", sd.ms.median) +
                 " | " + fmt("%.3f", ss.ms.median) + " | " +
                 fmt("%.2f", static_cast<double>(st.peakBytes) / static_cast<double>(n)) + " | " +
                 (ok ? "yes" : "**NO**") + " |");
        if (rep.csv.is_open()) {
            for (const CaseResult& c : r) {
                rep.csv << typeName<T>() << ',' << n << ',' << shape << ',' << c.algorithm << ','
                        << c.ms.min << ',' << c.ms.p10 << ',' << c.ms.median << ',' << c.ms.p90 << ','
                        << c.ms.max << ',' << c.ms.median * 1e6 / static_cast<double>(n) << ','
                        << c.ms.median / sd.ms.median << ',' << c.peakBytes << ','
                        << (c.haveComparisons ? std::to_string(c.comparisons) : std::string("")) << ','
                        << (c.correct ? 1 : 0) << '\n';
            }
            rep.csv.flush();
        }
        std::cout.flush();
    }
}

// ---- Floating point ---------------------------------------------------------------
template <typename F>
void runFloatTable(Report& rep, std::size_t n, int reps) {
    rep.line("");
    rep.line(std::string("### ") + (sizeof(F) == 4 ? "float" : "double") + ", n = " + std::to_string(n) +
             " (stratum::sort, IEEE totalOrder; no NaN, so std::sort is well defined)");
    rep.line("");
    rep.line("| shape | stratum::sort ms (p10-p90) | ns/elem | vs std::sort | std::sort ms | aux B/elem | ok |");
    rep.line("|---|---|---|---|---|---|---|");
    for (const std::string& shape : stratum::bench::floatShapes()) {
        const std::vector<F> input = stratum::bench::makeFloatShape<F>(shape, n);
        std::vector<F> expected = input;
        std::sort(expected.begin(), expected.end());
        std::vector<double> ts, tsd;
        bool ok = true;
        for (int r = 0; r < reps; ++r) {
            for (int k = 0; k < 2; ++k) {
                std::vector<F> d = input;
                if ((k + r) % 2 == 0) {
                    ts.push_back(timeMs([&] { stratum::sort(d); }));
                    ok = ok && d == expected; // -0 == +0 under ==, both orders are valid
                } else {
                    tsd.push_back(timeMs([&] { std::sort(d.begin(), d.end()); }));
                }
            }
        }
        std::size_t peak = 0;
        {
            std::vector<F> d = input;
            stratum::bench::AllocationScope scope;
            stratum::sort(d);
            peak = scope.peakBytes();
        }
        const Stats a = summarise(ts), b = summarise(tsd);
        rep.line("| " + shape + " | " + fmt("%.3f", a.median) + " (" + fmt("%.3f", a.p10) + "-" + fmt("%.3f", a.p90) +
                 ") | " + fmt("%.2f", a.median * 1e6 / static_cast<double>(n)) + " | " +
                 fmt("%.2fx", a.median / b.median) + " | " + fmt("%.3f", b.median) + " | " +
                 fmt("%.2f", static_cast<double>(peak) / static_cast<double>(n)) + " | " + (ok ? "yes" : "**NO**") + " |");
        if (rep.csv.is_open())
            rep.csv << (sizeof(F) == 4 ? "float" : "double") << ',' << n << ',' << shape << ",stratum::sort," << a.min
                    << ',' << a.p10 << ',' << a.median << ',' << a.p90 << ',' << a.max << ','
                    << a.median * 1e6 / static_cast<double>(n) << ',' << a.median / b.median << ',' << peak << ",,"
                    << (ok ? 1 : 0) << '\n';
    }
}

// ---- Key/value records -----------------------------------------------------------
template <std::size_t Bytes>
struct BenchRecord {
    uint64_t key;
    unsigned char payload[Bytes - sizeof(uint64_t)];
};

template <std::size_t Bytes>
void runRecordTable(Report& rep, std::size_t n, int reps) {
    using R = BenchRecord<Bytes>;
    rep.line("");
    rep.line("### " + std::to_string(Bytes) + "-byte records sorted by a uint64 key, n = " + std::to_string(n));
    rep.line("");
    rep.line("| keys | sort_by_key ms | vs std::sort | stable_sort_by_key ms | vs std::stable_sort | "
             "std::sort ms | std::stable_sort ms | stable aux B/elem | stable ok |");
    rep.line("|---|---|---|---|---|---|---|---|---|");
    for (const uint64_t distinct : {uint64_t{0}, uint64_t{1000}}) {
        std::mt19937_64 rng(Bytes + distinct);
        std::vector<R> input(n);
        for (std::size_t i = 0; i < n; ++i) {
            input[i].key = distinct ? rng() % distinct : rng();
            std::memset(input[i].payload, static_cast<int>(i & 0xFF), sizeof input[i].payload);
            std::memcpy(input[i].payload, &i, sizeof i < sizeof input[i].payload ? sizeof i : sizeof input[i].payload);
        }
        auto key = [](const R& r) { return r.key; };
        auto less = [](const R& a, const R& b) { return a.key < b.key; };
        std::vector<R> expected = input;
        std::stable_sort(expected.begin(), expected.end(), less);
        std::vector<double> t[4];
        bool ok = true;
        for (int r = 0; r < reps; ++r) {
            for (int k = 0; k < 4; ++k) {
                const int i = (k + r) % 4;
                std::vector<R> d = input;
                switch (i) {
                    case 0: t[0].push_back(timeMs([&] { stratum::sort_by_key(d, key); })); break;
                    case 1:
                        t[1].push_back(timeMs([&] { stratum::stable_sort_by_key(d, key); }));
                        ok = ok && std::memcmp(d.data(), expected.data(), n * sizeof(R)) == 0;
                        break;
                    case 2: t[2].push_back(timeMs([&] { std::sort(d.begin(), d.end(), less); })); break;
                    case 3: t[3].push_back(timeMs([&] { std::stable_sort(d.begin(), d.end(), less); })); break;
                }
            }
        }
        std::size_t peak = 0;
        {
            std::vector<R> d = input;
            stratum::bench::AllocationScope scope;
            stratum::stable_sort_by_key(d, key);
            peak = scope.peakBytes();
        }
        const double a = summarise(t[0]).median, b = summarise(t[1]).median, c = summarise(t[2]).median,
                     d = summarise(t[3]).median;
        rep.line(std::string("| ") + (distinct ? "1000 distinct" : "random 64-bit") + " | " + fmt("%.3f", a) + " | " +
                 fmt("%.2fx", a / c) + " | " + fmt("%.3f", b) + " | " + fmt("%.2fx", b / d) + " | " + fmt("%.3f", c) +
                 " | " + fmt("%.3f", d) + " | " + fmt("%.2f", static_cast<double>(peak) / static_cast<double>(n)) +
                 " | " + (ok ? "yes" : "**NO**") + " |");
    }
}

// ---- Memory policies ---------------------------------------------------------
// The same inputs under each workspace budget (Workspace.hpp): unlimited
// (the partner buffer, n elements), automatic (the default: the partner
// buffer up to 16 MiB, at most 16 MiB above), 600 KB (the in-place engine
// with its block buffers) and 0 (raised to the floor, the counter arena:
// every pass an American flag permutation). Time relative to the
// unlimited strategy and to std::sort; auxiliary bytes per element at the
// allocator. Pre-release study: research/history/V11_memoria.md.
template <typename T, typename Make, typename Sort, typename Less>
void memoryRow(Report& rep, const std::string& label, std::size_t n, int reps, Make make, Sort sortWith, Less less) {
    const std::vector<T> input = make();
    std::vector<T> expected = input;
    std::sort(expected.begin(), expected.end(), less);
    const std::size_t budgets[4] = {stratum::Workspace<T>::kUnlimited, stratum::Workspace<T>::kAutomatic,
                                    600 * 1024, 0};
    std::vector<double> t[5];
    bool ok = true;
    for (int r = 0; r < reps; ++r) {
        for (int k = 0; k < 5; ++k) {
            const int i = (k + r) % 5;
            std::vector<T> d = input;
            if (i == 4) {
                t[4].push_back(timeMs([&] { std::sort(d.begin(), d.end(), less); }));
            } else {
                stratum::Workspace<T> ws(budgets[i]);
                t[i].push_back(timeMs([&] { sortWith(d, ws); }));
            }
            for (std::size_t j = 1; j < n && ok; ++j) ok = !less(d[j], d[j - 1]);
        }
    }
    double peak[4];
    for (int i = 0; i < 4; ++i) {
        std::vector<T> d = input;
        stratum::bench::AllocationScope scope;
        stratum::Workspace<T> ws(budgets[i]);
        sortWith(d, ws);
        peak[i] = static_cast<double>(scope.peakBytes()) / static_cast<double>(n);
    }
    double m[5];
    for (int i = 0; i < 5; ++i) m[i] = summarise(t[i]).median;
    rep.line("| " + label + " | " + fmt("%.3f", m[0]) + " | " + fmt("%+.0f%%", 100 * (m[1] / m[0] - 1)) + " | " +
             fmt("%+.0f%%", 100 * (m[2] / m[0] - 1)) + " | " + fmt("%+.0f%%", 100 * (m[3] / m[0] - 1)) + " | " +
             fmt("%.3f", m[4]) + " | " + fmt("%.2fx", m[0] / m[4]) + " | " + fmt("%.2fx", m[1] / m[4]) + " | " +
             fmt("%.2fx", m[2] / m[4]) + " | " + fmt("%.2f", peak[0]) + " / " + fmt("%.3f", peak[1]) + " / " +
             fmt("%.3f", peak[2]) + " / " + fmt("%.3f", peak[3]) + " | " + (ok ? "yes" : "**NO**") + " |");
}

void memoryHeader(Report& rep, const std::string& title) {
    rep.line("");
    rep.line("### Memory policies: " + title);
    rep.line("");
    rep.line("| input | unlimited ms | automatic | 600 KB | floor | std::sort ms | unlimited vs std | "
             "automatic vs std | 600 KB vs std | aux B/elem: unlimited / automatic / 600 KB / floor | ok |");
    rep.line("|---|---|---|---|---|---|---|---|---|---|---|");
}

void memorySuite(Report& rep, bool full) {
    auto less = [](int64_t a, int64_t b) { return a < b; };
    auto sortI = [](std::vector<int64_t>& d, stratum::Workspace<int64_t>& ws) { stratum::sort(d, ws); };
    for (std::size_t n : {std::size_t{1000000}, std::size_t{10000000}}) {
        const int reps = n <= 1000000 ? 7 : 3;
        memoryHeader(rep, "int64, n = " + std::to_string(n));
        const std::vector<std::string> shapes =
            full || n <= 1000000 ? std::vector<std::string>{"random", "nearly_sorted", "sorted_tail", "organ_pipe",
                                                            "few_outliers", "adversarial", "worst_case"}
                                 : std::vector<std::string>{"random", "nearly_sorted", "organ_pipe", "adversarial"};
        for (const auto& sh : shapes)
            memoryRow<int64_t>(rep, sh, n, reps, [&] { return stratum::bench::makeShape<int64_t>(sh, n); }, sortI,
                               less);
    }
    using R = BenchRecord<72>;
    const std::size_t n = 10000000;
    memoryHeader(rep, "72-byte records by a uint64 key (sort_by_key), n = " + std::to_string(n));
    auto lessR = [](const R& a, const R& b) { return a.key < b.key; };
    auto sortR = [](std::vector<R>& d, stratum::Workspace<R>& ws) {
        stratum::sort_by_key(d, [](const R& r) { return r.key; }, ws);
    };
    memoryRow<R>(rep, "random 64-bit", n, 3,
                 [&] {
                     std::mt19937_64 rng(72);
                     std::vector<R> v(n);
                     for (std::size_t i = 0; i < n; ++i) {
                         v[i].key = rng();
                         std::memset(v[i].payload, static_cast<int>(i & 0xFF), sizeof v[i].payload);
                     }
                     return v;
                 },
                 sortR, lessR);
}

// ---- Lambda sweep --------------------------------------------------------------
// Which lambda is fastest, per (key, n, shape), normalised to the best
// lambda of the row. Feeds the automatic default (Config.hpp).
template <typename T>
void lambdaSweep(Report& rep, const std::string& shape, std::size_t n, int reps) {
    const std::vector<std::size_t> lambdas = {8, 16, 24, 32, 48, 64, 96, 128, 256};
    const std::vector<T> input = stratum::bench::makeShape<T>(shape, n);
    std::vector<std::vector<double>> times(lambdas.size());
    stratum::Workspace<T> ws;
    for (int r = 0; r < reps; ++r) {
        for (std::size_t k = 0; k < lambdas.size(); ++k) {
            const std::size_t i = (k + static_cast<std::size_t>(r)) % lambdas.size();
            const stratum::StratumSort<T> s(lambdas[i], 2 * lambdas[i]);
            std::vector<T> d = input;
            times[i].push_back(timeMs([&] { s.sort(d, ws); }));
        }
    }
    std::vector<double> med(lambdas.size());
    for (std::size_t i = 0; i < lambdas.size(); ++i) med[i] = summarise(times[i]).median;
    const std::size_t best = static_cast<std::size_t>(std::min_element(med.begin(), med.end()) - med.begin());
    std::string row = std::string("| ") + typeName<T>() + " | " + shape + " | " + std::to_string(n) + " | " +
                      std::to_string(lambdas[best]) + " | " + fmt("%.3f", med[best]) + " |";
    for (double m : med) row += " " + fmt("%.2f", m / med[best]) + " |";
    rep.line(row);
    if (rep.csv.is_open()) {
        for (std::size_t i = 0; i < lambdas.size(); ++i)
            rep.csv << typeName<T>() << ',' << n << ',' << shape << ",lambda=" << lambdas[i] << ",,," << med[i]
                    << ",,," << med[i] * 1e6 / static_cast<double>(n) << ',' << med[i] / med[best] << ",,,1\n";
    }
    std::cout.flush();
}

void lambdaSuite(Report& rep, bool large) {
    rep.line("");
    rep.line("### Lambda sweep (t = 2*lambda): time / time(best lambda), reused workspace");
    rep.line("");
    rep.line("| key | shape | n | best lambda | best ms | 8 | 16 | 24 | 32 | 48 | 64 | 96 | 128 | 256 |");
    rep.line("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|");
    const std::vector<std::size_t> sizes = large ? std::vector<std::size_t>{10000, 100000, 1000000, 10000000}
                                                 : std::vector<std::size_t>{10000, 100000, 1000000};
    for (const char* shape : {"random", "normal", "nearly_sorted", "whole_universe", "duplicates"})
        for (std::size_t n : sizes)
            lambdaSweep<int64_t>(rep, shape, n, n <= 100000 ? 15 : n <= 1000000 ? 9 : 5);
    // Around the automatic threshold (Config.hpp, AUTOMATIC_LARGE_INPUT).
    if (large)
        for (const char* shape : {"random", "normal"})
            for (std::size_t n : {std::size_t{1} << 21, std::size_t{1} << 22, std::size_t{1} << 23})
                lambdaSweep<int64_t>(rep, shape, n, 5);
    for (std::size_t n : sizes) lambdaSweep<uint32_t>(rep, "random", n, n <= 100000 ? 15 : n <= 1000000 ? 9 : 5);
}

template <typename T>
void suiteForType(Report& rep, const Options& o, const std::vector<std::size_t>& sizes,
                  const std::vector<std::string>& shapes) {
    for (std::size_t n : sizes) {
        const int reps = o.reps > 0 ? o.reps : n <= 100000 ? 21 : n <= 1000000 ? 9 : 5;
        runTable<T>(rep, shapes, n, reps, o.comparisons);
    }
}

void runTypes(Report& rep, const Options& o, const std::vector<std::string>& types,
              const std::vector<std::size_t>& sizes, const std::vector<std::string>& shapes) {
    for (const std::string& t : types) {
        if (t == "int64") suiteForType<int64_t>(rep, o, sizes, shapes);
        else if (t == "uint64") suiteForType<uint64_t>(rep, o, sizes, shapes);
        else if (t == "int32") suiteForType<int32_t>(rep, o, sizes, shapes);
        else if (t == "uint32") suiteForType<uint32_t>(rep, o, sizes, shapes);
        else if (t == "int16") suiteForType<int16_t>(rep, o, sizes, shapes);
        else if (t == "uint16") suiteForType<uint16_t>(rep, o, sizes, shapes);
        else if (t == "int8") suiteForType<int8_t>(rep, o, sizes, shapes);
        else if (t == "uint8") suiteForType<uint8_t>(rep, o, sizes, shapes);
        else std::cerr << "unknown type " << t << "\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--suite") o.suite = next();
        else if (a == "--md") o.mdPath = next();
        else if (a == "--csv") o.csvPath = next();
        else if (a == "--reps") o.reps = std::atoi(next().c_str());
        else if (a == "--sizes") for (auto& s : splitList(next())) o.sizes.push_back(static_cast<std::size_t>(std::atoll(s.c_str())));
        else if (a == "--types") o.types = splitList(next());
        else if (a == "--shapes") o.shapes = splitList(next());
        else if (a == "--comparisons") o.comparisons = true;
        else {
            std::cerr << "unknown option " << a << "\n";
            return 2;
        }
    }

    Report rep;
    if (!o.csvPath.empty()) {
        rep.csv.open(o.csvPath);
        rep.csv << "key,n,shape,algorithm,min_ms,p10_ms,median_ms,p90_ms,max_ms,ns_per_elem,"
                   "ratio_vs_std_sort,peak_aux_bytes,comparisons,correct\n";
    }

    const stratum::SystemInfo info = stratum::SystemInfo::collect();
    rep.line("## Stratum Sort benchmark - suite `" + o.suite + "`");
    rep.line("");
    rep.line("| | |");
    rep.line("|---|---|");
    rep.line("| Compiler | " + info.compilerName + " " + info.compilerVersion + " |");
    rep.line("| Standard | " + info.languageStandard + " |");
    rep.line("| Build | " + info.buildConfig + " |");
    rep.line("| Flags | `" + info.compileFlags + "` |");
    rep.line("| OS / arch | " + info.operatingSystem + " / " + info.architecture + " |");
    rep.line("| CPU | " + info.cpuModel + " |");
    rep.line("| Caches | " + info.caches + " |");
    rep.line("| Logical cores | " + std::to_string(info.logicalCores) + " |");
    {
        const stratum::StratumSort<int64_t> automatic;
        const auto lo = automatic.effectiveParameters(stratum::AUTOMATIC_LARGE_INPUT);
        const auto hi = automatic.effectiveParameters(stratum::AUTOMATIC_LARGE_INPUT + 1);
        rep.line("| Defaults | automatic: lambda = " + std::to_string(lo.targetElementsPerBin) + ", t = " +
                 std::to_string(lo.leafThreshold) + " up to 2^22 elements; " +
                 std::to_string(hi.targetElementsPerBin) + " / " + std::to_string(hi.leafThreshold) + " above |");
    }
    rep.line("");
    rep.line("Ratios are medians over medians, both sorts timed alternately on identical copies of the "
             "input. \"stratum\" is a fresh instance per call; \"reused ws\" keeps one workspace. "
             "Aux B/elem is the peak measured at the allocator.");

    const std::vector<std::string>& shapes = o.shapes.empty() ? stratum::bench::allShapes() : o.shapes;
    if (o.suite == "quick") {
        runTypes(rep, o, o.types.empty() ? std::vector<std::string>{"int64"} : o.types,
                 o.sizes.empty() ? std::vector<std::size_t>{1000000} : o.sizes, shapes);
    } else if (o.suite == "types") {
        for (std::size_t n : o.sizes.empty() ? std::vector<std::size_t>{100000, 1000000} : o.sizes) {
            const int reps = n <= 100000 ? 15 : 7;
            runFloatTable<float>(rep, n, reps);
            runFloatTable<double>(rep, n, reps);
            runRecordTable<16>(rep, n, reps);
            runRecordTable<64>(rep, n, reps);
        }
    } else if (o.suite == "lambda") {
        lambdaSuite(rep, true);
    } else if (o.suite == "memory") {
        memorySuite(rep, true);
    } else if (o.suite == "ci" || o.suite == "full") {
        const bool full = o.suite == "full";
        runTypes(rep, o, o.types.empty() ? std::vector<std::string>{"int64", "uint32", "uint8"} : o.types,
                 o.sizes.empty() ? (full ? std::vector<std::size_t>{10000, 100000, 1000000}
                                         : std::vector<std::size_t>{100000, 1000000})
                                 : o.sizes,
                 shapes);
        for (std::size_t n : {std::size_t{100000}, std::size_t{1000000}}) {
            const int reps = n <= 100000 ? 15 : 7;
            runFloatTable<float>(rep, n, reps);
            runFloatTable<double>(rep, n, reps);
            runRecordTable<16>(rep, n, reps);
            runRecordTable<64>(rep, n, reps);
        }
        Options big = o;
        big.reps = full ? 7 : 3;
        runTypes(rep, big, {"int64"}, {10000000},
                 o.shapes.empty() ? std::vector<std::string>{"random", "sorted", "normal", "adversarial"} : o.shapes);
        lambdaSuite(rep, true);
        memorySuite(rep, full);
    } else {
        std::cerr << "unknown suite " << o.suite << "\n";
        return 2;
    }

    if (!o.mdPath.empty()) {
        std::ofstream md(o.mdPath);
        md << rep.md.str();
    }
    return 0;
}
