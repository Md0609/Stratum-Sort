#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

namespace stratum::testing {

using DataVector = std::vector<int64_t>;

// ============================================================
// DatasetGenerator
// ============================================================
// Produces the dataset shapes requested in the TESTS section of
// the specification. A fixed seed is used by default so runs are
// reproducible.
// ============================================================
class DatasetGenerator {
public:
    explicit DatasetGenerator(uint64_t seed = 42) : rng_(seed) {}

    DataVector randomUniform(std::size_t n, int64_t minV = 0, int64_t maxV = 1'000'000) {
        std::uniform_int_distribution<int64_t> dist(minV, maxV);
        DataVector v(n);
        for (auto& x : v) x = dist(rng_);
        return v;
    }

    // Uniform, with the value range PROPORTIONAL to n, so that the density
    // n/range stays constant across a sweep of sizes.
    //
    // randomUniform() holds its range fixed at 10^6 while n sweeps several
    // orders of magnitude, so its density - and with it the redundancy that
    // lets refine() take the observedMin == observedMax shortcut - grows
    // with n by construction of the dataset. Fitting a complexity exponent
    // on that mixes the scaling in n with the rising redundancy, and the
    // second effect pushes hardest at the large points, which are exactly
    // the ones that dominate the fit.
    //
    // This generator isolates the variable, and is the only one in the
    // battery on which estimating an exponent is meaningful.
    DataVector randomUniformScaled(std::size_t n, uint64_t elementsPerValue = 16) {
        const uint64_t span = n == 0 ? 1 : static_cast<uint64_t>(n) * elementsPerValue;
        std::uniform_int_distribution<int64_t> dist(0, static_cast<int64_t>(span));
        DataVector v(n);
        for (auto& x : v) x = dist(rng_);
        return v;
    }

    DataVector sortedAscending(std::size_t n) {
        DataVector v(n);
        for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<int64_t>(i);
        return v;
    }

    DataVector sortedDescending(std::size_t n) {
        DataVector v(n);
        for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<int64_t>(n - i);
        return v;
    }

    DataVector manyRepeated(std::size_t n, int distinctValues = 5) {
        std::uniform_int_distribution<int> dist(0, distinctValues - 1);
        DataVector v(n);
        for (auto& x : v) x = dist(rng_);
        return v;
    }

    DataVector normalDistribution(std::size_t n, double mean = 500'000.0, double stddev = 50'000.0) {
        std::normal_distribution<double> dist(mean, stddev);
        DataVector v(n);
        for (auto& x : v) x = static_cast<int64_t>(std::llround(dist(rng_)));
        return v;
    }

    // Most values packed into a very narrow sub-range, with a small
    // fraction of far-away outliers. Exercises subdivision heavily.
    DataVector concentrated(std::size_t n) {
        std::uniform_int_distribution<int64_t> narrow(100'000, 100'100);
        std::uniform_int_distribution<int64_t> outlier(0, 1'000'000);
        std::bernoulli_distribution isOutlier(0.01);
        DataVector v(n);
        for (auto& x : v) x = isOutlier(rng_) ? outlier(rng_) : narrow(rng_);
        return v;
    }

    DataVector smallRangeManyElements(std::size_t n) {
        std::uniform_int_distribution<int64_t> dist(0, 99);
        DataVector v(n);
        for (auto& x : v) x = dist(rng_);
        return v;
    }

    DataVector hugeRangeFewElements(std::size_t n) {
        std::uniform_int_distribution<int64_t> dist(std::numeric_limits<int64_t>::min() / 2,
                                                      std::numeric_limits<int64_t>::max() / 2);
        DataVector v(n);
        for (auto& x : v) x = dist(rng_);
        return v;
    }

    // ========================================================================
    // Two cases none of the eight generators above exercises: a span
    // covering the whole key universe, and an input built to exhaust the
    // refinement depth.
    // ========================================================================

    // Whole-universe span. Contains INT64_MIN and INT64_MAX explicitly, so
    // that max - min + 1 is exactly 2^64 - the value that wraps to 0 in
    // 64-bit unsigned arithmetic, and the reason the algorithm works in
    // terms of the span rather than the count of distinct values. Both
    // extremes are planted on purpose: a uniform sample over the whole
    // universe essentially never reaches them.
    DataVector fullRangeExtremes(std::size_t n) {
        const int64_t lo = std::numeric_limits<int64_t>::min();
        const int64_t hi = std::numeric_limits<int64_t>::max();
        DataVector v(n);
        if (n == 0) return v;
        std::uniform_int_distribution<int64_t> dist(lo, hi);
        for (auto& x : v) x = dist(rng_);
        v[0] = lo;
        if (n > 1) v[n - 1] = hi;
        std::shuffle(v.begin(), v.end(), rng_);
        return v;
    }

    // Adversarial input: groups that shed exactly ONE element per
    // refinement level, which is the slowest possible progress.
    //
    // Within a group, start from a core of 'target' contiguous values
    // (span S) and repeatedly append a single distant value at offset
    // s*S, where s = ceil(size/target) is the fan-out the algorithm will
    // choose. At that separation the resulting interval width is S+1 > S,
    // so the entire core lands in interval 0 and the new value lands in a
    // higher one: the split shrinks the largest subproblem by one element.
    //
    // A group's span grows geometrically, so the number of levels that can
    // be forced is limited by the bits available per group, which in turn
    // depend on how many groups must be laid out disjointly in the key
    // universe.
    //
    // 'coreParam' sets the size of the group's contiguous core; 0 means
    // "use target". The core size selects the adversary's regime:
    //   small core -> splits = 2            -> 1 bit of span per level
    //                                       -> many degenerate levels, tiny residual
    //   large core -> splits = C/target     -> log2(C/target) bits per level
    //                                       -> few levels, large residual
    // Only the second regime produces a large bin for the comparison sort.
    DataVector adversarialPeeling(std::size_t n, std::size_t target = 64,
                                   std::size_t coreParam = 0) {
        DataVector v;
        v.reserve(n);
        if (n == 0) return v;
        if (target < 1) target = 1;

        // Span budget per group: the universe is split evenly between the
        // groups, which must be disjoint in value. Only the positive half
        // is used, leaving room for the separation between groups without
        // overflowing.
        const std::size_t coreSize = std::min(coreParam == 0 ? target : coreParam, n);
        std::size_t groupsEstimate = n / std::max<std::size_t>(coreSize * 2, 1);
        if (groupsEstimate == 0) groupsEstimate = 1;
        const uint64_t budget =
            (static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) / groupsEstimate) / 2;

        // One group's offsets, built once and reused: every group has the
        // same shape and differs only in its base value.
        std::vector<uint64_t> offsets;
        offsets.reserve(coreSize + 64);
        uint64_t span = 0;
        for (std::size_t i = 0; i < coreSize; ++i) {
            offsets.push_back(static_cast<uint64_t>(i));
            span = static_cast<uint64_t>(i);
        }
        while (offsets.size() < n) {
            const std::size_t sizeAfter = offsets.size() + 1;
            const uint64_t splits = (sizeAfter + target - 1) / target;
            if (splits < 2) break;
            if (span > budget / splits) break; // the group ran out of bits
            const uint64_t next = span * splits;
            if (next <= span) break;
            offsets.push_back(next);
            span = next;
        }

        const uint64_t stride = span + 2; // separation between groups
        uint64_t base = 0;
        while (v.size() < n) {
            for (uint64_t off : offsets) {
                if (v.size() == n) break;
                v.push_back(static_cast<int64_t>(base + off));
            }
            base += stride;
        }
        std::shuffle(v.begin(), v.end(), rng_);
        return v;
    }

private:
    std::mt19937_64 rng_;
};

} // namespace stratum::testing
