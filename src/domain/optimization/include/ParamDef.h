// ParamDef.h — 参数类型枚举与枚举选项定义
// domain::optimization 模块基础类型
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace domain::optimization {

/// 参数值类型
enum class ParamType : std::uint8_t {
    Int = 0,
    Double = 1,
    Bool = 2,
    Enum = 3
};

/// 枚举选项: value → label
struct EnumOption final {
    int value{0};
    std::string label;

    EnumOption() = default;
    EnumOption(int v, std::string lbl)
        : value(v), label(std::move(lbl)) {}
};

/// 优化算法类型
enum class OptimizerKind : std::uint8_t {
    GridSearch = 0,
    Bayesian = 1
};

/// 优化目标指标
enum class ObjectiveMetric : std::uint8_t {
    SharpeRatio = 0,
    AnnualizedReturn = 1,
    CalmarRatio = 2,
    SortinoRatio = 3,
    ProfitFactor = 4,
    Custom = 5
};

} // namespace domain::optimization
