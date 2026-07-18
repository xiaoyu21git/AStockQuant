#pragma once
// 规则条件求值器 — 编译 JSON 条件树为可执行谓词

#include "RuleTypes.h"
#include "foundation/json/json_facade.h"
#include <functional>

namespace domain::strategy::rules {

/// @brief 字符串枚举值的稳定数值编码 (FNV-1a 低32位)
/// 规则里 eq "repair" 这类字符串比较, 与 IRuleVariableProvider 返回的编码值对比;
/// provider 对字符串状态变量(如 market.emotion_cycle)必须用同一函数编码
[[nodiscard]] double ruleStringValueCode(const std::string& text);

/// @brief 从 JSON 条件节点编译为 TriState 求值函数
/// @param overrides 可选用户参数覆盖 map (paramKey → newValue)
[[nodiscard]] std::function<TriState(const IRuleVariableProvider&)>
compileCondition(const foundation::json::JsonFacade& conditionNode,
                 const std::map<std::string, double>* overrides = nullptr);

/// @brief 从 compiled.json 根节点加载规则库，paramOverrides 可选覆盖条件数值
[[nodiscard]] std::unique_ptr<RuleLibrary> loadRuleLibrary(
    const foundation::json::JsonFacade& compiledJson,
    const ParamOverrides& paramOverrides = {});

} // namespace domain::strategy::rules
