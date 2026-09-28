// Minimal Stratum Sort examples.
//
//   make examples
// or, with nothing but a compiler:
//   c++ -std=c++17 -O2 -Iinclude examples/basic.cpp -o basic && ./basic
#include <stratum/StratumSort.hpp>

#include <cstdint>
#include <iostream>
#include <vector>

struct Order {
    uint64_t customer;
    uint32_t amount;
    uint32_t sequence; // arrival order, to show stability
};

int main() {
    // 1. Integers: one call, ascending.
    std::vector<int64_t> data{5, 3, 9, 1, 7, 3};
    stratum::sort(data);
    for (auto x : data) std::cout << x << ' '; // 1 3 3 5 7 9
    std::cout << '\n';

    // 2. Floating point, in IEEE-754 totalOrder: -0.0 before +0.0, NaNs at
    //    the ends, every bit pattern preserved.
    std::vector<double> prices{2.5, -0.0, 0.0, -1.25, 1e300};
    stratum::sort(prices);
    for (auto x : prices) std::cout << x << ' '; // -1.25 -0 0 2.5 1e+300
    std::cout << '\n';

    // 3. Records by a key, keeping the arrival order of equal keys.
    std::vector<Order> orders{{7, 10, 0}, {3, 25, 1}, {7, 5, 2}, {3, 40, 3}};
    stratum::stable_sort_by_key(orders, [](const Order& o) { return o.customer; });
    for (const Order& o : orders)
        std::cout << o.customer << '#' << o.sequence << ' '; // 3#1 3#3 7#0 7#2
    std::cout << '\n';

    // 4. 0.10.0's class, unchanged. Without arguments lambda is chosen per
    //    call from n; Parameters names the two tuning knobs, so they
    //    cannot be swapped by accident, and a workspace makes the sorter
    //    shareable: sort(data, workspace) is const, so several threads can
    //    use one sorter, each with its own workspace.
    stratum::Parameters p;
    p.targetElementsPerBin = 32;
    p.leafThreshold = 64;
    const stratum::StratumSort<int64_t> tuned(p);
    stratum::Workspace<int64_t> workspace;
    std::vector<int64_t> other{9, 9, 2, 8, 2};
    tuned.sort(other, workspace);
    for (auto x : other) std::cout << x << ' '; // 2 2 8 9 9
    std::cout << '\n';
    std::cout << "lambda=" << tuned.targetElementsPerBin() << " t=" << tuned.leafThreshold()
              << " scratch held by the workspace: " << workspace.bytes() << " bytes\n";
    return 0;
}
