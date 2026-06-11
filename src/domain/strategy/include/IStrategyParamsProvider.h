#pragma once

#include "StrategyServiceTypes.h"

#include <optional>
#include <string>

namespace domain::strategy {

/// @brief 策略参数查询接口（纯 C++，domain 层使用）
///
/// 实现侧（UI 桥接层）负责从数据库 / 缓存中取出策略定义，
/// 转换为 StrategyCreationParams 后返回。
class IStrategyParamsProvider {
public:
    virtual ~IStrategyParamsProvider() = default;

    /// @brief 根据策略 ID 查询创建参数
    /// @return 找到则返回 StrategyCreationParams，否则 nullopt
    [[nodiscard]] virtual std::optional<StrategyCreationParams> findParams(
        const std::string& strategyId) = 0;
};

} // namespace domain::strategy