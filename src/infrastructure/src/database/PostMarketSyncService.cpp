#include "database/PostMarketSyncService.h"
#include "database/NativeMySQLConnectionPool.h"
#include "database/ISqlDatabase.h"
#include "../../../engine/include/GmSessionEngine.h"
#include "../../../thirdparty/gmsdk/gmapi.h"
#include "foundation/log/logging.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <map>
#include <sstream>
#include <vector>

namespace astock::infrastructure::database {

PostMarketSyncService::PostMarketSyncService() = default;

PostMarketSyncService::~PostMarketSyncService() {
    m_running.store(false);
    if (m_scheduler && m_scheduler->joinable())
        m_scheduler->join();
}

void PostMarketSyncService::start() {
    if (m_started) return;
    m_started = true;
    m_running.store(true);
    m_scheduler = std::make_unique<std::thread>(&PostMarketSyncService::schedulerLoop, this);
    INTERNAL_INFO_STREAM << "[PostMktSync] 调度线程已启动 today=" << getCurrentTradingDay();
}

bool PostMarketSyncService::forceSyncToday() {
    if (m_running.load()) return false;
    int today = getCurrentTradingDay();
    if (!isTradingDay(today)) { INTERNAL_WARN_STREAM << "[PostMktSync] 非交易日, 跳过"; return false; }
    std::thread([this, today]() { syncAll(today); }).detach();
    return true;
}

std::string PostMarketSyncService::getSyncStatus(int tradingDay) const {
    auto db = astock::database::NativeMySQLConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) return "unknown";
    auto r = db->executeQuery(
        "SELECT status FROM data.sync_task_log WHERE trading_day=$1 AND task_type='DAILY' ORDER BY id DESC LIMIT 1",
        {astock::database::SqlParam{tradingDay}});
    if (r.rowCount() > 0) return r.getRow(0).getString("status");
    return "idle";
}

// ═════════════════════════════════════════════════
// 调度线程
// ═════════════════════════════════════════════════

void PostMarketSyncService::schedulerLoop() {
    while (m_running.load()) {
        int today = getCurrentTradingDay();
        if (!isTradingDay(today)) {
            // 非交易日, 睡到明天
            std::this_thread::sleep_for(std::chrono::hours(4));
            continue;
        }
        int mins = getCurrentLocalMinutes();
        if (mins >= 901) {
            // 已过15:01, 立即触发
            syncAll(today);
            // 同步完成后睡到下一个交易日
            std::this_thread::sleep_for(std::chrono::hours(8));
        } else {
            // 等到15:01
            int waitMin = 901 - mins;
            INTERNAL_INFO_STREAM << "[PostMktSync] 等待 " << waitMin << " 分钟到15:01";
            std::this_thread::sleep_for(std::chrono::minutes(waitMin));
        }
    }
}

// ═════════════════════════════════════════════════
// syncAll
// ═════════════════════════════════════════════════

void PostMarketSyncService::syncAll(int tradingDay) {
    // 直接查今天日线是否有数据，有就跳过
    {
        int y = tradingDay / 10000, m = (tradingDay % 10000) / 100, d = tradingDay % 100;
        char ds[32]; snprintf(ds, sizeof(ds), "%04d-%02d-%02d", y, m, d);
        auto db = astock::database::NativeMySQLConnectionPool::instance().getConnection();
        if (db && db->isOpen()) {
            auto r = db->executeQuery(
                "SELECT COUNT(*) FROM mkt.daily_bar WHERE trade_date=$1",
                {astock::database::SqlParam{std::string(ds)}});
            if (r.rowCount() > 0 && r.getRow(0).getInt(0) > 0) {
                INTERNAL_INFO_STREAM << "[PostMktSync] today=" << tradingDay << " 已有数据, 跳过同步";
                return;
            }
        }
    }

    INTERNAL_INFO_STREAM << "[PostMktSync] 开始同步 today=" << tradingDay;

    // 盘中不允许同步
    int mins = getCurrentLocalMinutes();
    if (mins >= 565 && mins < 900) { // 9:25-15:00
        INTERNAL_WARN_STREAM << "[PostMktSync] 盘中禁止同步, 请15:00后操作";
        return;
    }

    // 预加载 symbol->id 映射
    auto db = astock::database::NativeMySQLConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) { INTERNAL_ERROR_STREAM << "[PostMktSync] DB不可用"; return; }
    auto res = db->executeQuery("SELECT id, symbol FROM ref.symbol_info WHERE status='ACTIVE'");
    std::unordered_map<std::string,int> symToId;
    std::vector<std::string> symbols;
    for (auto& row : res.getRows()) {
        std::string sym = row.getString("symbol");
        int id = row.getInt("id");
        symToId[sym] = id;
        symbols.push_back(sym);
    }
    INTERNAL_INFO_STREAM << "[PostMktSync] 活跃标的: " << symbols.size();

    // 阶段1: 日线
    if (!syncDaily(db, symToId, symbols, tradingDay)) {
        INTERNAL_ERROR_STREAM << "[PostMktSync] 日线失败, 终止";
        return;
    }
    // 阶段2: 分钟线 (独立, 失败继续)
    syncMinute(db, symToId, symbols, tradingDay);
    // 阶段3-4: 周月线
    syncWeekly(db, tradingDay);
    syncMonthly(db, tradingDay);

    INTERNAL_INFO_STREAM << "[PostMktSync] 全部完成 today=" << tradingDay;
}

// ═════════════════════════════════════════════════
// syncDaily
// ═════════════════════════════════════════════════

bool PostMarketSyncService::syncDaily(std::shared_ptr<astock::database::ISqlDatabase> db,
                                       const std::unordered_map<std::string,int>& symToId,
                                       const std::vector<std::string>& symbols, int tradingDay) {
    logTaskStart("DAILY", tradingDay);
    int ok = 0, noData = 0, err = 0;

    char dateStr[32];
    {
        int y = tradingDay / 10000, m = (tradingDay % 10000) / 100, d = tradingDay % 100;
        snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", y, m, d);
    }

    std::vector<std::vector<astock::database::SqlParam>> batch;
    auto flush = [&]() {
        if (batch.empty()) return;
        std::string sql =
            "INSERT INTO mkt.daily_bar(symbol_id,trade_date,open,high,low,close,volume,turnover,pre_close,data_source) "
            "VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,'GMSDK') "
            "ON CONFLICT(symbol_id,trade_date) DO UPDATE SET "
            "open=EXCLUDED.open,high=EXCLUDED.high,low=EXCLUDED.low,close=EXCLUDED.close,"
            "volume=EXCLUDED.volume,turnover=EXCLUDED.turnover,pre_close=EXCLUDED.pre_close";
        for (auto& p : batch) db->executeUpdate(sql, p);
        batch.clear();
    };

    for (const auto& sym : symbols) {
        const int kMaxRetry = 3;
        bool gotData = false;
        for (int retry = 0; retry < kMaxRetry; ++retry) {
            std::string gm = toGmSymbol(sym);
            if (gm.empty()) { ++err; break; }
            auto* bars = ::history_bars_n(gm.c_str(), "1d", 1, dateStr, 0, nullptr, true, nullptr);
            if (!bars || bars->status() || bars->count() <= 0) {
                if (bars) bars->release();
                if (retry < kMaxRetry - 1) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500 * (1 << retry)));
                    continue;
                }
                ++noData; // 停牌无数据 = 正常
                break;
            }
            auto& b = bars->at(0);
            if (b.close <= 0) { bars->release(); ++noData; break; }
            auto it = symToId.find(sym);
            if (it == symToId.end()) { bars->release(); ++err; break; }
            using P = astock::database::SqlParam;
            batch.push_back({P{it->second}, P{tradingDay},
                P{static_cast<double>(b.open)}, P{static_cast<double>(b.high)},
                P{static_cast<double>(b.low)}, P{static_cast<double>(b.close)},
                P{static_cast<int64_t>(b.volume)}, P{b.amount},
                P{static_cast<double>(b.pre_close > 0 ? b.pre_close : 0.0)}});
            bars->release();
            ++ok; gotData = true;
            if (batch.size() >= 500) flush();
            break;
        }
        if (!gotData && err > noData) { err = ok > 0 ? err : err; } // track errors
    }
    flush();

    int total = static_cast<int>(symbols.size());
    bool success = (err < total * 0.05) && (err < 50);
    logTaskEnd("DAILY", tradingDay, success, ok, std::to_string(err) + " errors " + std::to_string(noData) + " no_data");
    return success;
}

// ═════════════════════════════════════════════════
// syncMinute
// ═════════════════════════════════════════════════

bool PostMarketSyncService::syncMinute(std::shared_ptr<astock::database::ISqlDatabase> db,
                                        const std::unordered_map<std::string,int>& symToId,
                                        const std::vector<std::string>& symbols, int tradingDay) {
    logTaskStart("MINUTE", tradingDay);
    int ok = 0, err = 0;

    char sDate[32], eDate[32];
    int y = tradingDay / 10000, m = (tradingDay % 10000) / 100, d = tradingDay % 100;
    snprintf(sDate, sizeof(sDate), "%04d-%02d-%02d 09:30:00", y, m, d);
    snprintf(eDate, sizeof(eDate), "%04d-%02d-%02d 15:00:00", y, m, d);

    std::vector<std::vector<astock::database::SqlParam>> batch;
    auto flush = [&]() {
        if (batch.empty()) return;
        std::string sql =
            "INSERT INTO mkt.minute_bar(symbol_id,trade_ts,open,high,low,close,volume,amount) "
            "VALUES($1,$2,$3,$4,$5,$6,$7,$8) "
            "ON CONFLICT(symbol_id,trade_ts) DO UPDATE SET "
            "open=EXCLUDED.open,high=EXCLUDED.high,low=EXCLUDED.low,close=EXCLUDED.close,"
            "volume=EXCLUDED.volume,amount=EXCLUDED.amount";
        for (auto& p : batch) db->executeUpdate(sql, p);
        batch.clear();
    };

    for (const auto& sym : symbols) {
        std::string gm = toGmSymbol(sym);
        if (gm.empty()) { ++err; continue; }
        auto* bars = ::history_bars(gm.c_str(), "60s", sDate, eDate, 0, nullptr, true, nullptr);
        if (!bars || bars->status() || bars->count() <= 0) {
            if (bars) bars->release();
            continue;
        }
        auto it = symToId.find(sym);
        if (it == symToId.end()) { bars->release(); ++err; continue; }
        int sid = it->second;
        for (size_t i = 0; i < bars->count(); ++i) {
            auto& b = bars->at(i);
            if (b.close <= 0) continue;
            // bob is epoch seconds → timestamptz
            auto ts = static_cast<time_t>(static_cast<int64_t>(b.bob));
            char tsBuf[64];
            struct tm utc; gmtime_s(&utc, &ts);
            strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%d %H:%M:%S+08", &utc);
            using P = astock::database::SqlParam;
            batch.push_back({P{sid}, P{std::string(tsBuf)},
                P{static_cast<double>(b.open)}, P{static_cast<double>(b.high)},
                P{static_cast<double>(b.low)}, P{static_cast<double>(b.close)},
                P{static_cast<int64_t>(b.volume)}, P{b.amount}});
            if (batch.size() >= 500) flush();
            ++ok;
        }
        bars->release();
    }
    flush();
    logTaskEnd("MINUTE", tradingDay, true, ok);
    return true;
}

// ═════════════════════════════════════════════════
// syncWeekly (从daily_bar聚合)
// ═════════════════════════════════════════════════

bool PostMarketSyncService::syncWeekly(std::shared_ptr<astock::database::ISqlDatabase> db, int tradingDay) {
    logTaskStart("WEEKLY", tradingDay);
    // 聚合并upsert本周数据
    std::string sql = R"(
        INSERT INTO mkt.weekly_bar(symbol_id,trade_date,open,high,low,close,volume,turnover,pre_close,data_source)
        SELECT d.symbol_id,
               (date_trunc('week', d.trade_date) + interval '6 days')::date AS trade_date,
               (array_agg(d.open ORDER BY d.trade_date))[1] AS open,
               MAX(d.high) AS high, MIN(d.low) AS low,
               (array_agg(d.close ORDER BY d.trade_date))[array_upper(array_agg(d.close ORDER BY d.trade_date),1)] AS close,
               SUM(d.volume) AS volume, SUM(d.turnover) AS turnover,
               NULL::numeric AS pre_close, 'GMSDK' AS data_source
        FROM mkt.daily_bar d
        WHERE d.trade_date >= date_trunc('week', $1::date)::date
          AND d.trade_date <= (date_trunc('week', $1::date) + interval '6 days')::date
        GROUP BY d.symbol_id, date_trunc('week', d.trade_date)::date
        ON CONFLICT(symbol_id,trade_date) DO UPDATE SET
          open=EXCLUDED.open,high=EXCLUDED.high,low=EXCLUDED.low,close=EXCLUDED.close,
          volume=EXCLUDED.volume,turnover=EXCLUDED.turnover
    )";
    int y = tradingDay/10000, m = (tradingDay%10000)/100, d = tradingDay%100;
    char buf[16]; snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
    db->executeUpdate(sql, {astock::database::SqlParam{std::string(buf)}});
    logTaskEnd("WEEKLY", tradingDay, true, 0);
    return true;
}

// ═════════════════════════════════════════════════
// syncMonthly
// ═════════════════════════════════════════════════

bool PostMarketSyncService::syncMonthly(std::shared_ptr<astock::database::ISqlDatabase> db, int tradingDay) {
    logTaskStart("MONTHLY", tradingDay);
    std::string sql = R"(
        INSERT INTO mkt.monthly_bar(symbol_id,trade_date,open,high,low,close,volume,turnover,pre_close,data_source)
        SELECT d.symbol_id,
               (date_trunc('month', d.trade_date) + interval '1 month - 1 day')::date AS trade_date,
               (array_agg(d.open ORDER BY d.trade_date))[1] AS open,
               MAX(d.high) AS high, MIN(d.low) AS low,
               (array_agg(d.close ORDER BY d.trade_date))[array_upper(array_agg(d.close ORDER BY d.trade_date),1)] AS close,
               SUM(d.volume) AS volume, SUM(d.turnover) AS turnover,
               NULL::numeric AS pre_close, 'GMSDK' AS data_source
        FROM mkt.daily_bar d
        WHERE d.trade_date >= date_trunc('month', $1::date)::date
          AND d.trade_date <= (date_trunc('month', $1::date) + interval '1 month - 1 day')::date
        GROUP BY d.symbol_id, date_trunc('month', d.trade_date)::date
        ON CONFLICT(symbol_id,trade_date) DO UPDATE SET
          open=EXCLUDED.open,high=EXCLUDED.high,low=EXCLUDED.low,close=EXCLUDED.close,
          volume=EXCLUDED.volume,turnover=EXCLUDED.turnover
    )";
    int y = tradingDay/10000, m = (tradingDay%10000)/100, d = tradingDay%100;
    char buf[16]; snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
    db->executeUpdate(sql, {astock::database::SqlParam{std::string(buf)}});
    logTaskEnd("MONTHLY", tradingDay, true, 0);
    return true;
}

// ═════════════════════════════════════════════════
// 工具
// ═════════════════════════════════════════════════

void PostMarketSyncService::logTaskStart(const std::string& taskType, int tradingDay) {
    auto db = astock::database::NativeMySQLConnectionPool::instance().getConnection();
    if (!db) return;
    std::string sql = "INSERT INTO data.sync_task_log(trading_day,task_type,status,started_at) VALUES($1,$2,'running',NOW())";
    db->executeUpdate(sql, {astock::database::SqlParam{tradingDay}, astock::database::SqlParam{taskType}});
}

void PostMarketSyncService::logTaskEnd(const std::string& taskType, int tradingDay,
                                        bool success, int rows, const std::string& error) {
    auto db = astock::database::NativeMySQLConnectionPool::instance().getConnection();
    if (!db) return;
    std::string sql = "UPDATE data.sync_task_log SET status=$1, rows_written=$2, ended_at=NOW(), error_msg=$3 "
                      "WHERE trading_day=$4 AND task_type=$5 AND ended_at IS NULL";
    db->executeUpdate(sql, {
        astock::database::SqlParam{std::string(success ? "success" : "fail")},
        astock::database::SqlParam{rows},
        astock::database::SqlParam{error},
        astock::database::SqlParam{tradingDay},
        astock::database::SqlParam{taskType}});
}

int PostMarketSyncService::getCurrentLocalMinutes() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    struct tm local;
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local, &tt);
#else
    localtime_r(&tt, &local);
#endif
    return local.tm_hour * 60 + local.tm_min;
}

int PostMarketSyncService::getCurrentTradingDay() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    struct tm local;
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local, &tt);
#else
    localtime_r(&tt, &local);
#endif
    return (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
}

bool PostMarketSyncService::isTradingDay(int date) {
    auto db = astock::database::NativeMySQLConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) return true; // 兜底: 假设是交易日
    auto r = db->executeQuery(
        "SELECT is_trading_day FROM ref.trade_calendar WHERE trade_date=$1",
        {astock::database::SqlParam{date}});
    if (r.rowCount() > 0) return r.getRow(0).getInt("is_trading_day") == 1;
    // 周四五六日 → 可能是交易日, 简单跳过周末
    struct tm t = {};
    t.tm_year = date / 10000 - 1900;
    t.tm_mon = (date % 10000) / 100 - 1;
    t.tm_mday = date % 100;
    mktime(&t);
    return t.tm_wday != 0 && t.tm_wday != 6;
}

std::string PostMarketSyncService::toGmSymbol(const std::string& sym) {
    auto d = sym.find('.');
    if (d == std::string::npos) return "";
    std::string code = sym.substr(0, d), ex = sym.substr(d + 1);
    if (ex == "SH") return "SHSE." + code;
    if (ex == "SZ") return "SZSE." + code;
    if (ex == "BJ") return "BSE." + code;
    return "";
}

} // namespace astock::infrastructure::database
