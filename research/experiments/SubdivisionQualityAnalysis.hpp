#pragma once

#include "stratum/Metrics.hpp"
#include "Statistics.hpp"

#include <iomanip>
#include <map>
#include <ostream>
#include <vector>

namespace stratum::experimental {

// ============================================================
// Investigacion 1/4 (v6): subdivision quality report
// ============================================================
// "Se realizaron X subdivisiones. Y de ellas: A% mejoraron claramente.
// B% apenas aportaron. C% fueron innecesarias." - using the Useful /
// Marginal / Useless classification SortMetrics computes from each
// subdivision's measured reductionPct. Still part of the research build
// in v7 - unlike Difficulty Score (removed, see docs/history/ANALYSIS_v6.md
// "Investigacion 3" and docs/history/ANALYSIS_v7.md), this instrumentation never
// participated in any decision the algorithm makes, so there was nothing
// to remove from the hot path.
inline void printSubdivisionQualityReport(const stratum::SortMetrics& m, std::ostream& os) {
    const auto breakdown = m.subdivisionQualityBreakdown();
    if (breakdown.total == 0) {
        os << "Se realizaron 0 subdivisiones.\n";
        return;
    }
    const double usefulPct = 100.0 * static_cast<double>(breakdown.useful) / static_cast<double>(breakdown.total);
    const double marginalPct =
        100.0 * static_cast<double>(breakdown.marginal) / static_cast<double>(breakdown.total);
    const double uselessPct =
        100.0 * static_cast<double>(breakdown.useless) / static_cast<double>(breakdown.total);

    os << "Se realizaron " << breakdown.total << " subdivisiones.\n";
    os << "De ellas:\n";
    os << "  " << std::fixed << std::setprecision(1) << usefulPct << "% mejoraron claramente ("
       << breakdown.useful << ", reduccion >= 50%)\n";
    os << "  " << std::fixed << std::setprecision(1) << marginalPct << "% apenas aportaron ("
       << breakdown.marginal << ", reduccion 10-50%)\n";
    os << "  " << std::fixed << std::setprecision(1) << uselessPct << "% fueron innecesarias ("
       << breakdown.useless << ", reduccion < 10%)\n";
    os << "Subdivision Efficiency (reduccion media): " << std::fixed << std::setprecision(4)
       << m.subdivisionEfficiency() << "\n";
}

// ============================================================
// Estructura del arbol de refinamiento: trabajo por nivel
// ============================================================
inline void printWorkByDepthReport(const stratum::SortMetrics& m, std::ostream& os) {
    const auto& records = m.subdivisionRecords();
    if (records.empty()) {
        os << "No hay subdivisiones registradas.\n";
        return;
    }

    std::map<std::size_t, std::size_t> countByDepth;
    for (const auto& rec : records) ++countByDepth[rec.depth];
    os << "Subdivisiones por profundidad:\n";
    for (const auto& [depth, count] : countByDepth) {
        os << "  profundidad " << depth << ": " << count << " subdivisiones\n";
    }

    os << "Trabajo (elementos reprocesados) por nivel:\n";
    const auto& work = m.workByDepth();
    for (std::size_t d = 0; d < work.size(); ++d) {
        if (work[d] > 0) os << "  profundidad " << d << ": " << work[d] << " elementos\n";
    }
}

// ============================================================
// Investigacion 8 (v6): correlacion entre tamano/profundidad y reduccion
// ============================================================
// Difficulty Score se retiro de esta tabla en v7 junto con el resto de
// su codigo (ver docs/history/ANALYSIS_v6.md/v7.md) - las columnas que quedan son las
// que la investigacion de v6 encontro realmente predictivas.
inline void printCorrelationAnalysis(const stratum::SortMetrics& m, std::ostream& os) {
    const auto& records = m.subdivisionRecords();
    if (records.size() < 2) {
        os << "No hay suficientes subdivisiones para calcular correlaciones.\n";
        return;
    }

    std::vector<double> reduction, originalSize, depth, childCount;
    for (const auto& rec : records) {
        reduction.push_back(rec.reductionPct);
        originalSize.push_back(static_cast<double>(rec.originalSize));
        depth.push_back(static_cast<double>(rec.depth));
        childCount.push_back(static_cast<double>(rec.childCount));
    }

    os << "Correlacion de Pearson (n=" << records.size() << " subdivisiones):\n";
    os << "  Tamano original   vs reductionPct: " << std::fixed << std::setprecision(4)
       << stratum::stats::pearsonCorrelation(originalSize, reduction) << "\n";
    os << "  Profundidad       vs reductionPct: " << std::fixed << std::setprecision(4)
       << stratum::stats::pearsonCorrelation(depth, reduction) << "\n";
    os << "  Numero de hijos   vs reductionPct: " << std::fixed << std::setprecision(4)
       << stratum::stats::pearsonCorrelation(childCount, reduction) << "\n";
}

} // namespace stratum::experimental
