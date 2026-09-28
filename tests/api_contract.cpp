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
#include <exception>
#include <typeinfo>
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
//   3. the edge cases of the range arithmetic
//   4. instance and buffer reuse
//   5. the strong exception guarantee
//   6. determinism
//   7. the introsort fallback (heapSort)
//   8. the local-sort dispatch, decoupled from the leaf threshold
//   9. the parameter ceilings
//
// Built in the PRODUCTION configuration on purpose: it validates the code
// a caller actually gets. Assertions inside the algorithm are active
// because the build does not define NDEBUG.
#include "stratum/StratumSort.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <utility>
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

    // The ceilings, which make H2 hold for every argument: nothing a caller
    // passes - data.size(), data.size() + 1, SIZE_MAX - survives above them.
    const std::size_t LMAX = stratum::MAX_TARGET_ELEMENTS_PER_BIN;
    const std::size_t TMAX = stratum::MAX_LEAF_THRESHOLD;
    const std::size_t DEF = stratum::DEFAULT_TARGET_ELEMENTS_PER_BIN;
    const std::size_t huge = std::numeric_limits<std::size_t>::max();
    const std::size_t intMax = static_cast<std::size_t>(std::numeric_limits<int>::max());
    const std::size_t n = 1000000;
    struct Want { std::size_t lambda, t, wantLambda, wantT; };
    const std::vector<Want> table = {
        {n, n, LMAX, TMAX},           {n + 1, n + 1, LMAX, TMAX},
        {32, n, 32, TMAX},            {n, 64, LMAX, LMAX},        // t < lambda: raised, then capped
        {LMAX, TMAX, LMAX, TMAX},     {LMAX + 1, TMAX + 1, LMAX, TMAX},
        {huge, huge, LMAX, TMAX},     {huge, 0, LMAX, LMAX},
        {1, huge, 1, TMAX},           {intMax, intMax, LMAX, TMAX},
        {1, 1, 1, 1},                 {0, huge, DEF, TMAX},
        {500, 10, 500, 500},          {LMAX - 1, LMAX - 1, LMAX - 1, LMAX - 1}};
    bool ceilings = true;
    for (const Want& w : table) {
        StratumSort<int64_t> s(w.lambda, w.t);
        if (s.targetElementsPerBin() != w.wantLambda || s.leafThreshold() != w.wantT) {
            ceilings = false;
            std::cout << "  (" << w.lambda << ", " << w.t << ") became (" << s.targetElementsPerBin()
                      << ", " << s.leafThreshold() << "), expected (" << w.wantLambda << ", " << w.wantT
                      << ")\n";
        }
    }
    check(ceilings, "lambda is clamped into [1, 10000] and t into [lambda, 10000]");
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

// Diagnostic bookkeeping for the limiter. Fixed storage on purpose: recording
// an allocation must not itself allocate. Sizes discriminate whose allocation
// was refused - StratumSort's working buffers for n=5000 int64_t are tens of
// kilobytes, while a standard-library container's internal bookkeeping object
// is a few bytes. Which of the two the limiter refuses is the whole question.
std::size_t g_allocSizes[64];
std::size_t g_allocSeen = 0;
std::size_t g_refusedSize = 0;
int g_refusedDepth = -1; // std::uncaught_exceptions() at the point of refusal
std::size_t g_allocSkipped = 0; // too small to be a working buffer, left alone
std::size_t g_smallestSkipped = static_cast<std::size_t>(-1);

// Smallest allocation the limiter is willing to refuse, when it has to hold
// back at all. See probeNoexceptBookkeeping() below for when that is.
//
// 256 is not arbitrary: it sits between the largest bookkeeping allocation
// observed on any platform here (64 bytes) and the smallest working buffer
// StratumSort asks for when sorting these 5000 elements (1256 bytes), with a
// factor of four of margin on each side.
const std::size_t kLimiterMinBytes = 256;
std::size_t g_limiterMinBytes = kLimiterMinBytes;

// Can this standard library survive having a small allocation refused?
//
// Not everywhere. Some implementations attach a heap-allocated bookkeeping
// object to every container, and create it inside operations that are
// declared noexcept. An exception thrown there does not propagate: by
// [except.spec] it calls std::terminate, and no enclosing handler runs. A
// limiter that refuses the k-th allocation regardless of who asked for it
// will eventually refuse one of those, and the test does not fail - it dies.
//
// Default-constructing an empty vector is the exact probe for this. The
// standard requires no allocation, and vector's default constructor is
// noexcept for std::allocator, so ANY allocation it makes is by definition an
// allocation inside a noexcept function: the precise class that cannot be
// refused. Measured: libc++ and libstdc++ allocate nothing, MSVC's Debug
// library allocates 16 bytes.
//
// This asks the platform instead of naming it. A library that starts or stops
// doing this is detected on the spot rather than by someone remembering to
// update an #ifdef.
bool probeNoexceptBookkeeping() {
    g_allocSeen = 0;
    g_allocSkipped = 0;
    g_limiterMinBytes = 1; // count everything, refuse nothing
    g_allocBudget = static_cast<std::size_t>(-1);
    g_allocLimiterOn = true;
    {
        std::vector<int64_t> probe;
        // Keep the object alive and unelidable without allocating.
        volatile std::size_t sink = probe.capacity();
        (void)sink;
    }
    g_allocLimiterOn = false;
    const bool bookkeeps = (g_allocSeen > 0);
    g_limiterMinBytes = bookkeeps ? kLimiterMinBytes : 1;
    return bookkeeps;
}




void testStrongExceptionGuarantee() {
    section("5. Strong exception guarantee");
    std::mt19937_64 rng(4242);
    std::vector<int64_t> original(5000);
    std::uniform_int_distribution<int64_t> dist(-100000, 100000);
    for (auto& x : original) x = dist(rng);

    // Ask the platform what it can survive before deciding what to refuse.
    const bool bookkeeps = probeNoexceptBookkeeping();
    std::cout << "   standard library allocates inside noexcept container "
              << "operations: " << (bookkeeps ? "yes" : "no")
              << ", so the limiter refuses allocations of " << g_limiterMinBytes
              << " bytes and up" << std::endl;

    // Calibration: one sort with nothing ever refused, recording every size.
    // What a standard library allocates on the way to sorting the same data
    // differs so much between implementations that guessing makes the sweep
    // below meaningless. This run cannot terminate the process: with an
    // unlimited budget no allocation is ever refused, so nothing throws.
    {
        std::vector<int64_t> cal = original;
        StratumSort<int64_t> calSorter;
        g_allocSeen = 0;
        g_allocSkipped = 0;
        g_smallestSkipped = static_cast<std::size_t>(-1);
        g_allocBudget = static_cast<std::size_t>(-1);
        g_allocLimiterOn = true;
        calSorter.sort(cal);
        g_allocLimiterOn = false;
        std::cout << "   calibration: " << g_allocSeen << " refusable allocations, sizes:";
        for (std::size_t i = 0; i < g_allocSeen && i < 64; ++i) std::cout << " " << g_allocSizes[i];
        if (g_allocSkipped != 0) {
            std::cout << "; plus " << g_allocSkipped << " below " << g_limiterMinBytes
                      << " bytes left alone, smallest " << g_smallestSkipped;
        }
        std::cout << std::endl;
    }

    bool everThrew = false;
    bool everCompleted = false;
    bool alwaysIntact = true;

    for (std::size_t budget = 0; budget < 40; ++budget) {
        std::vector<int64_t> v = original;
        StratumSort<int64_t> sorter; // fresh, so it must allocate
        // NOTHING that allocates may run while the limiter is armed except
        // the call under test. std::cout allocates while formatting on some
        // standard libraries, so every trace line is printed outside the
        // armed window - printing inside it would consume the budget and
        // throw from the stream instead of from sort().
        std::cout << "   budget " << budget << " ..." << std::flush;
        bool threw = false;
        g_allocSeen = 0;
        g_allocSkipped = 0;
        g_smallestSkipped = static_cast<std::size_t>(-1);
        g_refusedSize = 0;
        g_refusedDepth = -1;
        g_allocBudget = budget;
        g_allocLimiterOn = true;
        try {
            sorter.sort(v);
        } catch (const std::bad_alloc&) {
            threw = true;
            everThrew = true;
        } catch (const std::exception& e) {
            g_allocLimiterOn = false;
            std::cout << " UNEXPECTED std::exception: " << e.what() << std::endl;
            throw;
        } catch (...) {
            g_allocLimiterOn = false;
            std::cout << " UNEXPECTED non-standard exception" << std::endl;
            throw;
        }
        g_allocLimiterOn = false;
        // How much of the budget the call actually used. MSVC's Debug
        // containers allocate a bookkeeping proxy per container, so its
        // allocation profile differs from libstdc++/libc++; printing it makes
        // that difference visible instead of leaving it to be assumed.
        std::cout << (threw ? " threw" : " no-throw")
                  << " (used " << (budget - g_allocBudget) << ", refused "
                  << g_refusedSize << " bytes, skipped " << g_allocSkipped
                  << " small, sizes:";
        for (std::size_t i = 0; i < g_allocSeen && i < 64; ++i) std::cout << " " << g_allocSizes[i];
        std::cout << ")" << std::endl;

        if (!threw) everCompleted = true;
        if (threw && v != original) alwaysIntact = false;
    }

    check(everThrew, "the allocation limiter actually fired");
    // Without this the suite can pass while testing almost nothing. If a
    // platform allocates more times than the sweep has budgets, every single
    // iteration throws, the guarantee is only ever exercised on its earliest
    // failure points, and the test still reports success. That is precisely
    // how the MSVC defect stayed invisible until it turned into a hang: the
    // sweep must be long enough to walk past the last allocation sort() makes.
    check(everCompleted, "the budget sweep reaches a run that allocates freely");
#ifdef STRATUM_ENABLE_METRICS
    // The research build does NOT offer the strong guarantee, and this
    // asymmetry is deliberate and documented in the header: the
    // instrumentation times the join phase, and recording that timing
    // allocates AFTER the output has already been written. Asserting the
    // guarantee here would be asserting something the code does not
    // promise. What is pinned instead is that the difference exists, so
    // that nobody "fixes" the production path by accident and leaves this
    // comment stale.
    // This asymmetry lives in a single small allocation: the join timing is
    // recorded after the output has already been written, and that record is
    // 48 bytes. Where the standard library allocates its own bookkeeping in
    // the same size range, the limiter cannot refuse one without risking the
    // other, so the asymmetry is real but not observable from here. Saying so
    // out loud beats asserting it where it cannot hold.
    if (g_limiterMinBytes == 1) {
        check(!alwaysIntact,
              "research build does not preserve the input on bad_alloc (documented asymmetry)");
    } else {
        std::cout << "   NOT CHECKED: the documented research-build asymmetry needs a "
                  << "sub-" << kLimiterMinBytes << "-byte allocation to be refused, which "
                  << "this standard library cannot survive" << std::endl;
    }
#else
    check(alwaysIntact, "on bad_alloc the input is left exactly as it was");
#endif
}

// ============================================================
// 7. The introsort fallback
// ============================================================
// introSortImpl finishes a leaf with heapSort once its partitioning budget
// runs out. That path shipped in 0.9.0 without sorting correctly: siftDown
// indexed a node's children from the node itself rather than from the heap's
// first slot, so the heapify loop never built a valid heap. Nothing caught it
// because reaching introSort is easy - the (lambda, t) sweep above does it
// routinely - while EXHAUSTING its budget needs adversarial data, and no test
// distinguished "introsort finished" from "introsort fell back".
//
// So this section does not merely exercise large leaves. It pins inputs that
// provably reach heapSort, and in the research build it asserts that they did.
static std::vector<int64_t> sawtooth(std::size_t n, std::size_t modulus) {
    std::vector<int64_t> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<int64_t>((n - i) % modulus);
    return v;
}

// One leaf holding the whole array: lambda and t above n leave nothing to
// refine, so sortLeaf sees all n elements and dispatches on size alone.
template <typename T>
static bool sortsLikeStdSort(std::vector<T> v, std::size_t& heapSorts) {
    std::vector<T> oracle = v;
    std::sort(oracle.begin(), oracle.end());
    StratumSort<T> sorter(v.size() + 1, v.size() + 1);
    sorter.sort(v);
#ifdef STRATUM_ENABLE_METRICS
    const auto& usage = sorter.metrics().algorithmUsage();
    const auto it = usage.find("HeapSort");
    if (it != usage.end()) heapSorts += it->second;
#else
    (void)heapSorts;
#endif
    return v == oracle; // equal to the oracle implies sorted AND same multiset
}

// A leaf produced by depth exhaustion at the DEFAULT configuration, holding
// 'core' in its original order. One value per refinement level is placed so
// that each split peels exactly that value off, and a final maximum sizes
// the top-level buckets so the whole group lands in bucket 0. Distribution
// is stable, so the leaf sees the core exactly as given. The core must
// contain 0, and its size must give the same fan-out at every level;
// otherwise this returns an empty vector.
static std::vector<int64_t> peeledFor(const std::vector<int64_t>& core, uint64_t L) {
    const uint64_t D = stratum::MAX_SUBDIVISION_DEPTH;
    const uint64_t s = (core.size() + D + L - 1) / L;
    if (core.empty() || (core.size() + 1 + L - 1) / L != s ||
        *std::min_element(core.begin(), core.end()) != 0)
        return {};
    // Every value must fit in int64_t. Unchecked, a construction that runs
    // out of bits wraps around and silently builds some OTHER input - which
    // is exactly what happened when 0.11.0 halved the default lambda: the
    // 4096-element core needs ~67 bits at L = 16.
    const uint64_t limit = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    uint64_t p = static_cast<uint64_t>(*std::max_element(core.begin(), core.end())) + 1;
    std::vector<int64_t> v(core);
    for (uint64_t k = 1; k <= D; ++k) {
        if (p > limit / s) return {};
        p *= s;
        v.push_back(static_cast<int64_t>(p));
    }
    const uint64_t sTop = (v.size() + 1 + L - 1) / L;
    if (p + 1 > limit / sTop) return {};
    v.push_back(static_cast<int64_t>(sTop * (p + 1) - 1));
    return v;
}

static std::vector<int64_t> peeledAtDefaults(const std::vector<int64_t>& core) {
    return peeledFor(core, stratum::DEFAULT_TARGET_ELEMENTS_PER_BIN);
}

// McIlroy's adversary ("A Killer Adversary for Quicksort", 1999) run against
// a replica of partition() and quickSort(). It decides each comparison as
// late as possible, so the final values force the most expensive path
// through this exact partition scheme. The research build checks below
// that the order still drives the shipped quickSort into its quadratic
// regime, so a change to partition() cannot quietly make it an easy case.
struct QuickSortAdversary {
    std::vector<std::size_t> val, a;
    std::size_t next = 0, candidate;
    explicit QuickSortAdversary(std::size_t m) : val(m, m), a(m), candidate(m) {
        for (std::size_t i = 0; i < m; ++i) a[i] = i;
    }
    bool less(std::size_t x, std::size_t y) { // val == size() means "not decided yet"
        const std::size_t gas = val.size();
        if (val[x] == gas && val[y] == gas) val[x == candidate ? x : y] = next++;
        if (val[x] == gas) candidate = x;
        else if (val[y] == gas) candidate = y;
        return val[x] < val[y];
    }
    void insertion(std::ptrdiff_t l, std::ptrdiff_t r) {
        for (std::ptrdiff_t i = l + 1; i <= r; ++i) {
            const std::size_t key = a[i];
            std::ptrdiff_t j = i - 1;
            while (j >= l && less(key, a[j])) {
                a[j + 1] = a[j];
                --j;
            }
            a[j + 1] = key;
        }
    }
    std::ptrdiff_t partition(std::ptrdiff_t l, std::ptrdiff_t r) {
        const std::ptrdiff_t mid = l + (r - l) / 2;
        if (less(a[mid], a[l])) std::swap(a[mid], a[l]);
        if (less(a[r], a[l])) std::swap(a[r], a[l]);
        if (less(a[r], a[mid])) std::swap(a[r], a[mid]);
        const std::size_t pivot = a[mid];
        std::swap(a[mid], a[r - 1]);
        std::ptrdiff_t i = l, j = r - 1;
        while (true) {
            do ++i; while (less(a[i], pivot));
            do --j; while (less(pivot, a[j]));
            if (i >= j) break;
            std::swap(a[i], a[j]);
        }
        std::swap(a[i], a[r - 1]);
        return i;
    }
    void quick(std::ptrdiff_t l, std::ptrdiff_t r) {
        while (r - l > static_cast<std::ptrdiff_t>(stratum::LOCAL_PARTITION_CUTOFF)) {
            const std::ptrdiff_t p = partition(l, r);
            if (p - l < r - p) { quick(l, p - 1); l = p + 1; }
            else { quick(p + 1, r); r = p - 1; }
        }
        insertion(l, r);
    }
};

// The values 0..m-1 in the order that is worst for quickSort.
static std::vector<int64_t> quickSortKillerOrder(std::size_t m) {
    QuickSortAdversary adv(m);
    adv.quick(0, static_cast<std::ptrdiff_t>(m) - 1);
    std::vector<std::size_t> byValue(m);
    for (std::size_t i = 0; i < m; ++i) byValue[i] = i;
    std::stable_sort(byValue.begin(), byValue.end(),
                     [&](std::size_t x, std::size_t y) { return adv.val[x] < adv.val[y]; });
    std::vector<int64_t> order(m);
    for (std::size_t rank = 0; rank < m; ++rank) order[byValue[rank]] = static_cast<int64_t>(rank);
    return order;
}

void testIntroSortFallback() {
    section("7. Introsort fallback (heapSort)");

    // Sizes straddling the dispatch thresholds: 384 is the last quickSort
    // size, 385 the first introSort one. The moduli are not decorative - each
    // was measured to drive introSortImpl through its whole depth budget at
    // that n, which is what puts execution inside heapSort.
    const std::vector<std::pair<std::size_t, std::size_t>> forcing = {
        {385, 179}, {386, 189}, {400, 194}, {512, 255}, {1000, 408}, {4096, 1523}};

    std::size_t heapSorts = 0;
    bool allCorrect = true;
    for (const auto& [n, modulus] : forcing) {
        if (!sortsLikeStdSort<int64_t>(sawtooth(n, modulus), heapSorts)) allCorrect = false;
    }
    check(allCorrect, "inputs that exhaust the introsort budget still sort correctly");
#ifdef STRATUM_ENABLE_METRICS
    check(heapSorts >= forcing.size(),
          "those inputs really did reach heapSort (not merely introSort)");
#endif

    // Shape coverage at every threshold size, including 384 so the quickSort
    // side of the boundary is pinned too.
    std::mt19937_64 rng(90210);
    std::size_t ignored = 0;
    bool shapes = true;
    for (std::size_t n : {384u, 385u, 386u, 400u, 512u, 1000u, 4096u}) {
        std::vector<int64_t> asc(n), desc(n), dup(n), extreme(n), rnd(n);
        for (std::size_t i = 0; i < n; ++i) {
            asc[i] = static_cast<int64_t>(i);
            desc[i] = static_cast<int64_t>(n - i);
            dup[i] = static_cast<int64_t>(i % 3);
            extreme[i] = (i % 2) ? std::numeric_limits<int64_t>::max()
                                 : std::numeric_limits<int64_t>::min();
            rnd[i] = static_cast<int64_t>(rng());
        }
        for (const auto* v : {&asc, &desc, &dup, &extreme, &rnd})
            if (!sortsLikeStdSort<int64_t>(*v, ignored)) shapes = false;
    }
    check(shapes, "ascending, descending, duplicate-heavy, extreme and random leaves all sort");

    // Every accepted key type, at the first size that reaches introSort.
    bool types = true;
    {
        std::vector<int8_t> a(385);
        std::vector<uint8_t> b(385);
        std::vector<int16_t> c(385);
        std::vector<uint16_t> d(385);
        std::vector<int32_t> e(385);
        std::vector<uint32_t> f(385);
        std::vector<uint64_t> g(385);
        for (std::size_t i = 0; i < 385; ++i) {
            const auto x = static_cast<uint64_t>(385 - i);
            a[i] = static_cast<int8_t>(x);   b[i] = static_cast<uint8_t>(x);
            c[i] = static_cast<int16_t>(x);  d[i] = static_cast<uint16_t>(x);
            e[i] = static_cast<int32_t>(x);  f[i] = static_cast<uint32_t>(x);
            g[i] = x;
        }
        if (!sortsLikeStdSort<int8_t>(a, ignored)) types = false;
        if (!sortsLikeStdSort<uint8_t>(b, ignored)) types = false;   // the 0.9.0 counterexample
        if (!sortsLikeStdSort<int16_t>(c, ignored)) types = false;
        if (!sortsLikeStdSort<uint16_t>(d, ignored)) types = false;
        if (!sortsLikeStdSort<int32_t>(e, ignored)) types = false;
        if (!sortsLikeStdSort<uint32_t>(f, ignored)) types = false;
        if (!sortsLikeStdSort<uint64_t>(g, ignored)) types = false;
    }
    check(types, "every accepted key type sorts through the introsort path");

    // The same defect at the DEFAULT configuration. There, a leaf above 384
    // elements can only come from depth exhaustion, and this 392-element
    // input produces one: a 385-element sawtooth, peeled out one refinement
    // level at a time. Modulus 190 both reaches heapSort and, measured,
    // came back unsorted from 0.9.0 (179 above reaches heapSort too, but
    // the broken heapSort happened to sort it).
    {
        const std::vector<int64_t> input = peeledAtDefaults(sawtooth(385, 190));
        std::vector<int64_t> oracle = input;
        std::sort(oracle.begin(), oracle.end());
        std::vector<int64_t> v = input;
        StratumSort<int64_t> sorter;
        sorter.sort(v);
        check(input.size() == 392 && v == oracle,
              "the n = 392 counterexample sorts with the default parameters");
#ifdef STRATUM_ENABLE_METRICS
        const auto& usage = sorter.metrics().algorithmUsage();
        check(usage.count("HeapSort") == 1 && usage.at("HeapSort") >= 1,
              "the n = 392 counterexample reaches heapSort with the default parameters");
#endif
    }

    // Property test. The oracle is std::sort, so one comparison covers both
    // "sorted" and "same multiset"; a counterexample is reported with the
    // parameters needed to reproduce it rather than just a failure count.
    bool property = true;
    std::size_t propHeapSorts = 0;
    for (std::size_t seed = 1; seed <= 400; ++seed) {
        std::mt19937_64 r(seed);
        const std::size_t n = 385 + (r() % 4000);
        std::vector<int64_t> v(n);
        switch (seed % 5) {
            case 0: for (auto& x : v) x = static_cast<int64_t>(r()); break;
            case 1: v = sawtooth(n, 2 + r() % 512); break;
            case 2: for (auto& x : v) x = static_cast<int64_t>(r() % 4); break;
            case 3: for (std::size_t i = 0; i < n; ++i)  // organ pipe
                        v[i] = static_cast<int64_t>(i < n / 2 ? i : n - i);
                    break;
            default: for (std::size_t i = 0; i < n; ++i)
                         v[i] = static_cast<int64_t>((i % 2) ? i : n - i);
                     break;
        }
        if (!sortsLikeStdSort<int64_t>(v, propHeapSorts)) {
            property = false;
            std::cout << "  counterexample: seed=" << seed << " n=" << n
                      << " pattern=" << (seed % 5) << std::endl;
        }
    }
    check(property, "400 randomised and adversarial large-leaf cases match std::sort");
#ifdef STRATUM_ENABLE_METRICS
    check(propHeapSorts > 0, "the property sweep also reached heapSort at least once");
    std::cout << "   heapSort executions: " << heapSorts << " pinned + "
              << propHeapSorts << " from the property sweep" << std::endl;
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
// 8. The local-sort dispatch is decoupled from the leaf threshold
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
    section("8. Local-sort dispatch decoupling");
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

// ------------------------------------------------------------------
// 9. Parameter ceilings
// ------------------------------------------------------------------
// Before the ceilings, lambda = t = n was legal, put the whole array in one
// leaf and made the worst case Theta(n log n). This runs the configurations
// that used to allow that, and the ones at and far past the ceiling,
// against inputs built to be expensive: correctness everywhere, and in the
// research build a leaf and a cost per element that no longer follow n.

// Distinct values in [0, n) in a scrambled order, plus two far outliers
// that make the top-level buckets much wider than the cluster, so a single
// bucket receives nearly everything.
static std::vector<int64_t> permutedCluster(std::size_t n) {
    std::vector<int64_t> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<int64_t>((i * 7919) % n);
    v[n / 3] = int64_t(1) << 62;
    v[2 * n / 3] = (int64_t(1) << 62) + 1;
    return v;
}

#ifdef STRATUM_ENABLE_METRICS
static std::size_t largestExpensiveLeaf(const std::vector<int64_t>& v, std::size_t lambda,
                                        std::size_t t) {
    StratumSort<int64_t> probe(lambda, t);
    const auto leaves = probe.debugPartitionOnly(v);
    std::size_t worst = 0;
    for (const auto& lf : leaves) {
        if (lf.count < 2) continue;
        const auto& buf = lf.inBufferA ? probe.debugBufferA() : probe.debugBufferB();
        const auto mm = std::minmax_element(buf.begin() + lf.start, buf.begin() + lf.start + lf.count);
        if (*mm.first != *mm.second) worst = std::max(worst, lf.count);
    }
    return worst;
}
#endif

void testParameterCeilings() {
    section("9. Parameter ceilings");
    const std::size_t huge = std::numeric_limits<std::size_t>::max();
    const std::size_t LMAX = stratum::MAX_TARGET_ELEMENTS_PER_BIN;
    const std::size_t TMAX = stratum::MAX_LEAF_THRESHOLD;
    const std::size_t n = 30000; // above both ceilings

    std::vector<std::pair<std::string, std::vector<int64_t>>> inputs;
    {
        std::vector<int64_t> v(n);
        for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<int64_t>(i);
        inputs.push_back({"ascending", v});
        std::reverse(v.begin(), v.end());
        inputs.push_back({"descending", v});
    }
    {
        std::vector<int64_t> v(n);
        for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<int64_t>(((i * 7919) % n) % 4);
        inputs.push_back({"four distinct values", v});
    }
    {
        std::vector<int64_t> v(n); // a tight cluster with a sprinkling of far outliers
        for (std::size_t i = 0; i < n; ++i)
            v[i] = (i % 1000 == 0) ? static_cast<int64_t>(i) << 40
                                   : 1000000 + static_cast<int64_t>((i * 7919) % 100);
        inputs.push_back({"concentrated", v});
    }
    {
        std::vector<int64_t> v; // dense core plus one far outlier per level, as in section 1
        uint64_t span = 200;
        for (uint64_t i = 0; i < span; ++i) v.push_back(static_cast<int64_t>(i));
        for (int k = 0; k < 40 && span < (1ULL << 60); ++k) {
            span *= 3;
            v.push_back(static_cast<int64_t>(span));
        }
        while (v.size() < n) v.push_back(static_cast<int64_t>((v.size() * 7919) % 200));
        inputs.push_back({"depth-exhausting", v});
    }
    inputs.push_back({"permuted cluster + 2 outliers", permutedCluster(n)});
    inputs.push_back({"quickSort worst order in a depth-exhausted leaf",
                      peeledAtDefaults(quickSortKillerOrder(384))});
    inputs.push_back({"introsort-exhausting sawtooth in a depth-exhausted leaf (0.10.0 defaults)",
                      peeledFor(sawtooth(4096, 1523), 32)});
    inputs.push_back({"introsort-exhausting sawtooth in a depth-exhausted leaf",
                      peeledAtDefaults(sawtooth(2048, 837))});
    inputs.push_back({"n = 392 default-parameter counterexample", peeledAtDefaults(sawtooth(385, 190))});

    const std::vector<std::pair<std::size_t, std::size_t>> configs = {
        {0, 0},       {32, 64},    {n, n},       {n + 1, n + 1},        {huge, huge}, {1, huge},
        {huge, 1},    {LMAX, TMAX}, {LMAX + 1, TMAX + 1}, {1, 1},       {32, n}};
    bool correct = true;
    auto run = [&](const std::string& name, const std::vector<int64_t>& v, std::size_t lambda,
                   std::size_t t) {
        if (v.empty() || !sortsCorrectly(v, lambda, t)) {
            correct = false;
            std::cout << "  failed: " << name << " with (" << lambda << ", " << t << ")\n";
        }
    };
    for (const auto& c : configs) {
        for (const auto& in : inputs) run(in.first, in.second, c.first, c.second);

        // Values on both sides of every top-level bucket boundary, for this
        // configuration's own grid.
        const uint64_t lambda = StratumSort<int64_t>(c.first, c.second).targetElementsPerBin();
        const uint64_t S = uint64_t(1) << 62;
        const uint64_t sTop = lambda >= n ? 1 : (n + lambda - 1) / lambda;
        const uint64_t W = S / sTop + 1;
        std::vector<int64_t> v = {0, static_cast<int64_t>(S)};
        for (uint64_t i = 0; v.size() < n; ++i) {
            const uint64_t b = sTop > 1 ? 1 + (i / 2) % (sTop - 1) : 1;
            v.push_back(static_cast<int64_t>((i & 1) ? b * W : b * W - 1));
        }
        run("bucket boundaries", v, c.first, c.second);
    }
    check(correct, "expensive inputs sort correctly below, at and far above the ceilings");

#ifdef STRATUM_ENABLE_METRICS
    // lambda = t = n on the permuted cluster: before the ceilings, one leaf
    // of n - 2 elements and a cost per element growing like log n.
    const std::size_t sizes[2] = {20000, 160000};
    double perElement[2] = {0, 0};
    bool bounded = true;
    for (int k = 0; k < 2; ++k) {
        const std::size_t m = sizes[k];
        std::vector<int64_t> v = permutedCluster(m);
        const std::size_t leaf = largestExpensiveLeaf(v, m, m);
        if (leaf > TMAX) bounded = false;
        StratumSort<int64_t> s(m, m);
        s.sort(v);
        perElement[k] = static_cast<double>(s.metrics().comparisons()) / static_cast<double>(m);
        std::cout << "   lambda = t = n = " << m << ": largest expensive leaf " << leaf << ", "
                  << perElement[k] << " comparisons per element" << std::endl;
    }
    check(bounded, "with lambda = t = n the largest expensive leaf stays within the ceiling");
    check(perElement[1] <= 1.05 * perElement[0],
          "with lambda = t = n the comparisons per element do not grow from n = 20000 to 160000");

    // The worst orders reach the paths they are meant to, at the defaults.
    StratumSort<int64_t> q;
    std::vector<int64_t> qv = peeledAtDefaults(quickSortKillerOrder(384));
    q.sort(qv);
    check(q.metrics().algorithmUsage().count("QuickSort") == 1 &&
              q.metrics().comparisons() >= 384u * 384u / 8u,
          "the quickSort worst order drives quickSort quadratic inside a depth-exhausted leaf");
    // The 4096-element sawtooth pinned where it was built, at 0.10.0's
    // defaults (32, 64): it needs ~67 bits of key at lambda = 16.
    StratumSort<int64_t> h(32, 64);
    std::vector<int64_t> hv = peeledFor(sawtooth(4096, 1523), 32);
    h.sort(hv);
    check(!hv.empty() && h.metrics().algorithmUsage().count("HeapSort") == 1,
          "the 4096-element sawtooth reaches heapSort inside a depth-exhausted leaf at (32, 64)");
    // And the same property at the CURRENT defaults, with a core that fits.
    StratumSort<int64_t> h2;
    std::vector<int64_t> hv2 = peeledAtDefaults(sawtooth(2048, 837));
    h2.sort(hv2);
    check(!hv2.empty() && h2.metrics().algorithmUsage().count("HeapSort") == 1,
          "a 2048-element sawtooth reaches heapSort inside a depth-exhausted leaf at the defaults");
#endif
}

// ------------------------------------------------------------------
// 10. Presorted inputs (0.11.0)
// ------------------------------------------------------------------
// The analysis pass recognises an input that is already ascending, one
// that is non-increasing, and one with a long ascending prefix; the first
// two finish without allocating anything, the third sorts only the tail and
// merges. These are new paths, so they get their own checks: correctness
// on the boundaries of each decision, no allocation where none is claimed,
// and the strong exception guarantee on the one path that allocates.
template <typename T>
static std::vector<T> prefixThenTail(std::size_t n, std::size_t prefix, int tailShape,
                                     std::mt19937_64& rng) {
    std::vector<T> v(n);
    for (auto& x : v) x = static_cast<T>(rng());
    std::sort(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(prefix));
    auto tb = v.begin() + static_cast<std::ptrdiff_t>(prefix);
    switch (tailShape) {
        case 0: break;                                       // random tail
        case 1: std::sort(tb, v.end()); break;               // two sorted runs
        case 2: std::sort(tb, v.end()); std::reverse(tb, v.end()); break;
        case 3: // tail made of the keys at the prefix's end: ties across the merge
            for (auto it = tb; it != v.end(); ++it)
                *it = prefix > 0 ? v[prefix - 1 - (rng() % std::min<std::size_t>(prefix, 3))] : T{};
            break;
        case 4: for (auto it = tb; it != v.end(); ++it) *it = static_cast<T>(rng() % 3); break;
    }
    return v;
}

void testPresortedInputs() {
    section("10. Presorted inputs");
    std::mt19937_64 rng(20260928);

    // Already ordered: correct, and nothing allocated - not even the
    // workspace a fresh instance would otherwise create.
    bool noAlloc = true;
    bool correct = true;
    for (std::size_t n : {2u, 3u, 64u, 65u, 1000u, 100000u}) {
        std::vector<int64_t> asc(n), desc(n), descTies(n), equal(n, -7);
        for (std::size_t i = 0; i < n; ++i) {
            asc[i] = static_cast<int64_t>(i) - 500;
            desc[i] = static_cast<int64_t>(n - i);
            descTies[i] = static_cast<int64_t>((n - i) / 3);
        }
        for (const auto* in : {&asc, &desc, &descTies, &equal}) {
            std::vector<int64_t> v = *in;
            StratumSort<int64_t> s;
            s.sort(v);
            std::vector<int64_t> want = *in;
            std::sort(want.begin(), want.end());
            correct = correct && v == want;
            noAlloc = noAlloc && s.scratchBytes() == 0;
        }
    }
    check(correct, "sorted, reversed, reversed-with-ties and constant inputs sort correctly");
    check(noAlloc, "already ordered inputs allocate no scratch at all");

    // Long ascending prefix + tail, on both sides of the n/2 decision and
    // with every tail shape, for three key widths.
    bool merged = true;
    for (std::size_t n : {2u, 3u, 5u, 64u, 100u, 1001u, 20000u}) {
        for (std::size_t prefix : {n / 2 - (n / 2 > 0 ? 1 : 0), n / 2, n / 2 + 1, (3 * n) / 4, n - 1}) {
            if (prefix == 0 || prefix >= n) continue;
            for (int tail = 0; tail < 5; ++tail) {
                const auto a = prefixThenTail<int64_t>(n, prefix, tail, rng);
                const auto b = prefixThenTail<uint8_t>(n, prefix, tail, rng);
                const auto c = prefixThenTail<int16_t>(n, prefix, tail, rng);
                if (!sortsCorrectly(a, 32, 64) || !sortsCorrectly(b, 32, 64) ||
                    !sortsCorrectly(c, 32, 64)) {
                    merged = false;
                    std::cout << "  failed: n=" << n << " prefix=" << prefix << " tail=" << tail
                              << "\n";
                }
            }
        }
    }
    check(merged, "sorted prefix + tail sorts correctly for every tail shape and key width");

    // A chain of prefixes: each tail again has a sorted half, so the tail
    // path recurses. Runs of 1/2, 1/4, 1/8, ... of the input.
    {
        const std::size_t n = 1 << 16;
        std::vector<int64_t> v;
        for (std::size_t len = n / 2; len >= 1; len /= 2) {
            std::vector<int64_t> run(len);
            for (auto& x : run) x = static_cast<int64_t>(rng() % 100000);
            std::sort(run.begin(), run.end());
            v.insert(v.end(), run.begin(), run.end());
        }
        check(sortsCorrectly(v, 32, 64), "geometric chain of sorted runs (recursive tail path)");
    }
}

// The one presorted path that allocates must keep the strong guarantee: the
// buffer for the tail is reserved before the tail is sorted.
void testPresortedExceptionGuarantee() {
    section("11. Strong exception guarantee on the sorted-prefix path");
    std::mt19937_64 rng(777);
    const std::vector<int64_t> original = prefixThenTail<int64_t>(5000, 4000, 0, rng);
    bool everThrew = false, everCompleted = false, intact = true;
    // Walk the budget up until a run completes: the research build
    // allocates for its bookkeeping too, so no fixed bound fits both.
    for (std::size_t budget = 0; budget < 4000 && !everCompleted; ++budget) {
        std::vector<int64_t> v = original;
        StratumSort<int64_t> sorter;
        bool threw = false;
        g_allocSeen = 0;
        g_allocSkipped = 0;
        g_allocBudget = budget;
        g_allocLimiterOn = true;
        try {
            sorter.sort(v);
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        g_allocLimiterOn = false;
        everThrew = everThrew || threw;
        everCompleted = everCompleted || !threw;
        if (threw && v != original) intact = false;
    }
    check(everThrew, "the limiter fired on the sorted-prefix path");
    check(everCompleted, "the sweep reached a run that allocates freely");
#ifndef STRATUM_ENABLE_METRICS
    check(intact, "on bad_alloc in the sorted-prefix path the input is left exactly as it was");
#else
    (void)intact; // the research build does not promise it (see section 5)
#endif
}

// ------------------------------------------------------------------
// 12. Automatic parameters (0.11.0)
// ------------------------------------------------------------------
// The constructor without arguments and a Parameters with 0 fields choose
// lambda from n; everything given explicitly keeps 0.10.0's fixed, clamped
// meaning. Both halves of that sentence are pinned here, plus a real sort
// on each side of the threshold.
void testAutomaticParameters() {
    section("12. Automatic parameters");
    const std::size_t big = stratum::AUTOMATIC_LARGE_INPUT;
    auto is = [](const stratum::Parameters& p, std::size_t l, std::size_t t) {
        return p.targetElementsPerBin == l && p.leafThreshold == t;
    };

    StratumSort<int64_t> a;
    check(a.automaticParameters(), "the default constructor is automatic");
    check(a.targetElementsPerBin() == 16 && a.leafThreshold() == 32,
          "an automatic sorter reports the small-input values (16, 32)");
    check(is(a.effectiveParameters(1000), 16, 32) && is(a.effectiveParameters(big), 16, 32) &&
              is(a.effectiveParameters(big + 1), 32, 64),
          "automatic: (16, 32) up to 2^22 elements, (32, 64) above");

    StratumSort<int64_t> fixed(16, 32);
    check(!fixed.automaticParameters() && is(fixed.effectiveParameters(100000000), 16, 32),
          "explicit (16, 32) stays fixed for every n");
    StratumSort<int64_t> zero(0, 0);
    check(!zero.automaticParameters() && is(zero.effectiveParameters(big + 1), 16, 16),
          "positional (0, 0) keeps 0.10.0's meaning: default lambda, t raised to lambda, fixed");

    stratum::Parameters p;
    check(StratumSort<int64_t>(p).automaticParameters(), "Parameters{} is automatic");
    p.targetElementsPerBin = 64;
    check(is(StratumSort<int64_t>(p).effectiveParameters(big + 1), 64, 128),
          "Parameters with lambda set and t = 0: t = 2 * lambda");
    stratum::Parameters q;
    q.leafThreshold = 100;
    check(is(StratumSort<int64_t>(q).effectiveParameters(10), 16, 100) &&
              is(StratumSort<int64_t>(q).effectiveParameters(big + 1), 32, 100),
          "Parameters with t set and lambda = 0: lambda automatic, t fixed");
    stratum::Parameters r;
    r.targetElementsPerBin = 20000;
    r.leafThreshold = 5;
    check(is(StratumSort<int64_t>(r).effectiveParameters(10), 10000, 10000),
          "Parameters fields are clamped like the positional arguments");

    // A real sort on each side of the threshold, by the class and by the
    // free functions, with a shape that refines.
    std::mt19937_64 rng(2222);
    for (std::size_t n : {big, big + 1}) {
        std::vector<int64_t> v(n);
        for (auto& x : v) x = static_cast<int64_t>(rng() % (n * 4));
        std::vector<int64_t> want = v;
        std::sort(want.begin(), want.end());
        std::vector<int64_t> w = v;
        StratumSort<int64_t>().sort(v);
        stratum::sort(w);
        check(v == want && w == want, "automatic sort of n = " + std::to_string(n));
    }
}

// ------------------------------------------------------------------
// 13. Counting instead of moving (0.11.0)
// ------------------------------------------------------------------
// When a grid has width 1 every bucket holds one key, and for an element
// that is its own key the engine counts and writes instead of moving
// (Engine.hpp, countingFill). That path rebuilds elements from keys, so it
// is checked for every self-keyed type, at the extremes of each type, at
// the top level (no element buffer allocated) and nested inside a
// refinement.
template <typename T>
static bool countingCase(std::vector<T> v) {
    std::vector<T> want = v;
    std::sort(want.begin(), want.end());
    stratum::sort(v);
    return v == want;
}

template <typename T>
static bool countingBattery(std::mt19937_64& rng) {
    const T lo = std::numeric_limits<T>::min(), hi = std::numeric_limits<T>::max();
    bool ok = true;
    for (std::size_t n : {3u, 100u, 5000u, 70000u}) {
        std::vector<T> few(n), extremes(n), nested(n);
        for (std::size_t i = 0; i < n; ++i) {
            few[i] = static_cast<T>(lo + static_cast<T>(rng() % 3));
            extremes[i] = (rng() & 1) ? lo : hi;
            // A far outlier makes the top level wide; the cluster below it
            // is then refined, and THAT node has width 1.
            nested[i] = (i % 997 == 0) ? hi : static_cast<T>(lo + static_cast<T>(rng() % 4));
        }
        ok = ok && countingCase(few) && countingCase(extremes) && countingCase(nested);
    }
    return ok;
}

enum class Level : int16_t { Low = -300, Mid = 0, High = 300 };

void testCountingFill() {
    section("13. Counting instead of moving");
    std::mt19937_64 rng(1313);
    const bool integral = countingBattery<int8_t>(rng) && countingBattery<uint8_t>(rng) &&
                          countingBattery<int16_t>(rng) && countingBattery<uint16_t>(rng) &&
                          countingBattery<int32_t>(rng) && countingBattery<uint32_t>(rng) &&
                          countingBattery<int64_t>(rng) && countingBattery<uint64_t>(rng) &&
                          countingBattery<char>(rng) && countingBattery<char16_t>(rng) &&
                          countingBattery<char32_t>(rng) && countingBattery<wchar_t>(rng);
    check(integral, "few distinct values, type extremes and a nested width-1 node, every integral type");

    std::vector<Level> e(50000);
    const Level levels[] = {Level::Low, Level::Mid, Level::High};
    for (auto& x : e) x = levels[rng() % 3];
    std::vector<Level> ew = e;
    std::sort(ew.begin(), ew.end());
    stratum::sort(e);
    check(e == ew, "an enum with a negative underlying value, rebuilt from its keys");

    // Floats with few bit patterns: -0 and +0 are different keys, and the
    // rebuilt element must be the exact bit pattern, not an equal value.
    std::vector<double> d(60000);
    const double pool[] = {-0.0, 0.0, 1.5, -std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::infinity()};
    for (auto& x : d) x = pool[rng() % 5];
    std::vector<double> dw = d;
    std::sort(dw.begin(), dw.end(), [](double a, double b) {
        return stratum::OrderedKey<double>::key(a) < stratum::OrderedKey<double>::key(b);
    });
    stratum::sort(d);
    check(std::memcmp(d.data(), dw.data(), d.size() * sizeof(double)) == 0,
          "doubles with signed zeros and infinities come back bit for bit");

    // At the top level nothing but the counters is allocated.
    std::vector<int64_t> dup(200000);
    for (auto& x : dup) x = static_cast<int64_t>(rng() % 7) - 3;
    StratumSort<int64_t> s;
    s.sort(dup);
    check(std::is_sorted(dup.begin(), dup.end()) && s.scratchBytes() < 1024,
          "a width-1 top level allocates only its counters, not an element buffer");
}

} // namespace

// Allocation limiter for the exception-safety test. Defined at namespace
// scope because operator new must be a global replacement.
void* operator new(std::size_t sz) {
    if (sz == 0) sz = 1;
    if (g_allocLimiterOn && sz < g_limiterMinBytes) {
        ++g_allocSkipped; // bookkeeping, not a working buffer - never refused
        if (sz < g_smallestSkipped) g_smallestSkipped = sz;
    } else if (g_allocLimiterOn) {
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
            g_refusedSize = sz;
            g_refusedDepth = std::uncaught_exceptions();
            g_allocLimiterOn = false;
            throw std::bad_alloc();
        }
        if (g_allocSeen < 64) g_allocSizes[g_allocSeen] = sz;
        ++g_allocSeen;
        --g_allocBudget;
    }
    void* p = std::malloc(sz);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t sz) { return operator new(sz); }
// The nothrow forms, with the standard's default behaviour: call the form
// above and return nullptr instead of throwing. Left unreplaced, a sanitizer
// runtime supplies its own, and the free() below then releases a block it
// did not allocate - std::stable_sort's temporary buffer, under ASan.
void* operator new(std::size_t sz, const std::nothrow_t&) noexcept {
    try { return operator new(sz); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t sz, const std::nothrow_t&) noexcept {
    try { return operator new[](sz); } catch (...) { return nullptr; }
}

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
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// An exception that escapes every handler calls std::terminate, whose default
// behaviour is abort(). On Windows a Debug-CRT abort() raises an error dialog,
// and a CI runner has nobody to dismiss it: the job stops failing and starts
// hanging until the workflow timeout kills it. That is why this suite showed
// up as a 20-minute timeout instead of a test failure.
//
// This handler is diagnosis, not suppression. The process still ends in
// failure - it just says what escaped, and says it now. std::set_terminate and
// std::_Exit are both plain C++11, so this behaves the same on every platform.
void reportTerminate() {
    g_allocLimiterOn = false; // so the reporting below can allocate
    std::cout << "\n*** std::terminate: ";
    if (std::current_exception()) {
        try {
            std::rethrow_exception(std::current_exception());
        } catch (const std::exception& e) {
            std::cout << "escaping " << typeid(e).name() << ": " << e.what();
        } catch (...) {
            std::cout << "escaping exception of non-standard type";
        }
    } else {
        std::cout << "no exception was in flight";
    }
    std::cout << " ***\n    refused a " << g_refusedSize << "-byte allocation"
              << " at uncaught_exceptions()=" << g_refusedDepth
              << "\n    allocations granted first:";
    for (std::size_t i = 0; i < g_allocSeen && i < 64; ++i) std::cout << " " << g_allocSizes[i];
    std::cout << std::endl;
    std::_Exit(70); // no atexit handlers, no abort dialog, still a failure
}

int main() {
    std::set_terminate(reportTerminate);
    std::cout << "=== StratumSort - API contract tests ===\n";
    testParameterSpace();
    testConstructorClamping();
    testRangeArithmetic();
    testInstanceReuse();
    testStrongExceptionGuarantee();
    testDeterminism();
    testIntroSortFallback();
#ifdef STRATUM_ENABLE_METRICS
    testDispatchDecoupling();
#endif
    testParameterCeilings();
    testPresortedInputs();
    testPresortedExceptionGuarantee();
    testAutomaticParameters();
    testCountingFill();

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    if (g_failures != 0) {
        std::cout << "FAILED\n";
        return 1;
    }
    std::cout << "All API contract tests passed." << std::endl;
    return 0;
}
