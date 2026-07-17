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
/// @param conditionNode when: {...} 或单个条件节点的 JsonFacade 引用
[[nodiscard]] std::function<TriState(const IRuleVariableProvider&)>
compileCondition(const foundation::json::JsonFacade& conditionNode);

/// @brief 从 compiled.json 根节点加载整个规则库
[[nodiscard]] std::unique_ptr<RuleLibrary> loadRuleLibrary(
    const foundation::json::JsonFacade& compiledJson);

} // namespace domain::strategy::rules
