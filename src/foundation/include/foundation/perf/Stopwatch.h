#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// foundation::perf::Stopwatch — 常驻累计计时器
//
// 设计约束 (性能链路专用):
//   - 默认只累计不打印, 显式 report() 才输出 [PERF] 台账条目,
//     避免高频打点在 INFO 锁上引入 5%~10% 额外延迟
//   - 多次 start()/stop() 配对累计 (适合循环内逐块计时)
//   - 单调时钟, 不受系统时间调整影响
// ══════════════════════════════════════════════════════════════════════════════

#include "foundation/log/logging.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace foundation::perf {

/// @brief 累计型计时器 (start/stop 配对累计, 只在汇总点显式输出)
class Stopwatch {
public:
    Stopwatch() = default;

    /// @brief 开始计时 (覆盖未配对的上一次起点)
    void start() noexcept
    {
        m_lastStart = m_clock.now();
        m_running = true;
    }

    /// @brief 停止计时并累计本段时长
    void stop() noexcept
    {
        if (!m_running) return;
        m_accumulatedNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
                               m_clock.now() - m_lastStart)
                               .count();
        m_running = false;
    }

    /// @brief 累计时长 (毫秒)
    [[nodiscard]] double elapsedMilliseconds() const noexcept
    {
        return static_cast<double>(m_accumulatedNs) / 1e6;
    }

    /// @brief 重置累计
    void reset() noexcept
    {
        m_accumulatedNs = 0;
        m_running = false;
    }

    /// @brief 显式输出 [PERF] 台账条目 (仅在块级/阶段级汇总点调用)
    void report(const std::string& label) const
    {
        INTERNAL_INFO_STREAM << "[PERF] " << label << ": "
            << elapsedMilliseconds() << " ms";
    }

private:
    using Clock = std::chrono::steady_clock;

    Clock m_clock{};
    Clock::time_point m_lastStart{};
    std::int64_t m_accumulatedNs = 0;
    bool m_running = false;
};

} // namespace foundation::perf
