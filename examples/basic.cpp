// Minimal Stratum Sort example. Build: make examples
#include "stratum/StratumSort.hpp"

#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

int main() {
    std::mt19937_64 rng(42);
    std::vector<int64_t> data(20);
    for (auto& x : data) x = static_cast<int64_t>(rng() % 1000);

    std::cout << "before:";
    for (auto x : data) std::cout << ' ' << x;
    std::cout << '\n';

    stratum::StratumSort<int64_t> sorter;
    sorter.sort(data);

    std::cout << "after: ";
    for (auto x : data) std::cout << ' ' << x;
    std::cout << '\n';

    // The tuning parameters are optional. lambda is the target elements per
    // bin; t is the size at which refinement stops. Both are clamped, so no
    // combination can misbehave - see docs/usage.md.
    stratum::StratumSort<int64_t> tuned(/*lambda=*/64, /*t=*/128);
    std::vector<int64_t> other = data;
    tuned.sort(other);
    std::cout << "lambda=" << tuned.targetElementsPerBin()
              << " t=" << tuned.leafThreshold() << '\n';
    return 0;
}
