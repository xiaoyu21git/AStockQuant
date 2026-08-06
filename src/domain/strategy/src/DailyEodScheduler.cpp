#include "DailyEodScheduler.h"
#include "../../market/include/MarketDataService.h"
#include "../../../thirdparty/gmsdk/gmapi.h"
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

    // ── 补单: 补评估 lastEvalDay 之后到上一交易日之间的所有缺失日 ──
    auto today = getCurrentTradingDay();
    std::string todayStr = std::to_string(today);
    std::string prevDay = getPreviousTradingDay(todayStr);
    while (!prevDay.empty()) {
        auto prev = std::stoll(prevDay);
        if (prev <= m_lastEvalDay.load()) break;
        INTERNAL_INFO_STREAM << "[DailyEod] 补单: 缺失评估日 " << prev
                             << " lastEval=" << m_lastEvalDay.load();
        doEvaluate(prevDay);
        prevDay = getPreviousTradingDay(prevDay);
    }

    // ── 启动时: 如果已过EOD触发时间且今天未评估，立即评估 ──
    {
        int mins = getCurrentLocalMinutes();
        if (mins >= m_eodTriggerMinute && today > m_lastEvalDay.load()) {
            INTERNAL_INFO_STREAM << "[DailyEod] 启动时已过触发时间 " << m_eodTriggerMinute << "min, 立即补评估: " << todayStr;
            doEvaluate(todayStr);
        }
    }

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
}

// ═══════════════════════════════════════════════════════════════════
// 轮询检查: 到 m_eodTriggerMinute 即投递评估, 不依赖 gmsdk
// ═══════════════════════════════════════════════════════════════════

void DailyEodScheduler::schedulePollCheck() {
    if (!m_polling.load()) return;
    int mins = getCurrentLocalMinutes();
    auto today = getCurrentTradingDay();
    if (mins >= m_eodTriggerMinute && today > m_lastEvalDay.load()) {
        std::string todayStr = std::to_string(today);
        INTERNAL_INFO_STREAM << "[DailyEod] 轮询触发 " << todayStr
                             << " (" << mins << "min >= " << m_eodTriggerMinute << "min)";
        m_post([this, todayStr]() { doEvaluate(todayStr); });
        // 当天已触发, 60分钟后恢复检查
        m_pollExecutor->post([this]() {
            std::this_thread::sleep_for(std::chrono::minutes(60));
            schedulePollCheck();
        });
    } else {
        // 未到时间, 30秒后重试
        m_pollExecutor->post([this]() {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            schedulePollCheck();
        });
    }
}

// ═══════════════════════════════════════════════════════════════════
// EOD 触发 (gmsdk 线程, 保留作为兜底)
// ═══════════════════════════════════════════════════════════════════

void DailyEodScheduler::onEodTrigger(const std::string& tradingDay) {
    // 已停止, 不投递 (stop() 先设标志, 再停 executor)
    if (!m_eodRegistered.load(std::memory_order_acquire)) return;

    // 检查是否已达到触发时间（EOD 回调可能在触发时间之后到，此时立即执行）
    if (getCurrentLocalMinutes() < m_eodTriggerMinute) {
        INTERNAL_INFO_STREAM << "[DailyEod] EOD 回调到来但未到触发时间"
            << " (当前=" << getCurrentLocalMinutes() << "min 触发=" << m_eodTriggerMinute << "min), 忽略";
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
    if (evalDay <= m_lastEvalDay.load()) {
        INTERNAL_INFO_STREAM << "[DailyEod] 交易日 " << tradingDay << " 已评估, 跳过 (last=" << m_lastEvalDay.load() << ")";
        return;
    }

    // 自动判定: 评估日 < 今天 → 补偿 (用历史收盘价)
    auto today = getCurrentTradingDay();
    bool isCompensation = (evalDay < today);

    // 补评: 仅更新日期, 不触发评估, 不下单
    if (isCompensation) {
        INTERNAL_INFO_STREAM << "[DailyEod] 补评: tradingDay=" << tradingDay
                             << " 仅更新日期, 跳过评估";
        m_lastEvalDay.store(evalDay);
        persistLastEvalDay();
        return;
    }

    if (!m_evalFn) {
        INTERNAL_WARN_STREAM << "[DailyEod] EvalFn 未设置, 跳过";
        return;
    }

    INTERNAL_INFO_STREAM << "[DailyEod] 开始评估 tradingDay=" << tradingDay
                         << " isCompensation=" << isCompensation;

    EodEvaluationStatus status = EodEvaluationStatus::Error;
    try {
        status = m_evalFn(tradingDay, false);
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[DailyEod] 评估异常: " << e.what();
        status = EodEvaluationStatus::Error;
    } catch (...) {
        INTERNAL_ERROR_STREAM << "[DailyEod] 评估未知异常";
        status = EodEvaluationStatus::Error;
    }

    // 只在篮子已提交或无信号时持久化，其他状态允许补单重试
    if (status == EodEvaluationStatus::Submitted || status == EodEvaluationStatus::NoSignal) {
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
    char out[32] = {};
    // date 格式: "YYYYMMDD", gmsdk 接口接受此格式
    int ret = ::get_previous_trading_date("SZSE", date.c_str(), out);
    if (ret != 0 || out[0] == '\0') {
        // 试 SHSE
        ret = ::get_previous_trading_date("SHSE", date.c_str(), out);
    }
    if (ret != 0 || out[0] == '\0') {
        INTERNAL_WARN_STREAM << "[DailyEod] gmsdk get_previous_trading_date 失败, 本地回退 date=" << date;
        // 本地回退: 减1天, 跳过周末
        int y = std::stoi(date.substr(0,4)), m = std::stoi(date.substr(4,2)), d = std::stoi(date.substr(6,2));
        struct tm t = {}; t.tm_year=y-1900; t.tm_mon=m-1; t.tm_mday=d;
        time_t epoch = mktime(&t);
        do { epoch -= 86400; struct tm prev; localtime_s(&prev, &epoch);
             if (prev.tm_wday != 0 && prev.tm_wday != 6) { // 非周六日
                 char buf[16]; snprintf(buf,sizeof(buf),"%04d%02d%02d",prev.tm_year+1900,prev.tm_mon+1,prev.tm_mday);
                 return std::string(buf);
             }
        } while (true);
    }
    return std::string(out);
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
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    struct tm local;
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local, &tt);
#else
    localtime_r(&tt, &local);
#endif
    return (local.tm_year + 1900) * 10000LL
         + (local.tm_mon + 1) * 100LL
         + local.tm_mday;
}

// ═══════════════════════════════════════════════════════════════════
// 持久化
// ═══════════════════════════════════════════════════════════════════

void DailyEodScheduler::loadLastEvalDay() {
    if (m_persistPath.empty() || m_strategyId.empty()) return;

    // 从统一 JSON 读取 strategyLastEval.<strategyId>
    auto json = foundation::json::JsonFacade::parseFile(m_persistPath);
    if (json.isNull() || !json.isObject()) return;
    if (!json.has("strategyLastEval")) return;
    auto evalMap = json.get("strategyLastEval");
    if (!evalMap.isObject() || !evalMap.has(m_strategyId)) return;

    try {
        m_lastEvalDay.store(static_cast<std::int64_t>(evalMap.get(m_strategyId).asInt()));
        INTERNAL_INFO_STREAM << "[DailyEod] 加载 lastEvalDay=" << m_lastEvalDay.load()
                             << " strategyId=" << m_strategyId;
    } catch (...) {
        m_lastEvalDay.store(0);
    }
}

void DailyEodScheduler::persistLastEvalDay() {
    if (m_persistPath.empty() || m_strategyId.empty()) return;

    // 读取现有 JSON，保留非 strategyLastEval 的顶层键
    auto root = foundation::json::JsonFacade::createObject();
    auto evalMap = foundation::json::JsonFacade::createObject();
    {
        auto existing = foundation::json::JsonFacade::parseFile(m_persistPath);
        if (!existing.isNull() && existing.isObject()) {
            for (const auto& key : existing.keys()) {
                if (key == "strategyLastEval") {
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
    root.set("strategyLastEval", evalMap);

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
