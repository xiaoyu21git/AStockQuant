#pragma once
// DailyEodScheduler — 日频策略收盘前下单调度器
// 职责: 管理下单窗口、交易日计算、EOD回调订阅、m_lastEvalDay持久化
// 零定时器, MarketDataService EOD 回调驱动

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace foundation::thread { class ThreadPoolExecutor; }

namespace domain::strategy {

/// @brief 日终评估结果状态
enum class EodEvaluationStatus {
    Submitted,    // 篮子已提交，至少一笔订单成功发出
    NoSignal,     // 策略评估后无交易信号
    AllRejected,  // 有信号但全部被风控/资金拒绝
    Skipped,      // 评估被跳过（非调仓日/回测/无标的）
    Error         // 异常中断
};

class DailyEodScheduler {
public:
    /// @brief 策略评估回调 (tradingDay, isCompensation) → 评估结果
    /// 所在线程: 策略专用线程
    using EvalFn = std::function<EodEvaluationStatus(const std::string& tradingDay, bool isCompensation)>;

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

    // 设置下单窗口(格式 "HH:MM"),未调用时使用默认值 15:00-15:30
    /// @brief 设置 EOD 触发时间(格式 "HH:MM")，策略每天在此时间触发日频评估下单
    void setEodTriggerTime(const std::string& time);

    /// @brief 设置当前策略 ID，用于在统一 JSON 文件中按策略 ID 键读写 lastEvalDay
    void setStrategyId(std::string id) { m_strategyId = std::move(id); }

private:
    void onEodTrigger(const std::string& tradingDay);
    void doEvaluate(const std::string& tradingDay);
    static std::string getPreviousTradingDay(const std::string& date);
    static int  getCurrentLocalMinutes();
    static std::int64_t getCurrentTradingDay();
    static bool isCompensationWindow() { return getCurrentLocalMinutes() < kCompensationEnd; }
    void loadLastEvalDay();
    void persistLastEvalDay();

    PostFn m_post;
    EvalFn m_evalFn;
    std::atomic<std::int64_t> m_lastEvalDay{0};
    std::string  m_persistPath;   // 统一 JSON 文件的全路径 (strategy_last_eval.json)
    std::string  m_strategyId;    // 当前策略 ID，JSON 中的键
    std::atomic<bool> m_eodRegistered{false};
    int m_eodTriggerMinute{900};         // EOD 触发时间(分钟, 默认 15:00)
    std::atomic<bool> m_polling{false};
    std::shared_ptr<foundation::thread::ThreadPoolExecutor> m_pollExecutor;
    void schedulePollCheck();            // 向线程池投递下次轮询检查
    static constexpr int kCompensationEnd = 570;
};

} // namespace domain::strategy
