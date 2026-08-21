// Half of the ODR guard. See CMakeLists.txt.
//
// STRATUM_ENABLE_METRICS adds a member to StratumSort, so the class has a
// different layout in the two configurations. The class therefore lives in
// an inline namespace tagged by the macro, which makes the two variants
// distinct types with distinct mangled names.
//
// This file defines a function whose signature mentions that type. The
// other half declares the same signature and calls it. If both are built
// with the same setting the program links; if they disagree the linker
// cannot resolve the call, which is the whole point: a layout mismatch
// must be a build error, never silent memory corruption at run time.
#include <stratum/StratumSort.hpp>

#include <cstdint>
#include <vector>

void stratum_odr_probe(stratum::StratumSort<int64_t>& sorter, std::vector<int64_t>& data) {
    sorter.sort(data);
}
