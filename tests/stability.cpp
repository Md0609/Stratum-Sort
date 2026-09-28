#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

// ============================================================
// Stability, key/value records, enums and indirect sorting
// ============================================================
// stable_sort_by_key promises that records with equal keys keep their
// original order, for every input. That promise is only worth what the
// tests behind it are worth, so every record here carries its original
// position as a payload, and the output is compared FIELD FOR FIELD with
// std::stable_sort on the same records - not merely checked to be sorted.
//
// The inputs are chosen to reach every place where a sort can lose
// stability (see detail/Engine.hpp, LocalSort):
//   - thousands of duplicates per key (5, 100 and 256 distinct keys), so
//     equal keys meet in every leaf and every distribution level;
//   - a non-increasing input with ties (the reversal fast path);
//   - descending runs inside leaves (the reversal of a leaf);
//   - leaves larger than 64 (merge sort), including a whole array that is
//     one leaf (lambda >= n) and depth-exhausted leaves of thousands;
//   - a long sorted prefix and a tail with keys equal to the prefix's
//     (the prefix merge, whose tie rule is what keeps it stable);
//   - every lambda/t combination the contract suite uses.
// The unstable sort_by_key is checked for what it does promise: sorted by
// key, and the same multiset of records.
#include "stratum/StratumSort.hpp"
#include "DatasetGenerator.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

int g_checks = 0, g_failures = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok && ++g_failures <= 30) std::printf("  FAIL: %s\n", what.c_str());
}

template <typename K>
struct Record {
    K key;
    uint32_t seq;      // original position: the identifiable payload
    uint32_t salt;     // a second field, so a record is not just (key, seq)
};

template <typename K>
bool sameRecords(const std::vector<Record<K>>& a, const std::vector<Record<K>>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::memcmp(&a[i].key, &b[i].key, sizeof(K)) != 0 || a[i].seq != b[i].seq ||
            a[i].salt != b[i].salt)
            return false;
    }
    return true;
}

template <typename K>
std::vector<Record<K>> withPayload(const std::vector<K>& keys) {
    std::vector<Record<K>> r(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i)
        r[i] = {keys[i], static_cast<uint32_t>(i), static_cast<uint32_t>(i * 2654435761u)};
    return r;
}

template <typename K>
uint64_t orderKey(const Record<K>& r) { return stratum::OrderedKey<K>::key(r.key); }

// The oracle: std::stable_sort by the same key order.
template <typename K>
std::vector<Record<K>> oracle(std::vector<Record<K>> r) {
    std::stable_sort(r.begin(), r.end(),
                     [](const Record<K>& a, const Record<K>& b) { return orderKey(a) < orderKey(b); });
    return r;
}

template <typename K>
void stableCase(const std::vector<K>& keys, std::size_t lambda, std::size_t t, const std::string& name) {
    const std::vector<Record<K>> input = withPayload(keys);
    const std::vector<Record<K>> want = oracle(input);
    auto byKey = [](const Record<K>& r) { return r.key; };

    std::vector<Record<K>> got = input;
    stratum::Workspace<Record<K>> ws;
    stratum::Parameters p;
    p.targetElementsPerBin = lambda;
    p.leafThreshold = t;
    stratum::stable_sort_by_key(got, byKey, ws, p);
    check(sameRecords(got, want), "stable_sort_by_key keeps equal keys in order: " + name +
                                      " lambda=" + std::to_string(lambda) + " t=" + std::to_string(t));

    // Unstable: sorted by key, same multiset of records.
    std::vector<Record<K>> u = input;
    stratum::sort_by_key(u, byKey, ws, p);
    bool sorted = true;
    for (std::size_t i = 1; i < u.size(); ++i) sorted = sorted && orderKey(u[i - 1]) <= orderKey(u[i]);
    std::vector<Record<K>> a = u, b = want;
    auto bySeq = [](const Record<K>& x, const Record<K>& y) { return x.seq < y.seq; };
    std::sort(a.begin(), a.end(), bySeq);
    std::sort(b.begin(), b.end(), bySeq);
    check(sorted && sameRecords(a, b), "sort_by_key sorts and permutes: " + name);

    // Indirect: the stable permutation.
    const std::vector<std::size_t> order = stratum::sorted_indices(input.begin(), input.end(), byKey);
    bool perm = order.size() == input.size();
    for (std::size_t i = 0; perm && i < order.size(); ++i) perm = input[order[i]].seq == want[i].seq;
    check(perm, "sorted_indices is the stable permutation: " + name);
}

template <typename K>
std::vector<K> keysFrom(const std::vector<uint64_t>& raw) {
    std::vector<K> k(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) k[i] = static_cast<K>(raw[i]);
    return k;
}

template <typename K>
void battery(const char* keyName) {
    std::printf("stable sort, %s keys\n", keyName);
    std::mt19937_64 rng(4242 + sizeof(K));
    const std::vector<std::pair<std::size_t, std::size_t>> params = {
        {32, 64}, {1, 1}, {2, 2}, {8, 16}, {64, 64}, {1000, 5000}, {10000, 10000}};

    for (std::size_t n : {0u, 1u, 2u, 3u, 63u, 64u, 65u, 385u, 5000u, 60000u}) {
        for (std::size_t distinct : {1u, 5u, 100u, 256u}) {
            std::vector<uint64_t> raw(n);
            for (auto& x : raw) x = rng() % distinct;
            for (const auto& lt : params)
                stableCase(keysFrom<K>(raw), lt.first, lt.second,
                           "n=" + std::to_string(n) + " distinct=" + std::to_string(distinct));
        }
        // Non-increasing with ties: the whole-input reversal.
        std::vector<uint64_t> desc(n);
        for (std::size_t i = 0; i < n; ++i) desc[i] = (n - i) / 7;
        stableCase(keysFrom<K>(desc), 32, 64, "non-increasing with ties n=" + std::to_string(n));
        // Sorted prefix, tail made of the prefix's own keys: the merge's ties.
        if (n >= 4) {
            std::vector<uint64_t> pt(n);
            for (std::size_t i = 0; i < n; ++i) pt[i] = i < (3 * n) / 4 ? i / 9 : (rng() % (n / 9 + 1));
            stableCase(keysFrom<K>(pt), 32, 64, "sorted prefix + tied tail n=" + std::to_string(n));
        }
        // Sawtooth of descending runs with ties: leaves that are
        // non-increasing, reversed stably inside the engine.
        std::vector<uint64_t> saw(n);
        for (std::size_t i = 0; i < n; ++i) saw[i] = 1000 - (i % 97) / 3 + (i / 97) * 2000;
        stableCase(keysFrom<K>(saw), 32, 64, "descending runs with ties n=" + std::to_string(n));
    }
}

void adversarialLeaves() {
    std::printf("stable sort, depth-exhausted leaves (merge sort inside the engine)\n");
    stratum::testing::DatasetGenerator g(99);
    for (std::size_t n : {2000u, 30000u}) {
        std::vector<int64_t> adv = g.adversarialPeeling(n, 32, 2048);
        // Collapse the core onto few values: many duplicates inside the big leaf.
        for (auto& x : adv) if (x >= 0 && x < 2048) x = x % 4;
        stableCase(adv, 32, 64, "adversarial core with duplicates n=" + std::to_string(n));
    }
}

// Float keys: -0/+0 and NaN payloads are distinct keys in totalOrder, so
// records keyed by them must come out in totalOrder, ties by position.
void floatKeyedRecords() {
    std::printf("stable sort, float and double keys\n");
    std::mt19937_64 rng(31);
    const float pool[] = {0.0f, -0.0f, 1.5f, -1.5f, std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
    std::vector<float> f(20000);
    for (auto& x : f) x = pool[rng() % 7];
    stableCase(f, 32, 64, "float keys with signed zeros, infinities and NaN");
    std::vector<double> d(20000);
    for (auto& x : d) x = static_cast<double>(static_cast<int>(rng() % 11) - 5) / 4.0;
    stableCase(d, 32, 64, "double keys with many ties");
}

enum class Colour : uint8_t { Red = 3, Green = 1, Blue = 2 };
enum Plain : int { Minus = -5, Zero = 0, Plus = 7 };

void enumsAndChars() {
    std::printf("enums and character types as keys and as elements\n");
    std::mt19937_64 rng(5);
    std::vector<Colour> c(10000);
    for (auto& x : c) x = static_cast<Colour>(1 + rng() % 3);
    stableCase(c, 32, 64, "scoped enum keys");
    std::vector<Plain> p(10000);
    const Plain vals[] = {Minus, Zero, Plus};
    for (auto& x : p) x = vals[rng() % 3];
    stableCase(p, 32, 64, "unscoped enum keys with a negative value");

    // Elements that are their own key: sort / stable_sort.
    std::vector<Colour> e = c;
    stratum::sort(e);
    check(std::is_sorted(e.begin(), e.end(), [](Colour a, Colour b) {
              return static_cast<uint8_t>(a) < static_cast<uint8_t>(b);
          }) && e.size() == c.size(),
          "stratum::sort on a scoped enum");
    std::vector<char16_t> u16(5000);
    std::vector<char32_t> u32(5000);
    std::vector<char> ch(5000);
    std::vector<wchar_t> wc(5000);
    for (std::size_t i = 0; i < 5000; ++i) {
        u16[i] = static_cast<char16_t>(rng());
        u32[i] = static_cast<char32_t>(rng());
        ch[i] = static_cast<char>(rng());
        wc[i] = static_cast<wchar_t>(rng());
    }
    auto ok = [](auto v) {
        auto w = v;
        std::sort(w.begin(), w.end());
        stratum::sort(v);
        return v == w;
    };
    check(ok(u16) && ok(u32) && ok(ch) && ok(wc), "char, char16_t, char32_t and wchar_t sort like std::sort");
    stratum::StratumSort<char32_t> classic;
    std::vector<char32_t> v = u32, w = u32;
    classic.sort(v);
    std::sort(w.begin(), w.end());
    check(v == w, "StratumSort<char32_t> (the 0.10.0 interface) sorts char32_t");
}

// A record with a large payload, to exercise the engine's copies of
// multi-cache-line elements.
struct Wide {
    uint64_t key;
    uint32_t seq;
    char payload[116];
};

void wideRecords() {
    std::printf("stable sort, 128-byte records\n");
    std::mt19937_64 rng(77);
    std::vector<Wide> v(30000);
    for (std::size_t i = 0; i < v.size(); ++i) {
        v[i].key = rng() % 300;
        v[i].seq = static_cast<uint32_t>(i);
        std::memset(v[i].payload, static_cast<int>(i & 0x7F), sizeof v[i].payload);
    }
    std::vector<Wide> want = v;
    std::stable_sort(want.begin(), want.end(), [](const Wide& a, const Wide& b) { return a.key < b.key; });
    stratum::stable_sort_by_key(v, [](const Wide& w) { return w.key; });
    bool same = true;
    for (std::size_t i = 0; i < v.size(); ++i)
        same = same && std::memcmp(&v[i], &want[i], sizeof(Wide)) == 0;
    check(same, "128-byte records: identical to std::stable_sort, payload bytes included");
}

} // namespace

int main() {
    battery<int64_t>("int64");
    battery<uint8_t>("uint8");
    battery<int16_t>("int16");
    battery<uint32_t>("uint32");
    adversarialLeaves();
    floatKeyedRecords();
    enumsAndChars();
    wideRecords();
    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    if (g_failures) {
        std::printf("FAILED\n");
        return 1;
    }
    std::printf("Stability and key/value tests passed.\n");
    return 0;
}
