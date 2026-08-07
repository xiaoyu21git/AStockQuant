#pragma once
// 回测变量提供者 — 日线OHLCV+持仓+全市场统计 → 97 个规则变量

#include "RuleTypes.h"
#include "StrategySnapshotTypes.h"
#include "../../factor/include/factor_compute/IMarketDataView.h"
#include "../../trading/TradingTypes.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace domain::strategy::rules {

/// @brief 单标的规则上下文(每日每候选/每持仓一份)
struct RuleCandidateContext {
    std::string symbol;            // fullSymbol (如 "300097.SZ")
    std::string code;              // 6位无后缀码
    int colIndex{-1};              // 行情视图列号
    bool isHolding{false};         // 当前是否有持仓
    double holdDays{0.0};          // 持仓天数
    double entryPrice{0.0};        // 持仓成本
    double pnlPercent{0.0};        // 持仓浮盈比例
};

/// @brief 单日全市场统计(每日计算一次, 所有候选复用)
struct RuleMarketSnapshot {
    // market index 类
    double indexClose{0.0};
    double indexMa60{0.0};
    double indexMa120{0.0};
    double indexMa200{0.0};
    double indexDrawdownFromRecentHigh{0.0};

    // 市场宽度
    double breadthAboveMa60Ratio{0.0};
    double breadthAboveMa20Ratio{0.0};
    double breadthAboveMa200Ratio{0.0};
    double advanceDeclineRatio{0.0};

    // 趋势强度
    double trendPullbackReboundRate{0.0};
    double trendStrengthScore{0.0};
    double volatilityShockScore{0.0};

    // 定性状态(由连续指标编码)
    double regimeState{0.0};           // ruleStringValueCode 编码
    double themeBreadth{0.0};

    // 涨停/打板聚合 (每日全市场统计)
    double limitUpRatio{0.0};          // 涨停标的占比
    double oneWordBoardRatio{0.0};     // 一字板占比
    double resealRate{0.0};            // 炸板回封率 (回封数/炸板数)
    double boardBreakRate{0.0};        // 炸板率 (炸板数/封板数)

    // 概念/题材聚合 (每日从 concept_daily_stats 加载)
    double conceptAvgReturn{0.0};      // 全市场概念等权平均涨幅
    std::string topConceptCode;        // 当日最强概念代码
    double topConceptReturn{0.0};      // 最强概念涨幅
    std::string topConceptLeader;      // 最强概念龙头 symbol
    // (candidate 级变量 leader_rank_in_theme 等需跨表 JOIN, 在 resolve 里实时查询)
};

/// @brief 回测实现: 日线+持仓为数据源的 IRuleVariableProvider
class BacktestRuleVariableProvider : public IRuleVariableProvider {
public:
    BacktestRuleVariableProvider();

    /// @brief 每日开始前重置视图引用+日期
    void setDay(const factor::compute::IMarketDataView* view,
                std::int32_t date,
                const std::unordered_map<std::string, domain::trading::Position>* positions);

    /// @brief 设置当前评估的标的上下文(候选或持仓, 每次求值前调用)
    void setCandidate(const RuleCandidateContext& ctx);

    [[nodiscard]] std::optional<double> resolve(const std::string& varPath) const override;

    /// @brief 启用/禁用 TA-Lib 蜡烛形态计算 (默认关闭)
    void setCandlePatternsEnabled(bool enabled);
    /// @brief 启用/禁用概念板块数据查询 (默认关闭, 无规则引用 concept.* 时跳过)
    void setConceptQueriesEnabled(bool enabled);

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};

/// @brief 回测: 从全市场行情视图计算市场快照(每日一次)
[[nodiscard]] RuleMarketSnapshot computeMarketSnapshot(
    const factor::compute::IMarketDataView* view, int lastRow, int lookback,
    bool conceptQueriesEnabled = false);

} // namespace domain::strategy::rules
