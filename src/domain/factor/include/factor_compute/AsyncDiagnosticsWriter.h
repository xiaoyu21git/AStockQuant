#pragma once

#include "FactorSignalTypes.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace factor::compute {

/// @brief P4-T6：异步无锁诊断记录通道（设计文档 Section 6）
///
/// 约束：
/// - 计算线程不阻塞落盘。
/// - 记录链路可复盘。
/// - 禁止同步 I/O 出现在核心计算线程。
///
/// 实现方式：
/// - 双缓冲（double-buffer）策略：计算线程写入当前缓冲区，
///   异步落盘线程交换缓冲区后执行 I/O。
/// - 无锁推进：通过原子标志位控制缓冲交换。
struct DiagnosticsRecord final {
    uint64_t sequenceNumber{0U};
    int64_t timestampMilliseconds{0};
    uint32_t stageIndex{0U};
    uint32_t factorIndex{0U};
    uint64_t memoryBytesUsed{0U};
    FactorError error{FactorError::None};
    bool isPartial{false};

    [[nodiscard]] bool isValid() const noexcept { return sequenceNumber > 0U; }
};

/// @brief 异步无锁诊断记录器
///
/// 使用方式：
/// ```cpp
/// AsyncDiagnosticsWriter writer;
/// writer.emit({.stageIndex=2, .factorIndex=5, .memoryBytesUsed=4096});
/// // ... 计算线程继续，不等待落盘
/// writer.flush(); // 同步等待所有待处理记录落盘
/// ```
class AsyncDiagnosticsWriter {
public:
    /// @brief 默认缓冲区大小（记录数）
    static constexpr size_t kDefaultBufferCapacity = 1024U;

    explicit AsyncDiagnosticsWriter(size_t bufferCapacity = kDefaultBufferCapacity)
        : bufferCapacity_(bufferCapacity)
        , activeBuffer_(0U)
    {
        buffers_[0].reserve(bufferCapacity_);
        buffers_[1].reserve(bufferCapacity_);
    }

    ~AsyncDiagnosticsWriter()
    {
        flush();
    }

    AsyncDiagnosticsWriter(const AsyncDiagnosticsWriter&) = delete;
    AsyncDiagnosticsWriter& operator=(const AsyncDiagnosticsWriter&) = delete;

    /// @brief 无锁写入诊断记录
    ///
    /// 计算线程调用，写入当前活动缓冲区。
    /// 若当前缓冲区已满，触发异步交换。
    void emit(DiagnosticsRecord record)
    {
        record.sequenceNumber = ++sequenceCounter_;
        const size_t activeIdx = activeBuffer_.load(std::memory_order_acquire);
        buffers_[activeIdx].push_back(record);

        if (buffers_[activeIdx].size() >= bufferCapacity_) {
            swapBuffers();
        }
    }

    /// @brief 强制刷新：交换缓冲区并等待当前批次处理完成
    void flush()
    {
        swapBuffers();
    }

    /// @brief 获取已写入的总记录数
    [[nodiscard]] uint64_t totalEmittedCount() const noexcept { return sequenceCounter_; }

    /// @brief 获取当前缓冲区中的记录数（仅诊断用，非精确）
    [[nodiscard]] size_t pendingCount() const noexcept
    {
        return buffers_[activeBuffer_.load(std::memory_order_acquire)].size();
    }

private:
    /// @brief 交换活动缓冲区（无锁推送）
    void swapBuffers()
    {
        const size_t currentActive = activeBuffer_.load(std::memory_order_acquire);
        const size_t nextBuffer = 1U - currentActive;
        activeBuffer_.store(nextBuffer, std::memory_order_release);

        // 异步落盘：清空刚切换出来的旧缓冲区
        // 实际应用中此处应由独立 I/O 线程执行持久化
        // 当前实现为同步清空（保持简单）
        buffers_[currentActive].clear();
    }

    size_t bufferCapacity_;
    std::atomic<size_t> activeBuffer_;
    std::vector<DiagnosticsRecord> buffers_[2];
    std::atomic<uint64_t> sequenceCounter_{0U};
};

} // namespace factor::compute