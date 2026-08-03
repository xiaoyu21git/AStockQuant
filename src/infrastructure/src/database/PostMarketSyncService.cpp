#include "database/PostMarketSyncService.h"
#include "database/NativePgConnectionPool.h"
#include "database/ISqlDatabase.h"
#include "../../../engine/include/GmSessionEngine.h"
#include "../../../thirdparty/gmsdk/gmapi.h"
#include "foundation/log/logging.hpp"
#include "foundation/config/ConfigManager.hpp"
#include "foundation/thread/ThreadPoolExecutor.h"
#include "foundation/json/json_facade.h"

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

PostMarketSyncService::PostMarketSyncService()
    : m_executor(std::make_shared<foundation::thread::ThreadPoolExecutor>(
          1, 2, std::chrono::seconds(120), "PostMktSync"))
{
}

PostMarketSyncService::~PostMarketSyncService() {
    m_running.store(false);
    if (m_scheduler && m_scheduler->joinable())
        m_scheduler->join();
}

void PostMarketSyncService::start() {
    if (m_started) return;
    m_started = true;
    m_persistPath = m_liveDataPath.empty()
        ? "app_state.json"
        : m_liveDataPath + "/app_state.json";
    loadLastSyncDay();
    m_running.store(true);
    m_scheduler = std::make_unique<std::thread>(&PostMarketSyncService::schedulerLoop, this);
    INTERNAL_INFO_STREAM << "[PostMktSync] 调度线程已启动 today=" << getCurrentTradingDay()
                         << " lastSync=" << m_lastSyncDay.load();
}

bool PostMarketSyncService::forceSyncToday() {
    int today = getCurrentTradingDay();
    if (!m_executor) return false;

    bool expected = false;
    if (!m_syncRunning.compare_exchange_strong(expected, true)) {
        INTERNAL_INFO_STREAM << "[PostMktSync] 已有同步在运行, 跳过手动触发";
        return false;
    }

    m_executor->post([this, today]() {
        struct Guard { std::atomic<bool>& flag; ~Guard() { flag.store(false); } };
        Guard g{m_syncRunning};

        // 盘中禁止同步：GM SDK 日线/分钟线 API 在交易时段调用可能崩溃
        const SyncWindow window = resolveSyncWindow();
        int mins = getCurrentLocalMinutes();
        if (mins >= window.blockStartMin && mins < window.triggerMin) {
            INTERNAL_WARN_STREAM << "[PostMktSync] 盘中禁止同步 ("
                << mins << "min, 屏蔽" << window.blockStartMin << "-" << window.triggerMin << ")";
            return;
        }

        auto db = astock::database::NativePgConnectionPool::instance().getConnection();
        if (!db || !db->isOpen()) { INTERNAL_ERROR_STREAM << "[PostMktSync] DB不可用"; return; }
        if (m_lastSyncDay.load() == today) {
            INTERNAL_INFO_STREAM << "[PostMktSync] 今日已同步, 跳过";
            return;
        }
        syncDailyMinute(today);
        syncWeeklyMonthly(today);
        syncConceptMembership();
        if (isMonthlyMaintenanceDay()) syncFinancialData(today);
        computeConceptDailyStats(today);
        m_lastSyncDay.store(today); saveLastSyncDay(today);
        INTERNAL_INFO_STREAM << "[PostMktSync] ====== 同步完成 ======";
        fillAdjFactors();
    });
    return true;
}

void PostMarketSyncService::forceSyncDate(int tradingDay) {
    INTERNAL_INFO_STREAM<<"[PostMktSync] 手动补同步 tradingDay="<<tradingDay;
    if (!m_executor) return;
    m_executor->post([this,tradingDay](){
        auto db=astock::database::NativePgConnectionPool::instance().getConnection();
        if(!db||!db->isOpen())return;
        auto res=db->executeQuery("SELECT id,symbol FROM ref.symbol_info WHERE status='ACTIVE'");
        std::unordered_map<std::string,int> s2i;std::vector<std::string> syms;
        for(auto&r:res.getRows()){s2i[r.getString("symbol")]=r.getInt("id");syms.push_back(r.getString("symbol"));}
        if(syncDaily(db,s2i,syms,tradingDay))syncMinute(db,s2i,syms,tradingDay);
        syncWeekly(db,tradingDay);syncMonthly(db,tradingDay);
    });
}

void PostMarketSyncService::forceSyncHistory(
    std::function<void(int,int,std::string)> onProgress) {
    INTERNAL_ERROR_STREAM << "[PostMktSync] forceSyncHistory — 从2015回补日线+分钟线";
    if (!m_executor) return;

    bool expected = false;
    if (!m_syncRunning.compare_exchange_strong(expected, true)) {
        INTERNAL_ERROR_STREAM << "[PostMktSync] forceSyncHistory 已有同步在运行, 跳过";
        return;
    }

    m_executor->post([this, onProgress = std::move(onProgress)]() {
        struct Guard { std::atomic<bool>& flag; ~Guard() { flag.store(false); } };
        Guard g{m_syncRunning};

        auto db = astock::database::NativePgConnectionPool::instance().getConnection();
        if (!db || !db->isOpen()) return;

        auto symRes = db->executeQuery("SELECT id,symbol FROM ref.symbol_info WHERE status='ACTIVE'");
        std::unordered_map<std::string, int> s2i;
        std::vector<std::string> syms;
        for (auto& r : symRes.getRows()) {
            s2i[r.getString("symbol")] = r.getInt("id");
            syms.push_back(r.getString("symbol"));
        }
        // 从日线最早日期补到分钟线最早日期，不逐日检测缺口
        auto sdRes = db->executeQuery("SELECT COALESCE(MIN(trade_date),'2015-01-01')::text AS sd FROM mkt.daily_bar");
        auto edRes = db->executeQuery("SELECT COALESCE(MIN(trade_ts::date),CURRENT_DATE)::text AS ed FROM mkt.minute_bar");
        std::string sd = sdRes.rowCount() > 0 ? sdRes.getRow(0).getString("sd") : "2015-01-01";
        std::string ed = edRes.rowCount() > 0 ? edRes.getRow(0).getString("ed") : "2026-07-27";
        INTERNAL_ERROR_STREAM << "[PostMktSync] 分钟回补范围: " << sd << " ~ " << ed;

        auto calRes = db->executeQuery(
            "SELECT trade_date::text AS dt FROM ref.trade_calendar "
            "WHERE is_trading_day=true AND trade_date>='" + sd + "' AND trade_date<='" + ed + "' "
            "ORDER BY trade_date");
        std::vector<std::string> days;
        for (auto& r : calRes.getRows()) days.push_back(r.getString("dt"));
        int total = static_cast<int>(days.size());
        for (int i = 0; i < total; ++i) {
            int td = 0;
            { int y, m, d; if (sscanf(days[i].c_str(), "%d-%d-%d", &y, &m, &d) == 3) td = y * 10000 + m * 100 + d; }
            if (td == 0) continue;
            syncMinute(db, s2i, syms, td);
            if (i % 5 == 4) std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (i % 50 == 0 || i == total - 1) {
                INTERNAL_ERROR_STREAM << "[PostMktSync] 分钟 " << (i+1) << "/" << total;
                if (onProgress) onProgress(i + 1, total, "分钟线 " + std::to_string(i + 1) + "/" + std::to_string(total));
            }
        }

        if (onProgress) onProgress(1, 1, "历史回补完成");
        INTERNAL_INFO_STREAM << "[PostMktSync] history 完成";
    });
}

void PostMarketSyncService::forceSyncMissingDays(int lookbackDays) {
    INTERNAL_INFO_STREAM<<"[PostMktSync] 扫描缺口 lookbackDays="<<lookbackDays;
    if (!m_executor) return;
    m_executor->post([this,lookbackDays](){
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
    });
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
    INTERNAL_INFO_STREAM<<"[PostMktSync] fillAdjFactors 开始(线程池异步)";
    if (!m_executor) { INTERNAL_ERROR_STREAM<<"[PostMktSync] fillAdjFactors executor不可用"; return; }
    m_executor->post([this](){
        auto db=astock::database::NativePgConnectionPool::instance().getConnection();
        if(!db||!db->isOpen()){INTERNAL_ERROR_STREAM<<"[PostMktSync] fillAdjFactors DB不可用";return;}

        // 读取上次复权因子同步日期，只拉该日期之后的新复权事件
        std::string lastAdjDate = "2000-01-01";
        {
            auto json = foundation::json::JsonFacade::parseFile(m_persistPath);
            if (!json.isNull() && json.isObject() && json.has("lastAdjFactorDate")) {
                auto val = json.get("lastAdjFactorDate").asString();
                if (val.size() >= 10) lastAdjDate = val;
            }
        }
        INTERNAL_INFO_STREAM<<"[PostMktSync] fillAdjFactors lastSync="<<lastAdjDate;

        // 全量标的(不仅 pre_adjust_factor=1.0——已有因子的也可能有新复权事件)
        auto res=db->executeQuery("SELECT DISTINCT d.symbol_id,si.symbol FROM mkt.daily_bar d JOIN ref.symbol_info si ON d.symbol_id=si.id");
        struct Sym{int sid;std::string sym;};std::vector<Sym> syms;
        for(auto&r:res.getRows())syms.push_back({r.getInt("symbol_id"),r.getString("symbol")});
        int total=static_cast<int>(syms.size());
        INTERNAL_INFO_STREAM<<"[PostMktSync] fillAdjFactors "<<total<<" 只标的待补(增量模式,since="<<lastAdjDate<<")";
        if(syms.empty())return;

        // 用今天作为 end_date，只拉上次同步后到今天的新复权事件
        time_t now=time(nullptr);struct tm today;localtime_s(&today,&now);
        char endDate[16];snprintf(endDate,sizeof(endDate),"%04d-%02d-%02d",today.tm_year+1900,today.tm_mon+1,today.tm_mday);

        std::string sql="UPDATE mkt.daily_bar SET pre_adjust_factor=$1,post_adjust_factor=$2 WHERE symbol_id=$3 AND (pre_adjust_factor=1.0 OR trade_date>=$4::date)";
        int ok=0,proc=0,fail=0,firstFailStreak=0;
        for(auto&s:syms){
            std::string gm;{auto d=s.sym.find('.');if(d!=std::string::npos){std::string c=s.sym.substr(0,d),e=s.sym.substr(d+1);if(e=="SH")gm="SHSE."+c;else if(e=="SZ")gm="SZSE."+c;else if(e=="BJ")gm="BSE."+c;}}
            if(gm.empty()){++proc;continue;}++proc;
            if(proc>1)std::this_thread::sleep_for(std::chrono::milliseconds(50));
            int symOk=0;bool gotData=false;
            for(int retry=0;retry<2&&!gotData;++retry){
                if(retry>0)std::this_thread::sleep_for(std::chrono::seconds(2));
                // 增量: 只查 lastAdjDate 之后的新复权事件
                auto* af=::stk_get_adj_factor(gm.c_str(),lastAdjDate.c_str(),endDate);
                if(af&&af->status()==0&&af->count()>0){
                    for(size_t i=0;i<af->count();++i){auto&f=af->at(i);
                        if(!std::isfinite(f.adj_factor_fwd)||f.adj_factor_fwd<=0||!std::isfinite(f.adj_factor_bwd)||f.adj_factor_bwd<=0)continue;
                        using P=astock::database::SqlParam;db->executeUpdate(sql,{P{f.adj_factor_fwd},P{f.adj_factor_bwd},P{s.sid},P{std::string(f.trade_date)}});++symOk;}
                    gotData=true;}
                if(af)af->release();
            }
            ok+=symOk;if(!gotData){++fail;++firstFailStreak;}else firstFailStreak=0;
            if(firstFailStreak>=50&&ok==0){INTERNAL_ERROR_STREAM<<"[PostMktSync] fillAdjFactors 连续"<<firstFailStreak<<"只失败, gm不可用, 终止";break;}
            if(proc%100==0||proc==total)INTERNAL_INFO_STREAM<<"[PostMktSync] fillAdjFactors "<<(proc*100/total)<<"% "<<proc<<"/"<<total<<" (ok="<<ok<<" fail="<<fail<<")";
        }

        // 保存本次同步日期到统一 JSON
        {
            auto root = foundation::json::JsonFacade::createObject();
            auto existing = foundation::json::JsonFacade::parseFile(m_persistPath);
            if (!existing.isNull() && existing.isObject()) {
                for (const auto& key : existing.keys()) {
                    if (key != "lastAdjFactorDate") root.set(key, existing.get(key));
                }
            }
            root.set("lastAdjFactorDate", foundation::json::JsonFacade::createString(endDate));
            std::string tmpPath = m_persistPath + ".tmp";
            { std::ofstream f(tmpPath, std::ios::trunc); if (f) f << root.toString() << "\n"; }
            std::rename(tmpPath.c_str(), m_persistPath.c_str());
        }
    });
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

PostMarketSyncService::SyncWindow PostMarketSyncService::resolveSyncWindow() const {
    SyncWindow window;
    auto& cfgMgr = foundation::config::ConfigManager::instance();
    auto cfg = cfgMgr.loadConfigFile(foundation::config::ConfigFile::TradingConnection);
    int eodTriggerMin = window.blockEndMin;   // 配置缺失时回退默认值
    int syncTriggerMin = window.triggerMin;
    if (cfg && !cfg->isNull()) {
        auto parseTime = [](const std::string& s) -> int {
            if (s.size() < 5) return -1;
            return std::stoi(s.substr(0, 2)) * 60 + std::stoi(s.substr(3, 2));
        };
        if (cfg->has("syncBlockStart")) {
            const int parsed = parseTime(cfg->get("syncBlockStart").asString());
            if (parsed >= 0) window.blockStartMin = parsed;
        }
        if (cfg->has("eodTriggerTime")) {
            const int parsed = parseTime(cfg->get("eodTriggerTime").asString());
            if (parsed >= 0) eodTriggerMin = parsed;
        }
        if (cfg->has("syncTriggerTime")) {
            const int parsed = parseTime(cfg->get("syncTriggerTime").asString());
            if (parsed >= 0) syncTriggerMin = parsed;
        }
    }
    // 屏蔽截止 = EOD 下单时间(唯一来源, 不写死); 同步触发不得早于下单
    window.blockEndMin = eodTriggerMin;
    window.triggerMin = (std::max)(syncTriggerMin, window.blockEndMin);
    return window;
}

void PostMarketSyncService::schedulerLoop() {
    while (m_running.load()) {
        int today = getCurrentTradingDay();
        if (!isTradingDay(today)) {
            std::this_thread::sleep_for(std::chrono::hours(4));
            continue;
        }
        const SyncWindow window = resolveSyncWindow();
        int mins = getCurrentLocalMinutes();
        if (mins >= window.triggerMin) {
            if (m_lastSyncDay.load() == today) {
                INTERNAL_INFO_STREAM << "[PostMktSync] today=" << today << " 已同步过，跳过";
                std::this_thread::sleep_for(std::chrono::hours(1));
                continue;
            }
            // 防并发：GM SDK 非线程安全，调度与手动同步互斥
            {
                bool expected = false;
                if (!m_syncRunning.compare_exchange_strong(expected, true)) {
                    INTERNAL_INFO_STREAM << "[PostMktSync] 手动同步进行中, 调度跳过";
                    std::this_thread::sleep_for(std::chrono::minutes(10));
                    continue;
                }
            }
            struct SchedulerGuard { std::atomic<bool>& flag; ~SchedulerGuard() { flag.store(false); } };
            SchedulerGuard sg{m_syncRunning};
            syncAll(today);
            m_lastSyncDay.store(today);
            saveLastSyncDay(today);
            // 同步完成后睡到下一个交易日
            std::this_thread::sleep_for(std::chrono::hours(8));
        } else if (mins >= window.blockStartMin && mins < window.blockEndMin) {
            // 屏蔽窗口内: EOD 下单未触发, 禁止同步 — 正常等待状态
            char timeBuf[48];
            std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d (屏蔽至 EOD 下单 %02d:%02d)",
                          mins / 60, mins % 60,
                          window.blockEndMin / 60, window.blockEndMin % 60);
            INTERNAL_DEBUG_STREAM << "[PostMktSync] 下单前禁止同步, 当前 " << timeBuf;
            std::this_thread::sleep_for(std::chrono::seconds(60));
        } else {
            int waitMin = window.triggerMin - mins;
            if (waitMin > 0) {
                INTERNAL_INFO_STREAM << "[PostMktSync] 等待 " << waitMin << " 分钟到同步时间";
                std::this_thread::sleep_for(std::chrono::minutes(waitMin));
            } else {
                syncAll(today);
                m_lastSyncDay.store(today);
                saveLastSyncDay(today);
                std::this_thread::sleep_for(std::chrono::hours(8));
            }
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

    // 下单前不允许同步 — 屏蔽窗口与调度循环共用同一派生逻辑(截止=EOD 下单时间)
    int mins = getCurrentLocalMinutes();
    {
        const SyncWindow window = resolveSyncWindow();
        if (mins >= window.blockStartMin && mins < window.blockEndMin) {
            INTERNAL_WARN_STREAM << "[PostMktSync] EOD 下单未触发, 禁止同步";
            return;
        }
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
    INTERNAL_INFO_STREAM << "[PostMktSync] ====== 日线完成 ======";
    // ── 补衍生字段 + 换手率 ──
    {
        char dt[16]; int y=tradingDay/10000,m=(tradingDay%10000)/100,d=tradingDay%100;
        snprintf(dt,sizeof(dt),"%04d-%02d-%02d",y,m,d);
        db->executeUpdate("UPDATE mkt.daily_bar SET change_pct=CASE WHEN pre_close>0 THEN (close-pre_close)/pre_close*100 ELSE NULL END,change_amt=close-pre_close,amplitude=CASE WHEN pre_close>0 THEN (high-low)/pre_close*100 ELSE NULL END WHERE trade_date=$1::date AND (change_pct IS NULL OR amplitude IS NULL)",{astock::database::SqlParam{std::string(dt)}});
        INTERNAL_INFO_STREAM<<"[PostMktSync] 衍生字段 updated";
        // 换手率
        std::unordered_map<std::string,int> gmToId;std::string chunk;
        for(const auto& sym:symbols){std::string g=toGmSymbol(sym);if(!g.empty()){gmToId[g]=symToId.at(sym);if(!chunk.empty())chunk+=",";chunk+=g;}}
        if(!chunk.empty()){auto* b=::stk_get_daily_basic_pt(chunk.c_str(),"turnrate",std::string(dt).c_str());
            if(b&&b->status()==0){std::vector<std::vector<astock::database::SqlParam>> tb;auto tf=[&](){if(tb.empty())return;
                std::string ts="UPDATE mkt.daily_bar SET turnover_rate=$1 WHERE symbol_id=$2 AND trade_date=$3::date";
                for(auto&p:tb)db->executeUpdate(ts,p);tb.clear();};
                while(!b->is_end()){const char* s=b->get_string("symbol");if(s&&s[0]){auto it=gmToId.find(std::string(s));if(it!=gmToId.end()){double tr=b->get_real("turnrate");if(std::isfinite(tr)&&tr>=0)tb.push_back({astock::database::SqlParam{tr},astock::database::SqlParam{it->second},astock::database::SqlParam{std::string(dt)}});if(tb.size()>=500)tf();}}b->next();}tf();}
            if(b)b->release();
        }
        INTERNAL_INFO_STREAM<<"[PostMktSync] 换手率 updated";
    }
    // PE/PB/市值(独立调用,与syncDailyMinute对齐)
    syncValuation(db,symToId,symbols,tradingDay);
    INTERNAL_INFO_STREAM<<"[PostMktSync] 估值已补";
    // 阶段2: 分钟线 (独立, 失败继续)
    syncMinute(db, symToId, symbols, tradingDay);
    INTERNAL_INFO_STREAM << "[PostMktSync] ====== 分钟线完成 ======";
    // 阶段3-4: 周月线
    syncWeekly(db, tradingDay);
    syncMonthly(db, tradingDay);
    INTERNAL_INFO_STREAM << "[PostMktSync] ====== 周月线完成 ======";
    // 阶段5: 概念同步+日聚合 (forceSyncToday 路径也走这里)
    syncConceptMembership();
    computeConceptDailyStats(tradingDay);

    INTERNAL_INFO_STREAM << "[PostMktSync] 全部完成 today=" << tradingDay;
    // 阶段6: 复权因子异步补（耗时，不阻塞完成通知）
    fillAdjFactors();
}

// ═════════════════════════════════════════════════
// syncDaily
// ═════════════════════════════════════════════════

bool PostMarketSyncService::syncDaily(std::shared_ptr<astock::database::ISqlDatabase> db,
                                       const std::unordered_map<std::string,int>& symToId,
                                       const std::vector<std::string>& symbols, int tradingDay) {
    logTaskStart("DAILY", tradingDay);
    int ok = 0, err = 0;

    // 与 syncDailyRange 一致: %Y-%m-%d 格式, startDay=tradingDay, endDay=tradingDay+1
    int nextDay = tradingDay;
    { int y=tradingDay/10000,m=(tradingDay%10000)/100,d=tradingDay%100;
      int nd=d+1,nm=m,ny=y;
      static const int md[]={0,31,28,31,30,31,30,31,31,30,31,30,31};
      int maxd=md[nm]+(nm==2&&(ny%4==0&&(ny%100!=0||ny%400==0))?1:0);
      if(nd>maxd){nd=1;if(++nm>12){nm=1;++ny;}}
      nextDay = ny*10000 + nm*100 + nd; }
    char startStr[16], endStr[16], dateParam[16];
    snprintf(startStr,sizeof(startStr),"%04d-%02d-%02d",tradingDay/10000,(tradingDay%10000)/100,tradingDay%100);
    snprintf(endStr,sizeof(endStr),"%04d-%02d-%02d",nextDay/10000,(nextDay%10000)/100,nextDay%100);
    snprintf(dateParam,sizeof(dateParam),"%04d-%02d-%02d",tradingDay/10000,(tradingDay%10000)/100,tradingDay%100);
    const std::string dateParamStr(dateParam);  // 持久化，避免临时对象析构后 SqlParam 悬空

    // gmsdk符号 → 原始符号 反向映射
    std::unordered_map<std::string,std::string> gmToSym;
    std::vector<std::string> gmList;
    for(const auto& sym:symbols){std::string g=toGmSymbol(sym);if(!g.empty()){gmToSym[g]=sym;gmList.push_back(g);}}

    std::vector<std::vector<astock::database::SqlParam>> batch;
    // 构造日期字符串，与 syncDailyRange 678行一样：$2::date + 传 std::string
    char dateBuf[16];
    snprintf(dateBuf,sizeof(dateBuf),"%04d-%02d-%02d",tradingDay/10000,(tradingDay%10000)/100,tradingDay%100);
    std::string dateStr(dateBuf);

    auto flush=[&](){if(batch.empty())return;
        std::string sql="INSERT INTO mkt.daily_bar(symbol_id,trade_date,open,high,low,close,volume,turnover,pre_close,data_source) "
            "VALUES($1,$2::date,$3,$4,$5,$6,$7,$8,$9,'GMSDK') ON CONFLICT(symbol_id,trade_date) DO UPDATE SET "
            "open=EXCLUDED.open,high=EXCLUDED.high,low=EXCLUDED.low,close=EXCLUDED.close,volume=EXCLUDED.volume,turnover=EXCLUDED.turnover,pre_close=EXCLUDED.pre_close";
        for(auto&p:batch)db->executeUpdate(sql,p);batch.clear();};

    static constexpr int kBatchSize=100;
    const size_t totalBatches = (gmList.size() + kBatchSize - 1) / kBatchSize;
    for(size_t i=0;i<gmList.size();i+=kBatchSize){
        size_t end=std::min(i+kBatchSize,gmList.size());
        std::string gmBatch;for(size_t j=i;j<end;++j){if(!gmBatch.empty())gmBatch+=",";gmBatch+=gmList[j];}
        auto* bars=::history_bars(gmBatch.c_str(),"1d",dateBuf,dateBuf,0,nullptr,true,nullptr);
        if(!bars||bars->status()||bars->count()<=0){
            if(bars)bars->release();err+=static_cast<int>(end-i);continue;
        }
        for(size_t k=0;k<bars->count();++k){
            auto&b=bars->at(k);if(b.close<=0)continue;
            std::string gsym(b.symbol?b.symbol:"");auto it=gmToSym.find(gsym);if(it==gmToSym.end())continue;
            auto si=symToId.find(it->second);if(si==symToId.end())continue;
            using P=astock::database::SqlParam;
            batch.push_back({P{si->second},P{dateStr},
                P{static_cast<double>(b.open)},P{static_cast<double>(b.high)},P{static_cast<double>(b.low)},P{static_cast<double>(b.close)},
                P{static_cast<int64_t>(b.volume)},P{b.amount},P{static_cast<double>(b.pre_close>0?b.pre_close:0.0)}});
            if(batch.size()>=500)flush();++ok;
        }
        bars->release();
        size_t batchIdx = i / kBatchSize;
        if (batchIdx % 5 == 0 || i + kBatchSize >= gmList.size()) {
            int pct = static_cast<int>((batchIdx + 1) * 100 / totalBatches);
            INTERNAL_INFO_STREAM << "[PostMktSync] 日线进度 " << pct << "% (" << (batchIdx+1) << "/" << totalBatches << ") ok=" << ok << " err=" << err;
        }
    }
    flush();
    int maxErr = static_cast<int>(gmList.size()) / 20;
    bool success = err <= maxErr;
    logTaskEnd("DAILY",tradingDay,success,ok,"batched "+std::to_string(gmList.size())+" symbols err="+std::to_string(err));
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
    { int y=tradingDay/10000,m=(tradingDay%10000)/100,d=tradingDay%100;
      snprintf(sDate,sizeof(sDate),"%04d-%02d-%02d",y,m,d);
      snprintf(eDate,sizeof(eDate),"%04d-%02d-%02d",y,m,d); }

    std::unordered_map<std::string,std::string> gmToSym;
    std::vector<std::string> gmList;
    for(const auto& sym:symbols){std::string g=toGmSymbol(sym);if(!g.empty()){gmToSym[g]=sym;gmList.push_back(g);}}

    std::vector<std::vector<astock::database::SqlParam>> batch;
    auto flush=[&](){if(batch.empty())return;
        std::string sql="INSERT INTO mkt.minute_bar(symbol_id,trade_ts,open,high,low,close,volume,amount) "
            "VALUES($1,$2::timestamptz,$3,$4,$5,$6,$7,$8) ON CONFLICT(symbol_id,trade_ts) DO UPDATE SET "
            "open=EXCLUDED.open,high=EXCLUDED.high,low=EXCLUDED.low,close=EXCLUDED.close,volume=EXCLUDED.volume,amount=EXCLUDED.amount";
        for(auto&p:batch)db->executeUpdate(sql,p);batch.clear();};

    static constexpr int kBatchSize=50;
    int gmTotal=static_cast<int>(gmList.size());
    for(size_t i=0;i<gmList.size();i+=kBatchSize){
        size_t end=std::min(i+kBatchSize,gmList.size());
        std::string gmBatch;for(size_t j=i;j<end;++j){if(!gmBatch.empty())gmBatch+=",";gmBatch+=gmList[j];}
        auto* bars=::history_bars(gmBatch.c_str(),"60s",sDate,eDate,0,nullptr,true,nullptr);
        if(i==0){int st=bars?bars->status():-1;int ct=bars?bars->count():0;INTERNAL_ERROR_STREAM<<"[PostMktSync] MINUTE 首批 bar: status="<<st<<" count="<<ct<<" sDate="<<sDate<<" eDate="<<eDate<<" gmBatch[0]="<<(gmList.empty()?"EMPTY":gmList[0]);}
        if(!bars||bars->status()||bars->count()<=0){if(bars)bars->release();err+=static_cast<int>(end-i);continue;}
        for(size_t k=0;k<bars->count();++k){
            auto&b=bars->at(k);if(b.close<=0)continue;
            std::string gsym(b.symbol?b.symbol:"");auto it=gmToSym.find(gsym);if(it==gmToSym.end())continue;
            auto si=symToId.find(it->second);if(si==symToId.end())continue;
            int sid=si->second;
            auto ts=static_cast<time_t>(static_cast<int64_t>(b.bob));char tsBuf[64];
            struct tm utc;gmtime_s(&utc,&ts);strftime(tsBuf,sizeof(tsBuf),"%Y-%m-%d %H:%M:%S+08",&utc);
            using P=astock::database::SqlParam;
            batch.push_back({P{sid},P{std::string(tsBuf)},
                P{static_cast<double>(b.open)},P{static_cast<double>(b.high)},P{static_cast<double>(b.low)},P{static_cast<double>(b.close)},
                P{static_cast<int64_t>(b.volume)},P{b.amount}});
            if(batch.size()>=500)flush();++ok;
        }
        bars->release();
        if(i%500==0||end>=gmList.size())INTERNAL_INFO_STREAM<<"[PostMktSync] MINUTE "<<(std::min(end,gmList.size())*100/gmTotal)<<"% "<<std::min(end,gmList.size())<<"/"<<gmTotal<<" (ok="<<ok<<")";
    }
    flush();
    if (ok == 0) INTERNAL_ERROR_STREAM << "[PostMktSync] MINUTE 无数据入库 " << tradingDay;
    logTaskEnd("MINUTE", tradingDay, true, ok);
    return true;
}

// ═════════════════════════════════════════════════
// syncMinuteRange — 批量回补分钟线
// ═════════════════════════════════════════════════
bool PostMarketSyncService::syncMinuteRange(int startDay, int endDay)
{
    INTERNAL_INFO_STREAM << "[PostMktSync] syncMinuteRange " << startDay << " ~ " << endDay;

    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) {
        INTERNAL_ERROR_STREAM << "[PostMktSync] syncMinuteRange DB 不可用";
        return false;
    }

    // 获取交易日历
    char sBuf[16], eBuf[16];
    {
        int y = startDay / 10000, m = (startDay % 10000) / 100, d = startDay % 100;
        snprintf(sBuf, sizeof(sBuf), "%04d-%02d-%02d", y, m, d);
    }
    {
        int y = endDay / 10000, m = (endDay % 10000) / 100, d = endDay % 100;
        snprintf(eBuf, sizeof(eBuf), "%04d-%02d-%02d", y, m, d);
    }

    auto calRes = db->executeQuery(
        "SELECT trade_date::text AS dt FROM ref.trade_calendar "
        "WHERE is_trading_day=true AND trade_date BETWEEN $1::date AND $2::date "
        "ORDER BY trade_date",
        {astock::database::SqlParam{std::string(sBuf)},
         astock::database::SqlParam{std::string(eBuf)}});

    std::vector<std::string> tradingDays;
    for (auto& r : calRes.getRows()) tradingDays.push_back(r.getString("dt"));
    if (tradingDays.empty()) {
        INTERNAL_INFO_STREAM << "[PostMktSync] syncMinuteRange 无交易日";
        return true;
    }

    // 获取活跃标的
    auto symRes = db->executeQuery("SELECT id,symbol FROM ref.symbol_info WHERE status='ACTIVE'");
    std::unordered_map<std::string, int> s2i;
    std::vector<std::string> syms;
    for (auto& r : symRes.getRows()) {
        s2i[r.getString("symbol")] = r.getInt("id");
        syms.push_back(r.getString("symbol"));
    }

    int total = static_cast<int>(tradingDays.size());
    int filled = 0, skipped = 0, failed = 0;

    for (int i = 0; i < total; ++i) {
        const auto& dt = tradingDays[static_cast<size_t>(i)];
        // 检查当日分钟线覆盖
        auto covRes = db->executeQuery(
            "SELECT COUNT(DISTINCT symbol_id) FROM mkt.minute_bar WHERE trade_ts::date=$1::date",
            {astock::database::SqlParam{dt}});
        int cov = (covRes.rowCount() > 0) ? covRes.getRow(0).getInt(0) : 0;
        int expected = static_cast<int>(syms.size());
        if (cov >= expected * 90 / 100) {
            ++skipped;
            continue;
        }

        // 解析 tradingDay
        int td = 0;
        {
            int y, m, d;
            if (sscanf(dt.c_str(), "%d-%d-%d", &y, &m, &d) == 3)
                td = y * 10000 + m * 100 + d;
        }
        if (td == 0) { ++failed; continue; }

        INTERNAL_INFO_STREAM << "[PostMktSync] syncMinuteRange " << dt
                             << " (" << (i + 1) << "/" << total << ") cov=" << cov
                             << "/" << expected;

        if (syncMinute(db, s2i, syms, td)) {
            ++filled;
        } else {
            ++failed;
        }

        // 速率控制：掘金 API 限流，每批间休息 200ms
        if (i % 5 == 4) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    INTERNAL_INFO_STREAM << "[PostMktSync] syncMinuteRange 完成: filled=" << filled
                         << " skipped=" << skipped << " failed=" << failed
                         << " / total=" << total;
    return failed == 0;
}

// ═════════════════════════════════════════════════
// syncWeekly (从daily_bar聚合)
// ═════════════════════════════════════════════════

bool PostMarketSyncService::syncWeekly(std::shared_ptr<astock::database::ISqlDatabase> db, int tradingDay) {
    logTaskStart("WEEKLY", tradingDay);
    // 聚合上一完整周(上周一~上周日)
    // 聚合上一完整周，trade_date 取当周最后交易日
    std::string sql = R"(
        INSERT INTO mkt.weekly_bar(symbol_id,trade_date,open,high,low,close,volume,turnover,pre_close,data_source)
        SELECT d.symbol_id,
               MAX(d.trade_date) AS trade_date,
               (array_agg(d.open ORDER BY d.trade_date))[1] AS open,
               MAX(d.high) AS high, MIN(d.low) AS low,
               (array_agg(d.close ORDER BY d.trade_date))[array_upper(array_agg(d.close ORDER BY d.trade_date),1)] AS close,
               SUM(d.volume) AS volume, SUM(d.turnover) AS turnover,
               NULL::numeric AS pre_close, 'GMSDK' AS data_source
        FROM mkt.daily_bar d
        WHERE d.trade_date >= date_trunc('week', $1::date)::date - interval '7 days'
          AND d.trade_date <= (date_trunc('week', $1::date)::date - interval '1 day')
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
    // 聚合上一完整月，trade_date 取当月最后交易日
    std::string sql = R"(
        INSERT INTO mkt.monthly_bar(symbol_id,trade_date,open,high,low,close,volume,turnover,pre_close,data_source)
        SELECT d.symbol_id,
               MAX(d.trade_date) AS trade_date,
               (array_agg(d.open ORDER BY d.trade_date))[1] AS open,
               MAX(d.high) AS high, MIN(d.low) AS low,
               (array_agg(d.close ORDER BY d.trade_date))[array_upper(array_agg(d.close ORDER BY d.trade_date),1)] AS close,
               SUM(d.volume) AS volume, SUM(d.turnover) AS turnover,
               NULL::numeric AS pre_close, 'GMSDK' AS data_source
        FROM mkt.daily_bar d
        WHERE d.trade_date >= date_trunc('month', $1::date)::date - interval '1 month'
          AND d.trade_date <= (date_trunc('month', $1::date)::date - interval '1 day')
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
        // date 是 int(YYYYMMDD), 需要转为 "YYYY-MM-DD" 字符串才能被 PG ::date 正确解析
        char dateBuf[16];
        std::snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d",
                      date / 10000, (date / 100) % 100, date % 100);
        auto r = db->executeQuery(
            "SELECT is_trading_day FROM ref.trade_calendar WHERE trade_date=$1::date",
            {astock::database::SqlParam{std::string(dateBuf)}});
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
    auto json = foundation::json::JsonFacade::parseFile(m_persistPath);
    if (json.isNull() || !json.isObject()) return;
    if (json.has("lastSyncDay")) {
        try { m_lastSyncDay.store(json.get("lastSyncDay").asInt()); } catch (...) {}
    }
    INTERNAL_INFO_STREAM << "[PostMktSync] 加载 lastSyncDay=" << m_lastSyncDay.load();
}

void PostMarketSyncService::saveLastSyncDay(int tradingDay) {
    if (m_persistPath.empty()) return;
    // 读取现有 JSON，保留 adjFactorDate 等字段
    auto root = foundation::json::JsonFacade::createObject();
    {
        auto existing = foundation::json::JsonFacade::parseFile(m_persistPath);
        if (!existing.isNull() && existing.isObject()) {
            for (const auto& key : existing.keys()) {
                if (key != "lastSyncDay") root.set(key, existing.get(key));
            }
        }
    }
    root.set("lastSyncDay", foundation::json::JsonFacade::createInt(tradingDay));
    // 原子写入
    std::string tmpPath = m_persistPath + ".tmp";
    {
        std::ofstream f(tmpPath, std::ios::trunc);
        if (!f.is_open()) return;
        f << root.toString() << "\n";
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
    std::vector<Row> rows;int total=static_cast<int>(symbols.size());
    // gm符号→原始符号 映射
    std::unordered_map<std::string,std::string> gmToSym;
    std::vector<std::string> gmList;
    for(const auto& sym:symbols){std::string g=toGmSymbol(sym);if(!g.empty()){gmToSym[g]=sym;gmList.push_back(g);}}

    // Pass1: 批量查询
    std::unordered_set<std::string> gotSyms;
    static constexpr int kBatchSize=100;
    int batchReq=0,batchEmpty=0,batchErr=0,batchTotalBars=0,batchMatchMiss=0;
    for(size_t i=0;i<gmList.size();i+=kBatchSize){
        size_t end=std::min(i+kBatchSize,gmList.size());
        std::string gmBatch;for(size_t j=i;j<end;++j){if(!gmBatch.empty())gmBatch+=",";gmBatch+=gmList[j];}
        ++batchReq;
        auto* bars=::history_bars(gmBatch.c_str(),"1d",sBuf,eBuf,0,nullptr,true,nullptr);
        if(!bars||bars->status()!=0||bars->count()<=0){
            if(!bars)++batchErr; else if(bars->count()<=0)++batchEmpty; else ++batchErr;
            if(bars)bars->release();continue;
        }
        batchTotalBars+=static_cast<int>(bars->count());
        // 第一批打印gm返回的symbol和日期样本
        if(i==0&&bars->count()>0){
            std::string s1(bars->at(0).symbol?bars->at(0).symbol:"");time_t bob0=static_cast<time_t>(static_cast<int64_t>(bars->at(0).bob));struct tm t0;gmtime_s(&t0,&bob0);char ds0[16];snprintf(ds0,sizeof(ds0),"%04d-%02d-%02d",t0.tm_year+1900,t0.tm_mon+1,t0.tm_mday);
            INTERNAL_INFO_STREAM<<"[PostMktSync] gm首个: sym=["<<s1<<"] dt="<<ds0<<" targets="<<targetDates.size()<<" 样例:"<<(targetDates.empty()?"无":targetDates[0]);
        }
        for(size_t k=0;k<bars->count();++k){auto&b=bars->at(k);
            if(!std::isfinite(b.close)||b.close<=0.01||b.close>=10000.0)continue;
            std::string gsym(b.symbol?b.symbol:"");auto sit=gmToSym.find(gsym);
            if(sit==gmToSym.end()){++batchMatchMiss;continue;}std::string sym=sit->second;gotSyms.insert(gsym);
            auto it=symToId.find(sym);if(it==symToId.end())continue;
            time_t bob=static_cast<time_t>(static_cast<int64_t>(b.bob));struct tm t;localtime_s(&t,&bob);
            char ds[16];snprintf(ds,sizeof(ds),"%04d-%02d-%02d",t.tm_year+1900,t.tm_mon+1,t.tm_mday);
            std::string dt(ds);if(!targets.count(dt))continue;
            rows.push_back({it->second,sym,dt,b.open,b.high,b.low,b.close,b.pre_close,static_cast<double>(b.volume),b.amount});}
        bars->release();
        if(i%500==0||end>=gmList.size())INTERNAL_INFO_STREAM<<"[PostMktSync] Pass1 "<<(std::min(end,gmList.size())*100/total)<<"% "
            <<std::min(end,gmList.size())<<"/"<<total<<" gmBars="<<batchTotalBars<<" rows="<<rows.size();
    }
    INTERNAL_INFO_STREAM<<"[PostMktSync] Pass1 批量统计: 请求 "<<batchReq<<" 批, 空 "<<batchEmpty<<" 批, 错 "<<batchErr
        <<" 批, gm返回 "<<batchTotalBars<<" 条, 符号匹配失败 "<<batchMatchMiss
        <<" 条, 收集 "<<rows.size()<<" 行(目标日期="<<targetDates.size()<<"天), 覆盖 "<<gotSyms.size()<<" 标的";

    // 批量全空→gmsdk暂无数据，跳过重试
    if(rows.empty()){
        INTERNAL_WARN_STREAM<<"[PostMktSync] syncDailyRange 全空(掘金暂无数据), 跳过重试";
        return false;
    }

    // 重试: 批量遗漏的标的单独拉
    std::vector<std::string> missing;
    for(auto&g:gmList)if(!gotSyms.count(g))missing.push_back(g);
    if(!missing.empty()){
        int retryTotal=static_cast<int>(missing.size()),retryOk=0,retryIdx=0,
            retryNull=0,retryEmpty=0,retryErr=0,retryStatusNonZero=0;
        INTERNAL_INFO_STREAM<<"[PostMktSync] 重试 "<<retryTotal<<" 只遗漏标的 (批量覆盖="<<gotSyms.size()<<"/"<<gmList.size()<<")";
        for(auto&g:missing){
            bool ok=false;int lastStatus=-1;
            for(int retry=0;retry<3&&!ok;++retry){
                if(retry>0)std::this_thread::sleep_for(std::chrono::milliseconds(500*(1<<retry)));
                auto* bars=::history_bars(g.c_str(),"1d",sBuf,eBuf,0,nullptr,true,nullptr);
                if(!bars){lastStatus=-1;continue;}
                lastStatus=bars->status();
                if(bars->status()!=0||bars->count()<=0){
                    if(bars->status()!=0)++retryStatusNonZero; else ++retryEmpty;
                    bars->release();continue;
                }
                for(size_t k=0;k<bars->count();++k){auto&b=bars->at(k);
                    if(!std::isfinite(b.close)||b.close<=0.01)continue;
                    std::string gsym(b.symbol?b.symbol:g);auto sit=gmToSym.find(gsym);
                    if(sit==gmToSym.end())continue;
                    auto it=symToId.find(sit->second);if(it==symToId.end())continue;
                    time_t bob=static_cast<time_t>(static_cast<int64_t>(b.bob));struct tm t;localtime_s(&t,&bob);
                    char ds[16];snprintf(ds,sizeof(ds),"%04d-%02d-%02d",t.tm_year+1900,t.tm_mon+1,t.tm_mday);
                    std::string dt(ds);if(!targets.count(dt))continue;
                    rows.push_back({it->second,sit->second,dt,b.open,b.high,b.low,b.close,b.pre_close,static_cast<double>(b.volume),b.amount});}
                ok=true;++retryOk;bars->release();
            }
            ++retryIdx;if(!ok){++retryNull;
                if(retryIdx<=5)INTERNAL_WARN_STREAM<<"[PostMktSync] 重试失败 "<<g<<" lastStatus="<<lastStatus;}
            if(retryIdx%50==0||retryIdx==retryTotal)
                INTERNAL_INFO_STREAM<<"[PostMktSync] 重试进度 "<<(retryIdx*100/retryTotal)<<"% "<<retryIdx<<"/"<<retryTotal<<" (ok="<<retryOk<<")";
        }
        INTERNAL_INFO_STREAM<<"[PostMktSync] 重试完成: ok="<<retryOk<<" null="<<retryNull
            <<" empty="<<retryEmpty<<" statusErr="<<retryStatusNonZero<<" / total="<<retryTotal;
    }

    if(rows.empty()){INTERNAL_ERROR_STREAM<<"[PostMktSync] syncDailyRange 无数据";return false;}
    INTERNAL_INFO_STREAM<<"[PostMktSync] Pass1 done, "<<rows.size()<<" 行";

    // 先写日线基础数据，再异步补估值 — 保证日线先入库
    std::vector<std::vector<astock::database::SqlParam>> batch;int writeOk=0;
    auto flush=[&](){if(batch.empty())return;
        std::string sql="INSERT INTO mkt.daily_bar(symbol_id,trade_date,open,high,low,close,pre_close,volume,turnover,change_pct,change_amt,amplitude,data_source) VALUES($1,$2::date,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,'GMSDK') ON CONFLICT(symbol_id,trade_date) DO UPDATE SET open=EXCLUDED.open,high=EXCLUDED.high,low=EXCLUDED.low,close=EXCLUDED.close,pre_close=EXCLUDED.pre_close,volume=EXCLUDED.volume,turnover=EXCLUDED.turnover,change_pct=EXCLUDED.change_pct,change_amt=EXCLUDED.change_amt,amplitude=EXCLUDED.amplitude";
        for(auto&p:batch)db->executeUpdate(sql,p);batch.clear();};
    for(auto&r:rows){double chg=(r.pc>0)?(r.c-r.pc)/r.pc*100:0,amp=(r.pc>0&&r.h-r.l>0)?(r.h-r.l)/r.pc*100:0;using P=astock::database::SqlParam;batch.push_back({P{r.sid},P{r.dt},P{r.o},P{r.h},P{r.l},P{r.c},P{r.pc},P{static_cast<int64_t>(r.vol)},P{r.amt},P{chg},P{r.c-r.pc},P{amp}});if(batch.size()>=500)flush();++writeOk;}
    flush();
    INTERNAL_INFO_STREAM<<"[PostMktSync] 日线已入库 "<<writeOk<<" 行, 开始补估值数据...";

    // Pass2: 估值数据异步补 (pe/pb/市值/换手率), 失败不影响已入库的日线
    std::unordered_map<std::string,int> gmToId;
    for(auto&r:rows){std::string g=toGmSymbol(r.sym);if(!g.empty())gmToId[g]=r.sid;}
    for(const auto& dt:targetDates){
        std::string chunk;for(const auto&r:rows)if(r.dt==dt){std::string g=toGmSymbol(r.sym);if(!g.empty()){if(!chunk.empty())chunk+=",";chunk+=g;}}
        if(chunk.empty())continue;
        auto* v=::stk_get_daily_valuation_pt(chunk.c_str(),"pe_lyr,pb_mrq",dt.c_str());
        if(v&&v->status()==0){std::vector<std::vector<astock::database::SqlParam>> ub;auto uf=[&](){if(ub.empty())return;
            std::string us="UPDATE mkt.daily_bar SET pe_ratio=$1,pb_ratio=$2 WHERE symbol_id=$3 AND trade_date=$4::date";
            for(auto&p:ub)db->executeUpdate(us,p);ub.clear();};
            while(!v->is_end()){const char* s=v->get_string("symbol");if(s&&s[0]){auto it=gmToId.find(std::string(s));if(it!=gmToId.end()){double pe=v->get_real("pe_lyr"),pb=v->get_real("pb_mrq");if(std::isfinite(pe)&&pe>=-10000&&pe<=100000)ub.push_back({astock::database::SqlParam{pe},astock::database::SqlParam{pb},astock::database::SqlParam{it->second},astock::database::SqlParam{dt}});if(ub.size()>=500)uf();}}v->next();}uf();}
        if(v){v->release();v=nullptr;}
        auto* mv=::stk_get_daily_mktvalue_pt(chunk.c_str(),"tot_mv,a_mv",dt.c_str());
        if(mv&&mv->status()==0){std::vector<std::vector<astock::database::SqlParam>> mb;auto mf=[&](){if(mb.empty())return;
            std::string ms="UPDATE mkt.daily_bar SET market_cap=$1,circulating_market_cap=$2 WHERE symbol_id=$3 AND trade_date=$4::date";
            for(auto&p:mb)db->executeUpdate(ms,p);mb.clear();};
            while(!mv->is_end()){const char* s=mv->get_string("symbol");if(s&&s[0]){auto it=gmToId.find(std::string(s));if(it!=gmToId.end()){double mc=mv->get_real("tot_mv"),cc=mv->get_real("a_mv");if(std::isfinite(mc)&&mc>=0&&mc<=1e16)mb.push_back({astock::database::SqlParam{mc},astock::database::SqlParam{cc},astock::database::SqlParam{it->second},astock::database::SqlParam{dt}});if(mb.size()>=500)mf();}}mv->next();}mf();}
        if(mv){mv->release();mv=nullptr;}
        auto* b=::stk_get_daily_basic_pt(chunk.c_str(),"turnrate",dt.c_str());
        if(b&&b->status()==0){std::vector<std::vector<astock::database::SqlParam>> tb;auto tf=[&](){if(tb.empty())return;
            std::string ts="UPDATE mkt.daily_bar SET turnover_rate=$1 WHERE symbol_id=$2 AND trade_date=$3::date";
            for(auto&p:tb)db->executeUpdate(ts,p);tb.clear();};
            while(!b->is_end()){const char* s=b->get_string("symbol");if(s&&s[0]){auto it=gmToId.find(std::string(s));if(it!=gmToId.end()){double tr=b->get_real("turnrate");if(std::isfinite(tr)&&tr>=0)tb.push_back({astock::database::SqlParam{tr},astock::database::SqlParam{it->second},astock::database::SqlParam{dt}});if(tb.size()>=500)tf();}}b->next();}tf();}
        if(b){b->release();b=nullptr;}
        INTERNAL_INFO_STREAM<<"[PostMktSync] Pass2 "<<dt;
    }
    INTERNAL_INFO_STREAM<<"[PostMktSync] ====== syncDailyRange 完成 "<<writeOk<<" 行 "<<targetDates.size()<<" 天 ======";
    return true;
}

// 通用日线同步: 任意交易日都走日线→估值→分钟线, 不区分今天还是历史
void PostMarketSyncService::syncDailyMinute(int tradingDay) {
    auto db=astock::database::NativePgConnectionPool::instance().getConnection();
    if(!db||!db->isOpen())return;
    auto res=db->executeQuery("SELECT id,symbol FROM ref.symbol_info WHERE status='ACTIVE'");
    std::unordered_map<std::string,int> s2i;std::vector<std::string> syms;
    for(auto&r:res.getRows()){s2i[r.getString("symbol")]=r.getInt("id");syms.push_back(r.getString("symbol"));}
    // 日线: 缺则补，满则跳过
    auto cntRes=db->executeQuery("SELECT COUNT(*) FROM mkt.daily_bar WHERE trade_date=$1::date",
        {astock::database::SqlParam{std::to_string(tradingDay)}});
    bool dailyExists=(cntRes.rowCount()>0&&cntRes.getRow(0).getInt(0)>static_cast<int>(syms.size()*0.9));
    // 估值状态
    auto peRes=db->executeQuery("SELECT COUNT(*) FILTER (WHERE pe_ratio=0 OR pe_ratio IS NULL) FROM mkt.daily_bar WHERE trade_date=$1::date",
        {astock::database::SqlParam{std::to_string(tradingDay)}});
    bool peMissing=(peRes.rowCount()>0&&peRes.getRow(0).getInt(0)>0);
    // 分钟线状态
    auto minRes=db->executeQuery("SELECT COUNT(*) FROM mkt.minute_bar WHERE trade_ts::date=$1::date",
        {astock::database::SqlParam{std::to_string(tradingDay)}});
    bool minMissing=(minRes.rowCount()==0||minRes.getRow(0).getInt(0)==0);

    const char* dStatus=dailyExists?"已覆盖":"补";const char* pStatus=peMissing?"补":"已覆盖";const char* mStatus=minMissing?"补":"已覆盖";
    INTERNAL_INFO_STREAM<<"[PostMktSync] "<<tradingDay<<" 日线="<<dStatus<<" PE="<<pStatus<<" 分钟="<<mStatus;

    if(!dailyExists){if(!syncDaily(db,s2i,syms,tradingDay)){INTERNAL_ERROR_STREAM<<"[PostMktSync] 日线失败 "<<tradingDay;return;}}
    // ── 补派生字段: change_pct/change_amt/amplitude(从已有OHLC计算,一条SQL完成)──
    {
        char dt[16]; int y=tradingDay/10000,m=(tradingDay%10000)/100,d=tradingDay%100;
        snprintf(dt,sizeof(dt),"%04d-%02d-%02d",y,m,d);
        auto ur=db->executeUpdate("UPDATE mkt.daily_bar SET "
            "change_pct=CASE WHEN pre_close>0 THEN (close-pre_close)/pre_close*100 ELSE NULL END,"
            "change_amt=close-pre_close,"
            "amplitude=CASE WHEN pre_close>0 THEN (high-low)/pre_close*100 ELSE NULL END "
            "WHERE trade_date=$1::date AND (change_pct IS NULL OR amplitude IS NULL)",
            {astock::database::SqlParam{std::string(dt)}});
        INTERNAL_INFO_STREAM<<"[PostMktSync] 衍生字段 updated for "<<dt;
    }
    // ── 补换手率: 参照 syncDailyRange 用 stk_get_daily_basic_pt ──
    {
        std::unordered_map<std::string,int> gmToId;std::string chunk;
        for(const auto& sym:syms){std::string g=toGmSymbol(sym);if(!g.empty()){gmToId[g]=s2i.at(sym);if(!chunk.empty())chunk+=",";chunk+=g;}}
        if(!chunk.empty()){
            char dt[16];int y=tradingDay/10000,m=(tradingDay%10000)/100,d=tradingDay%100;
            snprintf(dt,sizeof(dt),"%04d-%02d-%02d",y,m,d);
            std::string dtStr(dt);
            auto* b=::stk_get_daily_basic_pt(chunk.c_str(),"turnrate",dtStr.c_str());
            if(b&&b->status()==0){std::vector<std::vector<astock::database::SqlParam>> tb;auto tf=[&](){if(tb.empty())return;
                std::string ts="UPDATE mkt.daily_bar SET turnover_rate=$1 WHERE symbol_id=$2 AND trade_date=$3::date";
                for(auto&p:tb)db->executeUpdate(ts,p);tb.clear();};
                while(!b->is_end()){const char* s=b->get_string("symbol");if(s&&s[0]){auto it=gmToId.find(std::string(s));if(it!=gmToId.end()){double tr=b->get_real("turnrate");if(std::isfinite(tr)&&tr>=0)tb.push_back({astock::database::SqlParam{tr},astock::database::SqlParam{it->second},astock::database::SqlParam{dtStr}});if(tb.size()>=500)tf();}}b->next();}tf();}
            if(b){b->release();b=nullptr;}
            INTERNAL_INFO_STREAM<<"[PostMktSync] 换手率 updated for "<<dtStr;
        }
    }
    if(peMissing)syncValuation(db,s2i,syms,tradingDay);
    if(minMissing)syncMinute(db,s2i,syms,tradingDay);
}

// 估值补全: PE/PB/市值/换手率 (gmsdk批量)
void PostMarketSyncService::syncValuation(std::shared_ptr<astock::database::ISqlDatabase> db,
    const std::unordered_map<std::string,int>& symToId, const std::vector<std::string>& symbols, int tradingDay) {
    char dtBuf[16];{int y=tradingDay/10000,m=(tradingDay%10000)/100,d=tradingDay%100;snprintf(dtBuf,sizeof(dtBuf),"%04d-%02d-%02d",y,m,d);}
    std::string dt(dtBuf);
    std::unordered_map<std::string,int> gmToId;std::string chunk;
    for(const auto& sym:symbols){std::string g=toGmSymbol(sym);if(!g.empty()){gmToId[g]=symToId.at(sym);if(!chunk.empty())chunk+=",";chunk+=g;}}
    if(chunk.empty())return;
    auto* v=::stk_get_daily_valuation_pt(chunk.c_str(),"pe_lyr,pb_mrq",dt.c_str());
    if(v&&v->status()==0){std::vector<std::vector<astock::database::SqlParam>> ub;auto uf=[&](){if(ub.empty())return;
        std::string us="UPDATE mkt.daily_bar SET pe_ratio=$1,pb_ratio=$2 WHERE symbol_id=$3 AND trade_date=$4::date AND (pe_ratio=0 OR pe_ratio IS NULL OR pb_ratio=0 OR pb_ratio IS NULL)";
        for(auto&p:ub)db->executeUpdate(us,p);ub.clear();};
        while(!v->is_end()){const char* s=v->get_string("symbol");if(s&&s[0]){auto it=gmToId.find(std::string(s));if(it!=gmToId.end()){double pe=v->get_real("pe_lyr"),pb=v->get_real("pb_mrq");if(std::isfinite(pe))ub.push_back({astock::database::SqlParam{pe},astock::database::SqlParam{pb},astock::database::SqlParam{it->second},astock::database::SqlParam{dt}});if(ub.size()>=500)uf();}}v->next();}uf();}
    if(v){v->release();v=nullptr;}
    auto* mv=::stk_get_daily_mktvalue_pt(chunk.c_str(),"tot_mv,a_mv",dt.c_str());
    if(mv&&mv->status()==0){std::vector<std::vector<astock::database::SqlParam>> mb;auto mf=[&](){if(mb.empty())return;
        std::string ms="UPDATE mkt.daily_bar SET market_cap=$1,circulating_market_cap=$2 WHERE symbol_id=$3 AND trade_date=$4::date AND (market_cap=0 OR market_cap IS NULL OR circulating_market_cap=0 OR circulating_market_cap IS NULL)";
        for(auto&p:mb)db->executeUpdate(ms,p);mb.clear();};
        while(!mv->is_end()){const char* s=mv->get_string("symbol");if(s&&s[0]){auto it=gmToId.find(std::string(s));if(it!=gmToId.end()){double mc=mv->get_real("tot_mv"),cc=mv->get_real("a_mv");if(std::isfinite(mc))mb.push_back({astock::database::SqlParam{mc},astock::database::SqlParam{cc},astock::database::SqlParam{it->second},astock::database::SqlParam{dt}});if(mb.size()>=500)mf();}}mv->next();}mf();}
    if(mv){mv->release();mv=nullptr;}
    auto* b=::stk_get_daily_basic_pt(chunk.c_str(),"turnrate",dt.c_str());
    if(b&&b->status()==0){std::vector<std::vector<astock::database::SqlParam>> tb;auto tf=[&](){if(tb.empty())return;
        std::string ts="UPDATE mkt.daily_bar SET turnover_rate=$1 WHERE symbol_id=$2 AND trade_date=$3::date AND (turnover_rate=0 OR turnover_rate IS NULL)";
        for(auto&p:tb)db->executeUpdate(ts,p);tb.clear();};
        while(!b->is_end()){const char* s=b->get_string("symbol");if(s&&s[0]){auto it=gmToId.find(std::string(s));if(it!=gmToId.end()){double tr=b->get_real("turnrate");if(std::isfinite(tr))tb.push_back({astock::database::SqlParam{tr},astock::database::SqlParam{it->second},astock::database::SqlParam{dt}});if(tb.size()>=500)tf();}}b->next();}tf();}
    if(b){b->release();b=nullptr;}
    INTERNAL_INFO_STREAM<<"[PostMktSync] 估值已补 "<<dt;
}

void PostMarketSyncService::syncWeeklyMonthly(int tradingDay) {
    auto db=astock::database::NativePgConnectionPool::instance().getConnection();
    if(!db||!db->isOpen())return;

    char buf[16];
    int y=tradingDay/10000,m=(tradingDay%10000)/100,d=tradingDay%100;
    snprintf(buf,sizeof(buf),"%04d-%02d-%02d",y,m,d);
    std::string today(buf);

    // ── 周线缺口补齐 ──
    {
        auto res=db->executeQuery("SELECT MAX(trade_date)::text FROM mkt.weekly_bar");
        if(res.rowCount()>0){
            std::string last=res.getRow(0).getString(0);
            int filled=0;
            while(!last.empty() && last < today){
                // last 的下一个周日
                auto nr=db->executeQuery(
                    "SELECT (date_trunc('week',$1::date+interval'8 days')+interval'4 days')::date::text",
                    {astock::database::SqlParam{last}});
                if(nr.rowCount()==0)break;
                std::string nextSun=nr.getRow(0).getString(0);
                if(nextSun > today) break;
                // 该周日落在一个完整周内，取该周日作为 tradingDay
                auto cnt=db->executeQuery(
                    "SELECT COUNT(*) FROM mkt.daily_bar WHERE trade_date<=$1::date"
                    " AND trade_date>=date_trunc('week',$1::date)::date",
                    {astock::database::SqlParam{nextSun}});
                if(cnt.rowCount()>0 && cnt.getRow(0).getInt(0)>0){
                    int ny,nm,nd; sscanf(nextSun.c_str(),"%d-%d-%d",&ny,&nm,&nd);
                    syncWeekly(db,ny*10000+nm*100+nd); ++filled;
                }
                last=nextSun;
            }
            if(filled>0) INTERNAL_INFO_STREAM<<"[PostMktSync] weekly backfill "<<filled<<" weeks";
        }
    }

    // ── 月线缺口补齐 ──
    {
        auto res=db->executeQuery("SELECT MAX(trade_date)::text FROM mkt.monthly_bar");
        if(res.rowCount()>0){
            std::string last=res.getRow(0).getString(0);
            int filled=0;
            while(!last.empty() && last < today){
                // last 的下一个月末
                auto nr=db->executeQuery(
                    "SELECT (date_trunc('month',$1::date+interval'1 month')+interval'1 month-1 day')::date::text",
                    {astock::database::SqlParam{last}});
                if(nr.rowCount()==0)break;
                std::string nextEnd=nr.getRow(0).getString(0);
                if(nextEnd > today) break;
                auto cnt=db->executeQuery(
                    "SELECT COUNT(*) FROM mkt.daily_bar WHERE trade_date<=$1::date"
                    " AND trade_date>=date_trunc('month',$1::date)::date",
                    {astock::database::SqlParam{nextEnd}});
                if(cnt.rowCount()>0 && cnt.getRow(0).getInt(0)>0){
                    int ny,nm,nd; sscanf(nextEnd.c_str(),"%d-%d-%d",&ny,&nm,&nd);
                    syncMonthly(db,ny*10000+nm*100+nd); ++filled;
                }
                last=nextEnd;
            }
            if(filled>0) INTERNAL_INFO_STREAM<<"[PostMktSync] monthly backfill "<<filled<<" months";
        }
    }

    syncWeekly(db,tradingDay);
    syncMonthly(db,tradingDay);
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

// ═══════════════════════════════════════════════════════════════
// 概念/题材同步
// ═══════════════════════════════════════════════════════════════

void PostMarketSyncService::syncConceptMembership()
{
    // GM SDK 连接校验
    auto& gmEng = engine::GmSessionEngine::instance();
    if (!gmEng.initialized()) {
        INTERNAL_WARN_STREAM << "[PostMktSync] CONCEPT: GM 未连接, 跳过概念同步";
        return;
    }
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) return;

    // 0. 清掉旧符号格式的数据 (修复 SHSE.600000 → 600000.SH 映射后重新写入)
    db->executeUpdate("DELETE FROM live.concept_membership");
    db->executeUpdate("DELETE FROM live.concept_leader_rank");
    db->executeUpdate("DELETE FROM live.concept_daily_stats");

    // 1. 拉取全量概念板块 — sector_type 有效值: "1001"=市场 "1002"=地域 "1003"=概念
    auto* cats = ::stk_get_sector_category("1003");
    if (!cats || cats->status()) {
        INTERNAL_WARN_STREAM << "[PostMktSync] CONCEPT: stk_get_sector_category(1003) 失败 status="
                             << (cats ? cats->status() : -1);
        if (cats) cats->release();
        return;
    }
    int catCount = cats->count();
    INTERNAL_INFO_STREAM << "[PostMktSync] CONCEPT: 获取概念板块 " << catCount << " 个";
    // 2. 遍历概念, 拉取成分股
    int totalConcepts = 0, totalMembers = 0;
    using P = astock::database::SqlParam;
    for (int i = 0; i < catCount; ++i) {
        auto& cat = cats->at(i);
        std::string code(cat.sector_code);
        std::string name(cat.sector_name);
        if (code.empty()) continue;

        auto* members = ::stk_get_sector_constituents(code.c_str());
        if (!members || members->status()) {
            if (members) members->release();
            continue;
        }

        // 写 catalog
        db->executeUpdate(
            "INSERT INTO live.concept_catalog(concept_code,concept_name,sector_type) "
            "VALUES($1,$2,'concept') ON CONFLICT(concept_code) DO UPDATE SET "
            "concept_name=EXCLUDED.concept_name,stock_count=EXCLUDED.stock_count,updated_at=NOW()",
            {P{code}, P{name}});

        int n = members->count();
        // 批量 INSERT (ON CONFLICT 跳过重复)
        for (int j = 0; j < n; ++j) {
            auto& m = members->at(j);
            std::string sym(m.symbol);
            if (sym.empty()) continue;
            // GM symbol 格式 "SHSE.600000" → DB ref.symbol_info.symbol 格式 "600000.SH"
            auto dot = sym.find('.');
            std::string code6 = dot != std::string::npos ? sym.substr(dot + 1) : sym;
            std::string exchCode = dot != std::string::npos ? sym.substr(0, dot) : "";
            std::string exchange = (exchCode == "SHSE") ? "SH" :
                                   (exchCode == "SZSE") ? "SZ" : "BJ";
            std::string dbSymbol = code6 + "." + exchange;
            db->executeUpdate(
                "INSERT INTO live.concept_membership(concept_code,symbol_id,symbol) "
                "VALUES($1,(SELECT id FROM ref.symbol_info WHERE symbol=$2 OR symbol=$3),$2) ON CONFLICT DO NOTHING",
                {P{code}, P{dbSymbol}, P{code6}});
        }
        // 更新 catalog 的成分股数量
        db->executeUpdate(
            "UPDATE live.concept_catalog SET stock_count=$1,updated_at=NOW() WHERE concept_code=$2",
            {P{n}, P{code}});
        totalMembers += n;
        ++totalConcepts;
        members->release();
    }
    cats->release();
    INTERNAL_INFO_STREAM << "[PostMktSync] CONCEPT: 同步完成 concepts=" << totalConcepts
                         << " members=" << totalMembers;
}

void PostMarketSyncService::computeConceptDailyStats(int tradingDay)
{
    char dateBuf[16];
    int y = tradingDay / 10000, m = (tradingDay / 100) % 100, d = tradingDay % 100;
    std::snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", y, m, d);

    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) return;

    // 聚合: 每概念 = 成分股今涨幅等权均值 + 上涨比例(宽度代理) + 龙头
    // breadth 简化: 成分股中今涨幅>0 的比例 (width>0 是趋势宽度, 语义近似 MA60 宽度)
    // leader: PG 不支持 FIRST(), 用 DISTINCT ON + 子查询
    using P = astock::database::SqlParam;
    std::string dateStr(dateBuf);
    db->executeUpdate(
        "WITH concept_daily AS ("
        "  SELECT cm.concept_code, si.id AS sid, si.symbol, d.change_pct "
        "  FROM live.concept_membership cm "
        "  JOIN ref.symbol_info si ON cm.symbol_id = si.id "
        "  JOIN mkt.daily_bar d ON d.symbol_id = si.id AND d.trade_date=$1::date "
        "), leader AS ("
        "  SELECT DISTINCT ON (concept_code) concept_code, sid, symbol, change_pct "
        "  FROM concept_daily ORDER BY concept_code, change_pct DESC"
        ") "
        "INSERT INTO live.concept_daily_stats "
        "(concept_code, trade_date, avg_return, breadth, leader_id, leader_symbol, leader_return) "
        "SELECT cd.concept_code, $1::date, "
        "AVG(cd.change_pct), "
        "AVG(CASE WHEN cd.change_pct > 0 THEN 1.0 ELSE 0.0 END), "
        "l.sid, l.symbol, l.change_pct "
        "FROM concept_daily cd LEFT JOIN leader l ON cd.concept_code = l.concept_code "
        "GROUP BY cd.concept_code, l.sid, l.symbol, l.change_pct "
        "ON CONFLICT(concept_code, trade_date) DO UPDATE SET "
        "avg_return=EXCLUDED.avg_return, breadth=EXCLUDED.breadth, "
        "leader_id=EXCLUDED.leader_id, leader_symbol=EXCLUDED.leader_symbol, "
        "leader_return=EXCLUDED.leader_return",
        {P{dateStr}});

    // 龙头排名 (TOP5 per concept)
    db->executeUpdate(
        "INSERT INTO live.concept_leader_rank "
        "(concept_code, trade_date, symbol, rank, change_pct) "
        "SELECT concept_code, $1::date, symbol, rnk, change_pct FROM ("
        "  SELECT cm.concept_code, si.symbol, d.change_pct, "
        "  ROW_NUMBER() OVER (PARTITION BY cm.concept_code ORDER BY d.change_pct DESC) AS rnk "
        "  FROM live.concept_membership cm "
        "  JOIN ref.symbol_info si ON cm.symbol_id = si.id "
        "  JOIN mkt.daily_bar d ON d.symbol_id = si.id AND d.trade_date=$1::date"
        ") sub WHERE rnk <= 5 "
        "ON CONFLICT(concept_code, trade_date, symbol) DO UPDATE SET "
        "rank=EXCLUDED.rank, change_pct=EXCLUDED.change_pct",
        {P{dateStr}});

    INTERNAL_DEBUG_STREAM << "[PostMktSync] CONCEPT: 日聚合+排名完成 date=" << dateBuf;
}

} // namespace astock::infrastructure::database

