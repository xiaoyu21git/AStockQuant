// 规则条件求值器 + 规则库加载 — 实现
// 三态语义:
//   all: 任一 Fail→Fail; 无 Fail 但有 DataMissing→DataMissing; 全 Pass→Pass
//   any: 任一 Pass→Pass; 无 Pass 但有 DataMissing→DataMissing; 全 Fail→Fail
//   not: Pass↔Fail 反转, DataMissing 保持
//   比较/truthy: 变量缺失→DataMissing (绝不静默通过)

#include "RuleConditionEvaluator.h"

#include "foundation/log/logging.hpp"

#include <algorithm>
#include <cmath>

namespace domain::strategy::rules {

namespace {

constexpr double kTruthyEpsilon = 1e-9;
// 字符串枚举变量(如 market.emotion_cycle="repair")经 provider 以哈希槽位编码:
// provider 对该类变量按字符串字典返回编码值, eq 比较字符串时同样编码后对数值比较
// (编码约定见 IRuleVariableProvider 实现方)

std::function<TriState(const IRuleVariableProvider&)> makeFailAlways(const std::string& why) {
    INTERNAL_WARN_STREAM << "[RuleEngine] 条件编译失败, 该条件恒 DataMissing: " << why;
    return [](const IRuleVariableProvider&) { return TriState::DataMissing; };
}

/// 提取 {var: xxx} 引用; 非法返回空
std::string extractVar(const foundation::json::JsonFacade& node) {
    if (node.has("var")) return node.get("var").asString();
    return {};
}

/// 字符串值稳定编码 (与 provider 侧约定一致): FNV-1a 64 → double 可精确表示的低 32 位
double encodeStringValue(const std::string& text) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return static_cast<double>(static_cast<std::uint32_t>(hash));
}

} // namespace

double ruleStringValueCode(const std::string& text) { return encodeStringValue(text); }

using OverrideMap = std::map<std::string, double>; // paramKey → newValue

std::function<TriState(const IRuleVariableProvider&)>
compileCondition(const foundation::json::JsonFacade& node,
                 const OverrideMap* overrides = nullptr)
{
    if (!node.has("op")) {
        // 裸 {var: x} 视为 truthy (not.value 的隐式形态)
        std::string varPath = extractVar(node);
        if (varPath.empty()) return makeFailAlways("节点无 op 且无 var");
        return [varPath](const IRuleVariableProvider& provider) {
            auto value = provider.resolve(varPath);
            if (!value.has_value()) return TriState::DataMissing;
            return std::abs(*value) > kTruthyEpsilon ? TriState::Pass : TriState::Fail;
        };
    }

    const std::string op = node.get("op").asString();

    // ── 逻辑组合 ──
    if (op == "all" || op == "any") {
        std::vector<std::function<TriState(const IRuleVariableProvider&)>> children;
        if (node.has("conditions")) {
            auto conditions = node.get("conditions");
            for (std::size_t i = 0; i < conditions.size(); ++i)
                children.push_back(compileCondition(conditions.at(i), overrides));
        }
        if (children.empty()) return makeFailAlways("all/any 无子条件");
        const bool isAll = (op == "all");
        return [children, isAll](const IRuleVariableProvider& provider) {
            bool anyMissing = false;
            for (const auto& child : children) {
                const TriState verdict = child(provider);
                if (verdict == TriState::DataMissing) { anyMissing = true; continue; }
                if (isAll && verdict == TriState::Fail) return TriState::Fail;
                if (!isAll && verdict == TriState::Pass) return TriState::Pass;
            }
            if (anyMissing) return TriState::DataMissing;
            return isAll ? TriState::Pass : TriState::Fail;
        };
    }

    if (op == "not") {
        if (!node.has("value")) return makeFailAlways("not 无 value");
        auto inner = compileCondition(node.get("value"), overrides);
        return [inner](const IRuleVariableProvider& provider) {
            const TriState verdict = inner(provider);
            if (verdict == TriState::DataMissing) return TriState::DataMissing;
            return verdict == TriState::Pass ? TriState::Fail : TriState::Pass;
        };
    }

    if (op == "truthy") {
        std::string varPath = node.has("value") ? extractVar(node.get("value")) : "";
        if (varPath.empty()) return makeFailAlways("truthy 无 value.var");
        return [varPath](const IRuleVariableProvider& provider) {
            auto value = provider.resolve(varPath);
            if (!value.has_value()) return TriState::DataMissing;
            return std::abs(*value) > kTruthyEpsilon ? TriState::Pass : TriState::Fail;
        };
    }

    // ── 比较: left{var} op right(数值/字符串字面量) ──
    if (op == "lt" || op == "gt" || op == "le" || op == "ge" || op == "eq") {
        std::string varPath = node.has("left") ? extractVar(node.get("left")) : "";
        if (varPath.empty()) return makeFailAlways("比较缺 left.var: op=" + op);
        if (!node.has("right")) return makeFailAlways("比较缺 right: op=" + op);

        auto right = node.get("right");
        double rightValue = 0.0;
        if (right.isString()) rightValue = encodeStringValue(right.asString());
        else rightValue = right.asDouble();

        // 用户参数覆盖
        if (overrides && !overrides->empty() && !varPath.empty()) {
            std::string key = varPath + "__" + op;
            auto it = overrides->find(key);
            if (it != overrides->end()) {
                rightValue = it->second;
            }
        }

        enum class Cmp : std::uint8_t { Lt, Gt, Le, Ge, Eq };
        Cmp cmp = Cmp::Eq;
        if (op == "lt") cmp = Cmp::Lt;
        else if (op == "gt") cmp = Cmp::Gt;
        else if (op == "le") cmp = Cmp::Le;
        else if (op == "ge") cmp = Cmp::Ge;

        return [varPath, rightValue, cmp](const IRuleVariableProvider& provider) {
            auto leftValue = provider.resolve(varPath);
            if (!leftValue.has_value()) return TriState::DataMissing;
            bool pass = false;
            switch (cmp) {
            case Cmp::Lt: pass = *leftValue <  rightValue; break;
            case Cmp::Gt: pass = *leftValue >  rightValue; break;
            case Cmp::Le: pass = *leftValue <= rightValue; break;
            case Cmp::Ge: pass = *leftValue >= rightValue; break;
            case Cmp::Eq: pass = std::abs(*leftValue - rightValue) < kTruthyEpsilon; break;
            }
            return pass ? TriState::Pass : TriState::Fail;
        };
    }

    return makeFailAlways("未知 op: " + op);
}

namespace {

RuleAction parseAction(const std::string& result) {
    if (result == "block")           return RuleAction::Block;
    if (result == "candidate_entry") return RuleAction::CandidateEntry;
    if (result == "exit")            return RuleAction::Exit;
    if (result == "reduce")          return RuleAction::Reduce;
    if (result == "state_switch")    return RuleAction::StateSwitch;
    if (result == "freeze")          return RuleAction::Freeze;
    return RuleAction::Pass;
}

} // namespace

std::unique_ptr<RuleLibrary> loadRuleLibrary(const foundation::json::JsonFacade& compiledJson,
                                              const ParamOverrides& paramOverrides)
{
    if (!compiledJson.has("templates")) {
        INTERNAL_ERROR_STREAM << "[RuleEngine] compiled.json 缺 templates";
        return nullptr;
    }

    auto library = std::make_unique<RuleLibrary>();
    library->version = compiledJson.has("version") ? compiledJson.get("version").asInt() : 0;
    library->ns = compiledJson.has("namespace") ? compiledJson.get("namespace").asString() : "";

    auto templates = compiledJson.get("templates");
    library->templates.reserve(templates.size());
    for (std::size_t i = 0; i < templates.size(); ++i) {
        auto templateNode = templates.at(i);
        CompiledRuleTemplate compiledTemplate;
        compiledTemplate.templateId = templateNode.has("templateId")
            ? templateNode.get("templateId").asString() : "";
        compiledTemplate.displayName = templateNode.has("displayName")
            ? templateNode.get("displayName").asString() : "";
        compiledTemplate.phase = templateNode.has("phase")
            ? templateNode.get("phase").asString() : "";
        compiledTemplate.summary = templateNode.has("summary")
            ? templateNode.get("summary").asString() : "";
        compiledTemplate.ns = templateNode.has("namespace")
            ? templateNode.get("namespace").asString() : "";
        compiledTemplate.fileName = templateNode.has("fileName")
            ? templateNode.get("fileName").asString() : "";
        if (templateNode.has("tags")) {
            auto tags = templateNode.get("tags");
            for (std::size_t ti = 0; ti < tags.size(); ++ti)
                compiledTemplate.tags.push_back(tags.at(ti).asString());
        }
        if (templateNode.has("actions")) {
            auto acts = templateNode.get("actions");
            for (std::size_t ai = 0; ai < acts.size(); ++ai)
                compiledTemplate.actions.push_back(acts.at(ai).asString());
        }
        if (compiledTemplate.templateId.empty()) continue;

        if (templateNode.has("rules")) {
            auto rules = templateNode.get("rules");
            for (std::size_t j = 0; j < rules.size(); ++j) {
                auto ruleNode = rules.at(j);
                CompiledRule rule;
                rule.ruleId = ruleNode.has("id") ? ruleNode.get("id").asString() : "";
                rule.stage = ruleNode.has("stage") ? ruleNode.get("stage").asString() : "";
                rule.priority = ruleNode.has("priority") ? ruleNode.get("priority").asInt() : 0;
                if (ruleNode.has("when")) {
                    auto whenNode = ruleNode.get("when");
                    // 查找该模板的用户参数覆盖
                    const OverrideMap* ov = nullptr;
                    auto tmplIt = paramOverrides.find(compiledTemplate.templateId);
                    if (tmplIt != paramOverrides.end()) ov = &tmplIt->second;
                    rule.conditionJson = whenNode.toString();
                    rule.evaluateCondition = compileCondition(whenNode, ov);
                } else {
                    rule.evaluateCondition = [](const IRuleVariableProvider&) { return TriState::Fail; };
                }

                if (ruleNode.has("then")) {
                    auto thenNode = ruleNode.get("then");
                    rule.decision.action = parseAction(
                        thenNode.has("result") ? thenNode.get("result").asString() : "");
                    rule.decision.reasonCode = thenNode.has("reason_code")
                        ? thenNode.get("reason_code").asString() : "";
                    rule.decision.message = thenNode.has("message")
                        ? thenNode.get("message").asString() : "";
                    if (thenNode.has("payload")) {
                        auto payload = thenNode.get("payload");
                        if (payload.has("state"))
                            rule.decision.statePayload = payload.get("state").asString();
                    }
                }
                compiledTemplate.rules.push_back(std::move(rule));
            }
        }
        // 组内规则按 priority 降序 (数值大=优先判定, 与模板语义一致: invalidated 规则优先)
        std::sort(compiledTemplate.rules.begin(), compiledTemplate.rules.end(),
                  [](const CompiledRule& a, const CompiledRule& b) { return a.priority > b.priority; });
        library->templates.push_back(std::move(compiledTemplate));
    }

    for (const auto& compiledTemplate : library->templates)
        library->byId[compiledTemplate.templateId] = &compiledTemplate;

    INTERNAL_INFO_STREAM << "[RuleEngine] 规则库加载: " << library->templates.size() << " 个模板";
    return library;
}

} // namespace domain::strategy::rules
