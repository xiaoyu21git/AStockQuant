#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace factor::compute {

/// 模拟成交参数
struct SimulatedTradingParams final {
    int32_t numGroups{5};
    int32_t forwardDays{1};
    int32_t rebalanceDays{1};
    double commissionRate{0.001};
    double slippageRate{0.001};
    double riskFreeRate{0.02};
    double initialCapital{1000000.0};
    double maxFwdRetAbsLimit{0.5};
    std::string adjustPriceType{"pre"}; // "pre"=前复权 "post"=后复权
    std::function<void(double)> onProgress; // 进度回调 (0.0-1.0)
};

/// 单个分组的回测指标
struct GroupBacktestMetrics final {
    int32_t groupIndex{0};
    int32_t stockCount{0};
    double returnRate{0.0};         // 日均收益
    double annualizedReturn{0.0};   // 年化收益
    double minFactorValue{0.0};
    double maxFactorValue{0.0};
};

/// 模拟成交的完整回测结果
struct SimulatedTradingResult final {
    std::vector<GroupBacktestMetrics> groups;
    std::vector<double> strategyDailyReturns;       // 成本调整后的多空日收益（主序列）
    std::vector<double> rawLongShortReturns;        // 扣费前的原始多空日收益
    std::vector<double> costAdjustedLongShortReturns; // 扣费后的多空日收益 (= strategyDailyReturns)
    std::vector<double> riskAdjustedLongShortReturns;  // 风险调整后多空日收益
    std::vector<std::vector<double>> groupDailyReturns; // 每组每日收益 [groupIndex][dayIndex]
    double annualizedReturn{0.0};
    double maxDrawdown{0.0};
    double annualStdDev{0.0};
    double sharpeRatio{0.0};
    double totalReturn{0.0};
    double turnoverRate{0.0};
    std::vector<double> periodTurnovers; // 每期换手率序列
    double finalEquity{1.0};
    int32_t validSampleCount{0};
};

} // namespace factor::compute