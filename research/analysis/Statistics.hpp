#pragma once

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace stratum::stats {

// ============================================================
// Basic repetition statistics (min, max, mean, median, stddev)
// ============================================================
struct RepetitionStats {
    std::size_t repetitions = 0;
    double minMs = 0.0;
    double maxMs = 0.0;
    double meanMs = 0.0;
    double medianMs = 0.0;
    double stddevMs = 0.0;
};

inline RepetitionStats computeRepetitionStats(std::vector<double> samplesMs) {
    RepetitionStats result;
    result.repetitions = samplesMs.size();
    if (samplesMs.empty()) return result;

    std::sort(samplesMs.begin(), samplesMs.end());
    result.minMs = samplesMs.front();
    result.maxMs = samplesMs.back();

    double sum = 0.0;
    for (double v : samplesMs) sum += v;
    result.meanMs = sum / static_cast<double>(samplesMs.size());

    const std::size_t mid = samplesMs.size() / 2;
    result.medianMs = (samplesMs.size() % 2 == 0)
                           ? (samplesMs[mid - 1] + samplesMs[mid]) / 2.0
                           : samplesMs[mid];

    if (samplesMs.size() > 1) {
        double variance = 0.0;
        for (double v : samplesMs) variance += (v - result.meanMs) * (v - result.meanMs);
        variance /= static_cast<double>(samplesMs.size() - 1); // sample stddev
        result.stddevMs = std::sqrt(variance);
    }
    return result;
}

// ============================================================
// Complexity model fitting
// ============================================================
// Every candidate model has the form T(n) = a * f(n) for a single free
// parameter 'a' - this is what "T(n) = a*n", "T(n) = a*n*log2(n)", etc.
// all have in common. For a fixed f(n), the least-squares optimal 'a' has
// a closed form (no iterative solver needed):
//
//   a = sum(T_i * f(n_i)) / sum(f(n_i)^2)
//
// which is the standard single-variable linear regression through the
// origin. From that, SSE (sum of squared errors) and R^2 follow directly.
// ============================================================
struct DataPoint {
    double n;
    double timeMs;
};

struct ModelFit {
    std::string name;
    double a = 0.0;
    double sse = 0.0;   // sum of squared errors
    double r2 = 0.0;    // coefficient of determination
    double seA = 0.0;   // standard error of the coefficient 'a'
    double ciLow = 0.0; // 95% confidence interval for 'a', lower bound
    double ciHigh = 0.0;
};

// Two-tailed 95% Student's t critical values for small degrees of freedom
// (textbook table); falls back to the normal approximation (1.96) once
// the sample is large enough that t and z are practically the same.
inline double criticalT95(std::size_t degreesOfFreedom) {
    static const double table[] = {
        12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
        2.201,  2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
        2.080,  2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042,
    };
    if (degreesOfFreedom == 0) return table[0];
    if (degreesOfFreedom <= 30) return table[degreesOfFreedom - 1];
    return 1.96;
}

// f(n) implementations for each candidate model.
inline double modelF_Linear(double n) { return n; }
inline double modelF_NLogN(double n) { return n * std::log2(n); }
inline double modelF_NPow105(double n) { return std::pow(n, 1.05); }
inline double modelF_NPow110(double n) { return std::pow(n, 1.10); }
inline double modelF_NPow120(double n) { return std::pow(n, 1.20); }
inline double modelF_SqrtN(double n) { return std::sqrt(n); }

// Fits T(n) = a * f(n) to 'points' by least squares and returns the fit
// quality. 'name' is only used for reporting.
template <typename F>
inline ModelFit fitModel(const std::string& name, const std::vector<DataPoint>& points, F f) {
    double numerator = 0.0;   // sum(T_i * f(n_i))
    double denominator = 0.0; // sum(f(n_i)^2)
    for (const DataPoint& p : points) {
        const double fn = f(p.n);
        numerator += p.timeMs * fn;
        denominator += fn * fn;
    }

    ModelFit fit;
    fit.name = name;
    fit.a = (denominator > 0.0) ? (numerator / denominator) : 0.0;

    double meanT = 0.0;
    for (const DataPoint& p : points) meanT += p.timeMs;
    meanT /= static_cast<double>(points.size());

    double sse = 0.0; // sum of squared errors (residuals)
    double sst = 0.0; // total sum of squares (variance around the mean)
    for (const DataPoint& p : points) {
        const double predicted = fit.a * f(p.n);
        const double residual = p.timeMs - predicted;
        sse += residual * residual;
        const double deviation = p.timeMs - meanT;
        sst += deviation * deviation;
    }

    fit.sse = sse;
    fit.r2 = (sst > 0.0) ? (1.0 - sse / sst) : 1.0;

    // Standard error of 'a' for a single-parameter regression through the
    // origin: SE(a) = sqrt( (SSE / (N-1)) / sum(f(n_i)^2) ). With only one
    // free parameter, degrees of freedom = N - 1.
    const std::size_t nPoints = points.size();
    if (nPoints > 1 && denominator > 0.0) {
        const double residualVariance = sse / static_cast<double>(nPoints - 1);
        fit.seA = std::sqrt(residualVariance / denominator);
        const double tCrit = criticalT95(nPoints - 1);
        fit.ciLow = fit.a - tCrit * fit.seA;
        fit.ciHigh = fit.a + tCrit * fit.seA;
    } else {
        fit.seA = 0.0;
        fit.ciLow = fit.a;
        fit.ciHigh = fit.a;
    }
    return fit;
}

// Fits every candidate model from the research brief against the same
// data points and returns them all, in a fixed, documented order.
inline std::vector<ModelFit> fitAllModels(const std::vector<DataPoint>& points) {
    std::vector<ModelFit> fits;
    fits.push_back(fitModel("T(n) = a*n", points, modelF_Linear));
    fits.push_back(fitModel("T(n) = a*n*log2(n)", points, modelF_NLogN));
    fits.push_back(fitModel("T(n) = a*n^1.05", points, modelF_NPow105));
    fits.push_back(fitModel("T(n) = a*n^1.10", points, modelF_NPow110));
    fits.push_back(fitModel("T(n) = a*n^1.20", points, modelF_NPow120));
    fits.push_back(fitModel("T(n) = a*sqrt(n)", points, modelF_SqrtN));
    return fits;
}

// Prints a comparison table and identifies the best fit by R^2 (the
// standard, objective criterion for "which model explains the data
// better" among models fit by least squares) - no subjective judgment
// involved.
inline void printModelComparison(const std::vector<ModelFit>& fits, std::ostream& os) {
    os << std::left << std::setw(22) << "Model" << std::right << std::setw(14) << "a" << std::setw(24)
       << "95% CI for a" << std::setw(16) << "SSE" << std::setw(12) << "R^2" << "\n";
    os << std::string(88, '-') << "\n";

    std::size_t bestIdx = 0;
    for (std::size_t i = 1; i < fits.size(); ++i) {
        if (fits[i].r2 > fits[bestIdx].r2) bestIdx = i;
    }

    for (std::size_t i = 0; i < fits.size(); ++i) {
        const ModelFit& f = fits[i];
        std::ostringstream ci;
        ci << "[" << std::scientific << std::setprecision(3) << f.ciLow << ", " << f.ciHigh << "]";
        os << std::left << std::setw(22) << f.name << std::right << std::setw(14) << std::scientific
           << std::setprecision(3) << f.a << std::setw(24) << ci.str() << std::setw(16) << std::scientific
           << std::setprecision(3) << f.sse << std::setw(12) << std::fixed << std::setprecision(6) << f.r2
           << (i == bestIdx ? "  <- best" : "") << "\n";
    }
}

// ============================================================
// Distribution summary (used for the bin-size histogram, section 3)
// ============================================================
struct DistributionSummary {
    std::size_t count = 0;
    double median = 0.0;
    std::size_t mode = 0;
    std::size_t modeFrequency = 0;
    double p90 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
};

// Nearest-rank percentile (simple, deterministic, no interpolation
// assumptions beyond "the value at this rank in sorted order").
inline double percentileNearestRank(const std::vector<std::size_t>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    std::size_t rank = static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(sorted.size())));
    if (rank == 0) rank = 1;
    if (rank > sorted.size()) rank = sorted.size();
    return static_cast<double>(sorted[rank - 1]);
}

inline DistributionSummary summarizeDistribution(std::vector<std::size_t> values) {
    DistributionSummary summary;
    summary.count = values.size();
    if (values.empty()) return summary;

    std::sort(values.begin(), values.end());

    const std::size_t mid = values.size() / 2;
    summary.median = (values.size() % 2 == 0) ? (static_cast<double>(values[mid - 1] + values[mid]) / 2.0)
                                               : static_cast<double>(values[mid]);

    // Mode via a sorted-array scan (values are already sorted, so equal
    // values are contiguous - O(n), no hash map needed).
    std::size_t bestValue = values.front();
    std::size_t bestRun = 1;
    std::size_t currentValue = values.front();
    std::size_t currentRun = 1;
    for (std::size_t i = 1; i < values.size(); ++i) {
        if (values[i] == currentValue) {
            ++currentRun;
        } else {
            currentValue = values[i];
            currentRun = 1;
        }
        if (currentRun > bestRun) {
            bestRun = currentRun;
            bestValue = currentValue;
        }
    }
    summary.mode = bestValue;
    summary.modeFrequency = bestRun;

    summary.p90 = percentileNearestRank(values, 90.0);
    summary.p95 = percentileNearestRank(values, 95.0);
    summary.p99 = percentileNearestRank(values, 99.0);
    return summary;
}

// ============================================================
// Pearson correlation (v6, Investigacion 8)
// ============================================================
inline double pearsonCorrelation(const std::vector<double>& x, const std::vector<double>& y) {
    if (x.size() != y.size() || x.size() < 2) return 0.0;
    const std::size_t n = x.size();

    double meanX = 0.0, meanY = 0.0;
    for (std::size_t i = 0; i < n; ++i) { meanX += x[i]; meanY += y[i]; }
    meanX /= static_cast<double>(n);
    meanY /= static_cast<double>(n);

    double cov = 0.0, varX = 0.0, varY = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double dx = x[i] - meanX;
        const double dy = y[i] - meanY;
        cov += dx * dy;
        varX += dx * dx;
        varY += dy * dy;
    }
    if (varX <= 0.0 || varY <= 0.0) return 0.0;
    return cov / std::sqrt(varX * varY);
}

} // namespace stratum::stats
