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

    // ── 预收盘窗口: 14:50 ~ 15:00 启动 → 立即触发 ──
    if (isPreCloseWindow()) {
        auto today = getCurrentTradingDay();
        std::string todayStr = std::to_string(today);
        if (std::stoll(todayStr) > m_lastEvalDay) {
            INTERNAL_INFO_STREAM << "[DailyEod] 预收盘窗口内启动, 立即评估: " << todayStr;
            doEvaluate(todayStr);
        }
    }

    // 注册 MarketDataService EOD 回调
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

    // 二次校验时间窗口
    if (isCompensation && !isCompensationWindow()) {
        INTERNAL_WARN_STREAM << "[DailyEod] 补偿评估但不在补单窗口, 跳过";
        return;
    }
    if (!isCompensation && !isPreCloseWindow()) {
        INTERNAL_WARN_STREAM << "[DailyEod] 实时评估但不在预收盘窗口, 跳过";
        return;
    }

    if (!m_evalFn) {
        INTERNAL_WARN_STREAM << "[DailyEod] EvalFn 未设置, 跳过";
        return;
    }

    INTERNAL_INFO_STREAM << "[DailyEod] 开始评估 tradingDay=" << tradingDay
                         << " isCompensation=" << isCompensation;
    m_evalFn(tradingDay, isCompensation);

    m_lastEvalDay = evalDay;
    persistLastEvalDay();
    INTERNAL_INFO_STREAM << "[DailyEod] 评估完成, lastEvalDay=" << m_lastEvalDay;
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
        INTERNAL_WARN_STREAM << "[DailyEod] get_previous_trading_date 失败 date=" << date;
        return "";
    }
    // gmsdk returns YYYYMMDD
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
    if (std::rename(tmpPath.c_str(), m_persistPath.c_str()) != 0) {
        INTERNAL_WARN_STREAM << "[DailyEod] 持久化文件重命名失败: " << m_persistPath;
    }
}

} // namespace domain::strategy
