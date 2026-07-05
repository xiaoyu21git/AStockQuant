#pragma once
// ═════════════════════════════════════════════════════════════════════════
// FactorBacktestTypes — 回测纯数据类型 (零外部依赖，纯 C++)
// 替代已删除的 FactorBacktestExecutor.h
// ═════════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <string>
#include <vector>
#include "../../types/DomainDate.h"

namespace factor {

// ── 回测配置 ──
struct BacktestConfig {
    std::string instanceId;
    domain::DomainDate startDate;
    domain::DomainDate endDate;
    int forwardDays   = 30;
    int rebalanceDays = 1;
    int numGroups     = 10;
    double commissionRate = 0.001;
    double slippageRate   = 0.001;
    double riskFreeRate   = 0.02;
};

// ── IC/IR 统计 ──
struct ICIRResult {
    double icMean          = 0.0;
    double icStd           = 0.0;
    double ir              = 0.0;
    double icPositiveRatio = 0.0;
    std::vector<double> icSeries;
};

// ── 分组回测结果 ──
struct GroupBacktestResult {
    std::vector<double> groupReturns;
    std::vector<int>    groupStockCounts;
    std::vector<double> minFactorValues;
    std::vector<double> maxFactorValues;
    double longShortReturn   = 0.0;
    double topGroupReturn    = 0.0;
    double bottomGroupReturn = 0.0;
};

// ── 因子质量指标 ──
struct FactorBacktestMetrics {
    enum class Rating : int { FAIL = 0, PASS = 1, GOOD = 2, EXCELLENT = 3 };

    double rankIcMean           = 0.0;
    double rankIcStd            = 0.0;
    double rankIcir             = 0.0;
    double icWinRate            = 0.0;
    double icPValue             = 1.0;
    double monotonicityScore    = 0.0;
    double longShortSharpe      = 0.0;
    double longShortAnnualReturn = 0.0;
    double longShortMaxDrawdown = 0.0;
    double costAdjustedSharpe   = 0.0;
    int    icHalfLife           = 0;
    double annualTurnover       = 0.0;
    double alpha                = 0.0;
    double icTStat              = 0.0;
    double monthlyWinRate       = 0.0;
    int    numGroups            = 10;
    double topBottomSpreadReturn{0.0};   // G1收益 - GN收益（因子值最大组减最小组收益差）
    bool   spreadSignMatchIc{false};     // spread符号 == IC均值符号（策略方向与因子方向一致）
    std::vector<double> groupAnnualReturns;
    std::vector<double> groupSharpes;
    Rating coreRating{Rating::FAIL};

    void computeCoreRating();
};

// ── 完整回测结果 ──
struct BacktestResult {
    BacktestConfig     config;
    ICIRResult         icirResult;
    GroupBacktestResult groupResult;
    FactorBacktestMetrics factorMetrics;

    // 执行层指标
    double annualReturn       = 0.0;
    double sharpeRatio        = 0.0;
    double maxDrawdown        = 0.0;
    double winRate            = 0.0;
    double profitFactor       = 0.0;
    double turnoverRate       = 0.0;
    double benchmarkAnnualReturn = 0.0;
    double excessAnnualReturn = 0.0;
    double trackingError      = 0.0;
    double informationRatio   = 0.0;
    double beta               = 0.0;
    double alpha              = 0.0;
    double volatility         = 0.0;
    double downsideDeviation  = 0.0;
    double sortinoRatio       = 0.0;
    double calmarRatio        = 0.0;
    double valueAtRisk        = 0.0;
    double conditionalVaR     = 0.0;
    int    riskTriggeredCount = 0;

    std::string status{"SUCCESS"};
    std::string errorMessage;
};

// ── 因子质量评级 ──
struct FactorQuality {
    static FactorBacktestMetrics::Rating evaluate(const FactorBacktestMetrics& metrics);
};

} // namespace factor
