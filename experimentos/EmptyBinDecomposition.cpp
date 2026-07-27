// De donde salen los bins vacios de Concentrated: buckets INALCANZABLES
// (indice > span, que el tope de CC2 elimina) frente a buckets alcanzables
// pero SIN OCUPAR (que ningun tope puede eliminar).
#include "DynamicRangeSort.hpp"
#include "DatasetGenerator.hpp"
#include <cstdint>
#include <iostream>
#include <vector>
int main() {
    drs::testing::DatasetGenerator g;
    auto data = g.concentrated(1000000);
    // Nivel superior tal y como lo calcula computeRangeParameters
    int64_t mn = data[0], mx = data[0];
    for (auto v : data) { if (v < mn) mn = v; if (v > mx) mx = v; }
    const uint64_t span = (uint64_t)mx - (uint64_t)mn;
    std::size_t bins = (data.size() + 63) / 64;
    if (span < bins) bins = (std::size_t)(span + 1);
    const uint64_t W = span / bins + 1;
    std::vector<std::size_t> occ(bins, 0);
    for (auto v : data) occ[(std::size_t)(((uint64_t)v - (uint64_t)mn) / W)]++;
    std::size_t empty0 = 0, over = 0;
    for (std::size_t i = 0; i < bins; ++i) { if (occ[i] == 0) empty0++; else if (occ[i] > 64) over++; }
    std::cout << "Nivel 0: span=" << span << " bins=" << bins << " W=" << W << "\n";
    std::cout << "  bins vacios en el NIVEL 0 (alcanzables, sin ocupar): " << empty0 << "\n";
    std::cout << "  bins que superan target y entran en refine():        " << over << "\n";
    std::cout << "\nDe los bins que entran en refine(), cuantos buckets serian INALCANZABLES sin el tope:\n";
    std::size_t unreachable = 0, produced = 0;
    for (std::size_t i = 0; i < bins; ++i) {
        if (occ[i] <= 64) continue;
        // span observado del bin i (recalculado, como hace refine())
        int64_t bmn = 0, bmx = 0; bool first = true;
        for (auto v : data) {
            if ((std::size_t)(((uint64_t)v - (uint64_t)mn) / W) != i) continue;
            if (first) { bmn = bmx = v; first = false; }
            else { if (v < bmn) bmn = v; if (v > bmx) bmx = v; }
        }
        const uint64_t bspan = (uint64_t)bmx - (uint64_t)bmn;
        const std::size_t sinTope = (occ[i] + 63) / 64;
        const std::size_t conTope = bspan < sinTope ? (std::size_t)(bspan + 1) : sinTope;
        unreachable += sinTope - conTope;
        produced += conTope;
        std::cout << "  bin " << i << ": count=" << occ[i] << " span=" << bspan
                  << " splits " << sinTope << " -> " << conTope
                  << "  (inalcanzables eliminados: " << sinTope - conTope << ")\n";
    }
    std::cout << "\n  TOTAL buckets inalcanzables eliminados por el tope: " << unreachable << "\n";
    std::cout << "  buckets que el refinamiento sigue produciendo:      " << produced << "\n";
    std::cout << "  vacios irreducibles del nivel 0:                    " << empty0 << "\n";
    return 0;
}
