#pragma once

#include <cstdint>
#include <vector>

namespace factor::compute {

/// 模拟成交参数
struct SimulatedTradingParams final {
    int32_t numGroups{5};
    int32_t forwardDays{1};          // 持仓天数（T+N 日收益）
    int32_t rebalanceDays{1};        // 调仓周期（每隔 N 天重新分组）
    double commissionRate{0.001};    // 手续费率（每笔交易）
    double slippageRate{0.001};      // 滑点率（每笔交易）
    double riskFreeRate{0.02};       // 无风险利率（年化）
    double maxFwdRetAbsLimit{0.5};   // 过滤极端日收益
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
    double annualizedReturn{0.0};
    double maxDrawdown{0.0};
    double annualStdDev{0.0};
    double sharpeRatio{0.0};
    double totalReturn{0.0};
    double finalEquity{1.0};
    int32_t validSampleCount{0};
};

} // namespace factor::compute