#pragma once

#include "BacktestContracts.hpp"

#include "../../../domain/backtest/include/BacktestRequest.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace application::backtest {

struct StageTiming {
    RunStage stage;
    std::int64_t startMicros{0};
    std::int64_t elapsedMicros{0};
};

struct AggregatedPerformanceMetrics {
    // 收益 & 风险
    double totalReturn{0.0};
    double annualizedReturn{0.0};
    double volatility{0.0};
    double maxDrawdown{0.0};
    double sharpeRatio{0.0};
    double sortinoRatio{0.0};
    double calmarRatio{0.0};
    double informationRatio{0.0};
    int32_t maxConsecutiveLossDays{0};
    int32_t recoveryDays{0};
    double excessAnnualizedReturn{0.0};

    // 交易效率
    double winRate{0.0};
    double profitFactor{0.0};
    double averageWin{0.0};
    double averageLoss{0.0};
    double turnoverRate{0.0};

    // 因子效率
    double informationCoefficient{0.0};
    double rankIC{0.0};
    double icPositiveRate{0.0};
    double icNegativeRate{0.0};
    double icStdDev{0.0};

    // 成本 & 规模
    double totalCommissionCost{0.0};
    double totalSlippageCost{0.0};
    double totalTaxCost{0.0};
    double totalTradingCost{0.0};

    // 订单 & 持仓
    std::uint32_t totalOrders{0U};
    std::uint32_t filledOrders{0U};
    std::uint32_t rejectedOrders{0U};
    std::uint32_t averagePositionCount{0U};
    std::uint32_t maxPositionCount{0U};

    // 性能 & 资源
    double totalElapsedSeconds{0.0};
    std::vector<StageTiming> stageTimings;
    std::map<std::string, double> customMetrics;
};

class PerformanceMetricsAggregator final {
public:
    PerformanceMetricsAggregator();

    void reset();

    void recordStageStart(RunStage stage);
    void recordStageEnd(RunStage stage, const StageResult& result);
    void updateFromFillResult(const RunContext& context);
    void setWindowDates(int32_t startDate, int32_t endDate);

    [[nodiscard]] AggregatedPerformanceMetrics build(
        const domain::backtest::BacktestRequest& request) const;

    void setBenchmarkReturn(double benchAnnualized) { benchAnnualizedReturn_ = benchAnnualized; }
    void setDailyEquityCurve(const std::vector<double>& curve) { equityCurve_ = curve; }
    void setDailyBenchmarkCurve(const std::vector<double>& curve) { benchmarkCurve_ = curve; }

private:
    [[nodiscard]] static double safeDivide(double numerator, double denominator) noexcept;
    [[nodiscard]] static double annualizeReturn(double totalReturn,
                                                 int32_t windowDays) noexcept;
    [[nodiscard]] static double computeSharpeRatio(double annualizedReturn,
                                                    double annualizedVolatility) noexcept;

    [[nodiscard]] static std::int64_t nowMicros() noexcept;

    std::vector<StageTiming> stageTimings_;
    std::map<RunStage, std::int64_t> stageStartMicros_;
    int32_t windowStartDate_{0};
    int32_t windowEndDate_{0};
    std::uint32_t filledOrderCount_{0U};
    std::uint32_t generatedOrderCount_{0U};
    std::uint32_t approvedOrderCount_{0U};
    double benchAnnualizedReturn_{0.0};
    std::vector<double> equityCurve_;
    std::vector<double> benchmarkCurve_;
};

} // namespace application::backtest