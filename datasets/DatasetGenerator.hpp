#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

namespace drs::testing {

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

private:
    std::mt19937_64 rng_;
};

} // namespace drs::testing
