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
    m_templateTags.clear();
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
        m_templateTags[tid] = compiledTemplate.tags;
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
        // ── 规则运行时状态: disabled → 跳过; degraded → 评估但不阻断 ──
        const auto& ruleStates = getRuleStates();
        auto stateIt = ruleStates.find(bound.rule->ruleId);
        bool ruleDisabled = (stateIt != ruleStates.end() && !stateIt->second.enabled);
        bool ruleDegraded = (stateIt != ruleStates.end() && stateIt->second.severity == "degraded");

        const auto& statsEntry = m_stats.byTemplate[bound.templateId];
        auto& stats = const_cast<RuleTemplateStats&>(statsEntry);

        if (ruleDisabled) {
            ++stats.dataMissing;
            continue;
        }
        ++stats.evaluated;

        const TriState verdict = bound.rule->evaluateCondition(provider);
        if (verdict == TriState::DataMissing) { ++stats.dataMissing; continue; }
        if (verdict == TriState::Pass) {
            ++stats.hits;
            if (bound.rule->decision.action == RuleAction::Block) ++stats.blockedSignals;
            if (bound.rule->decision.action == RuleAction::Pass) continue;
            // degraded: 命中但仅记录归因, 不阻断
            if (ruleDegraded) continue;
            m_lastHitTemplateId = bound.templateId;
            m_lastHitRuleId = bound.rule->ruleId;
            auto tagIt = m_templateTags.find(bound.templateId);
            m_lastHitTemplateTags = tagIt != m_templateTags.end() ? tagIt->second : std::vector<std::string>{};
            m_lastHitRuleTags = bound.rule->tags;
            return bound.rule->decision.action;
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
        INTERNAL_WARN_STREAM << "[RuleGate] 新开仓冻结:"
            << " template=" << m_lastHitTemplateId
            << " rule=" << m_lastHitRuleId;
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

double RuleGate::entryScore(const IRuleVariableProvider& provider)
{
    // 遍历所有 signal/eligibility 规则, 统计 Pass 的比例
    if (m_signalRules.empty()) return 0.5;  // 无规则时中性

    int hits = 0, total = 0;
    for (auto& bound : m_signalRules) {
        ++total;
        const TriState verdict = bound.rule->evaluateCondition(provider);
        if (verdict == TriState::Pass) {
            ++hits;
        }
        // DataMissing 和 Fail 都不计入命中
    }
    return total > 0 ? static_cast<double>(hits) / static_cast<double>(total) : 0.5;
}

double RuleGate::exitScore(const IRuleVariableProvider& provider)
{
    // 遍历所有 rebalance 规则, 统计触发 exit/reduce 的比例
    if (m_positionRules.empty()) return 0.0;

    int hits = 0, total = 0;
    for (auto& bound : m_positionRules) {
        ++total;
        const TriState verdict = bound.rule->evaluateCondition(provider);
        if (verdict == TriState::Pass) {
            const auto action = bound.rule->decision.action;
            if (action == RuleAction::Exit || action == RuleAction::Reduce) {
                ++hits;
            }
        }
    }
    return total > 0 ? static_cast<double>(hits) / static_cast<double>(total) : 0.0;
}

} // namespace domain::strategy::rules
