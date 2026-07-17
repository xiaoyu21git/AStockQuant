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
    int hits{0};           // 动作命中次数(block/exit/reduce/freeze)
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
    /// @return 成功绑定的模板数
    int configure(const std::vector<std::string>& enabledTemplateIds,
                  const RuleLibrary& library);

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
};

/// @brief 进程级共享规则库 (首次调用加载 config/rules/compiled.json)
/// @return 加载失败返回 nullptr (调用方应记日志并禁用规则闸门, 不静默)
[[nodiscard]] const RuleLibrary* sharedRuleLibrary();

} // namespace domain::strategy::rules
