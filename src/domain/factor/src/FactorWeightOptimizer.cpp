// FactorWeightOptimizer — 多因子权重优化实现
#include "../include/FactorWeightOptimizer.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace factor {

// ═══════════════════════════════════════════════════════════════════
// 工具函数
// ═══════════════════════════════════════════════════════════════════

void FactorWeightOptimizer::computeIcStats(
    const FactorOptimizationInput& input,
    std::unordered_map<std::string, double>& icMeans,
    std::unordered_map<std::string, double>& icirs)
{
    for (const auto& fid : input.factorIds) {
        auto it = input.factorIcSeries.find(fid);
        if (it == input.factorIcSeries.end() || it->second.empty()) {
            icMeans[fid] = 0.0;
            icirs[fid] = 0.0;
            continue;
        }
        const auto& series = it->second;
        double sum = 0.0;
        for (double v : series) sum += v;
        double mean = sum / static_cast<double>(series.size());
        icMeans[fid] = mean;

        if (series.size() < 2) {
            icirs[fid] = 0.0;
            continue;
        }
        double variance = 0.0;
        for (double v : series) {
            double d = v - mean;
            variance += d * d;
        }
        variance /= static_cast<double>(series.size());
        double stddev = std::sqrt(variance);
        icirs[fid] = stddev > 0.0 ? mean / stddev : 0.0;
    }
}

void FactorWeightOptimizer::applyWeightCap(
    std::unordered_map<std::string, double>& weights,
    double maxWeightCap)
{
    if (maxWeightCap <= 0.0 || maxWeightCap >= 1.0) return;
    for (auto& [fid, w] : weights) {
        w = std::max(0.0, std::min(w, maxWeightCap));
    }
}

void FactorWeightOptimizer::normalizeWeights(
    std::unordered_map<std::string, double>& weights,
    const std::vector<std::string>& factorIds)
{
    double sum = 0.0;
    for (const auto& fid : factorIds) {
        sum += weights[fid];
    }
    if (sum > 0.0) {
        for (const auto& fid : factorIds) {
            weights[fid] /= sum;
        }
    } else {
        // 全零时 fallback 等权
        double eq = 1.0 / static_cast<double>(factorIds.size());
        for (const auto& fid : factorIds) {
            weights[fid] = eq;
        }
    }
}

double FactorWeightOptimizer::pearsonCorrelation(
    const std::vector<double>& x,
    const std::vector<double>& y)
{
    if (x.size() != y.size() || x.size() < 2) return 0.0;
    std::size_t n = x.size();

    double sumX = 0.0, sumY = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sumX += x[i];
        sumY += y[i];
    }
    double meanX = sumX / static_cast<double>(n);
    double meanY = sumY / static_cast<double>(n);

    double cov = 0.0, varX = 0.0, varY = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double dx = x[i] - meanX;
        double dy = y[i] - meanY;
        cov += dx * dy;
        varX += dx * dx;
        varY += dy * dy;
    }

    if (varX <= 0.0 || varY <= 0.0) return 0.0;
    return cov / std::sqrt(varX * varY);
}

// ═══════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════

FactorOptimizationResult FactorWeightOptimizer::optimize(
    const FactorOptimizationInput& input,
    WeightOptimizationMethod method) const
{
    // 训练数据不足检查 (先于单因子退路, 确保数据质量)
    int minDataPoints = input.minTrainingDays;
    for (const auto& fid : input.factorIds) {
        auto it = input.factorIcSeries.find(fid);
        if (it == input.factorIcSeries.end()) {
            FactorOptimizationResult result;
            result.success = false;
            result.errorMessage = "因子 " + fid + " 无 IC 数据";
            return result;
        }
        minDataPoints = std::min(minDataPoints, static_cast<int>(it->second.size()));
    }
    if (minDataPoints < input.minTrainingDays) {
        FactorOptimizationResult result;
        result.success = false;
        std::ostringstream oss;
        oss << "训练数据不足: 最少需要 " << input.minTrainingDays
            << " 天, 实际最少 " << minDataPoints << " 天";
        result.errorMessage = oss.str();
        return result;
    }

    // 单因子退路: 数据足够且只有一个因子 → 权重=1.0
    if (input.factorIds.size() < 2) {
        FactorOptimizationResult result;
        result.success = true;
        result.method = "single-factor";
        result.optimizedWeights[input.factorIds.front()] = 1.0;
        return result;
    }

    switch (method) {
    case WeightOptimizationMethod::IcWeighted:
        return optimizeIcWeighted(input);
    case WeightOptimizationMethod::IcirWeighted:
        return optimizeIcirWeighted(input);
    case WeightOptimizationMethod::EqualWeight:
    default:
        return optimizeEqualWeight(input);
    }
}

// ═══════════════════════════════════════════════════════════════════
// 等权 (基线)
// ═══════════════════════════════════════════════════════════════════

FactorOptimizationResult FactorWeightOptimizer::optimizeEqualWeight(
    const FactorOptimizationInput& input) const
{
    FactorOptimizationResult result;
    result.success = true;
    result.method = "EqualWeight";

    double eq = 1.0 / static_cast<double>(input.factorIds.size());
    for (const auto& fid : input.factorIds) {
        result.optimizedWeights[fid] = eq;
    }
    computeIcStats(input, result.icMeans, result.icirs);
    return result;
}

// ═══════════════════════════════════════════════════════════════════
// |IC| 加权
// ═══════════════════════════════════════════════════════════════════

FactorOptimizationResult FactorWeightOptimizer::optimizeIcWeighted(
    const FactorOptimizationInput& input) const
{
    FactorOptimizationResult result;
    result.method = "IC_Weighted";

    computeIcStats(input, result.icMeans, result.icirs);

    // w_i = |mean(IC_i)| / sum(|mean(IC_j)|)
    double sumAbsIc = 0.0;
    for (const auto& fid : input.factorIds) {
        double absIc = std::abs(result.icMeans[fid]);
        result.optimizedWeights[fid] = absIc;
        sumAbsIc += absIc;
    }

    if (sumAbsIc > 0.0) {
        for (const auto& fid : input.factorIds) {
            result.optimizedWeights[fid] /= sumAbsIc;
        }
        applyWeightCap(result.optimizedWeights, input.maxWeightCap);
        normalizeWeights(result.optimizedWeights, input.factorIds);
    } else {
        // 所有 IC 为 0 → fallback 等权
        normalizeWeights(result.optimizedWeights, input.factorIds);
    }

    result.success = true;
    return result;
}

// ═══════════════════════════════════════════════════════════════════
// ICIR 加权
// ═══════════════════════════════════════════════════════════════════

FactorOptimizationResult FactorWeightOptimizer::optimizeIcirWeighted(
    const FactorOptimizationInput& input) const
{
    FactorOptimizationResult result;
    result.method = "ICIR_Weighted";

    computeIcStats(input, result.icMeans, result.icirs);

    // w_i = max(0, ICIR_i) / sum(max(0, ICIR_j))
    double sumIcir = 0.0;
    for (const auto& fid : input.factorIds) {
        double icir = result.icirs[fid];
        if (icir < 0.0) {
            result.optimizedWeights[fid] = 0.0;
            result.negativeIcirFactors.push_back(fid);
        } else {
            result.optimizedWeights[fid] = icir;
            sumIcir += icir;
        }
    }

    if (sumIcir > 0.0) {
        for (const auto& fid : input.factorIds) {
            result.optimizedWeights[fid] /= sumIcir;
        }
        applyWeightCap(result.optimizedWeights, input.maxWeightCap);
        normalizeWeights(result.optimizedWeights, input.factorIds);
    } else {
        // 所有 ICIR <= 0 → fallback 等权
        normalizeWeights(result.optimizedWeights, input.factorIds);
    }

    result.success = true;
    return result;
}

// ═══════════════════════════════════════════════════════════════════
// 相关性矩阵 & 冗余检测
// ═══════════════════════════════════════════════════════════════════

std::vector<std::vector<double>> FactorWeightOptimizer::computeCorrelationMatrix(
    const std::unordered_map<std::string, std::vector<double>>& factorIcSeries,
    const std::vector<std::string>& factorIds) const
{
    std::size_t n = factorIds.size();
    std::vector<std::vector<double>> matrix(n, std::vector<double>(n, 0.0));

    for (std::size_t i = 0; i < n; ++i) {
        matrix[i][i] = 1.0;  // 自相关
        for (std::size_t j = i + 1; j < n; ++j) {
            auto itI = factorIcSeries.find(factorIds[i]);
            auto itJ = factorIcSeries.find(factorIds[j]);
            if (itI != factorIcSeries.end() && itJ != factorIcSeries.end()) {
                double corr = pearsonCorrelation(itI->second, itJ->second);
                matrix[i][j] = corr;
                matrix[j][i] = corr;
            }
        }
    }
    return matrix;
}

std::vector<std::string> FactorWeightOptimizer::detectRedundant(
    const FactorOptimizationInput& input,
    double threshold) const
{
    std::vector<std::string> redundant;

    if (input.factorIds.size() < 2) return redundant;

    auto corrMatrix = computeCorrelationMatrix(
        input.factorIcSeries, input.factorIds);
    std::size_t n = input.factorIds.size();

    // 计算每个因子的平均相关系数(与其他因子)
    std::unordered_map<std::string, double> avgCorr;
    for (std::size_t i = 0; i < n; ++i) {
        double sum = 0.0;
        for (std::size_t j = 0; j < n; ++j) {
            if (i != j) sum += std::abs(corrMatrix[i][j]);
        }
        avgCorr[input.factorIds[i]] = sum / static_cast<double>(n - 1);
    }

    // 对每对高相关因子, 保留 IC 均值更高的
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (corrMatrix[i][j] > threshold) {
                const auto& fi = input.factorIds[i];
                const auto& fj = input.factorIds[j];
                double icI = 0.0, icJ = 0.0;
                auto itI = input.factorIcSeries.find(fi);
                auto itJ = input.factorIcSeries.find(fj);
                if (itI != input.factorIcSeries.end() && !itI->second.empty()) {
                    icI = std::abs(std::accumulate(itI->second.begin(), itI->second.end(), 0.0)
                                   / static_cast<double>(itI->second.size()));
                }
                if (itJ != input.factorIcSeries.end() && !itJ->second.empty()) {
                    icJ = std::abs(std::accumulate(itJ->second.begin(), itJ->second.end(), 0.0)
                                   / static_cast<double>(itJ->second.size()));
                }
                // 保留 IC 更高的因子, 标记较低的为冗余
                if (icI >= icJ) {
                    if (std::find(redundant.begin(), redundant.end(), fj) == redundant.end())
                        redundant.push_back(fj);
                } else {
                    if (std::find(redundant.begin(), redundant.end(), fi) == redundant.end())
                        redundant.push_back(fi);
                }
            }
        }
    }
    return redundant;
}

} // namespace factor
