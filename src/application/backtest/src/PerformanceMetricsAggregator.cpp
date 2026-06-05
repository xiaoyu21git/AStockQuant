#include "../include/PerformanceMetricsAggregator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace application::backtest {

PerformanceMetricsAggregator::PerformanceMetricsAggregator()
{
    stageStartMicros_[RunStage::Validate] = 0;
    stageStartMicros_[RunStage::GenerateSignal] = 0;
    stageStartMicros_[RunStage::ConstructTargetPosition] = 0;
    stageStartMicros_[RunStage::RiskApprove] = 0;
    stageStartMicros_[RunStage::GenerateOrders] = 0;
    stageStartMicros_[RunStage::ExecuteFill] = 0;
    stageStartMicros_[RunStage::UpdatePositionState] = 0;
    stageStartMicros_[RunStage::AggregateMetrics] = 0;
    stageStartMicros_[RunStage::BuildDiagnostics] = 0;
}

void PerformanceMetricsAggregator::reset()
{
    stageTimings_.clear();
    stageStartMicros_.clear();
    windowStartDate_ = 0;
    windowEndDate_ = 0;
    filledOrderCount_ = 0U;
    generatedOrderCount_ = 0U;
    approvedOrderCount_ = 0U;
}

void PerformanceMetricsAggregator::recordStageStart(RunStage stage)
{
    stageStartMicros_[stage] = nowMicros();
}

void PerformanceMetricsAggregator::recordStageEnd(RunStage stage, const StageResult& result)
{
    auto it = stageStartMicros_.find(stage);
    if (it == stageStartMicros_.end()) {
        return;
    }

    const std::int64_t elapsed = nowMicros() - it->second;

    StageTiming timing;
    timing.stage = stage;
    timing.startMicros = it->second;
    timing.elapsedMicros = elapsed;
    stageTimings_.push_back(timing);
}

void PerformanceMetricsAggregator::updateFromFillResult(const RunContext& context)
{
    generatedOrderCount_ = context.workingSet.generatedOrderCount;
    approvedOrderCount_ = context.workingSet.approvedOrderCount;
    filledOrderCount_ = context.workingSet.filledOrderCount;
}

void PerformanceMetricsAggregator::setWindowDates(int32_t startDate, int32_t endDate)
{
    windowStartDate_ = startDate;
    windowEndDate_ = endDate;
}

AggregatedPerformanceMetrics PerformanceMetricsAggregator::build(
    const domain::backtest::BacktestRequest& request) const
{
    AggregatedPerformanceMetrics metrics;
    metrics.stageTimings = stageTimings_;

    // 时间统计
    if (!stageTimings_.empty()) {
        std::int64_t totalMicros = 0;
        for (const StageTiming& timing : stageTimings_) {
            totalMicros += timing.elapsedMicros;
        }
        metrics.totalElapsedSeconds = static_cast<double>(totalMicros) / 1000000.0;
    }

    // 订单统计
    metrics.totalOrders = generatedOrderCount_;
    metrics.filledOrders = filledOrderCount_;
    metrics.rejectedOrders = (generatedOrderCount_ > filledOrderCount_)
        ? generatedOrderCount_ - filledOrderCount_
        : 0U;

    // 成本（使用请求中的费率估算）
    if (request.costSpec.initialCapital.value > 0.0) {
        const double notional = request.costSpec.initialCapital.value
            * static_cast<double>(filledOrderCount_)
            * 0.1; // 假设每单 10% 本金
        metrics.totalCommissionCost = notional * request.costSpec.commissionRate.value;
        metrics.totalSlippageCost   = notional * request.costSpec.slippageRate.value;
        metrics.totalTaxCost        = notional * request.costSpec.taxRate.value;
        metrics.totalTradingCost = metrics.totalCommissionCost
            + metrics.totalSlippageCost + metrics.totalTaxCost;
    }

    // 收益 / 风险（从域层因子分析报告填充）
    // 这些字段在真实的因子→信号→成交 执行完成后，由域层分析模块填充
    metrics.customMetrics["stage_count"]
        = static_cast<double>(stageTimings_.size());

    // 超额收益
    metrics.excessAnnualizedReturn = metrics.annualizedReturn - benchAnnualizedReturn_;

    // 信息比率 = (年化超额收益) / (超额收益年化波动率)
    if (equityCurve_.size() >= 2 && benchmarkCurve_.size() >= 2) {
        std::vector<double> excessDaily;
        excessDaily.reserve(equityCurve_.size());
        for (std::size_t i = 0; i < equityCurve_.size() && i < benchmarkCurve_.size(); ++i)
            excessDaily.push_back(equityCurve_[i] - benchmarkCurve_[i]);
        double sum = 0.0, sumSq = 0.0;
        for (double v : excessDaily) { sum += v; sumSq += v * v; }
        double mean = sum / static_cast<double>(excessDaily.size());
        double var = (sumSq / static_cast<double>(excessDaily.size())) - (mean * mean);
        double trackingErrorDaily = std::sqrt((std::max)(0.0, var));
        double trackingErrorAnnual = trackingErrorDaily * std::sqrt(252.0);
        metrics.informationRatio = safeDivide(metrics.excessAnnualizedReturn, trackingErrorAnnual);
    }

    // 卡玛比率 = 年化收益 / 最大回撤绝对值
    if (metrics.maxDrawdown > 0.001)
        metrics.calmarRatio = metrics.annualizedReturn / metrics.maxDrawdown;

    // 最大连续亏损天数
    if (!equityCurve_.empty()) {
        int32_t maxStreak = 0, curStreak = 0;
        for (std::size_t i = 1; i < equityCurve_.size(); ++i) {
            if (equityCurve_[i] < equityCurve_[i - 1]) {
                ++curStreak;
                if (curStreak > maxStreak) maxStreak = curStreak;
            } else {
                curStreak = 0;
            }
        }
        metrics.maxConsecutiveLossDays = maxStreak;
    }

    // 回撤恢复天数（最长恢复期）
    if (!equityCurve_.empty()) {
        double peak = equityCurve_[0];
        int32_t recovery = 0, maxRecovery = 0;
        for (double v : equityCurve_) {
            if (v >= peak) { peak = v; recovery = 0; }
            else { ++recovery; if (recovery > maxRecovery) maxRecovery = recovery; }
        }
        metrics.recoveryDays = maxRecovery;
    }

    return metrics;
}

double PerformanceMetricsAggregator::safeDivide(double numerator, double denominator) noexcept
{
    if (std::abs(denominator) < 1e-12) {
        return 0.0;
    }
    return numerator / denominator;
}

double PerformanceMetricsAggregator::annualizeReturn(
    double totalReturn, int32_t windowDays) noexcept
{
    if (windowDays <= 0 || totalReturn <= -1.0) {
        return 0.0;
    }
    const double years = static_cast<double>(windowDays) / 252.0;
    if (years < 1e-9) {
        return 0.0;
    }
    return std::pow(1.0 + totalReturn, 1.0 / years) - 1.0;
}

double PerformanceMetricsAggregator::computeSharpeRatio(
    double annualizedReturn, double annualizedVolatility) noexcept
{
    if (annualizedVolatility < 1e-12) {
        return 0.0;
    }
    return (annualizedReturn - 0.02) / annualizedVolatility; // 假设无风险利率 2%
}

std::int64_t PerformanceMetricsAggregator::nowMicros() noexcept
{
    const auto now = std::chrono::steady_clock::now();
    const auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

} // namespace application::backtest