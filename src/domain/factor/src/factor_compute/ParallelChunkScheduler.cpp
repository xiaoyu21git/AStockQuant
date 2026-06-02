#include "factor_compute/ParallelChunkScheduler.h"

#include <algorithm>
#include <cstdint>

namespace factor::compute {

int32_t ParallelChunkScheduler::computeInstrumentChunkSize(
    int32_t fieldCount, ChunkPolicy policy) const noexcept
{
    if (fieldCount <= 0) {
        fieldCount = 1;
    }

    // 设计文档 Section 6.3：
    // 标的块大小按 L2_cache_bytes / (field_count * sizeof(double)) 估算
    const uint64_t bytesPerRow = static_cast<uint64_t>(fieldCount) * sizeof(double);
    int32_t optimalChunkSize = 0;
    if (bytesPerRow > 0U) {
        optimalChunkSize = static_cast<int32_t>(l2CacheBytes_ / bytesPerRow);
    }

    // 向下对齐到 SIMD 友好边界 (kSimdAlignmentSize = 4)
    if (optimalChunkSize < kSimdAlignmentSize) {
        optimalChunkSize = kSimdAlignmentSize;
    }
    optimalChunkSize = (optimalChunkSize / kSimdAlignmentSize) * kSimdAlignmentSize;

    // 上界：使用用户配置的块大小
    if (optimalChunkSize > static_cast<int32_t>(policy.instrumentChunkSize)) {
        optimalChunkSize = static_cast<int32_t>(policy.instrumentChunkSize);
    }

    // 下界：至少 1 个标的
    if (optimalChunkSize < 1) {
        optimalChunkSize = 1;
    }

    return optimalChunkSize;
}

ParallelChunkPlan ParallelChunkScheduler::buildPlan(
    int32_t dateCount,
    int32_t instrumentCount,
    int32_t fieldCount,
    ChunkPolicy chunkPolicy) const noexcept
{
    ParallelChunkPlan plan;
    plan.totalDateCount = dateCount;
    plan.totalInstrumentCount = instrumentCount;

    if (dateCount <= 0 || instrumentCount <= 0) {
        return plan;
    }

    // 设计文档 Section 6.3 约束：
    // - 日期块优先，保障时间窗口算子复用和缓存局部性。
    // - 分块顺序固定：先按日期块排序，再按标的块排序。

    const int32_t dateChunkSize = static_cast<int32_t>(chunkPolicy.dateChunkSize);
    const int32_t instChunkSize = computeInstrumentChunkSize(fieldCount, chunkPolicy);

    // 预分配块数估算
    const int32_t dateBlockCount = (dateCount + dateChunkSize - 1) / dateChunkSize;
    const int32_t instBlockCount = (instrumentCount + instChunkSize - 1) / instChunkSize;
    plan.blocks.reserve(static_cast<size_t>(dateBlockCount) * static_cast<size_t>(instBlockCount));

    for (int32_t dateStart = 0; dateStart < dateCount; dateStart += dateChunkSize) {
        const int32_t remainingDates = dateCount - dateStart;
        const int32_t actualDateCount = (remainingDates < dateChunkSize) ? remainingDates : dateChunkSize;

        for (int32_t instStart = 0; instStart < instrumentCount; instStart += instChunkSize) {
            const int32_t remainingInsts = instrumentCount - instStart;
            const int32_t actualInstCount = (remainingInsts < instChunkSize) ? remainingInsts : instChunkSize;

            ChunkBlock block;
            block.dateStart = dateStart;
            block.dateCount = actualDateCount;
            block.instrumentStart = instStart;
            block.instrumentCount = actualInstCount;
            plan.blocks.push_back(block);
        }
    }

    return plan;
}

} // namespace factor::compute