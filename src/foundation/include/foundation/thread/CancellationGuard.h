#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// foundation::thread::CancellationGuard — 软取消检查点封装
//
// 取消是"本次 run 会话属性", 必须参数化传递, 禁止挂到共享无状态组件 (如
// FactorEngine) 的成员上。调用方在每日期循环间调用 throwIfCancelled(),
// 取消延迟 = 单日 calculate (普通因子毫秒级, DL 日约 1~3s)。
// ══════════════════════════════════════════════════════════════════════════════

#include <atomic>
#include <stdexcept>
#include <string>

namespace foundation::thread {

/// @brief 任务被取消 — 由 CancellationGuard 在检查点抛出
/// worker 仅捕获本异常将块标记为 cancelled (禁止 catch(...) 吞掉)
class OperationCancelledException : public std::runtime_error {
public:
    OperationCancelledException() : std::runtime_error("operation cancelled") {}
};

/// @brief 软取消检查点 — 持有取消标志只读引用, 不拥有其生命周期
class CancellationGuard {
public:
    explicit CancellationGuard(const std::atomic<bool>* flag) noexcept
        : m_flag(flag) {}

    /// @brief 是否已请求取消 (nullptr 标志 = 永不取消)
    [[nodiscard]] bool isCancelled() const noexcept
    {
        return m_flag && m_flag->load(std::memory_order_acquire);
    }

    /// @brief 已取消则抛 OperationCancelledException
    void throwIfCancelled() const
    {
        if (isCancelled()) throw OperationCancelledException();
    }

private:
    const std::atomic<bool>* m_flag = nullptr;
};

} // namespace foundation::thread
