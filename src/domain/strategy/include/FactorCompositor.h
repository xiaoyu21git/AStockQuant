#pragma once

#include "../../factor/include/factor_compute/FactorSignalTypes.h"

#include <cstdint>
#include <vector>

namespace domain::strategy {

/// @brief 因子合成结果
struct CompositedSignal final {
    /// 合成后的信号值，展平为一维向量（T 日期 × N 标的）
    std::vector<factor::compute::signal_value_t> values;

    /// 时间维度大小
    std::int32_t timeCount{0};

    /// 标的维度大小
    std::int32_t instrumentCount{0};

    [[nodiscard]] bool isValid() const noexcept
    {
        return timeCount > 0
            && instrumentCount > 0
            && values.size() == static_cast<std::size_t>(timeCount) * static_cast<std::size_t>(instrumentCount);
    }
};

/// @brief 因子组合方式枚举
enum class CompositeMethod : std::uint8_t {
    EqualWeight = 0,  ///< 等权重合成
    CustomWeight = 1, ///< 自定义权重合成
    ICRankWeight = 2, ///< IC 排序加权合成
};

/// @brief 因子合成配置
struct FactorCompositeSpec final {
    /// 合成方式
    CompositeMethod method{CompositeMethod::EqualWeight};

    /// 自定义权重（用于 CustomWeight 模式）
    std::vector<double> customWeights;

    /// 信号维度数量（多因子场景下 = 因子数）
    std::uint32_t signalCount{0U};

    [[nodiscard]] bool isValid() const noexcept
    {
        if (signalCount == 0U) {
            return false;
        }
        if (method == CompositeMethod::CustomWeight) {
            return customWeights.size() == static_cast<std::size_t>(signalCount);
        }
        return true;
    }
};

/// @brief 因子加权合成器
///
/// 将多因子 SignalSet 按权重合成为单一维度策略信号。
/// 纯矩阵运算，不涉及逐标循环。
///
/// 约束：
/// - 纯 C++，零 Qt 依赖
/// - 输入：SignalSet（T×N×S 三维）+ 合成配置
/// - 输出：CompositedSignal（T×N 二维信号值）
class FactorCompositor final {
public:
    FactorCompositor() = default;

    /// @brief 执行因子合成
    ///
    /// @param signalSet  多因子信号集（T 日期 × N 标的 × S 信号维度）
    /// @param spec       合成配置（方式 + 权重）
    /// @return           合成后的 T×N 信号矩阵
    [[nodiscard]] CompositedSignal compose(
        const factor::compute::SignalSet& signalSet,
        const FactorCompositeSpec& spec) const;

private:
    /// @brief 等权合成：所有信号维度取均值
    [[nodiscard]] static CompositedSignal composeEqualWeight(
        const factor::compute::SignalSet& signalSet,
        std::int32_t timeCount,
        std::int32_t instrumentCount,
        std::uint32_t signalCount);

    /// @brief 自定义权重合成：按指定 weight 加权求和
    [[nodiscard]] static CompositedSignal composeCustomWeight(
        const factor::compute::SignalSet& signalSet,
        const std::vector<double>& weights,
        std::int32_t timeCount,
        std::int32_t instrumentCount);
};

} // namespace domain::strategy