#pragma once

#include "FactorSignalTypes.h"

#include <chrono>
#include <cstdint>

namespace factor::compute {

/// @brief P4-T4：预算闸门与提前终止（设计文档 Section 6.1）
///
/// 功能：
/// - 毫秒级预算检查
/// - 阶段级预算检查生效
/// - 主循环增量检查生效
/// - 超限返回 RuntimeExceeded + PartialDiagnostics
///
/// 约束（实施清单 P4-T4）：
/// - 预算超限立即停止后续阶段，返回部分结果与诊断。
/// - 每个阶段前后调用 check_budget()。
enum class BudgetStage : uint8_t {
    Validate = 0,
    LoadData = 1,
    Compute = 2,
    PostProcess = 3,
    Assemble = 4,
    Analyze = 5,
};

/// @brief 阶段预算检查结果
struct BudgetCheckResult final {
    bool exceeded{false};
    int64_t elapsedMilliseconds{0};
    int64_t remainingMilliseconds{0};

    [[nodiscard]] bool canContinue() const noexcept { return !exceeded; }
};

/// @brief 运行时预算闸门
///
/// 使用方式：
/// ```cpp
/// RuntimeBudgetGuard guard(spec.runtimeBudget);
/// for (auto& stage : stages) {
///     auto result = guard.checkBudget(BudgetStage::Compute);
///     if (result.exceeded) {
///         return FactorError::Timeout;
///     }
///     executeStage(stage);
/// }
/// ```
class RuntimeBudgetGuard {
public:
    explicit RuntimeBudgetGuard(const RuntimeBudget& budget) noexcept
        : budget_(budget)
        , startTime_(std::chrono::steady_clock::now())
    {
    }

    /// @brief 检查当前阶段是否超预算
    [[nodiscard]] BudgetCheckResult checkBudget(BudgetStage stage) const noexcept
    {
        BudgetCheckResult result;
        const auto now = std::chrono::steady_clock::now();
        result.elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - startTime_).count();
        result.remainingMilliseconds = budget_.timeoutMilliseconds - result.elapsedMilliseconds;
        result.exceeded = result.elapsedMilliseconds > budget_.timeoutMilliseconds;

        (void)stage; // 保留用于未来按阶段配置不同预算
        return result;
    }

    /// @brief 内存预算检查（设计文档 Section 6.2）
    [[nodiscard]] bool exceedsMemoryBudget(uint64_t requestedBytes) const noexcept
    {
        // 安全线 80% 上限
        static constexpr double kSafetyRatio = 0.8;
        const uint64_t safetyLimit = static_cast<uint64_t>(
            static_cast<double>(budget_.memoryLimitBytes) * kSafetyRatio);
        return requestedBytes > safetyLimit;
    }

    /// @brief 获取已耗时（毫秒）
    [[nodiscard]] int64_t elapsedMilliseconds() const noexcept
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime_).count();
    }

    /// @brief 获取预算总时间
    [[nodiscard]] const RuntimeBudget& budget() const noexcept { return budget_; }

private:
    const RuntimeBudget& budget_;
    std::chrono::steady_clock::time_point startTime_;
};

} // namespace factor::compute