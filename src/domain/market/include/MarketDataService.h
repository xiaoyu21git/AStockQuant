#pragma once
// ═════════════════════════════════════════════════════════════════════════
// MarketDataService — tick → LiveData 更新 (纯 C++，零 Qt)
//
// 职责: 接收 GmSessionEngine 推送的实时 tick，分发给各标的 LiveData。
// 不查数据库、不碰策略、不生成信号。
//
// v2: tradingDay 变更检测 → 触发日终回调 (日频策略用)
// ═════════════════════════════════════════════════════════════════════════

// 前向声明，避免域层依赖引擎层
namespace engine { struct GmTickData; }

#include "LiveData.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace domain::market {

/// @brief 行情数据服务（单例）
///
/// GmSessionEngine::on_tick() 调用 onTick()，内部更新对应标的的
/// LiveData（日K + 各分钟周期K线）。
///
/// 外部查询：liveData(symbol) 返回 LiveData 引用，读取一线数据。
///
/// 日终回调: tradingDay 变更时自动调用所有注册的回调函数，
/// 通知日频策略"昨天的日 Bar 已封口，可以评估"。
class MarketDataService final {
public:
    using EndOfDayCallback = std::function<void(const std::string& closedTradingDay)>;

    static MarketDataService& instance();

    /// @brief 接收单笔 tick，更新对应标的的全部 K 线
    /// 内部检测 tradingDay 变更 → 自动触发所有 EndOfDay 回调
    void onTick(const engine::GmTickData& td);

    /// @brief 注册日终回调（日频策略评估入口）
    /// 回调在 onTick() 上下文中同步调用，必须轻量（不应阻塞线程）
    void registerEndOfDayCallback(EndOfDayCallback cb);

    /// @brief 获取当前追踪的交易日（0 表示尚未收到任何 tick）
    [[nodiscard]] std::int64_t activeTradingDay() const noexcept { return m_activeTradingDay; }

    /// @brief 获取某标的的实时行情（不存在则创建空数据）
    [[nodiscard]] const LiveData& liveData(const std::string& symbol) const;

    /// @brief 获取某标的的实时行情（可写，仅 MarketDataService 使用）
    LiveData& mutableLiveData(const std::string& symbol);

    /// @brief 已追踪的标的列表
    [[nodiscard]] std::vector<std::string> symbols() const;

    // ── Tick 断点检测 ──

    /// @brief 距全局最后一次 tick 的秒数（-1 表示从未收到 tick）
    [[nodiscard]] double secondsSinceLastTick() const noexcept;

    /// @brief 返回停滞超过 thresholdSec 秒的标的及停滞秒数
    [[nodiscard]] std::vector<std::pair<std::string, double>>
    tickStalledSymbols(double thresholdSec = 5.0) const;

    /// @brief 全局 tick 总数
    [[nodiscard]] std::uint64_t totalTickCount() const noexcept { return m_totalTicks; }

private:
    MarketDataService() = default;

    /// @brief 遍历所有标的，将当日 dailyBar 做快照封存（供日终评估）
    /// 在 tradingDay 切换时，所有 LiveData 的 dailyBar 就是最终形态
    void fireEndOfDayCallbacks(std::int64_t closedTradingDay);

    mutable std::mutex mutex_;
    mutable std::unordered_map<std::string, LiveData> data_;

    std::int64_t m_activeTradingDay = 0;
    std::vector<EndOfDayCallback> m_eodCallbacks;

    // Tick 断点检测
    using Clock = std::chrono::steady_clock;
    Clock::time_point m_lastGlobalTickTime{};
    mutable std::unordered_map<std::string, Clock::time_point> m_lastTickTimeBySymbol;
    std::uint64_t m_totalTicks = 0;
};

} // namespace domain::market
