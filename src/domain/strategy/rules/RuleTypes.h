#pragma once
// 规则运行时引擎 — 类型定义 (零 Qt, 纯 C++)
// 从 config/rules/compiled.json 加载, 变量由 IRuleVariableProvider 供给

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace foundation::json { class JsonFacade; }

namespace domain::strategy::rules {

// ── 变量提供者接口 ──
class IRuleVariableProvider {
public:
    virtual ~IRuleVariableProvider() = default;
    /// @brief 查询变量值; 返回 nullopt 表示数据未就绪(不判定)
    [[nodiscard]] virtual std::optional<double> resolve(const std::string& varPath) const = 0;
};

// ── 条件节点 (树) ──
enum class RuleConditionOp : std::uint8_t {
    All, Any, Not,            // 逻辑
    Truthy,                    // 隐式: var 值 > 0
    Lt, Gt, Le, Ge, Eq         // 比较: left(var/字面量) op right(字面量)
};

struct RuleConditionNode;

/// @brief 比较操作的右值 (模板全部为数值/字符串字面量, 无变量引用)
struct ConditionRightValue {
    enum class Kind : std::uint8_t { Number, String };
    Kind kind{Kind::Number};
    double numValue{0.0};
    std::string strValue;
};

/// @brief 三态判决: 命中 / 未命中 / 数据未就绪
enum class TriState : std::uint8_t { Pass, Fail, DataMissing };

// ── 规则动作 ──
enum class RuleAction : std::uint8_t {
    Pass, Block, CandidateEntry,
    Exit, Reduce, StateSwitch, Freeze
};

/// @brief 单条规则的判定结果
struct RuleDecision {
    RuleAction action{RuleAction::Pass};
    std::string reasonCode;
    std::string message;
    std::string statePayload;   // state_switch 时写入的目标状态名
    double scoreBoost{0.0};     // candidate_entry 时可选评分加成
};

// ── 编译后的规则实体 (单条 rule) ──
struct CompiledRule {
    std::string ruleId;
    std::string stage;          // market/eligibility/signal/rebalance (YAML 原值)
    int priority{0};            // 越小越高优
    std::function<TriState(const IRuleVariableProvider&)> evaluateCondition;
    RuleDecision decision;
};

// ── 规则模板定义 (一个 template_id 包含多条 rules) ──
struct CompiledRuleTemplate {
    std::string templateId;
    std::string displayName;
    std::string phase;          // 对应 stage (规则本体内也有 stage, 此处为 catalog 汇总)
    std::vector<CompiledRule> rules;
};

// ── 规则库 (整体 loaded from compiled.json) ──
struct RuleLibrary {
    int version{0};
    std::string ns;
    std::vector<CompiledRuleTemplate> templates;                // 全部模板
    std::map<std::string, const CompiledRuleTemplate*> byId;    // templateId → 模板
};

/// @brief 加载规则库 (compiled.json); 失败返回 nullptr
[[nodiscard]] std::unique_ptr<RuleLibrary> loadRuleLibrary(
    const foundation::json::JsonFacade& compiledJson);

} // namespace domain::strategy::rules
