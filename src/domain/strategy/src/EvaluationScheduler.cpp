#include "EvaluationScheduler.h"
#include "../../market/include/MarketDataService.h"
#include "../../../infrastructure/include/database/AppStateStore.h"
#include "foundation/config/ConfigManager.hpp"
#include "foundation/log/logging.hpp"
#include "foundation/thread/ThreadPoolExecutor.h"
#include "foundation/time/LocalClock.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <string>
#include <thread>

namespace domain::strategy {

namespace {

/// @brief 可中断分片睡眠辅助 (两个调度器轮询循环共用, 消除重复)
/// 每秒检查运行标志, 收到停止信号立即退出; 未停止则在轮询线程池投递续跑任务
class InterruptibleSleep {
public:
    InterruptibleSleep(const std::atomic<bool>& running,
                       std::shared_ptr<foundation::thread::ThreadPoolExecutor> executor)
        : m_running(running), m_executor(std::move(executor)) {}

    /// @param totalSec 总等待秒数
    /// @param sliceSec 单次睡眠分片 (短等待用 1s, 长等待用 5s 降低唤醒频率)
    /// @param then 等待结束后投递的续跑任务
    void waitThen(int totalSec, int sliceSec, std::function<void()> then) {
        if (!m_running.load()) return;
        int elapsed = 0;
        while (elapsed < totalSec && m_running.load()) {
            int step = (std::min)(sliceSec, totalSec - elapsed);
            std::this_thread::sleep_for(std::chrono::seconds(step));
            elapsed += step;
        }
        if (!m_running.load()) return;
        m_executor->post([this, then = std::move(then)]() { then(); });
    }

private:
    const std::atomic<bool>& m_running;
    std::shared_ptr<foundation::thread::ThreadPoolExecutor> m_executor;
};

} // namespace

// ═══════════════════════════════════════════════════════════════════
// EvalScheduleConfig 配置加载 (P6: 时间必须读配置文件, 零兜底)
// 文件不可用/键缺失/非法 → ERROR + nullopt, 调度器拒绝启动 (不用硬编码时间顶替)
// ═══════════════════════════════════════════════════════════════════

bool EvalScheduleConfig::isValid() const noexcept {
    if (triggerMinute <= 0 || triggerWindowMinutes <= 0 || compensationEndMinute <= 0
        || eodCallbackStartMinute <= 0 || eodCallbackEndMinute <= 0) return false;
    // 触发时间必须晚于补单窗口结束 (补单分支优先, 否则 EOD 窗口被吞掉)
    if (triggerMinute <= compensationEndMinute) return false;
    // 触发窗口不得跨天
    if (triggerMinute + triggerWindowMinutes > 1440) return false;
    // 回调窗口 start < end (盘后回调兜底窗口)
    if (eodCallbackStartMinute >= eodCallbackEndMinute) return false;
    return true;
}

std::optional<EvalScheduleConfig> EvalScheduleConfig::loadFromTradingConfig() {
    EvalScheduleConfig config;  // 零兜底: 逐键严格解析, 任一失败即 nullopt
    auto& cfgMgr = foundation::config::ConfigManager::instance();
    auto cfg = cfgMgr.loadConfigFile(foundation::config::ConfigFile::TradingConnection);
    if (!cfg || cfg->isNull()) {
        INTERNAL_ERROR_STREAM << "[DailyEod] trading_connection.json 不可用, "
                              << "无法读取调度时间, 调度器拒绝启动";
        return std::nullopt;
    }
    // 时间键: 缺失/非法 → ERROR + 失败 (HH:MM 解析复用 foundation::time::LocalClock)
    const auto applyTime = [&cfg](const char* key, int& target) -> bool {
        if (!cfg->has(key)) {
            INTERNAL_ERROR_STREAM << "[DailyEod] 配置缺失 " << key << ", 调度器拒绝启动";
            return false;
        }
        const int parsed = foundation::time::LocalClock::parseHhMm(cfg->get(key).asString());
        if (parsed < 0) {
            INTERNAL_ERROR_STREAM << "[DailyEod] 配置 " << key << " 非法: '"
                                  << cfg->get(key).asString() << "' (需 HH:MM), 调度器拒绝启动";
            return false;
        }
        target = parsed;
        return true;
    };
    if (!applyTime("eodTriggerTime", config.triggerMinute)) return std::nullopt;
    if (!applyTime("compensationEndTime", config.compensationEndMinute)) return std::nullopt;
    // EOD 回调兜底窗口 (收盘回调接受区间, 14:50-15:00 亦走配置, 零硬编码)
    if (!applyTime("eodCallbackStartTime", config.eodCallbackStartMinute)) return std::nullopt;
    if (!applyTime("eodCallbackEndTime", config.eodCallbackEndMinute)) return std::nullopt;
    // 触发窗口宽度 (分钟, 整数): 缺失/非法/越界 [1,60] → ERROR + 失败
    if (!cfg->has("triggerWindowMinutes")) {
        INTERNAL_ERROR_STREAM << "[DailyEod] 配置缺失 triggerWindowMinutes, 调度器拒绝启动";
        return std::nullopt;
    }
    const int window = cfg->get("triggerWindowMinutes").asInt();
    if (window < 1 || window > 60) {
        INTERNAL_ERROR_STREAM << "[DailyEod] 配置 triggerWindowMinutes 非法: " << window
                              << " (需 [1,60] 分钟), 调度器拒绝启动";
        return std::nullopt;
    }
    config.triggerWindowMinutes = window;
    return config;
}

// ═══════════════════════════════════════════════════════════════════
// TriggerPolicy 实现
// ═══════════════════════════════════════════════════════════════════

int WeeklyTriggerPolicy::weekdayOf(const std::string& yyyymmdd) noexcept {
    if (yyyymmdd.size() != 8) return 0;
    int y = std::stoi(yyyymmdd.substr(0, 4));
    int m = std::stoi(yyyymmdd.substr(4, 2));
    int d = std::stoi(yyyymmdd.substr(6, 2));
    if (y <= 0 || m <= 0 || d <= 0) return 0;
    if (m < 3) { m += 12; --y; }
    // Zeller 公式: h=0周六,1周日,2周一...6周五
    int h = (d + 26 * (m + 1) / 10 + y + y / 4 + 6 * (y / 100) + (y / 100) / 4) % 7;
    // 换算 ISO 周序 (1=周一..7=周日)
    return (h + 5) % 7 + 1;
}

bool WeeklyTriggerPolicy::isTriggerDay(const std::string& tradingDay) const {
    const int wd = weekdayOf(tradingDay);
    return wd != 0 && std::find(m_weekdays.begin(), m_weekdays.end(), wd) != m_weekdays.end();
}

bool MonthlyTriggerPolicy::isTriggerDay(const std::string& tradingDay) const {
    if (tradingDay.size() != 8) return false;
    return std::stoi(tradingDay.substr(6, 2)) == m_dayOfMonth;
}

// ═══════════════════════════════════════════════════════════════════
// CronEvaluationScheduler (由 DailyEodScheduler 演进, 行为与重构前逐项一致)
// ═══════════════════════════════════════════════════════════════════

CronEvaluationScheduler::CronEvaluationScheduler(PostFn postToStrategyThread,
                                                 const std::string& persistPath,
                                                 EvalScheduleConfig config)
    : m_post(std::move(postToStrategyThread)),
      m_persistPath(persistPath),
      m_config(config)
{
    // 统一持久化写者: 与 PostMarketSyncService/PositionBook 共享同路径 AppStateStore (互斥串行)
    if (!m_persistPath.empty())
        m_store = astock::infrastructure::database::AppStateStore::forPath(m_persistPath);
}

CronEvaluationScheduler::~CronEvaluationScheduler() {
    stop();
}

// ═══════════════════════════════════════════════════════════════════
// 启动 / 停止
// ═══════════════════════════════════════════════════════════════════

void CronEvaluationScheduler::start() {
    // P6: 配置非法 → 拒绝启动 (零兜底: 时间必须来自配置文件并通过严格校验)
    if (!m_config.isValid()) {
        INTERNAL_ERROR_STREAM << "[DailyEod] 调度配置非法, 拒绝启动: trigger=" << m_config.triggerMinute
                              << "min window=" << m_config.triggerWindowMinutes
                              << "min compensationEnd=" << m_config.compensationEndMinute
                              << "min eodCallback=[" << m_config.eodCallbackStartMinute
                              << "," << m_config.eodCallbackEndMinute
                              << ")min (需 eodTriggerTime 晚于 compensationEndTime 且窗口不跨天)";
        return;
    }
    loadLastEvalKey();

    // 调度时间上报 (P6: 时间来自配置文件, 此处输出生效值供运维核对)
    INTERNAL_INFO_STREAM << "[DailyEod] 调度时间: trigger=" << m_config.triggerMinute
                         << "min window=" << m_config.triggerWindowMinutes
                         << "min compensationEnd=" << m_config.compensationEndMinute
                         << "min eodCallback=[" << m_config.eodCallbackStartMinute
                         << "," << m_config.eodCallbackEndMinute << ")min";

    // C11/P3: 因子缓存预热 (终审 4.1 — 由调度器 start() 显式调用; 空回调跳过)
    if (m_warmUpFn) m_warmUpFn();

    auto today = getCurrentTradingDay();
    if (today == 0) {
        INTERNAL_ERROR_STREAM << "[DailyEod] 无法获取当前交易日, 调度器不启动";
        return;
    }
    std::string todayStr = std::to_string(today);
    int mins = foundation::time::LocalClock::minutesOfDay();

    // ── 分支1: 补单窗口 (0:00–补偿结束) → 补单入口, 补偿未评估的上一交易日 ──
    if (mins < m_config.compensationEndMinute) {
        std::string prevDay = getPreviousTradingDay(todayStr);
        if (!prevDay.empty()) {
            auto prev = std::stoll(prevDay);
            if (prev > m_lastEvalDay.load()) {
                INTERNAL_INFO_STREAM << "[DailyEod] 补单窗口(" << mins << "min): "
                    << "补偿 " << prev << " > lastEval=" << m_lastEvalDay.load();
                evaluateCompensation(prevDay);
            }
        }
    }
    // ── 分支2: EOD 窗口 [triggerMinute, +triggerWindowMinutes), 严格限制 → EOD 入口 ──
    else if (mins >= m_config.triggerMinute
             && mins < m_config.triggerMinute + m_config.triggerWindowMinutes
             && today > m_lastEvalDay.load()) {
        INTERNAL_INFO_STREAM << "[DailyEod] EOD窗口(" << mins << "min >= "
            << m_config.triggerMinute << "min): 立即评估 " << todayStr;
        evaluateEod(todayStr);
    }
    // ── 分支3: 其他时间 → 只启动轮询, 不做任何评估 ──

    // 启动轮询检查: 不依赖 gmsdk, 到 triggerMinute 即触发
    if (!m_polling.load()) {
        m_polling.store(true);
        m_pollExecutor = std::make_shared<foundation::thread::ThreadPoolExecutor>(1);
        m_pollExecutor->post([this]() { schedulePollCheck(); });
    }

    // 注册 MarketDataService EOD 回调 (兜底), 持有 token 供 stop() 真注销
    if (!m_eodRegistered.load(std::memory_order_acquire)) {
        m_eodToken = domain::market::MarketDataService::instance()
            .registerEndOfDayCallback([this](const std::string& tradingDay) {
                onEodTrigger(tradingDay);
            });
        m_eodRegistered.store(true, std::memory_order_release);
        INTERNAL_INFO_STREAM << "[DailyEod] EOD 回调已注册 token=" << m_eodToken;
    }
}

void CronEvaluationScheduler::stop() {
    m_eodRegistered.store(false, std::memory_order_release);
    // 真注销: 从 MarketDataService 移除回调 (重构前仅设标志, 回调残留)
    if (m_eodToken != 0) {
        domain::market::MarketDataService::instance().unregisterEndOfDayCallback(m_eodToken);
        m_eodToken = 0;
    }
    m_polling.store(false);
    // 停轮询线程池: 唤醒 worker 并等待退出 (最多 5s, sleep 分片已改为每秒检查标志位)
    if (m_pollExecutor) {
        m_pollExecutor->shutdown(false);
        m_pollExecutor->awaitTermination(std::chrono::milliseconds(5000));
        m_pollExecutor.reset();
    }
}

// ═══════════════════════════════════════════════════════════════════
// 轮询检查: 到 triggerMinute 即投递评估, 不依赖 gmsdk
// ═══════════════════════════════════════════════════════════════════

void CronEvaluationScheduler::schedulePollCheck() {
    if (!m_polling.load()) return;
    InterruptibleSleep sleeper(m_polling, m_pollExecutor);
    int mins = foundation::time::LocalClock::minutesOfDay();
    auto today = getCurrentTradingDay();

    // 今天已评估 → 低频等待, 不需要密集轮询
    if (today <= m_lastEvalDay.load()) {
        sleeper.waitThen(m_config.evaluatedIdleWaitSec, 5, [this]() { schedulePollCheck(); });
        return;
    }

    if (mins >= m_config.triggerMinute
        && mins < m_config.triggerMinute + m_config.triggerWindowMinutes) {
        std::string todayStr = std::to_string(today);
        INTERNAL_INFO_STREAM << "[DailyEod] 轮询触发 " << todayStr
                             << " (" << mins << "min >= " << m_config.triggerMinute << "min)";
        m_post([this, todayStr]() { evaluateEod(todayStr); });
        // 触发后低频等待 (此时 lastEvalDay 已更新, 走低频分支)
        sleeper.waitThen(m_config.postTriggerWaitSec, 5, [this]() { schedulePollCheck(); });
    } else if (mins >= m_config.triggerMinute + m_config.triggerWindowMinutes) {
        // 已过 EOD 窗口, 今天没评也不再评估, 低频等待
        sleeper.waitThen(m_config.evaluatedIdleWaitSec, 5, [this]() { schedulePollCheck(); });
    } else {
        // 未到触发时间, 短步长重试
        sleeper.waitThen(m_config.pollIntervalSec, 1, [this]() { schedulePollCheck(); });
    }
}

// ═══════════════════════════════════════════════════════════════════
// EOD 触发 (gmsdk 线程, 保留作为兜底)
// ═══════════════════════════════════════════════════════════════════

void CronEvaluationScheduler::onEodTrigger(const std::string& tradingDay) {
    // 已停止, 不投递 (stop() 先设标志, 再停 executor)
    if (!m_eodRegistered.load(std::memory_order_acquire)) return;

    // 接受窗口 = 配置的回调窗口 [eodCallbackStartMinute, eodCallbackEndMinute) (零硬编码)
    // 回调窗口与轮询触发窗口同段 (14:50-15:00): 先到者评估, 后到者 evaluateEod 去重跳过;
    // 轮询漏评估 → 此处兜底补上
    int mins = foundation::time::LocalClock::minutesOfDay();
    if (mins < m_config.eodCallbackStartMinute
        || mins >= m_config.eodCallbackEndMinute) {
        INTERNAL_INFO_STREAM << "[DailyEod] EOD 回调但不在回调窗口内"
            << " (当前=" << mins << "min 窗口=["
            << m_config.eodCallbackStartMinute << "," << m_config.eodCallbackEndMinute << ")), 忽略";
        return;
    }

    // gmsdk 线程回调 — 仅投递到策略线程 (EOD 入口, 模式显式固定)
    m_post([this, tradingDay]() {
        evaluateEod(tradingDay);
    });
}

// ═══════════════════════════════════════════════════════════════════
// 评估入口 (策略线程, P6: 正常下单与补单是两个界限清晰的功能)
// 模式由入口显式决定, 不做日期推断: evaluateEod=当日窗口评估 / evaluateCompensation=次日补单
// 共享部分收敛在 invokeEval (纯执行段), 去重与持久化规则各自独立, 修改一方不影响另一方
// ═══════════════════════════════════════════════════════════════════

void CronEvaluationScheduler::evaluateEod(const std::string& tradingDay) {
    const auto evalDay = std::stoll(tradingDay);

    // C6: TriggerPolicy 门控; 未设置 policy = 恒触发
    if (m_triggerPolicy && !m_triggerPolicy->isTriggerDay(tradingDay)) {
        INTERNAL_INFO_STREAM << "[DailyEod] 非触发日, 跳过评估 tradingDay=" << tradingDay;
        return;
    }

    // 当日已评估过 → 跳过 (防轮询/回调重复触发)
    if (evalDay <= m_lastEvalDay.load()) {
        INTERNAL_INFO_STREAM << "[DailyEod] 交易日 " << tradingDay
                             << " 已评估, 跳过 (last=" << m_lastEvalDay.load() << ")";
        return;
    }

    const EvalResult result = invokeEval(tradingDay, false);

    // 持久化 (§8 异常矩阵): Submitted/NoSignal/AllRejected/Skipped 均标记已评估,
    // 防止窗口内重复触发 (AllRejected 为最终决定); Error → 不写, 等待补单重试
    if (result.status == EvalStatus::Submitted
        || result.status == EvalStatus::NoSignal
        || result.status == EvalStatus::AllRejected
        || result.status == EvalStatus::Skipped) {
        m_lastEvalDay.store(evalDay);
        persistLastEvalKey();
    } else {
        INTERNAL_WARN_STREAM << "[DailyEod] 不持久化 lastEvalDay, status="
                             << static_cast<int>(result.status) << " (等待补单重试)";
    }
    INTERNAL_INFO_STREAM << "[DailyEod] 评估完成, lastEvalDay=" << m_lastEvalDay.load()
                         << " status=" << static_cast<int>(result.status);
}

void CronEvaluationScheduler::evaluateCompensation(const std::string& tradingDay) {
    // C6: TriggerPolicy 门控 (C5: 以被补单交易日重算, tradingDay 即被补单日); 未设置 = 恒触发
    if (m_triggerPolicy && !m_triggerPolicy->isTriggerDay(tradingDay)) {
        INTERNAL_INFO_STREAM << "[DailyEod] 非触发日, 跳过补单 tradingDay=" << tradingDay;
        return;
    }

    // 允许重评估 (当日已提交 → 幂等由 Finalizer lastBasket 去重拦截, 不重复下单)
    const EvalResult result = invokeEval(tradingDay, true);

    // 持久化: 补单仅 Submitted 写入, 其余状态允许下次补单窗口重试
    if (result.status == EvalStatus::Submitted) {
        m_lastEvalDay.store(std::stoll(tradingDay));
        persistLastEvalKey();
    } else {
        INTERNAL_WARN_STREAM << "[DailyEod] 补单不持久化 lastEvalDay, status="
                             << static_cast<int>(result.status) << " (允许下次重试)";
    }
    INTERNAL_INFO_STREAM << "[DailyEod] 补单完成, lastEvalDay=" << m_lastEvalDay.load()
                         << " status=" << static_cast<int>(result.status);
}

EvalResult CronEvaluationScheduler::invokeEval(const std::string& tradingDay, bool isCompensation) {
    EvalResult result;  // status 默认 Error
    if (!m_evalFn) {
        INTERNAL_WARN_STREAM << "[DailyEod] EvalFn 未设置, 跳过";
        result.reason = "EvalFn 未设置";
        return result;
    }

    INTERNAL_INFO_STREAM << "[DailyEod] 开始评估 tradingDay=" << tradingDay
                         << " isCompensation=" << isCompensation;

    // ── K线数据缺口检测: dataSyncDay 落后 evalDay 超过2天 → 警告但不阻止 ──
    // dataSyncDay 不可用(<=0, 键缺失/未同步) → INFO 跳过, 不误报缺口
    // (P5: 调度器内置 AppStateStore 读取, 与写侧同源, 不再由 Facade 注入裸 lambda)
    {
        const std::int64_t syncDay = currentDataSyncDay();
        const auto evalDay = std::stoll(tradingDay);
        if (syncDay <= 0) {
            INTERNAL_INFO_STREAM << "[DailyEod] dataSyncDay 不可用, 跳过K线缺口检测";
        } else {
            const int gap = static_cast<int>(evalDay) - static_cast<int>(syncDay);
            if (gap > 2) {
                INTERNAL_WARN_STREAM << "[DailyEod] ⚠️ K线数据缺口 " << gap
                    << " 天! dataSyncDay=" << syncDay << " evalDay=" << evalDay
                    << " — 请先手动同步K线后再评估, 当前仍按现有数据执行";
            }
        }
    }

    try {
        result = m_evalFn(tradingDay, isCompensation);
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[DailyEod] 评估异常: " << e.what();
        result.status = EvalStatus::Error;
        result.reason = e.what();
    } catch (...) {
        INTERNAL_ERROR_STREAM << "[DailyEod] 评估未知异常";
        result.status = EvalStatus::Error;
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════
// 交易日工具
// ═══════════════════════════════════════════════════════════════════

std::string CronEvaluationScheduler::getPreviousTradingDay(const std::string& date) {
    if (!m_getPrevTradingDay) {
        INTERNAL_ERROR_STREAM << "[DailyEod] getPreviousTradingDay: PrevTradingDayFn 未注入!";
        return {};
    }
    return m_getPrevTradingDay(date);
}

// ═══════════════════════════════════════════════════════════════════
// 交易日工具
// ═══════════════════════════════════════════════════════════════════

std::int64_t CronEvaluationScheduler::getCurrentTradingDay() {
    if (!m_getTradingDay) {
        INTERNAL_ERROR_STREAM << "[DailyEod] getCurrentTradingDay: TradingDayFn 未注入!";
        return 0;
    }
    return m_getTradingDay();
}

// ═══════════════════════════════════════════════════════════════════
// 持久化 (键 lastEvalDay.<strategyId>.<period>, P1 门禁: 键含 period 维度)
// ═══════════════════════════════════════════════════════════════════

void CronEvaluationScheduler::loadLastEvalKey() {
    if (!m_store || m_strategyId.empty()) return;

    std::int64_t day = 0;
    if (m_store->readInt("lastEvalDay", evalKey(), day)) {
        m_lastEvalDay.store(day);
        INTERNAL_INFO_STREAM << "[DailyEod] 加载 lastEvalDay." << evalKey()
                             << "=" << day;
    }
}

std::int64_t CronEvaluationScheduler::currentDataSyncDay() const {
    // 与写侧 (PostMarketSyncService::saveLastSyncDay) 同源: AppStateStore 顶层键 dataSyncDay
    if (!m_store) return 0;
    std::int64_t day = 0;
    return m_store->readInt("dataSyncDay", day) ? day : 0;
}

void CronEvaluationScheduler::persistLastEvalKey() {
    if (!m_store || m_strategyId.empty()) return;

    // 合并语义: 保留其余顶层键与 lastEvalDay 内其他条目 (AppStateStore 原子写)
    m_store->writeInt("lastEvalDay", evalKey(),
                      static_cast<std::int64_t>(m_lastEvalDay.load()));
}

// ═══════════════════════════════════════════════════════════════════
// IntervalEvaluationScheduler (ADR-003 框架, 当前策略全 Daily 不实例化)
// ═══════════════════════════════════════════════════════════════════

IntervalEvaluationScheduler::IntervalEvaluationScheduler(PostFn postToStrategyThread,
                                                         const std::string& persistPath,
                                                         BarPeriod period)
    : m_post(std::move(postToStrategyThread)),
      m_persistPath(persistPath),
      m_period(period)
{
    // 周期步长: 1min→60s/30s, 5min→300s/150s (ADR-003: 轮询步长=周期/2)
    const int periodMinutes = (period == BarPeriod::Minute5) ? 5 : 1;
    m_barLengthSec = periodMinutes * 60;
    m_pollIntervalSec = m_barLengthSec / 2;
    if (period != BarPeriod::Minute1 && period != BarPeriod::Minute5) {
        INTERNAL_ERROR_STREAM << "[IntervalEval] 非法周期, 仅支持 Minute1/Minute5";
    }
    if (!m_persistPath.empty())
        m_store = astock::infrastructure::database::AppStateStore::forPath(m_persistPath);
}

IntervalEvaluationScheduler::~IntervalEvaluationScheduler() {
    stop();
}

void IntervalEvaluationScheduler::start() {
    loadLastFiredBar();
    // C11/P3: 因子缓存预热 (终审 4.1 — 由调度器 start() 显式调用; 空回调跳过)
    if (m_warmUpFn) m_warmUpFn();
    if (!m_polling.load()) {
        m_polling.store(true);
        m_pollExecutor = std::make_shared<foundation::thread::ThreadPoolExecutor>(1);
        m_pollExecutor->post([this]() { schedulePollCheck(); });
    }
    INTERNAL_INFO_STREAM << "[IntervalEval] 已启动 period="
                         << BarPeriodNaming::suffix(m_period)
                         << " 轮询步长=" << m_pollIntervalSec << "s";
}

void IntervalEvaluationScheduler::stop() {
    m_polling.store(false);
    if (m_pollExecutor) {
        m_pollExecutor->shutdown(false);
        m_pollExecutor->awaitTermination(std::chrono::milliseconds(5000));
        m_pollExecutor.reset();
    }
}

void IntervalEvaluationScheduler::schedulePollCheck() {
    if (!m_polling.load()) return;
    InterruptibleSleep sleeper(m_polling, m_pollExecutor);

    auto today = m_getTradingDay ? m_getTradingDay() : 0;
    if (today <= 0) {
        sleeper.waitThen(m_pollIntervalSec, 1, [this]() { schedulePollCheck(); });
        return;
    }

    // 最近闭合的 K 线边界 (边界为周期整倍数), 边界+5s 后触发 (ADR-003)
    const int nowSec = foundation::time::LocalClock::secondsOfDay();
    const std::int64_t boundarySec = (nowSec / m_barLengthSec) * m_barLengthSec;
    if (nowSec >= boundarySec + kBoundaryOffsetSec) {
        const std::int64_t barTime = localBarTimestamp(boundarySec);
        const int boundaryMin = static_cast<int>(boundarySec / 60);
        if (isTradingBoundary(boundaryMin) && barTime > m_lastFiredBar.load()) {
            const std::string dayStr = std::to_string(today);
            if (!m_triggerPolicy || m_triggerPolicy->isTriggerDay(dayStr)) {
                INTERNAL_INFO_STREAM << "[IntervalEval] bar 边界触发 barTime=" << barTime;
                m_post([this, dayStr, barTime]() { runBarEval(dayStr, barTime); });
            }
        }
    }
    sleeper.waitThen(m_pollIntervalSec, 1, [this]() { schedulePollCheck(); });
}

void IntervalEvaluationScheduler::runBarEval(const std::string& tradingDay, std::int64_t barTime) {
    if (!m_evalFn) {
        INTERNAL_WARN_STREAM << "[IntervalEval] EvalFn 未设置, 跳过 bar=" << barTime;
        return;
    }

    EvalResult result;
    try {
        result = m_evalFn(tradingDay, false);
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[IntervalEval] 评估异常: " << e.what();
        result.status = EvalStatus::Error;
        result.reason = e.what();
    } catch (...) {
        INTERNAL_ERROR_STREAM << "[IntervalEval] 评估未知异常";
        result.status = EvalStatus::Error;
    }

    // bar 处理标记: 无论结果状态都持久化最近 bar 时间, 防同 bar 重启重复触发
    m_lastFiredBar.store(barTime);
    persistLastFiredBar();
    INTERNAL_INFO_STREAM << "[IntervalEval] bar=" << barTime
                         << " status=" << static_cast<int>(result.status);
}

std::int64_t IntervalEvaluationScheduler::localBarTimestamp(std::int64_t boundarySecondsOfDay) {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    struct tm local;
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local, &tt);
#else
    localtime_r(&tt, &local);
#endif
    std::int64_t day = (local.tm_year + 1900) * 10000LL
                     + (local.tm_mon + 1) * 100LL
                     + local.tm_mday;
    return day * 10000LL + boundarySecondsOfDay / 60;
}

bool IntervalEvaluationScheduler::isTradingBoundary(int boundaryMinutes) const {
    // bar 起点须落在交易时段: [9:30,11:30) ∪ [13:00,15:00) (午餐边界 13:00 的 bar 不存在, 不触发)
    const int periodMinutes = m_barLengthSec / 60;
    const int barStart = boundaryMinutes - periodMinutes;
    return (barStart >= kTradingAmStartMin && barStart < kTradingAmEndMin)
        || (barStart >= kTradingPmStartMin && barStart < kTradingPmEndMin);
}

void IntervalEvaluationScheduler::loadLastFiredBar() {
    if (!m_store || m_strategyId.empty()) return;

    std::int64_t barTime = 0;
    if (m_store->readInt("lastEvalDay", evalKey(), barTime)) {
        m_lastFiredBar.store(barTime);
        INTERNAL_INFO_STREAM << "[IntervalEval] 加载 lastEvalDay." << evalKey()
                             << "=" << barTime;
    }
}

void IntervalEvaluationScheduler::persistLastFiredBar() {
    if (!m_store || m_strategyId.empty()) return;
    m_store->writeInt("lastEvalDay", evalKey(), m_lastFiredBar.load());
}

} // namespace domain::strategy
