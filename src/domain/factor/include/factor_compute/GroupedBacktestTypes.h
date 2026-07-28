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
    double winsorizeQuantile{0.005}; // 双侧缩尾分位数，0.0=不缩尾，默认 0.5%/99.5%
    std::string adjustPriceType{"pre"}; // "pre"=前复权 "post"=后复权
    bool ascending{true};  // 因子方向: true=值越大越好, false=值越小越好
    std::function<void(double)> onProgress; // 进度回调 (0.0-1.0)
};

/// 单个分组的回测指标
struct GroupBacktestMetrics final {
    int32_t groupIndex{0};
    int32_t stockCount{0};
    double returnRate{0.0};         // 单期平均收益（算术平均）
    double cumulativeReturn{0.0};   // 累计复合收益（逐期复利累积），不同于 returnRate
    double annualizedReturn{0.0};   // 年化收益
    double minFactorValue{0.0};
    double maxFactorValue{0.0};
    int32_t validDays{0};           // 有效回测期数
};

/// 单笔交易记录
struct TradeRecord final {
    std::string symbol;
    std::string date;       // 交易日期
    std::string side;       // "BUY" / "SELL"
    std::string basket;     // "long" / "short"
    double price{0.0};
    double cost{0.0};       // 本次交易的费用率
};

/// 每期调仓追踪数据
struct PeriodTracking final {
    std::string date;          // 调仓日期
    int32_t longHeld{0};       // 多头持仓数
    int32_t shortHeld{0};      // 空头持仓数
    int32_t longBought{0};     // 本期买入
    int32_t longSold{0};       // 本期卖出
    int32_t shortBought{0};
    int32_t shortSold{0};
    double longTurnover{0.0};  // 多头换手率
    double shortTurnover{0.0}; // 空头换手率
    double longRawReturn{0.0}; // 多头原始收益
    double shortRawReturn{0.0};// 空头原始收益
    double strategyNetReturn{0.0}; // 策略净收益（扣费后多空价差）
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

    // ── 追踪数据（嵌入回测结果供 QML / 日志消费）──
    std::vector<TradeRecord> tradeLog;           // 每笔交易的完整记录
    std::vector<PeriodTracking> periodTrackings; // 每期调仓快照
};

} // namespace factor::compute