#include "MatrixPnLEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace domain::backtest {

namespace {

using signal_value_t = factor::compute::signal_value_t;

/// @brief 二维矩阵索引 [row][col] 展平为 [row * colCount + col]
inline size_t idx(int32_t row, int32_t col, int32_t colCount) noexcept
{
    return static_cast<size_t>(row) * static_cast<size_t>(colCount) + static_cast<size_t>(col);
}

/// @brief 每日持仓权重：信号按行归一化（每只股票权重 = signal / sum(|signal|)）
/// 输出 T x N 矩阵，float32
std::vector<signal_value_t> computePositionMatrix(
    const std::vector<signal_value_t>& signals,
    int32_t T, int32_t N)
{
    std::vector<signal_value_t> positions(signals.size(), signal_value_t{0});

    for (int32_t t = 0; t < T; ++t) {
        // 计算该日信号绝对值之和
        double absSum = 0.0;
        for (int32_t i = 0; i < N; ++i) {
            absSum += static_cast<double>(std::abs(signals[idx(t, i, N)]));
        }
        if (absSum <= std::numeric_limits<double>::epsilon()) {
            continue;  // 信号全为零，持仓为零
        }
        const double invSum = 1.0 / absSum;
        for (int32_t i = 0; i < N; ++i) {
            positions[idx(t, i, N)] = static_cast<signal_value_t>(
                static_cast<double>(signals[idx(t, i, N)]) * invSum);
        }
    }
    return positions;
}

/// @brief 日收益率矩阵: return[t][i] = close[t+1][i] / close[t][i] - 1
/// 输出 (T-1) x N 矩阵
std::vector<signal_value_t> computeDailyReturnMatrix(
    const std::vector<signal_value_t>& closePrices,
    int32_t T, int32_t N)
{
    if (T < 2) return {};
    const int32_t returnDays = T - 1;

    std::vector<signal_value_t> returns(
        static_cast<size_t>(returnDays) * static_cast<size_t>(N), signal_value_t{0});

    for (int32_t t = 0; t < returnDays; ++t) {
        for (int32_t i = 0; i < N; ++i) {
            const double prev = static_cast<double>(closePrices[idx(t, i, N)]);
            const double curr = static_cast<double>(closePrices[idx(t + 1, i, N)]);
            if (prev > std::numeric_limits<double>::epsilon()) {
                returns[idx(t, i, N)] = static_cast<signal_value_t>(curr / prev - 1.0);
            }
            // prev == 0 时保持 0
        }
    }
    return returns;
}

/// @brief 每日 PnL 向量: pnl[t] = sum(position[t][i] * return[t][i])
/// position 为 T x N，returns 为 (T-1) x N，不匹配时截断
std::vector<signal_value_t> computeDailyPnL(
    const std::vector<signal_value_t>& positions,
    const std::vector<signal_value_t>& returns,
    int32_t T, int32_t N)
{
    if (returns.empty()) return {};
    const int32_t pnlDays = static_cast<int32_t>(
        returns.size() / static_cast<size_t>(N));
    std::vector<signal_value_t> pnl(static_cast<size_t>(pnlDays), signal_value_t{0});

    for (int32_t t = 0; t < pnlDays; ++t) {
        double sum = 0.0;
        for (int32_t i = 0; i < N; ++i) {
            sum += static_cast<double>(positions[idx(t, i, N)])
                 * static_cast<double>(returns[idx(t, i, N)]);
        }
        pnl[static_cast<size_t>(t)] = static_cast<signal_value_t>(sum);
    }
    return pnl;
}

/// @brief 累计损益
std::vector<signal_value_t> computeCumulativePnL(
    const std::vector<signal_value_t>& dailyPnL,
    double initialCash)
{
    const size_t L = dailyPnL.size();
    std::vector<signal_value_t> cum(L, signal_value_t{0});
    double running = initialCash;
    for (size_t t = 0; t < L; ++t) {
        running *= (1.0 + static_cast<double>(dailyPnL[t]));
        cum[t] = static_cast<signal_value_t>(running);
    }
    return cum;
}

/// @brief 每日换手率: turnover[t] = sum(|position[t+1] - position[t]|) / 2
std::vector<signal_value_t> computeTurnover(
    const std::vector<signal_value_t>& positions,
    int32_t T, int32_t N)
{
    if (T < 2) return std::vector<signal_value_t>(static_cast<size_t>(T), signal_value_t{0});
    const size_t turnoverDays = static_cast<size_t>(T - 1);
    std::vector<signal_value_t> turnover(turnoverDays, signal_value_t{0});

    for (size_t t = 0; t < turnoverDays; ++t) {
        double sum = 0.0;
        for (int32_t i = 0; i < N; ++i) {
            sum += std::abs(static_cast<double>(
                positions[idx(static_cast<int32_t>(t + 1), i, N)]
                - positions[idx(static_cast<int32_t>(t), i, N)]));
        }
        turnover[t] = static_cast<signal_value_t>(sum * 0.5);
    }
    return turnover;
}

/// @brief 汇总指标计算
struct AggregatedMetrics final {
    double sharpeRatio{0.0};
    double maxDrawdown{0.0};
    double annualizedReturn{0.0};
    double totalReturn{0.0};
};

AggregatedMetrics computeMetrics(
    const std::vector<signal_value_t>& dailyPnL,
    const std::vector<signal_value_t>& cumulativePnL,
    double initialCash,
    double riskFreeRate)
{
    AggregatedMetrics metrics;

    if (dailyPnL.empty() || cumulativePnL.empty()) return metrics;

    // 总收益率
    metrics.totalReturn = static_cast<double>(cumulativePnL.back()) / initialCash - 1.0;

    // 年化收益率 (假设 252 个交易日)
    const double totalDays = static_cast<double>(dailyPnL.size());
    metrics.annualizedReturn = std::pow(1.0 + metrics.totalReturn, 252.0 / totalDays) - 1.0;

    // 日收益率均值与标准差 (用于夏普)
    double mean = 0.0;
    double m2 = 0.0;
    double count = 0.0;
    for (const auto& v : dailyPnL) {
        ++count;
        const double value = static_cast<double>(v);
        const double delta = value - mean;
        mean += delta / count;
        m2 += delta * (value - mean);
    }

    if (count > 1.0) {
        const double dailyVariance = m2 / (count - 1.0);
        if (dailyVariance > 0.0) {
            const double dailyStd = std::sqrt(dailyVariance);
            const double dailyRiskFree = riskFreeRate / 252.0;
            metrics.sharpeRatio = (mean - dailyRiskFree) / dailyStd * std::sqrt(252.0);
        }
    }

    // 最大回撤
    double peak = -std::numeric_limits<double>::infinity();
    for (const auto& v : cumulativePnL) {
        const double value = static_cast<double>(v);
        if (value > peak) peak = value;
        const double drawdown = (peak > 0.0) ? (peak - value) / peak : 0.0;
        if (drawdown > metrics.maxDrawdown) metrics.maxDrawdown = drawdown;
    }

    return metrics;
}

} // anonymous namespace

MatrixPnLEngine::PnLResult MatrixPnLEngine::computePnL(const PnLSpec& spec)
{
    if (!spec.isValid()) {
        return PnLResult{};
    }

    const int32_t T = spec.timeCount;
    const int32_t N = spec.instrumentCount;

    // === 步骤 1: 计算持仓权重矩阵 (T x N) ===
    std::vector<signal_value_t> positions = computePositionMatrix(
        spec.signalMatrix, T, N);

    // === 步骤 2: 计算日收益率矩阵 ((T-1) x N) ===
    std::vector<signal_value_t> returns = computeDailyReturnMatrix(
        spec.closePrices, T, N);

    // === 步骤 3: 一次性计算每日损益 (T-1 维) ===
    std::vector<signal_value_t> dailyPnL = computeDailyPnL(
        positions, returns, T, N);

    // === 步骤 4: 滑点调整 ===
    std::vector<signal_value_t> turnover = computeTurnover(positions, T, N);
    if (spec.slippageRate > 0.0 && !turnover.empty() && !dailyPnL.empty()) {
        const size_t adjustDays = (std::min)(dailyPnL.size(), turnover.size());
        for (size_t t = 0; t < adjustDays; ++t) {
            dailyPnL[t] -= static_cast<signal_value_t>(
                static_cast<double>(turnover[t]) * spec.slippageRate);
        }
    }

    // === 步骤 5: 手续费调整 ===
    if (spec.commissionRate > 0.0 && !turnover.empty() && !dailyPnL.empty()) {
        const size_t adjustDays = (std::min)(dailyPnL.size(), turnover.size());
        for (size_t t = 0; t < adjustDays; ++t) {
            dailyPnL[t] -= static_cast<signal_value_t>(
                static_cast<double>(turnover[t]) * spec.commissionRate);
        }
    }

    // === 步骤 6: 累计损益 ===
    std::vector<signal_value_t> cumulativePnL = computeCumulativePnL(
        dailyPnL, spec.initialCash);

    // === 步骤 7: 汇总指标 ===
    constexpr double kDefaultRiskFreeRate = 0.02;
    AggregatedMetrics metrics = computeMetrics(
        dailyPnL, cumulativePnL, spec.initialCash, kDefaultRiskFreeRate);

    // === 组装结果 ===
    PnLResult result;
    result.dailyPnL = std::move(dailyPnL);
    result.cumulativePnL = std::move(cumulativePnL);
    result.positions = std::move(positions);
    result.turnover = std::move(turnover);
    result.sharpeRatio = metrics.sharpeRatio;
    result.maxDrawdown = metrics.maxDrawdown;
    result.annualizedReturn = metrics.annualizedReturn;
    result.totalReturn = metrics.totalReturn;

    return result;
}

} // namespace domain::backtest