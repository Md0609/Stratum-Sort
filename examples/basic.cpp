// Minimal Stratum Sort example.
//
//   make examples
// or, with nothing but a compiler:
//   c++ -std=c++17 -O2 -Iinclude examples/basic.cpp -o basic && ./basic
#include <stratum/StratumSort.hpp>

#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    std::vector<int64_t> data{5, 3, 9, 1, 7, 3};

    stratum::StratumSort<int64_t> sorter;
    sorter.sort(data);

    for (auto x : data) std::cout << x << ' ';   // 1 3 3 5 7 9
    std::cout << '\n';

    // The tuning parameters are optional and both are clamped, so no
    // combination can misbehave: lambda is the target elements per bin, t
    // the size at which refinement stops. docs/usage.md explains when to
    // change them - the short answer is "only for inputs much larger than
    // a million elements".
    stratum::StratumSort<int64_t> tuned(/*lambda=*/64, /*t=*/128);
    std::vector<int64_t> other{9, 9, 2, 8, 2};
    tuned.sort(other);

    for (auto x : other) std::cout << x << ' ';  // 2 2 8 9 9
    std::cout << '\n';
    std::cout << "lambda=" << tuned.targetElementsPerBin()
              << " t=" << tuned.leafThreshold() << '\n';
    return 0;
}
