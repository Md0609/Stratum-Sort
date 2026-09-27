// The other half of the ODR guard. See odr_guard_lib.cpp and CMakeLists.txt.
#include <stratum/StratumSort.hpp>

#include <cstdint>
#include <vector>

void stratum_odr_probe(stratum::StratumSort<int64_t>& sorter, std::vector<int64_t>& data);

int main() {
    stratum::StratumSort<int64_t> sorter;
    std::vector<int64_t> data{3, 1, 2};
    stratum_odr_probe(sorter, data);
    return (data[0] == 1 && data[1] == 2 && data[2] == 3) ? 0 : 1;
}
