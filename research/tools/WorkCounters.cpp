// ============================================================
// Total-work counters over the section 4 battery (objective.md A2/A3, G2)
// ============================================================
// EXPERIMENTAL EVIDENCE, deterministic: no clock anywhere. For every
// dataset of datasets/Battery.hpp, every size and both key types, sorts
// once in the research build and prints SortMetrics' total-work counter -
// element reads, writes, comparisons and bucket operations in EVERY phase
// (see the cost model in include/stratum/Metrics.hpp) - plus the per-phase
// split.
//
// Then, per (type, dataset), the A3 summary: the exponent b of the least-
// squares fit  total ~ a * n^b  over the sizes present, and the largest
// work/n. A3 asks for b <= 1.01 between 1e3 and 1e7 and work/n bounded by
// an explicit constant. Read the exponent with care on datasets whose
// value density changes with n (RandomUniform has a fixed range of 10^6):
// it then mixes the scaling in n with the changing redundancy, exactly as
// DatasetGenerator::randomUniformScaled explains.
//
// G2 (objective.md section 5): run this before and after a change and diff
// the CSVs. Every number is exact and reproducible, so ANY difference is a
// change in the algorithm's behaviour, never noise.
//
// Usage: research_WorkCounters [--sizes LIST|all] [--types int64,uint32|all]
//                              [--datasets LIST]
// Defaults: all sizes 1e3..1e7, both types, the whole battery.
#include "Battery.hpp"
#include "stratum/StratumSort.hpp"

#ifndef STRATUM_ENABLE_METRICS
#error "WorkCounters reads sorter.metrics(); build it with -DSTRATUM_ENABLE_METRICS (make counters)."
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using stratum::SortMetrics;
using stratum::testing::AdversaryTarget;
using stratum::testing::battery;
using stratum::testing::batteryDataset;
using stratum::testing::batteryIndex;

namespace {

std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ','))
        if (!item.empty()) out.push_back(item);
    return out;
}

std::size_t parseSize(const std::string& s) {
    const std::size_t e = s.find_first_of("eE");
    if (e == std::string::npos) return static_cast<std::size_t>(std::strtoull(s.c_str(), nullptr, 10));
    std::size_t v = static_cast<std::size_t>(std::strtoull(s.substr(0, e).c_str(), nullptr, 10));
    for (int i = std::atoi(s.substr(e + 1).c_str()); i > 0; --i) v *= 10;
    return v;
}

struct Point {
    double n;
    double total;
};
std::map<std::pair<std::string, std::string>, std::vector<Point>> g_points;
bool g_allCorrect = true;

template <typename T>
void runType(const char* typeName, const std::vector<std::size_t>& sizes,
             const std::vector<std::size_t>& datasets) {
    const AdversaryTarget target{stratum::StratumSort<T>().targetElementsPerBin(),
                                 stratum::MAX_SUBDIVISION_DEPTH};
    for (const std::size_t idx : datasets) {
        const char* name = battery()[idx].name;
        for (const std::size_t n : sizes) {
            std::vector<T> data = batteryDataset<T>(idx, n, target);
            if (data.empty()) {
                std::printf("%s,%s,%zu,0,,,,,,,,,,,,,,,,na\n", typeName, name, n);
                continue;
            }
            std::vector<T> reference = data;
            std::sort(reference.begin(), reference.end());
            stratum::StratumSort<T> sorter;
            sorter.sort(data);
            const bool ok = data == reference && !sorter.metrics().hasUnknownWorkPhase();
            if (!ok) g_allCorrect = false;

            const SortMetrics& m = sorter.metrics();
            const SortMetrics::WorkCounts w = m.totalWork();
            const double perN = static_cast<double>(w.total()) / static_cast<double>(data.size());
            std::printf("%s,%s,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%.4f", typeName, name, n, data.size(),
                        w.reads, w.writes, w.comparisons, w.bucketOps, w.total(), perN);
            for (std::size_t p = 0; p < SortMetrics::kWorkPhases; ++p)
                std::printf(",%zu", m.work(static_cast<SortMetrics::WorkPhase>(p)).total());
            std::printf(",%zu,%zu,%zu,%s\n", m.comparisons(), m.maxSubdivisionDepth(),
                        m.maxBinSize(), ok ? "ok" : "MISMATCH");
            g_points[{typeName, name}].push_back(
                {static_cast<double>(data.size()), static_cast<double>(w.total())});
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::size_t> sizes = {1000, 10000, 100000, 1000000, 10000000};
    std::vector<std::string> types = {"int64", "uint32"};
    std::vector<std::size_t> datasets;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string a = argv[i], v = argv[i + 1];
        if (a == "--sizes" && v != "all") {
            sizes.clear();
            for (const auto& s : splitList(v)) sizes.push_back(parseSize(s));
        } else if (a == "--types" && v != "all") {
            types = splitList(v);
        } else if (a == "--datasets") {
            for (const auto& d : splitList(v)) {
                const std::size_t idx = batteryIndex(d);
                if (idx == battery().size()) {
                    std::fprintf(stderr, "unknown dataset %s\n", d.c_str());
                    return 2;
                }
                datasets.push_back(idx);
            }
        }
    }
    if (datasets.empty())
        for (std::size_t i = 0; i < battery().size(); ++i) datasets.push_back(i);

    std::printf("# Total-work counters, research build (deterministic; no timings). lambda=%zu t=%zu D=%zu\n",
                stratum::DEFAULT_TARGET_ELEMENTS_PER_BIN, stratum::DEFAULT_LEAF_THRESHOLD,
                stratum::MAX_SUBDIVISION_DEPTH);
    std::printf("type,dataset,n,n_real,reads,writes,comparisons,bucketOps,total,work_per_n");
    for (std::size_t p = 0; p < SortMetrics::kWorkPhases; ++p)
        std::printf(",w_%s", SortMetrics::workPhaseName(static_cast<SortMetrics::WorkPhase>(p)));
    std::printf(",leaf_comparisons,max_depth,max_leaf,status\n");

    for (const auto& t : types) {
        if (t == "int64") runType<int64_t>("int64", sizes, datasets);
        else if (t == "uint32") runType<uint32_t>("uint32", sizes, datasets);
    }

    // A3 summary: least-squares slope of log(total) on log(n).
    std::printf("\n# A3 summary: total ~ a*n^b over the sizes present; A3 asks b <= 1.01\n");
    std::printf("# %-7s %-22s %6s %10s %12s %s\n", "type", "dataset", "points", "exponent",
                "max work/n", "");
    double worstPerN = 0.0;
    for (const auto& [k, pts] : g_points) {
        double maxPerN = 0.0;
        for (const Point& p : pts) maxPerN = std::max(maxPerN, p.total / p.n);
        worstPerN = std::max(worstPerN, maxPerN);
        if (pts.size() < 2) {
            std::printf("# %-7s %-22s %6zu %10s %12.2f\n", k.first.c_str(), k.second.c_str(),
                        pts.size(), "-", maxPerN);
            continue;
        }
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (const Point& p : pts) {
            const double x = std::log(p.n), y = std::log(p.total);
            sx += x; sy += y; sxx += x * x; sxy += x * y;
        }
        const double m = static_cast<double>(pts.size());
        const double b = (m * sxy - sx * sy) / (m * sxx - sx * sx);
        std::printf("# %-7s %-22s %6zu %10.4f %12.2f %s\n", k.first.c_str(), k.second.c_str(),
                    pts.size(), b, maxPerN, b > 1.01 ? "> 1.01" : "");
    }
    std::printf("# largest work/n over every point: %.2f\n", worstPerN);
    if (!g_allCorrect) {
        std::fprintf(stderr, "WorkCounters: a sort disagreed with std::sort or used an unknown phase\n");
        return 1;
    }
    return 0;
}
