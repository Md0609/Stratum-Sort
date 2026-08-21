// A test binary compiled with NDEBUG would silently skip every internal
// invariant check: the bucket-index bound in countAndPlace and the leaf
// tiling verification in sort() are assert()s. That is exactly what a
// CMake Release build does, because CMake appends -DNDEBUG after any
// target flag. Forcing them on here makes the suites correct under every
// build system and configuration, at the cost of some speed - which a
// test should always trade away.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>

// ============================================================
// Contract tests for the public API of StratumSort
// ============================================================
// An audit found that the only tested configuration was the default
// constructor. The defect it uncovered - the local-sort dispatcher
// following a compile-time constant instead of the runtime leaf threshold
// - lived precisely in the untested part of the interface.
//
// This file tests the CONTRACT, not the performance:
//   1. correctness across the whole parameter space
//   2. the clamping rules of the constructor
//   3. the documented guarantees (strong exception safety, buffer reuse)
//   4. the invariants the algorithm claims to maintain
//   5. the edge cases of the range arithmetic
//
// Built in the PRODUCTION configuration on purpose: it validates the code
// a caller actually gets. Assertions inside the algorithm are active
// because the build does not define NDEBUG.
#include "stratum/StratumSort.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <string>
#include <vector>

using stratum::StratumSort;

namespace {

int g_failures = 0;
int g_checks = 0;

// Progress reporting. Every section announces itself with the elapsed
// time and FLUSHES: a suite that is killed by a CI timeout must still have
// said how far it got, otherwise the log shows nothing and the failure has
// to be guessed at.
static std::chrono::steady_clock::time_point g_start = std::chrono::steady_clock::now();

void section(const char* name) {
    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - g_start).count();
    std::cout << "[" << static_cast<long long>(ms) << " ms] " << name << std::endl;
}

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cout << "  FAIL: " << what << "\n";
    }
}

// The one property that must hold for every input and every parameter
// combination: the output is a sorted permutation of the input.
template <typename T>
bool sortsCorrectly(std::vector<T> data, std::size_t lambda, std::size_t leaf) {
    std::vector<T> expected = data;
    std::sort(expected.begin(), expected.end());
    StratumSort<T> sorter(lambda, leaf);
    sorter.sort(data);
    return data == expected;
}

// ------------------------------------------------------------------
// 1. Correctness across the parameter space
// ------------------------------------------------------------------
// The audit's defect only appeared for leafThreshold > 64, a region no
// test had ever entered. This sweeps well past it in both directions.
void testParameterSpace() {
    section("1. Parameter space");
    std::mt19937_64 rng(12345);

    const std::vector<std::size_t> lambdas = {1, 2, 7, 16, 32, 64, 128, 1000};
    const std::vector<std::size_t> leaves = {0, 1, 8, 32, 64, 96, 128, 384, 500, 5000};

    for (std::size_t lambda : lambdas) {
        for (std::size_t leaf : leaves) {
            // A size comfortably above every leaf threshold tested, so
            // the refinement actually runs.
            std::vector<int64_t> v(20000);
            std::uniform_int_distribution<int64_t> dist(-1000000, 1000000);
            for (auto& x : v) x = dist(rng);
            check(sortsCorrectly(v, lambda, leaf),
                  "random n=20000 with lambda=" + std::to_string(lambda) +
                      " leaf=" + std::to_string(leaf));
        }
    }

    // Same sweep against inputs designed to exhaust the depth cap, which
    // is the only way a leaf larger than the threshold reaches the local
    // sort - i.e. the only way QuickSort and Introsort are reachable.
    const std::vector<std::size_t> advLambdas = {2, 16, 32, 64};
    const std::vector<std::size_t> advLeaves = {0, 32, 64, 256, 4096};
    for (std::size_t lambda : advLambdas) {
        for (std::size_t leaf : advLeaves) {
            std::vector<int64_t> v;
            v.reserve(10000);
            // A dense core plus one far outlier per level: each split
            // leaves nearly everything in bucket 0.
            uint64_t span = 200;
            for (uint64_t i = 0; i < span; ++i) v.push_back(static_cast<int64_t>(i));
            for (int k = 0; k < 40 && span < (1ULL << 60); ++k) {
                span *= 3;
                v.push_back(static_cast<int64_t>(span));
            }
            while (v.size() < 10000) v.push_back(static_cast<int64_t>(v.size() % 200));
            std::shuffle(v.begin(), v.end(), rng);
            check(sortsCorrectly(v, lambda, leaf),
                  "depth-exhausting input with lambda=" + std::to_string(lambda) +
                      " leaf=" + std::to_string(leaf));
        }
    }
}

// ------------------------------------------------------------------
// 2. Constructor clamping
// ------------------------------------------------------------------
// The clamping is silent, so it must be observable and it must be
// exactly what the header documents.
void testConstructorClamping() {
    section("2. Constructor clamping");

    StratumSort<int64_t> a;
    check(a.targetElementsPerBin() == stratum::DEFAULT_TARGET_ELEMENTS_PER_BIN,
          "default lambda");
    check(a.leafThreshold() == stratum::DEFAULT_LEAF_THRESHOLD, "default leaf threshold");

    StratumSort<int64_t> b(0, 0);
    check(b.targetElementsPerBin() == stratum::DEFAULT_TARGET_ELEMENTS_PER_BIN,
          "lambda = 0 falls back to the default");
    check(b.leafThreshold() == b.targetElementsPerBin(),
          "leaf threshold below lambda is raised to lambda");

    StratumSort<int64_t> c(64, 16);
    check(c.targetElementsPerBin() == 64, "lambda kept");
    check(c.leafThreshold() == 64, "leaf threshold 16 < lambda 64 is raised to 64");

    StratumSort<int64_t> d(16, 128);
    check(d.targetElementsPerBin() == 16 && d.leafThreshold() == 128,
          "a leaf threshold above lambda is kept as given");

    // The documented swap hazard: (64, 32) reads as (64, 64), not (32, 64).
    StratumSort<int64_t> e(64, 32);
    check(e.targetElementsPerBin() == 64 && e.leafThreshold() == 64,
          "swapped arguments are clamped, not reinterpreted");
}

// ------------------------------------------------------------------
// 3. Edge cases of the range arithmetic
// ------------------------------------------------------------------
// The span formulation exists because max - min + 1 overflows exactly
// when the input spans the whole universe. These are the inputs that
// exercise it.
void testRangeArithmetic() {
    section("3. Range arithmetic edge cases");
    const int64_t lo = std::numeric_limits<int64_t>::min();
    const int64_t hi = std::numeric_limits<int64_t>::max();

    check(sortsCorrectly<int64_t>({lo, hi}, 32, 64), "span = 2^64-1, n = 2");
    check(sortsCorrectly<int64_t>({hi, lo}, 32, 64), "span = 2^64-1, reversed");

    // n above the leaf threshold, so refinement actually runs on a bin
    // whose span is the whole universe.
    {
        std::vector<int64_t> v;
        for (int i = 0; i < 300; ++i) v.push_back(i % 2 ? hi - i : lo + i);
        check(sortsCorrectly(v, 32, 64), "span = 2^64-1 with refinement");
    }
    {
        std::vector<int64_t> v(500, lo);
        v[250] = hi;
        check(sortsCorrectly(v, 32, 64), "span = 2^64-1, almost all at the minimum");
    }

    check(sortsCorrectly(std::vector<int64_t>(5000, 7), 32, 64), "all elements equal");
    check(sortsCorrectly<int64_t>({}, 32, 64), "empty");
    check(sortsCorrectly<int64_t>({42}, 32, 64), "single element");
    check(sortsCorrectly<int64_t>({2, 1}, 32, 64), "two elements");

    // Unsigned and narrow types: the arithmetic must not assume int64_t.
    check(sortsCorrectly<uint8_t>(std::vector<uint8_t>(1000, 200), 32, 64), "uint8_t constant");
    {
        std::vector<uint8_t> v(1000);
        std::mt19937_64 rng(7);
        for (auto& x : v) x = static_cast<uint8_t>(rng() & 0xFF);
        check(sortsCorrectly(v, 32, 64), "uint8_t random (span fits in the type)");
    }
    {
        std::vector<int16_t> v(1000);
        std::mt19937_64 rng(9);
        for (auto& x : v)
            x = static_cast<int16_t>(rng() % 65536 - 32768);
        check(sortsCorrectly(v, 32, 64), "int16_t spanning the whole type");
    }
    {
        std::vector<uint64_t> v;
        v.push_back(0);
        v.push_back(std::numeric_limits<uint64_t>::max());
        for (int i = 0; i < 300; ++i) v.push_back(static_cast<uint64_t>(i) * 1000000007ULL);
        check(sortsCorrectly(v, 32, 64), "uint64_t spanning the whole type");
    }
}

// ------------------------------------------------------------------
// 4. Documented guarantees
// ------------------------------------------------------------------
// Buffer reuse: the same instance must give the same answer on inputs of
// wildly different sizes, in either order. This catches stale-state bugs
// in the scratch buffers, which are the only mutable state that survives
// a call.
void testInstanceReuse() {
    section("4. Instance reuse");
    std::mt19937_64 rng(999);
    StratumSort<int64_t> sorter;

    bool allOk = true;
    const std::vector<std::size_t> sizes = {50000, 3, 20000, 1, 0, 100, 40000, 2};
    for (std::size_t n : sizes) {
        std::vector<int64_t> v(n);
        std::uniform_int_distribution<int64_t> dist(-100000, 100000);
        for (auto& x : v) x = dist(rng);
        std::vector<int64_t> expected = v;
        std::sort(expected.begin(), expected.end());
        sorter.sort(v);
        if (v != expected) allOk = false;
    }
    check(allOk, "one instance reused across sizes 50000..0 in mixed order");
}

// Strong exception guarantee: if an allocation fails, the caller's array
// must be untouched. Forced by making operator new throw on the k-th
// allocation, for every k up to the number the sort performs.
std::size_t g_allocBudget = 0;
bool g_allocLimiterOn = false;

void testStrongExceptionGuarantee() {
    section("5. Strong exception guarantee");
    std::mt19937_64 rng(4242);
    std::vector<int64_t> original(5000);
    std::uniform_int_distribution<int64_t> dist(-100000, 100000);
    for (auto& x : original) x = dist(rng);

    bool everThrew = false;
    bool alwaysIntact = true;

    for (std::size_t budget = 0; budget < 40; ++budget) {
        std::vector<int64_t> v = original;
        StratumSort<int64_t> sorter; // fresh, so it must allocate
        g_allocBudget = budget;
        g_allocLimiterOn = true;
        bool threw = false;
        try {
            sorter.sort(v);
        } catch (const std::bad_alloc&) {
            threw = true;
            everThrew = true;
        }
        g_allocLimiterOn = false;

        if (threw && v != original) alwaysIntact = false;
    }

    check(everThrew, "the allocation limiter actually fired");
#ifdef STRATUM_ENABLE_METRICS
    // The research build does NOT offer the strong guarantee, and this
    // asymmetry is deliberate and documented in the header: the
    // instrumentation times the join phase, and recording that timing
    // allocates AFTER the output has already been written. Asserting the
    // guarantee here would be asserting something the code does not
    // promise. What is pinned instead is that the difference exists, so
    // that nobody "fixes" the production path by accident and leaves this
    // comment stale.
    check(!alwaysIntact,
          "research build does not preserve the input on bad_alloc (documented asymmetry)");
#else
    check(alwaysIntact, "on bad_alloc the input is left exactly as it was");
#endif
}

// ------------------------------------------------------------------
// 6. Determinism
// ------------------------------------------------------------------
void testDeterminism() {
    section("6. Determinism");
    std::mt19937_64 rng(31337);
    std::vector<int64_t> base(30000);
    std::uniform_int_distribution<int64_t> dist(-1000, 1000);
    for (auto& x : base) x = dist(rng);

    std::vector<int64_t> first = base;
    StratumSort<int64_t>().sort(first);
    bool same = true;
    for (int r = 0; r < 5; ++r) {
        std::vector<int64_t> v = base;
        StratumSort<int64_t> s;
        s.sort(v);
        if (v != first) same = false;
    }
    check(same, "repeated sorts of the same input give byte-identical output");
}

#ifdef STRATUM_ENABLE_METRICS
// ------------------------------------------------------------------
// 7. The local-sort dispatch is decoupled from the leaf threshold
// ------------------------------------------------------------------
// THIS IS THE REGRESSION GUARD FOR THAT DEFECT. The dispatch
// thresholds used to be derived from the leaf threshold, so changing how
// the partitioning stops silently changed which sorting algorithm ran on
// the leaves - two unrelated concepts sharing one number.
//
// The property that must hold: which local sort runs depends ONLY on the
// size of the leaf, never on the constructor parameters. The test builds
// one leaf of a fixed size under several different parameter settings and
// checks the dispatch is identical every time.
std::size_t usageOf(const stratum::SortMetrics& m, const char* algo) {
    const auto& u = m.algorithmUsage();
    const auto it = u.find(algo);
    return it == u.end() ? 0u : it->second;
}

void testDispatchDecoupling() {
    section("7. Local-sort dispatch decoupling");
    std::mt19937_64 rng(555);

    // Sizes chosen to land in each of the three dispatch branches given
    // the documented constants, plus the boundaries themselves.
    const std::vector<std::size_t> leafSizes = {20, 63, 64, 65, 100, 383, 384, 385, 900};
    for (std::size_t leafSize : leafSizes) {
        std::string expected;
        bool first = true;
        bool consistent = true;

        // Every (lambda, t) pair here is large enough that the whole input
        // becomes a single leaf, so the leaf size is exactly leafSize and
        // only the parameters differ.
        const std::vector<std::size_t> thresholds = {leafSize, leafSize + 1, leafSize * 2,
                                                    leafSize * 8, 10000};
        for (std::size_t t : thresholds) {
            std::vector<int64_t> v(leafSize);
            std::uniform_int_distribution<int64_t> dist(0, 1000000);
            for (auto& x : v) x = dist(rng);
            std::sort(v.begin(), v.end());
            std::swap(v[0], v[leafSize / 2]); // guarantee it is not a run

            StratumSort<int64_t> sorter(leafSize, t);
            sorter.sort(v);
            const auto& m = sorter.metrics();

            std::string got = usageOf(m, "InsertionSort") ? "InsertionSort"
                              : usageOf(m, "QuickSort")   ? "QuickSort"
                              : usageOf(m, "Introsort")   ? "Introsort"
                                                          : "none";
            if (first) {
                expected = got;
                first = false;
            } else if (got != expected) {
                consistent = false;
            }
        }
        check(consistent, "leaf of " + std::to_string(leafSize) +
                              " elements dispatches to the same local sort for every "
                              "parameter setting (chose " + expected + ")");
    }
}
#endif

} // namespace

// Allocation limiter for the exception-safety test. Defined at namespace
// scope because operator new must be a global replacement.
void* operator new(std::size_t sz) {
    if (sz == 0) sz = 1;
    if (g_allocLimiterOn) {
        if (g_allocBudget == 0) {
            // DISARM BEFORE THROWING. This is not tidiness, it is the
            // difference between a working test and a hung one.
            //
            // Throwing and unwinding is not allocation-free on every
            // platform. Under the Itanium ABI the exception object comes
            // from __cxa_allocate_exception, which does not route through
            // operator new, so a limiter left armed here is harmless and
            // the bug is invisible. MSVC's machinery does allocate while
            // propagating the exception: with the limiter still armed and
            // the budget at zero, that allocation throws too - a throw
            // during unwinding, which is std::terminate, which on Windows
            // is an abort dialog that blocks forever with nobody to click
            // it. The job did not fail, it waited.
            //
            // Failing exactly one allocation is also what this test means
            // in the first place: "make the k-th allocation fail". Once
            // sort() has thrown, no further allocation of its own happens,
            // so nothing about the coverage changes.
            g_allocLimiterOn = false;
            throw std::bad_alloc();
        }
        --g_allocBudget;
    }
    void* p = std::malloc(sz);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t sz) { return operator new(sz); }

// GCC's -Wmismatched-new-delete fires here, and it is a false positive:
// replacing the GLOBAL operator new/delete pair with malloc/free is exactly
// what the standard permits, and the pair is consistent. The warning's
// heuristic does not recognise a replacement pair, only a mismatch between
// an allocation and the matching deallocation call.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

int main() {
    std::cout << "=== StratumSort - API contract tests ===\n";
    testParameterSpace();
    testConstructorClamping();
    testRangeArithmetic();
    testInstanceReuse();
    testStrongExceptionGuarantee();
    testDeterminism();
#ifdef STRATUM_ENABLE_METRICS
    testDispatchDecoupling();
#endif

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    if (g_failures != 0) {
        std::cout << "FAILED\n";
        return 1;
    }
    std::cout << "All API contract tests passed." << std::endl;
    return 0;
}
