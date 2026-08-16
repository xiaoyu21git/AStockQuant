#pragma once
// EvaluationScheduler — 策略评估调度器 (P1, ADR-002/003/006)
// IEvaluationScheduler 公共接口, 两种实现:
//   CronEvaluationScheduler   日/周/月评估: 交易日固定窗口触发 + 次日补单窗口 + EOD 回调兜底
//   IntervalEvaluationScheduler 1min/5min 评估: K线周期边界+5s 轮询触发 (ADR-003, 非 tick 级回调)
// EvalFn 返回 EvalResult (P1 门禁); 持久化键 lastEvalDay.<strategyId>.<period> 含周期维度
// 回调生命周期: MarketDataService::registerEndOfDayCallback 返回 token, stop() 真注销

#include "EvalTypes.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace foundation::thread { class ThreadPoolExecutor; }
namespace astock::infrastructure::database { class AppStateStore; }

namespace domain::strategy {

/// @brief 评估调度参数 (P6: 时间参数必须读配置文件, 零兜底零硬编码)
/// 默认值为无效哨兵 0: isValid() 不过 → 调度器拒绝启动 (不静默用硬编码时间顶替)
struct EvalScheduleConfig {
    int triggerMinute{0};          // EOD 触发分钟 (14:50 = 890; 配置键 eodTriggerTime "HH:MM")
    int triggerWindowMinutes{0};   // 触发窗口宽度 [triggerMinute, triggerMinute+width) (配置键 triggerWindowMinutes)
    int compensationEndMinute{0};  // 补单窗口结束分钟 (09:30; 配置键 compensationEndTime "HH:MM")
    int eodCallbackStartMinute{0}; // EOD 回调窗口开始 (配置键 eodCallbackStartTime "HH:MM", 默认 14:50)
    int eodCallbackEndMinute{0};   // EOD 回调窗口结束 (配置键 eodCallbackEndTime "HH:MM", 默认 15:00)
    int pollIntervalSec{30};       // 未到触发时间时的轮询步长 (轮询节律, 非配置时间)
    int postTriggerWaitSec{3600};  // 触发后的低频等待 (60min)
    int evaluatedIdleWaitSec{1800};// 当日已评估后的低频等待 (30min)

    /// @brief 严格校验: 五个时间 > 0; 触发时间晚于补单结束; 触发窗口不跨天 (trigger+width<=1440);
    /// 回调窗口 start < end
    [[nodiscard]] bool isValid() const noexcept;

    /// @brief 从 config/trading_connection.json 读取时间参数 (零兜底)
    /// 文件不可用/任一键缺失/非法 → ERROR + nullopt (Facade 拒绝启动调度器)
    [[nodiscard]] static std::optional<EvalScheduleConfig> loadFromTradingConfig();
};

/// @brief 调仓节奏判定 (C6 裁定: 触发≠调仓; 是否调仓由管道 checkRebalance 判定, P1 间隔语义仍在引擎)
class ITriggerPolicy {
public:
    virtual ~ITriggerPolicy() = default;

    /// @brief 该交易日是否允许调度器触发评估
    /// DailyPolicy: 每交易日恒触发 (interval 调仓判定在引擎/管道侧, C6);
    /// Weekly/MonthlyPolicy: 仅配置日触发 (C5: 补单窗口按被补单交易日重算, 同一入口)
    [[nodiscard]] virtual bool isTriggerDay(const std::string& tradingDay) const = 0;
};

/// @brief 日频策略: 每交易日触发 (intervalDays 供 P2 管道 checkRebalance 接线, §2 轴2)
class DailyTriggerPolicy final : public ITriggerPolicy {
public:
    explicit DailyTriggerPolicy(int intervalDays = 1) : m_intervalDays(intervalDays) {}

    [[nodiscard]] bool isTriggerDay(const std::string&) const override { return true; }
    [[nodiscard]] int intervalDays() const noexcept { return m_intervalDays; }

private:
    int m_intervalDays;  // P2 管道 checkRebalance 消费 (现 m_rebalanceInterval 语义)
};

/// @brief 周频策略: 仅配置星期几触发 (1=周一..7=周日)
class WeeklyTriggerPolicy final : public ITriggerPolicy {
public:
    explicit WeeklyTriggerPolicy(std::vector<int> weekdays) : m_weekdays(std::move(weekdays)) {}

    [[nodiscard]] bool isTriggerDay(const std::string& tradingDay) const override;
    [[nodiscard]] const std::vector<int>& weekdays() const noexcept { return m_weekdays; }

private:
    /// @brief YYYYMMDD → 星期 (1=周一..7=周日, Zeller 公式), 无效输入返回 0
    static int weekdayOf(const std::string& yyyymmdd) noexcept;

    std::vector<int> m_weekdays;
};

/// @brief 月频策略: 仅每月配置日触发 (配置日当月不存在则不触发, 如 2月31日)
class MonthlyTriggerPolicy final : public ITriggerPolicy {
public:
    explicit MonthlyTriggerPolicy(int dayOfMonth) : m_dayOfMonth(dayOfMonth) {}

    [[nodiscard]] bool isTriggerDay(const std::string& tradingDay) const override;
    [[nodiscard]] int dayOfMonth() const noexcept { return m_dayOfMonth; }

private:
    int m_dayOfMonth;
};

/// @brief 评估调度器公共接口
class IEvaluationScheduler {
public:
    /// @brief 策略评估回调 (tradingDay, isCompensation) → 评估结果
    /// 所在线程: 策略专用线程 (m_post 投递)
    using EvalFn = std::function<EvalResult(const std::string& tradingDay, bool isCompensation)>;

    /// @brief 投递任务到策略线程
    using PostFn = std::function<void(std::function<void()>)>;

    /// @brief 查询当前交易日 (YYYYMMDD int), 替代 localtime() 手算
    using TradingDayFn = std::function<std::int64_t()>;

    virtual ~IEvaluationScheduler() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void setEvalCallback(EvalFn fn) = 0;
    virtual void setStrategyId(std::string id) = 0;
    virtual void setTriggerPolicy(std::shared_ptr<ITriggerPolicy> policy) = 0;

    /// @brief 设置因子缓存预热回调 (终审 4.1 + C11, P3 接线)
    /// start() 时显式调用一次 (未设置/空回调 → 跳过); 预热不做引擎全局预热
    virtual void setWarmUpFn(std::function<void()> fn) = 0;
};

/// @brief 日/周/月评估调度器 (交易日固定窗口 + 补单窗口 + EOD 回调兜底)
/// 由 DailyEodScheduler 演进: EvalScheduleConfig 消除魔法数字, TriggerPolicy 门控 (C6),
/// EvalFn 返回 EvalResult, 回调 token 真注销, 持久化键含 period 维度 (P1 门禁)
class CronEvaluationScheduler final : public IEvaluationScheduler {
public:
    /// @brief 查询上一交易日 date(YYYYMMDD) → prev(YYYYMMDD string)
    using PrevTradingDayFn = std::function<std::string(const std::string&)>;

    /// @param postToStrategyThread 投递闭包到策略线程
    /// @param persistPath app_state.json 全路径
    /// @param config 调度时间配置 (来自 EvalScheduleConfig::loadFromTradingConfig; 非法则 start() 拒绝启动)
    CronEvaluationScheduler(PostFn postToStrategyThread, const std::string& persistPath,
                            EvalScheduleConfig config);
    ~CronEvaluationScheduler() override;

    void start() override;
    void stop() override;
    void setEvalCallback(EvalFn fn) override { m_evalFn = std::move(fn); }
    void setStrategyId(std::string id) override { m_strategyId = std::move(id); }
    void setTriggerPolicy(std::shared_ptr<ITriggerPolicy> policy) override { m_triggerPolicy = std::move(policy); }
    void setWarmUpFn(std::function<void()> fn) override { m_warmUpFn = std::move(fn); }

    /// @brief 注入交易日查询 (DB trade_calendar 查, 替代 localtime 手算)
    void setTradingDayProvider(TradingDayFn fn) { m_getTradingDay = std::move(fn); }
    void setPrevTradingDayProvider(PrevTradingDayFn fn) { m_getPrevTradingDay = std::move(fn); }

private:
    void onEodTrigger(const std::string& tradingDay);
    /// @brief 正常 EOD 评估入口 (当日 [triggerMinute, +window) 窗口内, 模式显式固定为 EOD)
    /// 去重: 当日已评估 → 跳过; 持久化: Submitted/NoSignal/AllRejected/Skipped 均标记已评估
    void evaluateEod(const std::string& tradingDay);
    /// @brief 补单评估入口 (0:00-补偿结束窗口, 补偿上一交易日, 模式显式固定为补单)
    /// 允许重评估 (幂等由 Finalizer lastBasket 去重); 持久化: 仅 Submitted
    void evaluateCompensation(const std::string& tradingDay);
    /// @brief 两种模式共享的纯执行段 (EvalFn 校验 + 缺口检测 + 异常收敛), 无去重/持久化逻辑
    EvalResult invokeEval(const std::string& tradingDay, bool isCompensation);
    std::string getPreviousTradingDay(const std::string& date);
    std::int64_t getCurrentTradingDay();
    /// @brief 持久化键: lastEvalDay.<strategyId>.<period后缀> (P1 门禁: 键含 period 维度)
    std::string evalKey() const { return m_strategyId + "." + BarPeriodNaming::suffix(m_period); }
    /// @brief 查询数据同步日 (app_state.json dataSyncDay, K线缺口检测; P5: 调度器内置读取, 不再注入)
    std::int64_t currentDataSyncDay() const;
    void loadLastEvalKey();
    void persistLastEvalKey();
    void schedulePollCheck();

    PostFn m_post;
    EvalFn m_evalFn;
    TradingDayFn m_getTradingDay;       // DB查询: 当前交易日
    PrevTradingDayFn m_getPrevTradingDay; // DB查询: 上一交易日
    std::function<void()> m_warmUpFn;    // C11/P3: 因子缓存预热, start() 显式调用一次
    std::shared_ptr<ITriggerPolicy> m_triggerPolicy;  // C6: 门控是否触发 (未设置=恒触发)
    EvalScheduleConfig m_config;
    BarPeriod m_period{BarPeriod::Daily};  // Cron 仅服务日频层 (周/月=TriggerPolicy 变体, 仍日线数据)
    std::atomic<std::int64_t> m_lastEvalDay{0};
    std::string m_persistPath;   // app_state.json 全路径
    std::string m_strategyId;    // 当前策略 ID, 持久化键组成
    std::shared_ptr<astock::infrastructure::database::AppStateStore> m_store;  // app_state.json 统一写者
    std::atomic<bool> m_eodRegistered{false};
    std::uint64_t m_eodToken{0};  // MarketDataService 回调 token (stop() 真注销)
    std::atomic<bool> m_polling{false};
    std::shared_ptr<foundation::thread::ThreadPoolExecutor> m_pollExecutor;
};

/// @brief 分钟周期评估调度器 (ADR-003: K线快照轮询驱动, 非 tick 级回调)
/// 轮询步长 = 周期/2 (1min→30s, 5min→150s); 最近闭合 K 线边界 + kBoundaryOffsetSec 后触发
/// 本次仅落地框架: 仅当策略配置 period=Minute1/5 时由 Facade 实例化 (当前策略全 Daily, 不实例化)
class IntervalEvaluationScheduler final : public IEvaluationScheduler {
public:
    IntervalEvaluationScheduler(PostFn postToStrategyThread, const std::string& persistPath,
                                BarPeriod period);
    ~IntervalEvaluationScheduler() override;

    void start() override;
    void stop() override;
    void setEvalCallback(EvalFn fn) override { m_evalFn = std::move(fn); }
    void setStrategyId(std::string id) override { m_strategyId = std::move(id); }
    void setTriggerPolicy(std::shared_ptr<ITriggerPolicy> policy) override { m_triggerPolicy = std::move(policy); }
    void setWarmUpFn(std::function<void()> fn) override { m_warmUpFn = std::move(fn); }
    void setTradingDayProvider(TradingDayFn fn) { m_getTradingDay = std::move(fn); }

private:
    void schedulePollCheck();
    /// @brief 策略线程执行: 评估一次 bar 边界并持久化 (最近 bar 时间, 键含 period 维度)
    void runBarEval(const std::string& tradingDay, std::int64_t barTime);
    /// @brief 本地日期 + 边界分钟 → YYYYMMDDHHMM (bar 去重键)
    static std::int64_t localBarTimestamp(std::int64_t boundarySecondsOfDay);
    /// @brief bar 起点是否落在交易时段 [9:30,11:30)∪[13:00,15:00) (午餐/收盘后边界不触发)
    bool isTradingBoundary(int boundaryMinutes) const;
    std::string evalKey() const { return m_strategyId + "." + BarPeriodNaming::suffix(m_period); }
    void loadLastFiredBar();
    void persistLastFiredBar();

    static constexpr int kBoundaryOffsetSec = 5;   // ADR-003: 边界+5s 触发
    static constexpr int kTradingAmStartMin = 570; // 09:30
    static constexpr int kTradingAmEndMin = 690;   // 11:30
    static constexpr int kTradingPmStartMin = 780; // 13:00
    static constexpr int kTradingPmEndMin = 900;   // 15:00

    PostFn m_post;
    EvalFn m_evalFn;
    TradingDayFn m_getTradingDay;
    std::function<void()> m_warmUpFn;    // C11/P3: 因子缓存预热, start() 显式调用一次
    std::shared_ptr<ITriggerPolicy> m_triggerPolicy;
    std::string m_persistPath;
    std::string m_strategyId;
    BarPeriod m_period;
    int m_barLengthSec;     // 周期分钟 * 60
    int m_pollIntervalSec;  // barLength/2 (ADR-003)
    std::atomic<std::int64_t> m_lastFiredBar{0};  // 最近已评估 bar 边界 (YYYYMMDDHHMM)
    std::shared_ptr<astock::infrastructure::database::AppStateStore> m_store;
    std::atomic<bool> m_polling{false};
    std::shared_ptr<foundation::thread::ThreadPoolExecutor> m_pollExecutor;
};

} // namespace domain::strategy
