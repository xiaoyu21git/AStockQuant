// RuleGate — 实现

#include "RuleGate.h"
#include "RuleConditionEvaluator.h"
#include "RuleLibrary.h"

#include "foundation/log/logging.hpp"

#include <algorithm>

namespace domain::strategy::rules {

int RuleGate::configure(const std::vector<std::string>& enabledTemplateIds,
                        const RuleLibrary& library,
                        const std::vector<std::string>& ablatedTemplateIds)
{
    m_marketRules.clear(); m_signalRules.clear(); m_positionRules.clear();
    m_boundTemplates = 0;

    for (const auto& tid : enabledTemplateIds) {
        auto it = library.byId.find(tid);
        if (it == library.byId.end()) {
            INTERNAL_DEBUG_STREAM << "[RuleGate] 模板不存在于库, 跳过: " << tid;
            continue;
        }
        // 消融测试: 跳过黑名单模板
        if (std::find(ablatedTemplateIds.begin(), ablatedTemplateIds.end(), tid)
            != ablatedTemplateIds.end()) {
            INTERNAL_INFO_STREAM << "[RuleGate] 消融测试: 跳过模板 " << tid;
            continue;
        }
        const auto& compiledTemplate = *it->second;
        for (const auto& rule : compiledTemplate.rules) {
            BoundRule bound{&rule, tid};
            if (rule.stage == "market")
                m_marketRules.push_back(bound);
            else if (rule.stage == "signal" || rule.stage == "eligibility")
                m_signalRules.push_back(bound);
            else if (rule.stage == "rebalance")
                m_positionRules.push_back(bound);
        }
        ++m_boundTemplates;
    }

    // 各阶段内按规则 priority 降序 (已在 loadRuleLibrary 排序, 这里仅确认)
    auto sortByPriority = [](std::vector<BoundRule>& vec) {
        std::sort(vec.begin(), vec.end(),
                  [](const BoundRule& a, const BoundRule& b) {
                      return a.rule->priority > b.rule->priority;
                  });
    };
    sortByPriority(m_marketRules);
    sortByPriority(m_signalRules);
    sortByPriority(m_positionRules);

    INTERNAL_INFO_STREAM << "[RuleGate] 绑定模板: " << m_boundTemplates
                         << " market=" << m_marketRules.size()
                         << " signal=" << m_signalRules.size()
                         << " rebalance=" << m_positionRules.size();
    return m_boundTemplates;
}

RuleAction RuleGate::runRules(std::vector<BoundRule>& rules,
                              const IRuleVariableProvider& provider)
{
    // 市场/信号/出场共用:
    // - Block/Freeze/Exit/Reduce 等阻断动作 → 立即返回, 终止审核
    // - Pass 类动作 (如 eligibility 资格确认) → 仅计数继续看下一个模板
    for (auto& bound : rules) {
        const auto& statsEntry = m_stats.byTemplate[bound.templateId];
        auto& stats = const_cast<RuleTemplateStats&>(statsEntry);
        ++stats.evaluated;

        const TriState verdict = bound.rule->evaluateCondition(provider);
        if (verdict == TriState::DataMissing) { ++stats.dataMissing; continue; }
        if (verdict == TriState::Pass) {
            ++stats.hits;
            if (bound.rule->decision.action == RuleAction::Block) ++stats.blockedSignals;
            if (bound.rule->decision.action == RuleAction::Pass) continue;  // 资格确认不计入阻断
            m_lastHitTemplateId = bound.templateId;
            m_lastHitRuleId = bound.rule->ruleId;
            return bound.rule->decision.action;  // Block/Freeze/Exit/Reduce → 立即生效
        }
        // Fail: 本条规则条件不满足, 继续下一条
    }
    return RuleAction::Pass;
}

bool RuleGate::allowNewEntriesToday(const IRuleVariableProvider& provider)
{
    if (m_marketRules.empty()) return true;
    const RuleAction action = runRules(m_marketRules, provider);
    if (action == RuleAction::Block || action == RuleAction::Freeze) {
        ++m_stats.frozenDays;
        return false;
    }
    return true;
}

bool RuleGate::allowSignal(const IRuleVariableProvider& provider)
{
    if (m_signalRules.empty()) return true;
    const RuleAction action = runRules(m_signalRules, provider);
    if (action == RuleAction::Block) {
        ++m_stats.signalsBlocked;
        return false;
    }
    return true;
}

RuleAction RuleGate::positionAction(const IRuleVariableProvider& provider)
{
    if (m_positionRules.empty()) return RuleAction::Pass;
    const RuleAction action = runRules(m_positionRules, provider);
    if (action == RuleAction::Exit) ++m_stats.positionExits;
    return action;
}

} // namespace domain::strategy::rules
