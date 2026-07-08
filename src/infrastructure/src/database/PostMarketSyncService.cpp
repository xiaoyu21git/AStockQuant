#include "database/PostMarketSyncService.h"
#include "database/NativePgConnectionPool.h"
#include "database/ISqlDatabase.h"
#include "../../../engine/include/GmSessionEngine.h"
#include "../../../thirdparty/gmsdk/gmapi.h"
#include "foundation/log/logging.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace astock::infrastructure::database {

PostMarketSyncService& PostMarketSyncService::instance() {
    static PostMarketSyncService s;
    return s;
}

PostMarketSyncService::PostMarketSyncService() = default;

PostMarketSyncService::~PostMarketSyncService() {
    m_running.store(false);
    if (m_scheduler && m_scheduler->joinable())
        m_scheduler->join();
}

void PostMarketSyncService::start() {
    if (m_started) return;
    m_started = true;
    m_persistPath = "post_market_sync_last.txt";
    loadLastSyncDay();
    m_running.store(true);
    m_scheduler = std::make_unique<std::thread>(&PostMarketSyncService::schedulerLoop, this);
    INTERNAL_INFO_STREAM << "[PostMktSync] 调度线程已启动 today=" << getCurrentTradingDay()
                         << " lastSync=" << m_lastSyncDay.load();
}

bool PostMarketSyncService::forceSyncToday() {
    int today = getCurrentTradingDay();
    if (!isTradingDay(today)) { INTERNAL_WARN_STREAM << "[PostMktSync] 非交易日, 跳过"; return false; }
    std::thread([this, today]() {
        auto db = astock::database::NativePgConnectionPool::instance().getConnection();
        if (!db || !db->isOpen()) { INTERNAL_ERROR_STREAM << "[PostMktSync] DB不可用"; return; }
        auto calRes = db->executeQuery(
            "SELECT trade_date::text AS dt FROM ref.trade_calendar "
            "WHERE is_trading_day=true AND trade_date>=CURRENT_DATE-365 AND trade_date<=CURRENT_DATE ORDER BY trade_date");
        std::vector<std::string> tds; for(auto&r:calRes.getRows())tds.push_back(r.getString("dt"));
        if(tds.empty())return;
        auto symRes=db->executeQuery("SELECT id,symbol FROM ref.symbol_info WHERE status='ACTIVE'");
        std::unordered_map<std::string,int> s2i;std::vector<std::string> syms;
        for(auto&r:symRes.getRows()){s2i[r.getString("symbol")]=r.getInt("id");syms.push_back(r.getString("symbol"));}
        INTERNAL_INFO_STREAM<<"[PostMktSync] 同步启动, 活跃标的: "<<syms.size();
        auto covRes=db->executeQuery("SELECT trade_date::text AS dt,COUNT(DISTINCT symbol_id) AS cnt FROM mkt.daily_bar WHERE trade_date>=CURRENT_DATE-365 GROUP BY trade_date");
        std::unordered_map<std::string,int> cov;int mc=0;
        for(auto&r:covRes.getRows()){int c=r.getInt("cnt");cov[r.getString("dt")]=c;if(c>mc)mc=c;}
        std::vector<std::string> md;
        for(auto&d:tds){int c=cov.count(d)?cov[d]:0;if(c<mc*90/100)md.push_back(d);}
        if(!md.empty()){
            INTERNAL_INFO_STREAM<<"[PostMktSync] ====== 补齐缺口 "<<md.size()<<" 天: "<<md.front()<<" ~ "<<md.back()<<" ======";
            int mindate=foundation::utils::Timestamp(md.front(),"%Y-%m-%d").to_yyyymmdd();
            int maxdate=foundation::utils::Timestamp(md.back(),"%Y-%m-%d").to_yyyymmdd();
            syncDailyRange(db,s2i,syms,mindate,maxdate,md);
            for(auto&dt:md){int td=foundation::utils::Timestamp(dt,"%Y-%m-%d").to_yyyymmdd();syncMinute(db,s2i,syms,td);syncWeekly(db,td);syncMonthly(db,td);}
        }
        int mins=getCurrentLocalMinutes();
        if(mins>=900&&m_lastSyncDay.load()!=today){syncDailyMinute(today);syncWeeklyMonthly(today);if(isMonthlyMaintenanceDay())syncFinancialData(today);}
        if(mins>=900){m_lastSyncDay.store(today);saveLastSyncDay(today);}
        INTERNAL_INFO_STREAM<<"[PostMktSync] ====== 同步完成 ======";
    }).detach();
    return true;
}

void PostMarketSyncService::forceSyncDate(int tradingDay) {
    INTERNAL_INFO_STREAM<<"[PostMktSync] 手动补同步 tradingDay="<<tradingDay;
    std::thread([this,tradingDay](){
        auto db=astock::database::NativePgConnectionPool::instance().getConnection();
        if(!db||!db->isOpen())return;
        auto res=db->executeQuery("SELECT id,symbol FROM ref.symbol_info WHERE status='ACTIVE'");
        std::unordered_map<std::string,int> s2i;std::vector<std::string> syms;
        for(auto&r:res.getRows()){s2i[r.getString("symbol")]=r.getInt("id");syms.push_back(r.getString("symbol"));}
        if(syncDaily(db,s2i,syms,tradingDay))syncMinute(db,s2i,syms,tradingDay);
        syncWeekly(db,tradingDay);syncMonthly(db,tradingDay);
    }).detach();
}

void PostMarketSyncService::forceSyncMissingDays(int lookbackDays) {
    INTERNAL_INFO_STREAM<<"[PostMktSync] 扫描缺口 lookbackDays="<<lookbackDays;
    std::thread([this,lookbackDays](){
        auto db=astock::database::NativePgConnectionPool::instance().getConnection();
        if(!db||!db->isOpen())return;
        auto cal=db->executeQuery("SELECT trade_date::text AS dt FROM ref.trade_calendar WHERE is_trading_day=true AND trade_date>=CURRENT_DATE-"+std::to_string(lookbackDays)+" AND trade_date<=CURRENT_DATE ORDER BY trade_date");
        std::vector<std::string> tds;for(auto&r:cal.getRows())tds.push_back(r.getString("dt"));
        if(tds.empty())return;
        auto sr=db->executeQuery("SELECT id,symbol FROM ref.symbol_info WHERE status='ACTIVE'");
        std::unordered_map<std::string,int> s2i;std::vector<std::string> syms;
        for(auto&r:sr.getRows()){s2i[r.getString("symbol")]=r.getInt("id");syms.push_back(r.getString("symbol"));}
        auto cr=db->executeQuery("SELECT trade_date::text AS dt,COUNT(DISTINCT symbol_id) AS cnt FROM mkt.daily_bar WHERE trade_date>=CURRENT_DATE-"+std::to_string(lookbackDays)+" GROUP BY trade_date");
        std::unordered_map<std::string,int> cov;int mc=0;
        for(auto&r:cr.getRows()){int c=r.getInt("cnt");cov[r.getString("dt")]=c;if(c>mc)mc=c;}
        std::vector<std::string> md;
        for(auto&d:tds){int c=cov.count(d)?cov[d]:0;if(c<mc*90/100)md.push_back(d);}
        if(md.empty())return;
        for(auto&dt:md){int td=foundation::utils::Timestamp(dt,"%Y-%m-%d").to_yyyymmdd();
            if(syncDaily(db,s2i,syms,td))syncMinute(db,s2i,syms,td);syncWeekly(db,td);syncMonthly(db,td);}
        INTERNAL_INFO_STREAM<<"[PostMktSync] 补齐 "<<md.size()<<" 天";
    }).detach();
}

void PostMarketSyncService::probeGmCoverage(const std::string& symbol, const std::vector<std::string>& dates) {
    auto d=symbol.find('.');std::string gm;
    if(d!=std::string::npos){std::string c=symbol.substr(0,d),e=symbol.substr(d+1);if(e=="SH")gm="SHSE."+c;else if(e=="SZ")gm="SZSE."+c;else if(e=="BJ")gm="BSE."+c;}
    if(gm.empty()){INTERNAL_ERROR_STREAM<<"[GmProbe] 无效标的: "<<symbol;return;}
    for(auto&dt:dates){
        auto* bars=::history_bars_n(gm.c_str(),"1d",1,dt.c_str(),0,nullptr,true,nullptr);
        if(!bars)INTERNAL_WARN_STREAM<<"[GmProbe] "<<dt<<" bars=null";
        else if(bars->status()!=0){INTERNAL_WARN_STREAM<<"[GmProbe] "<<dt<<" status="<<bars->status();bars->release();}
        else if(bars->count()<=0){INTERNAL_INFO_STREAM<<"[GmProbe] "<<dt<<" count=0";bars->release();}
        else{auto&b=bars->at(0);INTERNAL_INFO_STREAM<<"[GmProbe] "<<dt<<" OK O="<<b.open<<" C="<<b.close;bars->release();}
    }
}

void PostMarketSyncService::fillAdjFactors() {
    INTERNAL_INFO_STREAM<<"[PostMktSync] fillAdjFactors 开始";
    std::thread([this](){
        auto db=astock::database::NativePgConnectionPool::instance().getConnection();
        if(!db||!db->isOpen()){INTERNAL_ERROR_STREAM<<"[PostMktSync] fillAdjFactors DB不可用";return;}
        auto res=db->executeQuery("SELECT d.symbol_id,si.symbol,d.trade_date::text FROM mkt.daily_bar d JOIN ref.symbol_info si ON d.symbol_id=si.id WHERE d.data_source='GMSDK' AND d.pre_adjust_factor=1.0");
        struct Rec{int sid;std::string sym,dt;};std::vector<Rec> recs;
        for(auto&r:res.getRows())recs.push_back({r.getInt("symbol_id"),r.getString("symbol"),r.getString("trade_date")});
        INTERNAL_INFO_STREAM<<"[PostMktSync] fillAdjFactors "<<recs.size()<<" 条待补";if(recs.empty())return;
        std::string sql="UPDATE mkt.daily_bar SET pre_adjust_factor=$1,post_adjust_factor=$2 WHERE symbol_id=$3 AND trade_date=$4::date";
        int ok=0,proc=0,total=static_cast<int>(recs.size());
        for(auto&r:recs){
            std::string gm;{auto d=r.sym.find('.');if(d!=std::string::npos){std::string c=r.sym.substr(0,d),e=r.sym.substr(d+1);if(e=="SH")gm="SHSE."+c;else if(e=="SZ")gm="SZSE."+c;else if(e=="BJ")gm="BSE."+c;}}
            if(gm.empty())continue;
            auto* af=::stk_get_adj_factor(gm.c_str(),r.dt.c_str(),r.dt.c_str());
            if(af&&af->status()==0&&af->count()>0){auto&f=af->at(0);
                if(std::isfinite(f.adj_factor_fwd)&&f.adj_factor_fwd>0&&std::isfinite(f.adj_factor_bwd)&&f.adj_factor_bwd>0){
                    using P=astock::database::SqlParam;db->executeUpdate(sql,{P{f.adj_factor_fwd},P{f.adj_factor_bwd},P{r.sid},P{r.dt}});++ok;
                }}if(af){af->release();af=nullptr;}
            ++proc;if(proc%500==0)INTERNAL_INFO_STREAM<<"[PostMktSync] fillAdjFactors "<<(proc*100/total)<<"% "<<proc<<"/"<<total<<" (ok="<<ok<<")";
        }
        INTERNAL_INFO_STREAM<<"[PostMktSync] fillAdjFactors 完成 "<<ok<<"/"<<total;
    }).detach();
}

std::string PostMarketSyncService::getSyncStatus(int tradingDay) const {
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
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
            // 已过15:01 — 检查是否今天已经同步过
            if (m_lastSyncDay.load() == today) {
                INTERNAL_INFO_STREAM << "[PostMktSync] today=" << today << " 已同步过，跳过";
                std::this_thread::sleep_for(std::chrono::hours(1));
                continue;
            }
            syncAll(today);
            m_lastSyncDay.store(today);
            saveLastSyncDay(today);
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
        auto db = astock::database::NativePgConnectionPool::instance().getConnection();
        if (db && db->isOpen()) {
            auto r = db->executeQuery(
                "SELECT COUNT(*) FROM mkt.daily_bar WHERE trade_date=$1::date",
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
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
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
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db) return;
    std::string sql = "INSERT INTO data.sync_task_log(trading_day,task_type,status,started_at) VALUES($1,$2,'running',NOW())";
    db->executeUpdate(sql, {astock::database::SqlParam{tradingDay}, astock::database::SqlParam{taskType}});
}

void PostMarketSyncService::logTaskEnd(const std::string& taskType, int tradingDay,
                                        bool success, int rows, const std::string& error) {
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
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
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (db && db->isOpen()) {
        auto r = db->executeQuery(
            "SELECT is_trading_day FROM ref.trade_calendar WHERE trade_date=$1::date",
            {astock::database::SqlParam{date}});
        if (r.rowCount() > 0) return r.getRow(0).getInt("is_trading_day") == 1;
    }
    // DB 不可用或查不到时，用 tm_wday 兜底：跳过周六日
    struct tm t = {};
    t.tm_year = date / 10000 - 1900;
    t.tm_mon = (date % 10000) / 100 - 1;
    t.tm_mday = date % 100;
    t.tm_isdst = -1;
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

void PostMarketSyncService::loadLastSyncDay() {
    if (m_persistPath.empty()) return;
    std::ifstream f(m_persistPath);
    if (!f.is_open()) return;
    std::string line;
    if (std::getline(f, line)) {
        try { m_lastSyncDay.store(std::stoi(line)); } catch (...) {}
    }
    INTERNAL_INFO_STREAM << "[PostMktSync] 加载 lastSyncDay=" << m_lastSyncDay.load();
}

void PostMarketSyncService::saveLastSyncDay(int tradingDay) {
    if (m_persistPath.empty()) return;
    std::string tmpPath = m_persistPath + ".tmp";
    {
        std::ofstream f(tmpPath, std::ios::trunc);
        if (!f.is_open()) return;
        f << tradingDay << "\n";
    }
    std::rename(tmpPath.c_str(), m_persistPath.c_str());
}

bool PostMarketSyncService::syncDailyRange(std::shared_ptr<astock::database::ISqlDatabase> db,
    const std::unordered_map<std::string,int>& symToId, const std::vector<std::string>& symbols,
    int startDay, int endDay, const std::vector<std::string>& targetDates) {
    std::unordered_set<std::string> targets(targetDates.begin(),targetDates.end());
    char sBuf[32],eBuf[32];
    {int y=startDay/10000,m=(startDay%10000)/100,d=startDay%100;snprintf(sBuf,sizeof(sBuf),"%04d-%02d-%02d",y,m,d);}
    {int ed=(foundation::utils::Timestamp::from_yyyymmdd(endDay)+foundation::utils::Duration::days(1)).to_yyyymmdd();
     int y=ed/10000,m=(ed%10000)/100,d=ed%100;snprintf(eBuf,sizeof(eBuf),"%04d-%02d-%02d",y,m,d);}
    INTERNAL_INFO_STREAM<<"[PostMktSync] syncDailyRange "<<sBuf<<" ~ "<<eBuf;
    struct Row{int sid;std::string sym,dt;double o,h,l,c,pc,vol,amt;};
    std::vector<Row> rows;int total=static_cast<int>(symbols.size()),ok=0;
    for(const auto& sym:symbols){
        std::string gm=toGmSymbol(sym);if(gm.empty())continue;
        auto* bars=::history_bars(gm.c_str(),"1d",sBuf,eBuf,0,nullptr,true,nullptr);
        if(!bars||bars->status()!=0||bars->count()<=0){if(bars)bars->release();continue;}
        auto it=symToId.find(sym);if(it==symToId.end()){bars->release();continue;}
        for(size_t k=0;k<bars->count();++k){auto&b=bars->at(k);
            if(!std::isfinite(b.close)||b.close<=0.01||b.close>=10000.0)continue;
            time_t bob=static_cast<time_t>(static_cast<int64_t>(b.bob));struct tm t;gmtime_s(&t,&bob);
            char ds[16];snprintf(ds,sizeof(ds),"%04d-%02d-%02d",t.tm_year+1900,t.tm_mon+1,t.tm_mday);
            std::string dt(ds);if(!targets.count(dt))continue;
            rows.push_back({it->second,sym,dt,b.open,b.high,b.low,b.close,b.pre_close,static_cast<double>(b.volume),b.amount});}
        bars->release();++ok;if(ok%500==0)INTERNAL_INFO_STREAM<<"[PostMktSync] Pass1 "<<(ok*100/total)<<"% "<<ok<<"/"<<total;
    }
    if(rows.empty())return false;
    INTERNAL_INFO_STREAM<<"[PostMktSync] Pass1 done, "<<rows.size()<<" 行";
    std::unordered_map<std::string,int> gmToId;
    for(auto&r:rows){std::string g=toGmSymbol(r.sym);if(!g.empty())gmToId[g]=r.sid;}
    std::unordered_map<std::string,std::unordered_map<int,double>> peM,pbM,mtM,ciM,trM;
    for(const auto& dt:targetDates){
        std::string chunk;for(const auto&r:rows)if(r.dt==dt){std::string g=toGmSymbol(r.sym);if(!g.empty()){if(!chunk.empty())chunk+=",";chunk+=g;}}
        if(chunk.empty())continue;
        auto* v=::stk_get_daily_valuation_pt(chunk.c_str(),"pe_lyr,pb_mrq",dt.c_str());
        if(v&&v->status()==0){while(!v->is_end()){const char* s=v->get_string("symbol");if(s&&s[0]){auto it=gmToId.find(std::string(s));if(it!=gmToId.end()){double pe=v->get_real("pe_lyr"),pb=v->get_real("pb_mrq");if(std::isfinite(pe)&&pe>=-10000&&pe<=100000)peM[dt][it->second]=pe;if(std::isfinite(pb)&&pb>=-10000&&pb<=100000)pbM[dt][it->second]=pb;}}v->next();}}if(v){v->release();v=nullptr;}
        auto* mv=::stk_get_daily_mktvalue_pt(chunk.c_str(),"tot_mv,a_mv",dt.c_str());
        if(mv&&mv->status()==0){while(!mv->is_end()){const char* s=mv->get_string("symbol");if(s&&s[0]){auto it=gmToId.find(std::string(s));if(it!=gmToId.end()){double mc=mv->get_real("tot_mv"),cc=mv->get_real("a_mv");if(std::isfinite(mc)&&mc>=0&&mc<=1e16)mtM[dt][it->second]=mc;if(std::isfinite(cc)&&cc>=0&&cc<=1e16)ciM[dt][it->second]=cc;}}mv->next();}}if(mv){mv->release();mv=nullptr;}
        auto* b=::stk_get_daily_basic_pt(chunk.c_str(),"turnrate",dt.c_str());
        if(b&&b->status()==0){while(!b->is_end()){const char* s=b->get_string("symbol");if(s&&s[0]){auto it=gmToId.find(std::string(s));if(it!=gmToId.end()){double tr=b->get_real("turnrate");if(std::isfinite(tr)&&tr>=0)trM[dt][it->second]=tr;}}b->next();}}if(b){b->release();b=nullptr;}
        INTERNAL_INFO_STREAM<<"[PostMktSync] Pass2 "<<dt;
    }
    std::vector<std::vector<astock::database::SqlParam>> batch;int writeOk=0;
    auto flush=[&](){if(batch.empty())return;
        std::string sql="INSERT INTO mkt.daily_bar(symbol_id,trade_date,open,high,low,close,pre_close,volume,turnover,change_pct,change_amt,amplitude,turnover_rate,pe_ratio,pb_ratio,market_cap,circulating_market_cap,pre_adjust_factor,post_adjust_factor,data_source) VALUES($1,$2::date,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19,'GMSDK') ON CONFLICT(symbol_id,trade_date) DO UPDATE SET open=EXCLUDED.open,high=EXCLUDED.high,low=EXCLUDED.low,close=EXCLUDED.close,pre_close=EXCLUDED.pre_close,volume=EXCLUDED.volume,turnover=EXCLUDED.turnover,change_pct=EXCLUDED.change_pct,change_amt=EXCLUDED.change_amt,amplitude=EXCLUDED.amplitude,turnover_rate=EXCLUDED.turnover_rate,pe_ratio=EXCLUDED.pe_ratio,pb_ratio=EXCLUDED.pb_ratio,market_cap=EXCLUDED.market_cap,circulating_market_cap=EXCLUDED.circulating_market_cap,pre_adjust_factor=EXCLUDED.pre_adjust_factor,post_adjust_factor=EXCLUDED.post_adjust_factor";
        for(auto&p:batch)db->executeUpdate(sql,p);batch.clear();};
    for(auto&r:rows){double pe=peM[r.dt].count(r.sid)?peM[r.dt][r.sid]:0,pb=pbM[r.dt].count(r.sid)?pbM[r.dt][r.sid]:0;double mc=mtM[r.dt].count(r.sid)?mtM[r.dt][r.sid]:0,cc=ciM[r.dt].count(r.sid)?ciM[r.dt][r.sid]:0;double tr=trM[r.dt].count(r.sid)?trM[r.dt][r.sid]:0;double chg=(r.pc>0)?(r.c-r.pc)/r.pc*100:0,amp=(r.pc>0&&r.h-r.l>0)?(r.h-r.l)/r.pc*100:0;using P=astock::database::SqlParam;batch.push_back({P{r.sid},P{r.dt},P{r.o},P{r.h},P{r.l},P{r.c},P{r.pc},P{static_cast<int64_t>(r.vol)},P{r.amt},P{chg},P{r.c-r.pc},P{amp},P{tr},P{pe},P{pb},P{mc},P{cc},P{1.0},P{1.0}});if(batch.size()>=500)flush();++writeOk;}
    flush();
    INTERNAL_INFO_STREAM<<"[PostMktSync] syncDailyRange 完成 "<<writeOk<<" 行 "<<targetDates.size()<<" 天";
    return true;
}

void PostMarketSyncService::syncDailyMinute(int tradingDay) {
    auto db=astock::database::NativePgConnectionPool::instance().getConnection();
    if(!db||!db->isOpen())return;
    auto res=db->executeQuery("SELECT id,symbol FROM ref.symbol_info WHERE status='ACTIVE'");
    std::unordered_map<std::string,int> s2i;std::vector<std::string> syms;
    for(auto&r:res.getRows()){s2i[r.getString("symbol")]=r.getInt("id");syms.push_back(r.getString("symbol"));}
    if(!syncDaily(db,s2i,syms,tradingDay))INTERNAL_ERROR_STREAM<<"[PostMktSync] syncDailyMinute 日线失败";
    else syncMinute(db,s2i,syms,tradingDay);
}

void PostMarketSyncService::syncWeeklyMonthly(int tradingDay) {
    auto db=astock::database::NativePgConnectionPool::instance().getConnection();
    if(db&&db->isOpen()){syncWeekly(db,tradingDay);syncMonthly(db,tradingDay);}
}

void PostMarketSyncService::syncFinancialData(int tradingDay) {
    auto db=astock::database::NativePgConnectionPool::instance().getConnection();
    if(!db||!db->isOpen())return;
    auto res=db->executeQuery("SELECT id,symbol FROM ref.symbol_info WHERE status='ACTIVE'");
    std::unordered_map<std::string,int> s2i;std::vector<std::string> syms;
    for(auto&r:res.getRows()){s2i[r.getString("symbol")]=r.getInt("id");syms.push_back(r.getString("symbol"));}
    syncFinancial(db,s2i,syms,tradingDay);
}

bool PostMarketSyncService::isMonthlyMaintenanceDay() {
    auto now=std::chrono::system_clock::now();auto tt=std::chrono::system_clock::to_time_t(now);struct tm local;
#if defined(_WIN32)||defined(_WIN64)
    localtime_s(&local,&tt);
#else
    localtime_r(&tt,&local);
#endif
    return local.tm_mday>=1&&local.tm_mday<=5;
}

bool PostMarketSyncService::syncFinancial(std::shared_ptr<astock::database::ISqlDatabase> db,
    const std::unordered_map<std::string,int>& symToId,const std::vector<std::string>& symbols,int tradingDay) {
    logTaskStart("FINANCIAL",tradingDay);int ok=0,skip=0;char dateStr[32];
    {int y=tradingDay/10000,m=(tradingDay%10000)/100,d=tradingDay%100;snprintf(dateStr,sizeof(dateStr),"%04d-%02d-%02d",y,m,d);}
    std::string gmList;for(const auto& sym:symbols){std::string gm=toGmSymbol(sym);if(gm.empty())continue;if(!gmList.empty())gmList+=",";gmList+=gm;}
    if(gmList.empty()){logTaskEnd("FINANCIAL",tradingDay,true,0,"no valid symbols");return true;}
    auto* prime=::stk_get_finance_prime_pt(gmList.c_str(),"eps_basic,bps_pcom_ps,roe,net_prof_pcom,ttl_inc_oper,ttl_ast,ttl_liab,ttl_eqy_pcom,net_cf_oper,ttl_prof",0,0,dateStr);
    auto* deriv=::stk_get_finance_deriv_pt(gmList.c_str(),"roa,sale_gpm,sale_npm,ast_liab_rate,curr_rate,quick_rate,oper_prof_toi",0,0,dateStr);
    std::unordered_map<int,double> eps,bps,roe,np,rev,ast,liab,eq,ocf,prof,roa,gm2,pm,de,cr,qr,om;
    auto fill=[&](auto* data,const std::vector<std::pair<std::string,std::unordered_map<int,double>*>>& fields){
        if(!data||data->status()!=0)return;std::unordered_map<std::string,int> g2i;
        for(const auto& sym:symbols){std::string g=toGmSymbol(sym);if(!g.empty())g2i[g]=symToId.at(sym);}
        while(!data->is_end()){const char* s=data->get_string("symbol");if(s){auto it=g2i.find(s);if(it!=g2i.end()){int id=it->second;for(auto&[fn,mp]:fields){double v=data->get_real(fn.c_str());if(std::isfinite(v))(*mp)[id]=v;}}}data->next();}
    };
    fill(prime,{{"eps_basic",&eps},{"bps_pcom_ps",&bps},{"roe",&roe},{"net_prof_pcom",&np},{"ttl_inc_oper",&rev},{"ttl_ast",&ast},{"ttl_liab",&liab},{"ttl_eqy_pcom",&eq},{"net_cf_oper",&ocf},{"ttl_prof",&prof}});
    fill(deriv,{{"roa",&roa},{"sale_gpm",&gm2},{"sale_npm",&pm},{"ast_liab_rate",&de},{"curr_rate",&cr},{"quick_rate",&qr},{"oper_prof_toi",&om}});
    if(prime)prime->release();if(deriv)deriv->release();
    std::string isql=R"(INSERT INTO fund.financial_indicator_daily(symbol_id,trade_date,eps,bps,roa,roe,profit_margin,gross_margin,operating_margin,debt_to_equity,current_ratio,quick_ratio,operating_cash_flow,total_revenue,net_profit,total_assets,total_liabilities,equity) VALUES($1,$2::date,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18) ON CONFLICT(symbol_id,trade_date) DO NOTHING)";
    using P=astock::database::SqlParam;
    for(const auto&[sym,sid]:symToId){if(!eps.count(sid)){++skip;continue;}
        db->executeUpdate(isql,{P{sid},P{std::string(dateStr)},P{eps[sid]},P{bps[sid]},P{roa[sid]},P{roe[sid]},P{pm[sid]},P{gm2[sid]},P{om[sid]},P{de[sid]},P{cr[sid]},P{qr[sid]},P{ocf[sid]},P{rev[sid]},P{np[sid]},P{ast[sid]},P{liab[sid]},P{eq[sid]}});++ok;}
    logTaskEnd("FINANCIAL",tradingDay,true,ok,std::to_string(skip)+" skipped");
    return true;
}

} // namespace astock::infrastructure::database
