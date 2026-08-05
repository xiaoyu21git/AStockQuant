#pragma once
// RulePipeline — 规则管线编排器
// 封装 RuleGate 三闸门 (市场/信号/出场) 的上下文构建+迭代样板代码,
// 消除 step() / evaluateEndOfDay() / backtest() 中 ~200 行重复审核逻辑。
//
// 职责: 只做编排 (构建上下文 → 调用闸门 → 收集结果), 不包含规则判断本身。

#include "RuleGate.h"
#include "RuleVariableProvider.h"  // RuleCandidateContext
#include "../../trading/TradingTypes.h"

#include <functional>
#include <string>
#include <vector>

namespace factor { namespace compute { class IMarketDataView; } }

namespace domain::strategy {

/// @brief 持仓出场上下文（跨实盘/回测的统一抽象）
struct PositionExitInput {
    std::string symbol;         // fullSymbol (如 "300767.SZ")
    std::string code;           // 无后缀 6 位码
    int colIndex{-1};           // 在视图中的列号
    bool isHolding{true};
    double entryPrice{0.0};
    double currentPrice{0.0};
    double pnlPercent{0.0};
    std::int64_t quantity{0};
};

class RulePipeline {
public:
    /// @param gate RuleGate 实例引用（生命周期由外部 StrategyEngine 管理）
    explicit RulePipeline(rules::RuleGate& gate) : m_gate(gate) {}

    /// @brief 查询规则闸门是否已绑定模板
    [[nodiscard]] bool enabled() const noexcept { return m_gate.enabled(); }

    /// @brief 市场闸门: 当日是否允许新开仓
    [[nodiscard]] bool allowNewEntriesToday(const rules::IRuleVariableProvider& provider) {
        return m_gate.allowNewEntriesToday(provider);
    }

    /// @brief 信号审核: 对买入订单列表逐一审核，返回通过审核的订单
    /// @param buyOrders 待审核买入订单
    /// @param buildContext 构建 RuleCandidateContext 的回调 (symbol, colIndex) → context
    /// @param provider 规则变量提供者（已设置 day 和市场快照）
    /// @return 通过审核的订单（保持原顺序）
    [[nodiscard]] std::vector<domain::trading::OrderRequest> filterBuySignals(
        const std::vector<domain::trading::OrderRequest>& buyOrders,
        const std::function<void(rules::RuleCandidateContext& ctx,
                                  const std::string& symbol)>& buildContext,
        rules::IRuleVariableProvider& provider) const;

    /// @brief 持仓出场审核: 对持仓列表逐一检查，生成出场订单
    /// @param positions 当前持仓列表
    /// @param buildContext 构建 RuleCandidateContext 的回调 (PositionExitInput) → context
    /// @param provider 规则变量提供者
    /// @return 生成的出场订单列表（Exit/Reduce 动作）
    [[nodiscard]] std::vector<domain::trading::OrderRequest> collectPositionExits(
        const std::vector<PositionExitInput>& positions,
        const std::function<void(rules::RuleCandidateContext& ctx,
                                  const PositionExitInput& pos)>& buildContext,
        rules::IRuleVariableProvider& provider) const;

    /// @brief 获取底层 RuleGate 引用（供高级场景直接调用）
    [[nodiscard]] rules::RuleGate& gate() noexcept { return m_gate; }
    [[nodiscard]] const rules::RuleGate& gate() const noexcept { return m_gate; }

    /// @brief 绑定模板数
    [[nodiscard]] int boundTemplateCount() const noexcept { return m_gate.boundTemplateCount(); }

    /// @brief 统计数据
    [[nodiscard]] const rules::RuleGateStats& stats() const noexcept { return m_gate.stats(); }

private:
    rules::RuleGate& m_gate;
};

} // namespace domain::strategy
