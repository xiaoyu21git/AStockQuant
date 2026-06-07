#pragma once

#include <cstdint>
#include <functional>

namespace domain::scheduler {

/// @brief 资源使用级别枚举（文档 8.5 要求支持降速策略）
enum class ResourceLevel : std::uint8_t {
    Normal = 0,
    Warning = 1,
    Throttling = 2,
    Suspended = 3,
};

/// @brief 资源治理快照
struct ResourceSnapshot final {
    double cpuUsagePercent{0.0};
    std::uint64_t memoryUsedBytes{0U};
    std::uint64_t memoryLimitBytes{0U};
    ResourceLevel currentLevel{ResourceLevel::Normal};
};

/// @brief 系统级资源管控治理器
///
/// 约束（文档 8.4-8.6）：
/// - 仅负责策略层面调度（分批次、资源上限、降速），
///   不实现 OS 级 CPU 亲和性设置（由平台启动脚本负责）。
/// - 依赖组件全部已就绪：
///   - factor::compute::RuntimeBudgetGuard（阶段时间+内存预算）
///   - factor::compute::OptimizerTimeoutGuard（优化器超时治理）
///   - factor::compute::AsyncDiagnosticsWriter（异步诊断落盘）
///
/// 使用方式：
/// ```cpp
/// ResourceGovernor governor(memoryLimitBytes);
/// for (auto& batch : batches) {
///     if (governor.shouldThrottle()) {
///         governor.throttleYield();
///         continue;
///     }
///     executeBatch(batch);
/// }
/// ```
class ResourceGovernor final {
public:
    static constexpr double kDefaultMemoryRatio = 0.70;
    static constexpr double kDefaultCpuRatio = 0.80;
    static constexpr std::uint64_t kDefaultBatchSize = 500U;

    explicit ResourceGovernor(std::uint64_t memoryLimitBytes = 0U) noexcept
        : memoryLimitBytes_(memoryLimitBytes)
        , memoryLimitInternal_(memoryLimitBytes > 0U
            ? static_cast<std::uint64_t>(static_cast<double>(memoryLimitBytes) * kDefaultMemoryRatio)
            : 0U)
    {
    }

    /// @brief 设置当前内存使用量，返回治理级别
    [[nodiscard]] ResourceLevel updateMemoryUsage(std::uint64_t usedBytes) noexcept
    {
        currentMemoryUsed_ = usedBytes;
        return evaluateLevel();
    }

    /// @brief 检查是否需要降速
    [[nodiscard]] bool shouldThrottle() const noexcept
    {
        return currentLevel_ >= ResourceLevel::Throttling;
    }

    /// @brief 检查是否应暂停
    [[nodiscard]] bool shouldSuspend() const noexcept
    {
        return currentLevel_ == ResourceLevel::Suspended;
    }

    /// @brief 获取当前资源快照
    [[nodiscard]] ResourceSnapshot snapshot() const noexcept
    {
        ResourceSnapshot s;
        s.cpuUsagePercent = currentCpuPercent_;
        s.memoryUsedBytes = currentMemoryUsed_;
        s.memoryLimitBytes = memoryLimitInternal_;
        s.currentLevel = currentLevel_;
        return s;
    }

    /// @brief 获取当前治理级别
    [[nodiscard]] ResourceLevel currentLevel() const noexcept { return currentLevel_; }

    /// @brief 分批迭代回调类型
    /// batchIndex: 批次序号(0-based)
    /// batchSize: 本批处理数量
    /// totalCount: 总待处理数量
    using BatchProgressCallback = std::function<void(std::size_t batchIndex,
                                                      std::size_t batchSize,
                                                      std::size_t totalCount)>;

    /// @brief 计算分批参数
    /// @return 总批次数
    [[nodiscard]] std::size_t computeBatchCount(
        std::size_t totalItemCount,
        std::size_t batchSize = kDefaultBatchSize) const noexcept
    {
        if (batchSize == 0U || totalItemCount == 0U) {
            return 0U;
        }
        return (totalItemCount + batchSize - 1U) / batchSize;
    }

private:
    [[nodiscard]] ResourceLevel evaluateLevel() const noexcept
    {
        if (memoryLimitInternal_ == 0U) {
            return ResourceLevel::Normal;
        }
        if (currentMemoryUsed_ >= memoryLimitInternal_) {
            return ResourceLevel::Suspended;
        }
        const double ratio = static_cast<double>(currentMemoryUsed_)
            / static_cast<double>(memoryLimitInternal_);
        if (ratio >= kDefaultCpuRatio) {
            return ResourceLevel::Throttling;
        }
        if (ratio >= 0.50) {
            return ResourceLevel::Warning;
        }
        return ResourceLevel::Normal;
    }

    std::uint64_t memoryLimitBytes_;
    std::uint64_t memoryLimitInternal_;
    std::uint64_t currentMemoryUsed_{0U};
    double currentCpuPercent_{0.0};
    ResourceLevel currentLevel_{ResourceLevel::Normal};
};

} // namespace domain::scheduler