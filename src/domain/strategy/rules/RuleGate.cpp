// RuleGate — 实现

#include "RuleGate.h"
#include "RuleConditionEvaluator.h"

#include "foundation/json/json_facade.h"
#include "foundation/log/logging.hpp"

#include <algorithm>

namespace domain::strategy::rules {

namespace {

const RuleLibrary* s_sharedLibrary = nullptr;  // 首次加载后全局复用
ParamOverrides s_paramOverrides;               // 桥接层注入的用户参数覆盖

} // namespace

const RuleLibrary* sharedRuleLibrary()
{
    if (!s_sharedLibrary) {
        // 运行时工作目录可能是 build/bin/Release/, 尝试多个候选路径
        static const char* kCandidatePaths[] = {
            "config/rules/compiled.json",             // 项目根 (开发/测试)
            "../config/rules/compiled.json",           // bin/Release → 项目根
            "../../config/rules/compiled.json",        // bin/Release/config → 项目根
        };
        foundation::json::JsonFacade root;
        for (const char* path : kCandidatePaths) {
            auto candidate = foundation::json::JsonFacade::parseFile(path);
            if (!candidate.isNull()) { root = std::move(candidate); break; }
        }
        if (root.isNull()) {
            INTERNAL_ERROR_STREAM << "[RuleGate] compiled.json 解析失败(工作目录搜索了3个候选路径), 规则库不可用";
            return nullptr;
        }
        // 加载用户参数覆盖: 优先用桥接层注入的（内存），否则读文件
        ParamOverrides paramOverrides = s_paramOverrides;
        if (paramOverrides.empty()) {
            static const char* kUserParamsPaths[] = {
                "config/rule_params_user.json",
                "../config/rule_params_user.json",
                "../../config/rule_params_user.json",
            };
            for (const char* up : kUserParamsPaths) {
                auto userRoot = foundation::json::JsonFacade::parseFile(up);
                if (!userRoot.isNull() && userRoot.has("params")) {
                    auto params = userRoot.get("params");
                    for (const auto& tid : params.keys()) {
                        auto pmap = params.get(tid);
                        std::map<std::string, double> overrides;
                        for (const auto& pkey : pmap.keys())
                            overrides[pkey] = pmap.get(pkey).asDouble();
                        paramOverrides[tid] = overrides;
                    }
                    INTERNAL_INFO_STREAM << "[RuleGate] 加载用户规则参数覆盖(文件): " << paramOverrides.size() << " 个模板";
                    break;
                }
            }
        } else {
            INTERNAL_INFO_STREAM << "[RuleGate] 加载用户规则参数覆盖(注入): " << paramOverrides.size() << " 个模板";
        }

        auto lib = loadRuleLibrary(root, paramOverrides);
        if (lib) {
            s_sharedLibrary = lib.release();  // 转移所有权到静态指针
            INTERNAL_INFO_STREAM << "[RuleGate] 共享规则库就绪: " << s_sharedLibrary->templates.size() << " 模板";
        }
    }
    return s_sharedLibrary;
}

void reloadSharedRuleLibrary() {
    delete s_sharedLibrary;
    s_sharedLibrary = nullptr;
    INTERNAL_INFO_STREAM << "[RuleGate] 规则库缓存已清除，下次访问将重新加载";
}

void setSharedParamOverrides(const ParamOverrides& overrides) {
    s_paramOverrides = overrides;
    INTERNAL_INFO_STREAM << "[RuleGate] 注入用户参数覆盖: " << overrides.size() << " 个模板";
}

int RuleGate::configure(const std::vector<std::string>& enabledTemplateIds,
                        const RuleLibrary& library)
{
    m_marketRules.clear(); m_signalRules.clear(); m_positionRules.clear();
    m_boundTemplates = 0;

    for (const auto& tid : enabledTemplateIds) {
        auto it = library.byId.find(tid);
        if (it == library.byId.end()) {
            INTERNAL_DEBUG_STREAM << "[RuleGate] 模板不存在于库, 跳过: " << tid;
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
