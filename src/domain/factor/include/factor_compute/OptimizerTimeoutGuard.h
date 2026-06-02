#pragma once

#include "FactorSignalTypes.h"

#include <chrono>
#include <cstdint>
#include <functional>

namespace factor::compute {

/// @brief P4-T5：优化器超时治理（实施任务清单 Phase 4）
///
/// 约束：
/// - 约束组合优化耗时波动。
/// - 超时可返回次优可行解或跳过当次再平衡。
/// - 诊断码可追踪。
/// - 求解超时无保护导致整体超时为阻断条件。
///
/// 使用方式：
/// ```cpp
/// OptimizerTimeoutGuard guard(spec.runtimeBudget);
/// auto result = guard.runWithTimeout([&]() {
///     return portfolioOptimizer.solve();
/// });
/// if (result.timedOut) {
///     // 使用次优解或跳过本次再平衡
/// }
/// ```
enum class OptimizerFallbackStrategy : uint8_t {
    SkipRebalance = 0,    // 跳过当次再平衡，保持当前仓位
    UseEqualWeight = 1,   // 回退到等权分配
    UseLastSolution = 2,  // 使用上一期最优解
};

struct OptimizerTimeoutResult final {
    bool timedOut{false};
    int64_t elapsedMilliseconds{0};
    int64_t budgetMilliseconds{0};
    OptimizerFallbackStrategy appliedFallback{OptimizerFallbackStrategy::SkipRebalance};
};

/// @brief 优化器超时守卫
///
/// 在优化器求解前设置超时预算，超时后返回降级策略。
/// 约束（实施清单 P4-T5）：
/// - 超时可返回次优可行解或跳过当次再平衡。
/// - 诊断码可追踪。
class OptimizerTimeoutGuard {
public:
    /// @brief 构造守卫
    ///
    /// @param budget 运行时预算
    /// @param maxIterations 最大迭代次数（0 表示无限制）
    explicit OptimizerTimeoutGuard(
        const RuntimeBudget& budget,
        uint32_t maxIterations = 0U) noexcept
        : budget_(budget)
        , maxIterations_(maxIterations)
        , startTime_(std::chrono::steady_clock::now())
    {
    }

    /// @brief 在执行优化器之前检查预算
    [[nodiscard]] bool canStart() const noexcept
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime_);
        return elapsed.count() < budget_.timeoutMilliseconds;
    }

    /// @brief 检查迭代是否应继续
    ///
    /// @param currentIteration 当前迭代次数（从 1 开始）
    /// @return true 如果应继续迭代
    [[nodiscard]] bool shouldContinueIteration(uint32_t currentIteration) const noexcept
    {
        if (maxIterations_ > 0U && currentIteration > maxIterations_) {
            return false;
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime_);
        return elapsed.count() < budget_.timeoutMilliseconds;
    }

    /// @brief 构建超时结果
    [[nodiscard]] OptimizerTimeoutResult buildResult(bool completed) const noexcept
    {
        OptimizerTimeoutResult result;
        if (!completed) {
            result.timedOut = true;
            result.appliedFallback = OptimizerFallbackStrategy::SkipRebalance;
        }
        result.elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime_).count();
        result.budgetMilliseconds = budget_.timeoutMilliseconds;
        return result;
    }

    /// @brief 设置降级策略
    void setFallbackStrategy(OptimizerFallbackStrategy strategy) noexcept
    {
        fallbackStrategy_ = strategy;
    }

    /// @brief 获取当前降级策略
    [[nodiscard]] OptimizerFallbackStrategy fallbackStrategy() const noexcept
    {
        return fallbackStrategy_;
    }

    /// @brief 获取已耗时（毫秒）
    [[nodiscard]] int64_t elapsedMilliseconds() const noexcept
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime_).count();
    }

private:
    const RuntimeBudget& budget_;
    uint32_t maxIterations_;
    std::chrono::steady_clock::time_point startTime_;
    OptimizerFallbackStrategy fallbackStrategy_{OptimizerFallbackStrategy::SkipRebalance};
};

} // namespace factor::compute