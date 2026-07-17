#include "database/BacktestResultRepository.h"
#include "database/ISqlDatabase.h"
#include "foundation/log/logging.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <sstream>
#include <iomanip>

namespace domain::backtest {

BacktestResultRepository::BacktestResultRepository(astock::database::ISqlDatabase& db)
    : m_db(db) {}

bool BacktestResultRepository::ensureTables()
{
    if (m_tablesEnsured) return true;

    // 旧版 strategy_backtest_results 是 (id, data) 两列, 与当前 schema 不兼容;
    // 检测到缺少 strategy_id 列时重建 (该表仅为回测结果缓存, 可安全重建)
    {
        auto probe = m_db.executeQuery(
            "SELECT column_name FROM information_schema.columns "
            "WHERE table_schema='live' AND table_name='strategy_backtest_results' "
            "AND column_name='strategy_id'");
        auto exists = m_db.executeQuery(
            "SELECT 1 FROM information_schema.tables "
            "WHERE table_schema='live' AND table_name='strategy_backtest_results'");
        if (!exists.isEmpty() && probe.isEmpty()) {
            INTERNAL_WARN_STREAM << "[BacktestRepo] 检测到旧版 strategy_backtest_results schema, 重建";
            m_db.executeUpdate("DROP TABLE live.strategy_backtest_results");
        }
    }

    const char* kCreateStrategyBacktest = R"SQL(
        CREATE TABLE IF NOT EXISTS live.strategy_backtest_results (
            id            VARCHAR(36) PRIMARY KEY,
            strategy_id   VARCHAR(64) NOT NULL,
            run_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
            behavior_kind INT DEFAULT 0,
            metrics_json  TEXT,
            time_series_json TEXT,
            trade_stats_json  TEXT
        ))SQL";
    const char* kIdxStrategyBacktest =
        "CREATE INDEX IF NOT EXISTS idx_strategy_backtest_results_sid "
        "ON live.strategy_backtest_results(strategy_id, run_at DESC)";

    const char* kCreateStrategyTrades = R"SQL(
        CREATE TABLE IF NOT EXISTS live.strategy_backtest_trades (
            run_id      VARCHAR(36) NOT NULL,
            trade_date  DATE NOT NULL,
            symbol      VARCHAR(16) NOT NULL,
            side        CHAR(1) NOT NULL,
            quantity    BIGINT NOT NULL,
            price       DOUBLE PRECISION NOT NULL,
            realized_pnl DOUBLE PRECISION DEFAULT 0
        ))SQL";
    const char* kIdxStrategyTrades =
        "CREATE INDEX IF NOT EXISTS idx_strategy_backtest_trades_run "
        "ON live.strategy_backtest_trades(run_id, trade_date)";


    const char* kCreateFactorBacktest = R"SQL(
        CREATE TABLE IF NOT EXISTS alpha.factor_backtest_results (
            id          VARCHAR(36) PRIMARY KEY,
            factor_id   VARCHAR(64) NOT NULL,
            run_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
            num_groups  INT DEFAULT 5,
            metrics_json TEXT
        ))SQL";
    const char* kIdxFactorBacktest =
        "CREATE INDEX IF NOT EXISTS idx_factor_backtest_results_fid "
        "ON alpha.factor_backtest_results(factor_id, run_at DESC)";

    const char* kCreateDailySnapshots = R"SQL(
        CREATE TABLE IF NOT EXISTS live.daily_equity_snapshots (
            id          VARCHAR(36) PRIMARY KEY,
            strategy_id VARCHAR(64) NOT NULL,
            snap_date   DATE NOT NULL,
            total_asset DOUBLE PRECISION DEFAULT 0,
            daily_return DOUBLE PRECISION DEFAULT 0,
            UNIQUE (strategy_id, snap_date)
        ))SQL";

    m_db.executeQuery(kCreateStrategyBacktest);
    m_db.executeQuery(kIdxStrategyBacktest);
    m_db.executeQuery(kCreateStrategyTrades);
    m_db.executeQuery(kIdxStrategyTrades);
    m_db.executeQuery(kCreateFactorBacktest);
    m_db.executeQuery(kIdxFactorBacktest);
    m_db.executeQuery(kCreateDailySnapshots);
    m_tablesEnsured = true;
    return true;
}

// ── 策略回测 ──

bool BacktestResultRepository::saveStrategyBacktest(const StoredStrategyBacktest& r)
{
    ensureTables();
    using P = astock::database::SqlParam;
    // 参数化写入 (旧实现字符串拼接 JSON, 含引号即炸且错误被吞)
    const int affected = m_db.executeUpdate(
        "INSERT INTO live.strategy_backtest_results "
        "(id, strategy_id, behavior_kind, metrics_json, time_series_json, trade_stats_json) "
        "VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT(id) DO UPDATE SET "
        "strategy_id=EXCLUDED.strategy_id, run_at=NOW(), "
        "behavior_kind=EXCLUDED.behavior_kind, metrics_json=EXCLUDED.metrics_json, "
        "time_series_json=EXCLUDED.time_series_json, trade_stats_json=EXCLUDED.trade_stats_json",
        {P{r.id}, P{r.strategyId}, P{r.behaviorKind},
         P{r.metricsJson}, P{r.timeSeriesJson}, P{r.tradeStatsJson}});
    if (affected <= 0) {
        INTERNAL_ERROR_STREAM << "[BacktestRepo] saveStrategyBacktest failed id=" << r.id
                              << " error=" << m_db.lastError();
        return false;
    }
    return true;
}

bool BacktestResultRepository::saveStrategyTrades(const std::vector<StoredStrategyTrade>& trades)
{
    ensureTables();
    using P = astock::database::SqlParam;
    for (const auto& trade : trades) {
        const int affected = m_db.executeUpdate(
            "INSERT INTO live.strategy_backtest_trades "
            "(run_id, trade_date, symbol, side, quantity, price, realized_pnl) "
            "VALUES (?, ?::date, ?, ?, ?, ?, ?)",
            {P{trade.runId}, P{trade.tradeDate}, P{trade.symbol},
             P{std::string(trade.isBuy ? "B" : "S")},
             P{static_cast<std::int64_t>(trade.quantity)}, P{trade.price}, P{trade.realizedPnl}});
        if (affected <= 0) {
            INTERNAL_ERROR_STREAM << "[BacktestRepo] saveStrategyTrades failed run=" << trade.runId
                                  << " error=" << m_db.lastError();
            return false;
        }
    }
    return true;
}

std::vector<StoredStrategyBacktest> BacktestResultRepository::loadStrategyBacktests(
    const std::string& strategyId, int limit)
{
    ensureTables();
    std::ostringstream sql;
    sql << "SELECT id, strategy_id, run_at, behavior_kind, metrics_json, time_series_json, trade_stats_json "
           "FROM live.strategy_backtest_results WHERE strategy_id='"
        << strategyId << "' ORDER BY run_at DESC LIMIT " << limit;

    auto result = m_db.executeQuery(sql.str());
    std::vector<StoredStrategyBacktest> records;
    for (int i = 0; i < result.rowCount(); ++i) {
        const auto& row = result.getRow(i);
        StoredStrategyBacktest r;
        r.id           = row.getString("id");
        r.strategyId   = row.getString("strategy_id");
        r.runAt        = row.getString("run_at");
        r.behaviorKind = row.getInt("behavior_kind");
        r.metricsJson  = row.getString("metrics_json");
        r.timeSeriesJson = row.getString("time_series_json");
        r.tradeStatsJson = row.getString("trade_stats_json");
        records.push_back(std::move(r));
    }
    return records;
}

// ── 因子回测 ──

bool BacktestResultRepository::saveFactorBacktest(const StoredFactorBacktest& r)
{
    ensureTables();
    std::ostringstream sql;
    sql << "INSERT INTO alpha.factor_backtest_results "
           "(id, factor_id, run_at, num_groups, metrics_json) VALUES ('"
        << r.id << "','" << r.factorId << "','" << r.runAt << "',"
        << r.numGroups << ",'" << r.metricsJson
        << "') ON CONFLICT(id) DO UPDATE SET "
           "factor_id=EXCLUDED.factor_id, run_at=EXCLUDED.run_at, "
           "num_groups=EXCLUDED.num_groups, metrics_json=EXCLUDED.metrics_json";
    m_db.executeQuery(sql.str()); return true;
}

std::vector<StoredFactorBacktest> BacktestResultRepository::loadFactorBacktests(
    const std::string& factorId, int limit)
{
    ensureTables();
    std::ostringstream sql;
    sql << "SELECT id, factor_id, run_at, num_groups, metrics_json "
           "FROM alpha.factor_backtest_results WHERE factor_id='"
        << factorId << "' ORDER BY run_at DESC LIMIT " << limit;

    auto result = m_db.executeQuery(sql.str());
    std::vector<StoredFactorBacktest> records;
    for (int i = 0; i < result.rowCount(); ++i) {
        const auto& row = result.getRow(i);
        StoredFactorBacktest r;
        r.id        = row.getString("id");
        r.factorId  = row.getString("factor_id");
        r.runAt     = row.getString("run_at");
        r.numGroups = row.getInt("num_groups");
        r.metricsJson = row.getString("metrics_json");
        records.push_back(std::move(r));
    }
    return records;
}

// ── 每日净值 ──

bool BacktestResultRepository::saveDailySnapshot(const DailyEquitySnapshot& snap)
{
    ensureTables();
    std::ostringstream sql;
    sql << "INSERT INTO live.daily_equity_snapshots "
           "(id, strategy_id, snap_date, total_asset, daily_return) VALUES ('"
        << snap.id << "','" << snap.strategyId << "','" << snap.date << "',"
        << snap.totalAsset << "," << snap.dailyReturn
        << ") ON CONFLICT(strategy_id, snap_date) DO UPDATE SET "
           "total_asset=EXCLUDED.total_asset, daily_return=EXCLUDED.daily_return";
    m_db.executeQuery(sql.str()); return true;
}

std::vector<DailyEquitySnapshot> BacktestResultRepository::loadDailySnapshots(
    const std::string& strategyId, int limit)
{
    ensureTables();
    std::ostringstream sql;
    sql << "SELECT id, strategy_id, snap_date, total_asset, daily_return "
           "FROM live.daily_equity_snapshots WHERE strategy_id='"
        << strategyId << "' ORDER BY snap_date DESC LIMIT " << limit;

    auto result = m_db.executeQuery(sql.str());
    std::vector<DailyEquitySnapshot> records;
    for (int i = 0; i < result.rowCount(); ++i) {
        const auto& row = result.getRow(i);
        DailyEquitySnapshot s;
        s.id         = row.getString("id");
        s.strategyId = row.getString("strategy_id");
        s.date       = row.getString("snap_date");
        s.totalAsset = row.getDouble("total_asset");
        s.dailyReturn = row.getDouble("daily_return");
        records.push_back(std::move(s));
    }
    return records;
}

std::vector<double> BacktestResultRepository::loadRecentDailyReturns(
    const std::string& strategyId, int lookback)
{
    auto snaps = loadDailySnapshots(strategyId, lookback);
    std::vector<double> returns;
    returns.reserve(snaps.size());
    // snapshots are DESC; reverse to chronological
    for (auto it = snaps.rbegin(); it != snaps.rend(); ++it)
        returns.push_back(it->dailyReturn);
    return returns;
}

} // namespace domain::backtest
