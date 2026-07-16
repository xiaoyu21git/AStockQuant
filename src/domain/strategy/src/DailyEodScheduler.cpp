#include "DailyEodScheduler.h"
#include "../../market/include/MarketDataService.h"
#include "../../../thirdparty/gmsdk/gmapi.h"
#include "foundation/log/logging.hpp"

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

    // ── 补单窗口: 0:00 ~ 9:30 ──
    if (isCompensationWindow()) {
        auto today = getCurrentTradingDay();
        std::string todayStr = std::to_string(today);
        std::string prevDay = getPreviousTradingDay(todayStr);
        if (!prevDay.empty()) {
            auto prev = std::stoll(prevDay);
            if (m_lastEvalDay < prev) {
                INTERNAL_INFO_STREAM << "[DailyEod] 补单窗口, 缺失评估日: " << prev
                                     << " 当前 lastEval=" << m_lastEvalDay;
                doEvaluate(prevDay);
            }
        }
    }

    // ── 启动时检查: 如果当前时间已过EOD触发时间且今天未评估，立即补评估 ──
    {
        int mins = getCurrentLocalMinutes();
        auto today = getCurrentTradingDay();
        std::string todayStr = std::to_string(today);
        if (mins >= m_eodTriggerMinute && std::stoll(todayStr) > m_lastEvalDay) {
            INTERNAL_INFO_STREAM << "[DailyEod] 启动时已过触发时间 " << m_eodTriggerMinute << "min, 立即补评估: " << todayStr;
            doEvaluate(todayStr);
        }
    }

    // 注册 MarketDataService EOD 回调 (gmsdk 15:00-15:30 触发, onEodTrigger 时间门控)
    if (!m_eodRegistered) {
        domain::market::MarketDataService::instance()
            .registerEndOfDayCallback([this](const std::string& tradingDay) {
                onEodTrigger(tradingDay);
            });
        m_eodRegistered = true;
        INTERNAL_INFO_STREAM << "[DailyEod] EOD 回调已注册";
    }
}

void DailyEodScheduler::stop() {
    // EOD 回调注销由 MarketDataService 生命周期管理，这里仅设标志
    m_eodRegistered = false;
}

// ═══════════════════════════════════════════════════════════════════
// EOD 触发 (gmsdk 线程)
// ═══════════════════════════════════════════════════════════════════

void DailyEodScheduler::onEodTrigger(const std::string& tradingDay) {
    // 已停止, 不投递 (executor 可能已销毁)
    if (!m_eodRegistered) return;

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
    if (evalDay <= m_lastEvalDay) {
        INTERNAL_INFO_STREAM << "[DailyEod] 交易日 " << tradingDay << " 已评估, 跳过 (last=" << m_lastEvalDay << ")";
        return;
    }

    // 自动判定: 评估日 < 今天 → 补偿 (用历史收盘价)
    auto today = getCurrentTradingDay();
    bool isCompensation = (evalDay < today);

    // start() 路径需要窗口校验, onEodTrigger(MarketDataService已校验)跳过
    if (isCompensation && !isCompensationWindow()) {
        INTERNAL_WARN_STREAM << "[DailyEod] 补单窗口校验失败, 跳过";
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
        status = m_evalFn(tradingDay, isCompensation);
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[DailyEod] 评估异常: " << e.what();
        status = EodEvaluationStatus::Error;
    } catch (...) {
        INTERNAL_ERROR_STREAM << "[DailyEod] 评估未知异常";
        status = EodEvaluationStatus::Error;
    }

    // 只在篮子已提交或无信号时持久化，其他状态允许补单重试
    if (status == EodEvaluationStatus::Submitted || status == EodEvaluationStatus::NoSignal) {
        m_lastEvalDay = evalDay;
        persistLastEvalDay();
    } else {
        INTERNAL_WARN_STREAM << "[DailyEod] 不持久化 lastEvalDay, status="
                             << static_cast<int>(status) << " (等待补单重试)";
    }
    INTERNAL_INFO_STREAM << "[DailyEod] 评估完成, lastEvalDay=" << m_lastEvalDay
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
    if (m_persistPath.empty()) return;
    std::ifstream f(m_persistPath);
    if (!f.is_open()) return;
    std::string line;
    if (std::getline(f, line)) {
        try {
            m_lastEvalDay = std::stoll(line);
            INTERNAL_INFO_STREAM << "[DailyEod] 加载 lastEvalDay=" << m_lastEvalDay;
        } catch (...) {
            m_lastEvalDay = 0;
        }
    }
}

void DailyEodScheduler::persistLastEvalDay() {
    if (m_persistPath.empty()) return;
    // 原子写入: 先写临时文件, 再重命名
    std::string tmpPath = m_persistPath + ".tmp";
    {
        std::ofstream f(tmpPath, std::ios::trunc);
        if (!f.is_open()) {
            INTERNAL_WARN_STREAM << "[DailyEod] 无法写入持久化文件: " << tmpPath;
            return;
        }
        f << m_lastEvalDay << "\n";
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
