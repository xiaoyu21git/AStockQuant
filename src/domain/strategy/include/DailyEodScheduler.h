#pragma once
// DailyEodScheduler — 日频策略收盘前下单调度器
// 职责: 管理下单窗口、交易日计算、EOD回调订阅、m_lastEvalDay持久化
// 零定时器, MarketDataService EOD 回调驱动

#include <cstdint>
#include <functional>
#include <string>

namespace domain::strategy {

class DailyEodScheduler {
public:
    /// @brief 策略评估回调 (tradingDay, isCompensation)
    /// 所在线程: 策略专用线程
    using EvalFn = std::function<void(const std::string& tradingDay, bool isCompensation)>;

    /// @brief 投递任务到策略线程
    using PostFn = std::function<void(std::function<void()>)>;

    /// @param postToStrategyThread 投递闭包到策略线程
    /// @param persistPath m_lastEvalDay 持久化文件路径
    DailyEodScheduler(PostFn postToStrategyThread, const std::string& persistPath);
    ~DailyEodScheduler();

    void setEvalCallback(EvalFn fn) { m_evalFn = std::move(fn); }

    /// @brief 启动: 加载持久化状态 → 检查补单窗口 → 注册 MarketDataService EOD
    void start();

    /// @brief 停止: 注销 EOD 回调
    void stop();

private:
    // ── MarketDataService EOD 回调 (gmsdk 线程) ──
    void onEodTrigger(const std::string& tradingDay);

    // ── 策略线程评估入口 ──
    void doEvaluate(const std::string& tradingDay);

    // ── 交易日工具 ──
    static std::string getPreviousTradingDay(const std::string& date);

    // ── 时间窗口 ──
    static int  getCurrentLocalMinutes();
    static std::int64_t getCurrentTradingDay();
    static bool isCompensationWindow() { return getCurrentLocalMinutes() < kCompensationEnd; }
    static bool isPreCloseWindow()     { auto m = getCurrentLocalMinutes(); return m >= kPreCloseStart && m < kPreCloseEnd; }

    // ── 持久化 ──
    void loadLastEvalDay();
    void persistLastEvalDay();

    PostFn m_post;
    EvalFn m_evalFn;
    std::int64_t m_lastEvalDay = 0;
    std::string  m_persistPath;
    bool m_eodRegistered = false;

    static constexpr int kPreCloseStart   = 901;  // 15:01
    static constexpr int kPreCloseEnd     = 930;  // 15:30
    static constexpr int kCompensationEnd = 570;  // 09:30
};

} // namespace domain::strategy
