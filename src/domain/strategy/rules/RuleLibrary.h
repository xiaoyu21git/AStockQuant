#pragma once
// RuleLibrary — 规则仓库抽象与文件加载实现
// 零 Qt, 纯 C++

#include "RuleTypes.h"

#include <memory>
#include <string>
#include <vector>

namespace domain::strategy::rules {

/// @brief 进程级共享规则库
[[nodiscard]] const RuleLibrary* sharedRuleLibrary();

/// @brief 强制重载 (参数修改后调用)
void reloadSharedRuleLibrary();

/// @brief 注入用户参数覆盖
void setSharedParamOverrides(const ParamOverrides& overrides);

} // namespace domain::strategy::rules
