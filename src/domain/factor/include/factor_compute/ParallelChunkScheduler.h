#pragma once

#include "FactorSignalTypes.h"
#include "IFactorOperatorLibrary.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace factor::compute {

/// @brief P4-T1：二维并行调度器（设计文档 Section 6.3）
///
/// 分块策略约束：
/// - 日期块优先，保障时间窗口算子复用和缓存局部性。
/// - 标的块次级切分，块大小按 L2_cache_bytes / (field_count * sizeof(double)) 估算
///   并向下对齐到 SIMD 友好边界。
/// - 块大小必须是可配置项。
///
/// 并行归约协议（强制）：
/// - 分块顺序固定：先按日期块排序，再按标的块排序。
/// - 归约顺序固定：同一输出单元总是按递增 instrument index 归并。
/// - 浮点归约统一使用 double 并采用固定二叉树归约拓扑。
/// - 相同输入、相同版本、相同线程配置下输出位级可复现。
struct ChunkBlock final {
    int32_t dateStart{0};
    int32_t dateCount{0};
    int32_t instrumentStart{0};
    int32_t instrumentCount{0};

    [[nodiscard]] bool isValid() const noexcept
    {
        return dateCount > 0 && instrumentCount > 0;
    }
};

/// @brief 二维并行分块计划
struct ParallelChunkPlan final {
    int32_t totalDateCount{0};
    int32_t totalInstrumentCount{0};
    std::vector<ChunkBlock> blocks;

    /// @brief 总块数
    [[nodiscard]] size_t blockCount() const noexcept
    {
        return blocks.size();
    }

    [[nodiscard]] bool isValid() const noexcept
    {
        return totalDateCount > 0
            && totalInstrumentCount > 0
            && !blocks.empty();
    }
};

/// @brief 二维并行分块调度器
///
/// 使用方式：
/// ```cpp
/// ParallelChunkScheduler scheduler;
/// auto plan = scheduler.buildPlan(dateCount, instCount, chunkPolicy);
/// for (const auto& block : plan.blocks) {
///     processBlock(block);
/// }
/// ```
class ParallelChunkScheduler {
public:
    /// @brief 默认 L2 缓存估算值（可配置）
    static constexpr uint64_t kDefaultL2CacheBytes = 256ULL * 1024ULL; // 256 KB

    /// @brief 默认 SIMD 对齐大小（AVX-256 双精度 = 4 个 double）
    static constexpr int32_t kSimdAlignmentSize = 4;

    /// @brief 构造调度器
    ///
    /// @param l2CacheBytes L2 缓存大小（字节），用于估算标的块大小
    explicit ParallelChunkScheduler(uint64_t l2CacheBytes = kDefaultL2CacheBytes) noexcept
        : l2CacheBytes_(l2CacheBytes)
    {
    }

    /// @brief 构建二维分块计划
    ///
    /// @param dateCount 总日期数
    /// @param instrumentCount 总标的数
    /// @param fieldCount 要处理的字段数（用于内存估算）
    /// @param chunkPolicy 分块策略（可配置块大小）
    /// @return 分块计划，包含按固定顺序排列的块列表
    [[nodiscard]] ParallelChunkPlan buildPlan(
        int32_t dateCount,
        int32_t instrumentCount,
        int32_t fieldCount,
        ChunkPolicy chunkPolicy) const noexcept;

    /// @brief 获取当前 L2 缓存配置
    [[nodiscard]] uint64_t l2CacheBytes() const noexcept { return l2CacheBytes_; }

    /// @brief 设置 L2 缓存配置
    void setL2CacheBytes(uint64_t bytes) noexcept { l2CacheBytes_ = bytes; }

private:
    /// @brief 计算最优的标的块大小
    [[nodiscard]] int32_t computeInstrumentChunkSize(
        int32_t fieldCount,
        ChunkPolicy policy) const noexcept;

    uint64_t l2CacheBytes_;
};

} // namespace factor::compute