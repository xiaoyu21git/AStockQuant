#pragma once
// RuleGate — 规则管线闸门: 策略 rule_composer_state 勾选的模板 → 运行时三个审核点
//   1. 每日市场闸门: market 阶段模板命中 block/freeze → 当日禁新开仓
//   2. 信号审核: signal/eligibility 阶段模板命中 block → 拒绝该买入候选
//   3. 持仓出场: rebalance 阶段模板命中 exit/reduce → 触发离场动作
// 统计每模板 评估/阻断/出场/数据未就绪 次数, 保证"生效可见、退化可查"

#include "RuleTypes.h"

#include <map>
#include <string>
#include <vector>

namespace domain::strategy::rules {

struct RuleTemplateStats {
    int evaluated{0};      // 规则求值次数
    int hits{0};           // 动作命中次数(block/exit/reduce/freeze/state_switch/candidate_entry)
    int blockedSignals{0}; // 仅 block 动作命中次数(用于计算拦截率 = blockedSignals/evaluated)
    int dataMissing{0};    // 因变量缺失未判定次数
};

struct RuleGateStats {
    std::map<std::string, RuleTemplateStats> byTemplate;
    int frozenDays{0};         // 市场闸门冻结的交易日数
    int signalsBlocked{0};     // 被规则拒绝的买入候选数
    int positionExits{0};      // 规则触发的出场数
};

class RuleGate {
public:
    /// @brief 绑定策略启用的模板集 (来自 parameters.rule_composer_state 的勾选)
    /// @param enabledTemplateIds 策略勾选的 templateId 列表
    /// @param library 已加载的规则库 (生命周期须长于本对象)
    /// @param ablatedTemplateIds 消融测试: 跳过这些 templateId (不参与评估, 仅统计)
    /// @return 成功绑定的模板数
    int configure(const std::vector<std::string>& enabledTemplateIds,
                  const RuleLibrary& library,
                  const std::vector<std::string>& ablatedTemplateIds = {});

    [[nodiscard]] bool enabled() const noexcept
    {
        return !m_marketRules.empty() || !m_signalRules.empty() || !m_positionRules.empty();
    }

    /// @brief 每日市场闸门 (market 阶段); false = 当日禁新开仓
    [[nodiscard]] bool allowNewEntriesToday(const IRuleVariableProvider& provider);

    /// @brief 买入候选审核 (signal/eligibility 阶段); false = 拒绝
    [[nodiscard]] bool allowSignal(const IRuleVariableProvider& provider);

    /// @brief 持仓出场审核 (rebalance 阶段); 返回 Exit/Reduce/Pass
    [[nodiscard]] RuleAction positionAction(const IRuleVariableProvider& provider);

    [[nodiscard]] const RuleGateStats& stats() const noexcept { return m_stats; }
    [[nodiscard]] int boundTemplateCount() const noexcept { return m_boundTemplates; }

    /// @brief 最近一次命中规则的 templateId (供归因记录)
    [[nodiscard]] const std::string& lastHitTemplateId() const noexcept { return m_lastHitTemplateId; }
    /// @brief 最近一次命中规则的 ruleId (供归因记录)
    [[nodiscard]] const std::string& lastHitRuleId() const noexcept { return m_lastHitRuleId; }

    // ── v2.1 评分接口: 规则从闸门升级为评分工 ──

    /// @brief 买入形态评分: 遍历所有 signal/eligibility 规则, 统计命中比例
    /// @return 0~1, 0=无规则命中, 1=所有规则命中; 无信号规则时返回 0.5(中性)
    [[nodiscard]] double entryScore(const IRuleVariableProvider& provider);

    /// @brief 出场形态评分: 遍历所有 rebalance 规则, 统计触发 exit/reduce 的比例
    /// @return 0~1, 0=无出场触发, 1=所有规则触发; 无持仓规则时返回 0.0
    [[nodiscard]] double exitScore(const IRuleVariableProvider& provider);

private:
    struct BoundRule {
        const CompiledRule* rule{nullptr};
        std::string templateId;
    };

    /// 按阶段执行绑定规则集; 返回第一个命中的动作 (priority 已排序)
    RuleAction runRules(std::vector<BoundRule>& rules, const IRuleVariableProvider& provider);

    std::vector<BoundRule> m_marketRules;
    std::vector<BoundRule> m_signalRules;
    std::vector<BoundRule> m_positionRules;
    RuleGateStats m_stats;
    int m_boundTemplates{0};
    std::string m_lastHitTemplateId;
    std::string m_lastHitRuleId;
};

} // namespace domain::strategy::rules
