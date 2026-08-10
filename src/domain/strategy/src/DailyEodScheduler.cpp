#include "DailyEodScheduler.h"
#include "../../market/include/MarketDataService.h"
#include "foundation/log/logging.hpp"
#include "foundation/json/json_facade.h"
#include "foundation/thread/ThreadPoolExecutor.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>

namespace domain::strategy {

DailyEodScheduler::DailyEodScheduler(PostFn postToStrategyThread, const std::string& persistPath)
    : m_post(std::move(postToStrategyThread)), m_persistPath(persistPath) {}

DailyEodScheduler::~DailyEodScheduler() {
    stop();
}

// ═══════════════════════════════════════════════════════════════════
// 启动 / 停止
// ═══════════════════════════════════════════════════════════════════

void DailyEodScheduler::start() {
    loadLastEvalDay();

    auto today = getCurrentTradingDay();
    if (today == 0) {
        INTERNAL_ERROR_STREAM << "[DailyEod] 无法获取当前交易日, 调度器不启动";
        return;
    }
    std::string todayStr = std::to_string(today);
    int mins = getCurrentLocalMinutes();

    // ── 分支1: 补单窗口 (0:00–9:30) → 补偿未评估的交易日 ──
    if (mins < kCompensationEnd) {
        std::string prevDay = getPreviousTradingDay(todayStr);
        if (!prevDay.empty()) {
            auto prev = std::stoll(prevDay);
            if (prev > m_lastEvalDay.load()) {
                INTERNAL_INFO_STREAM << "[DailyEod] 补单窗口(" << mins << "min): "
                    << "补偿 " << prev << " > lastEval=" << m_lastEvalDay.load();
                doEvaluate(prevDay);
            }
        }
    }
    // ── 分支2: EOD 窗口 (triggerMin ~ triggerMin+10min, 如14:50–15:00), 严格限制 ──
    else if (mins >= m_eodTriggerMinute && mins < m_eodTriggerMinute + 10
             && today > m_lastEvalDay.load()) {
        INTERNAL_INFO_STREAM << "[DailyEod] EOD窗口(" << mins << "min >= "
            << m_eodTriggerMinute << "min): 立即评估 " << todayStr;
        doEvaluate(todayStr);
    }
    // ── 分支3: 其他时间 → 只启动轮询, 不做任何评估 ──

    // 启动轮询检查: 不依赖 gmsdk, 到 m_eodTriggerMinute 即触发
    if (!m_polling.load()) {
        m_polling.store(true);
        m_pollExecutor = std::make_shared<foundation::thread::ThreadPoolExecutor>(1);
        m_pollExecutor->post([this]() { schedulePollCheck(); });
    }

    // 注册 MarketDataService EOD 回调 (兜底)
    if (!m_eodRegistered.load(std::memory_order_acquire)) {
        domain::market::MarketDataService::instance()
            .registerEndOfDayCallback([this](const std::string& tradingDay) {
                onEodTrigger(tradingDay);
            });
        m_eodRegistered.store(true, std::memory_order_release);
        INTERNAL_INFO_STREAM << "[DailyEod] EOD 回调已注册";
    }
}

void DailyEodScheduler::stop() {
    m_eodRegistered.store(false, std::memory_order_release);
    m_polling.store(false);
    // 停轮询线程池: 唤醒 worker 并等待退出 (最多 5s, sleep 分片已改为每秒检查标志位)
    if (m_pollExecutor) {
        m_pollExecutor->shutdown(false);
        m_pollExecutor->awaitTermination(std::chrono::milliseconds(5000));
        m_pollExecutor.reset();
    }
}

// ═══════════════════════════════════════════════════════════════════
// 轮询检查: 到 m_eodTriggerMinute 即投递评估, 不依赖 gmsdk
// ═══════════════════════════════════════════════════════════════════

void DailyEodScheduler::schedulePollCheck() {
    if (!m_polling.load()) return;
    int mins = getCurrentLocalMinutes();
    auto today = getCurrentTradingDay();

    // 可中断等待: 分片 sleep, 每秒检查 m_polling, 收到停止信号立即退出
    auto interruptibleSleep = [this](int totalSec, int sliceSec, auto then) {
        if (!m_polling.load()) return;
        int elapsed = 0;
        while (elapsed < totalSec && m_polling.load()) {
            int step = (std::min)(sliceSec, totalSec - elapsed);
            std::this_thread::sleep_for(std::chrono::seconds(step));
            elapsed += step;
        }
        if (!m_polling.load()) return;
        m_pollExecutor->post([this, then]() { then(); });
    };

    // 今天已评估 → 低频等待 (30分钟), 不需要密集轮询
    if (today <= m_lastEvalDay.load()) {
        interruptibleSleep(30 * 60, 5, [this]() { schedulePollCheck(); });
        return;
    }

    if (mins >= m_eodTriggerMinute && mins < m_eodTriggerMinute + 10) {
        std::string todayStr = std::to_string(today);
        INTERNAL_INFO_STREAM << "[DailyEod] 轮询触发 " << todayStr
                             << " (" << mins << "min >= " << m_eodTriggerMinute << "min)";
        m_post([this, todayStr]() { doEvaluate(todayStr); });
        // 触发后 60 分钟恢复检查 (此时 lastEvalDay 已更新, 走低频分支)
        interruptibleSleep(60 * 60, 5, [this]() { schedulePollCheck(); });
    } else if (mins >= m_eodTriggerMinute + 10) {
        // 已过 EOD 窗口 (>15:00), 今天没评也不再评估, 低频等待
        interruptibleSleep(30 * 60, 5, [this]() { schedulePollCheck(); });
    } else {
        // 未到触发时间, 30秒后重试
        interruptibleSleep(30, 1, [this]() { schedulePollCheck(); });
    }
}

// ═══════════════════════════════════════════════════════════════════
// EOD 触发 (gmsdk 线程, 保留作为兜底)
// ═══════════════════════════════════════════════════════════════════

void DailyEodScheduler::onEodTrigger(const std::string& tradingDay) {
    // 已停止, 不投递 (stop() 先设标志, 再停 executor)
    if (!m_eodRegistered.load(std::memory_order_acquire)) return;

    // 严格时间窗口: 只在 [triggerMin, triggerMin+10) 内允许触发
    int mins = getCurrentLocalMinutes();
    if (mins < m_eodTriggerMinute || mins >= m_eodTriggerMinute + 10) {
        INTERNAL_INFO_STREAM << "[DailyEod] EOD 回调但不在窗口内"
            << " (当前=" << mins << "min 窗口=["
            << m_eodTriggerMinute << "," << m_eodTriggerMinute+10 << ")), 忽略";
        return;
    }

    // gmsdk 线程回调 — 仅投递到策略线程
    m_post([this, tradingDay]() {
        doEvaluate(tradingDay);
    });
}

// ═══════════════════════════════════════════════════════════════════
// 评估入口 (策略线程)
// ═══════════════════════════════════════════════════════════════════

void DailyEodScheduler::doEvaluate(const std::string& tradingDay) {
    auto evalDay = std::stoll(tradingDay);
    auto today = getCurrentTradingDay();
    bool isCompensation = (evalDay < today);

    // 非补单: 已评估过则跳过; 补单: 允许重评估
    if (!isCompensation && evalDay <= m_lastEvalDay.load()) {
        INTERNAL_INFO_STREAM << "[DailyEod] 交易日 " << tradingDay << " 已评估, 跳过 (last=" << m_lastEvalDay.load() << ")";
        return;
    }

    if (!m_evalFn) {
        INTERNAL_WARN_STREAM << "[DailyEod] EvalFn 未设置, 跳过";
        return;
    }

    INTERNAL_INFO_STREAM << "[DailyEod] 开始评估 tradingDay=" << tradingDay
                         << " isCompensation=" << isCompensation;

    // ── K线数据缺口检测: dataSyncDay 落后 evalDay 超过2天 → 警告但不阻止 ──
    if (m_getDataSyncDay) {
        int syncDay = m_getDataSyncDay();
        int gap = static_cast<int>(evalDay) - syncDay;
        if (gap > 2) {
            INTERNAL_WARN_STREAM << "[DailyEod] ⚠️ K线数据缺口 " << gap
                << " 天! dataSyncDay=" << syncDay << " evalDay=" << evalDay
                << " — 请先手动同步K线后再评估, 当前仍按现有数据执行";
        }
    }

    EodEvaluationStatus status = EodEvaluationStatus::Error;
    try {
        status = m_evalFn(tradingDay, isCompensation);
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[DailyEod] 评估异常: " << e.what();
        status = EodEvaluationStatus::Error;
    } catch (...) {
        INTERNAL_ERROR_STREAM << "[DailyEod] 评估未知异常";
        status = EodEvaluationStatus::Error;
    }

    // 持久化策略:
    //   正常EOD(非补单): Submitted/NoSignal/Skipped 都标记已评估, 防止轮询重复触发
    //   补单: 仅 Submitted 持久化, 其余状态允许下次重试
    bool shouldPersist = false;
    if (!isCompensation) {
        shouldPersist = (status == EodEvaluationStatus::Submitted
                      || status == EodEvaluationStatus::NoSignal
                      || status == EodEvaluationStatus::Skipped);
    } else {
        shouldPersist = (status == EodEvaluationStatus::Submitted);
    }
    if (shouldPersist) {
        m_lastEvalDay.store(evalDay);
        persistLastEvalDay();
    } else {
        INTERNAL_WARN_STREAM << "[DailyEod] 不持久化 lastEvalDay, status="
                             << static_cast<int>(status) << " (等待补单重试)";
    }
    INTERNAL_INFO_STREAM << "[DailyEod] 评估完成, lastEvalDay=" << m_lastEvalDay.load()
                         << " status=" << static_cast<int>(status);
}

// ═══════════════════════════════════════════════════════════════════
// 交易日工具
// ═══════════════════════════════════════════════════════════════════

std::string DailyEodScheduler::getPreviousTradingDay(const std::string& date) {
    if (!m_getPrevTradingDay) {
        INTERNAL_ERROR_STREAM << "[DailyEod] getPreviousTradingDay: PrevTradingDayFn 未注入!";
        return {};
    }
    return m_getPrevTradingDay(date);
}

// ═══════════════════════════════════════════════════════════════════
// 时间窗口
// ═══════════════════════════════════════════════════════════════════

int DailyEodScheduler::getCurrentLocalMinutes() {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    struct tm local;
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local, &tt);
#else
    localtime_r(&tt, &local);
#endif
    return local.tm_hour * 60 + local.tm_min;
}

std::int64_t DailyEodScheduler::getCurrentTradingDay() {
    if (!m_getTradingDay) {
        INTERNAL_ERROR_STREAM << "[DailyEod] getCurrentTradingDay: TradingDayFn 未注入!";
        return 0;
    }
    return m_getTradingDay();
}

// ═══════════════════════════════════════════════════════════════════
// 持久化
// ═══════════════════════════════════════════════════════════════════

void DailyEodScheduler::loadLastEvalDay() {
    if (m_persistPath.empty() || m_strategyId.empty()) return;

    // 从统一 JSON 读取 eodExecuted.<strategyId>
    auto json = foundation::json::JsonFacade::parseFile(m_persistPath);
    if (json.isNull() || !json.isObject()) return;

    if (json.has("eodExecuted")) {
        auto evalMap = json.get("eodExecuted");
        if (evalMap.isObject() && evalMap.has(m_strategyId)) {
            try {
                m_lastEvalDay.store(static_cast<std::int64_t>(evalMap.get(m_strategyId).asInt()));
                INTERNAL_INFO_STREAM << "[DailyEod] 加载 eodExecuted." << m_strategyId
                                     << "=" << m_lastEvalDay.load();
            } catch (...) {
                m_lastEvalDay.store(0);
            }
        }
    }
}

void DailyEodScheduler::persistLastEvalDay() {
    if (m_persistPath.empty() || m_strategyId.empty()) return;

    // 读取现有 JSON，保留非 EOD 的顶层键，合并旧键 strategyLastEval → 新键 eodExecuted
    auto root = foundation::json::JsonFacade::createObject();
    auto evalMap = foundation::json::JsonFacade::createObject();
    {
        auto existing = foundation::json::JsonFacade::parseFile(m_persistPath);
        if (!existing.isNull() && existing.isObject()) {
            for (const auto& key : existing.keys()) {
                if (key == "eodExecuted") {
                    // 保留其他策略的条目
                    auto oldMap = existing.get(key);
                    if (oldMap.isObject()) {
                        for (const auto& sk : oldMap.keys())
                            evalMap.set(sk, oldMap.get(sk));
                    }
                } else {
                    root.set(key, existing.get(key));
                }
            }
        }
    }
    // 更新当前策略
    evalMap.set(m_strategyId, foundation::json::JsonFacade::createInt(
        static_cast<int>(m_lastEvalDay.load())));
    root.set("eodExecuted", evalMap);

    // 原子写入: 先写临时文件, 再重命名
    std::string tmpPath = m_persistPath + ".tmp";
    {
        std::ofstream f(tmpPath, std::ios::trunc);
        if (!f.is_open()) {
            INTERNAL_WARN_STREAM << "[DailyEod] 无法写入持久化文件: " << tmpPath;
            return;
        }
        f << root.toString() << "\n";
        f.close();
    }
    // Windows rename 不覆盖已有文件, 先删再rename
    std::remove(m_persistPath.c_str());
    if (std::rename(tmpPath.c_str(), m_persistPath.c_str()) != 0) {
        INTERNAL_WARN_STREAM << "[DailyEod] 持久化文件重命名失败: " << m_persistPath;
    }
}

void DailyEodScheduler::setEodTriggerTime(const std::string& time) {
    if(time.size()>=5) m_eodTriggerMinute = std::stoi(time.substr(0,2))*60 + std::stoi(time.substr(3,2));
}

} // namespace domain::strategy
