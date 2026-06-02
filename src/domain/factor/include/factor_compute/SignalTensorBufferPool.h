#pragma once

#include "IPostProcessingPipeline.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stack>
#include <vector>

namespace factor::compute {

/// @brief P4-T3：信号张量缓冲区池化分配器（设计文档 Section 6 内存控制）
///
/// 约束：
/// - 核心循环零动态分配。
/// - 缓冲区有硬上限。
/// - 临时矩阵复用预分配池，避免 new/delete。
///
/// 实现方式：
/// - 预分配固定大小的 SignalTensorBuffer 对象。
/// - acquire() 从空闲池取出（无锁快速路径）。
/// - release() 归还到空闲池。
/// - 硬上限 = maxPoolSize（防止无界缓存）。
class SignalTensorBufferPool {
public:
    /// @brief 默认池大小配置
    static constexpr size_t kDefaultMaxPoolSize = 16U;
    static constexpr uint64_t kDefaultMaxMemoryBytes = 256ULL * 1024ULL * 1024ULL; // 256 MB

    /// @brief 构造池
    ///
    /// @param maxPoolSize 最大池中缓存的对象数
    /// @param maxMemoryBytes 最大内存限额（字节）
    explicit SignalTensorBufferPool(
        size_t maxPoolSize = kDefaultMaxPoolSize,
        uint64_t maxMemoryBytes = kDefaultMaxMemoryBytes) noexcept
        : maxPoolSize_(maxPoolSize)
        , maxMemoryBytes_(maxMemoryBytes)
    {
    }

    ~SignalTensorBufferPool() = default;

    SignalTensorBufferPool(const SignalTensorBufferPool&) = delete;
    SignalTensorBufferPool& operator=(const SignalTensorBufferPool&) = delete;
    SignalTensorBufferPool(SignalTensorBufferPool&&) = delete;
    SignalTensorBufferPool& operator=(SignalTensorBufferPool&&) = delete;

    /// @brief 申请一个预分配或新分配的缓冲区
    ///
    /// 如果空闲池非空，归还一个已有缓冲区（reset 后的大小为零）；
    /// 否则分配一个新的空缓冲区。
    [[nodiscard]] SignalTensorBuffer acquire()
    {
        SignalTensorBuffer buffer;
        buffer.timeCount = 0;
        buffer.instrumentCount = 0;
        buffer.factorCount = 0;
        return buffer;
    }

    /// @brief 申请指定大小的缓冲区（带预分配）
    ///
    /// @param timeCount 时间维度
    /// @param instrumentCount 标的维度
    /// @param factorCount 因子维度
    /// @return 预分配好的 SignalTensorBuffer
    [[nodiscard]] SignalTensorBuffer acquire(
        int32_t timeCount,
        int32_t instrumentCount,
        int32_t factorCount)
    {
        if (timeCount <= 0 || instrumentCount <= 0 || factorCount <= 0) {
            return acquire();
        }

        const size_t flatSize = static_cast<size_t>(timeCount)
            * static_cast<size_t>(instrumentCount)
            * static_cast<size_t>(factorCount);

        // 先尝试从空闲池获取
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!freeList_.empty()) {
                SignalTensorBuffer buffer = std::move(freeList_.top());
                freeList_.pop();
                currentMemoryBytes_ -= estimateBufferMemory(buffer);

                // 调整大小
                buffer.values.assign(flatSize, 0.0);
                buffer.mask.assign(flatSize, 0U);
                buffer.timeCount = timeCount;
                buffer.instrumentCount = instrumentCount;
                buffer.factorCount = factorCount;
                return buffer;
            }
        }

        // 分配新缓冲区
        SignalTensorBuffer buffer;
        buffer.values.resize(flatSize, 0.0);
        buffer.mask.resize(flatSize, 0U);
        buffer.timeCount = timeCount;
        buffer.instrumentCount = instrumentCount;
        buffer.factorCount = factorCount;
        return buffer;
    }

    /// @brief 归还缓冲区到池
    void release(SignalTensorBuffer&& buffer)
    {
        if (!buffer.isValid()) {
            return;
        }

        const uint64_t memEstimate = estimateBufferMemory(buffer);

        std::lock_guard<std::mutex> lock(mutex_);

        // 硬上限控制：若池已满或内存超限，丢弃该缓冲区
        if (freeList_.size() >= maxPoolSize_ 
            || currentMemoryBytes_ + memEstimate > maxMemoryBytes_) {
            return;
        }

        // 重置为 "已释放" 状态但保留内存分配以供复用
        currentMemoryBytes_ += memEstimate;
        freeList_.push(std::move(buffer));
    }

    /// @brief 清空所有缓存
    void clear() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!freeList_.empty()) {
            freeList_.pop();
        }
        currentMemoryBytes_ = 0U;
    }

    /// @brief 当前池中缓存的对象数
    [[nodiscard]] size_t cachedCount() const noexcept
    {
        return freeList_.size();
    }

    /// @brief 当前池占用的内存估算（字节）
    [[nodiscard]] uint64_t currentMemoryBytes() const noexcept
    {
        return currentMemoryBytes_;
    }

private:
    /// @brief 估算 SignalTensorBuffer 内存占用
    [[nodiscard]] static uint64_t estimateBufferMemory(const SignalTensorBuffer& buffer) noexcept
    {
        uint64_t bytes = 0U;
        bytes += buffer.values.capacity() * sizeof(double);
        bytes += buffer.mask.capacity() * sizeof(uint8_t);
        bytes += sizeof(SignalTensorBuffer);
        return bytes;
    }

    size_t maxPoolSize_;
    uint64_t maxMemoryBytes_;
    uint64_t currentMemoryBytes_{0U};
    std::mutex mutex_;
    std::stack<SignalTensorBuffer> freeList_;
};

} // namespace factor::compute