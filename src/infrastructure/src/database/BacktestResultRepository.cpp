#include "database/BacktestResultRepository.h"
#include "database/ISqlDatabase.h"

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

    const char* kCreateStrategyBacktest = R"SQL(
        CREATE TABLE IF NOT EXISTS strategy_backtest_results (
            id            VARCHAR(36) PRIMARY KEY,
            strategy_id   VARCHAR(64) NOT NULL,
            run_at        DATETIME NOT NULL,
            behavior_kind INT DEFAULT 0,
            metrics_json  MEDIUMTEXT,
            time_series_json MEDIUMTEXT,
            trade_stats_json  MEDIUMTEXT,
            INDEX idx_strategy_run (strategy_id, run_at DESC)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
    )SQL";

    const char* kCreateFactorBacktest = R"SQL(
        CREATE TABLE IF NOT EXISTS factor_backtest_results (
            id          VARCHAR(36) PRIMARY KEY,
            factor_id   VARCHAR(64) NOT NULL,
            run_at      DATETIME NOT NULL,
            num_groups  INT DEFAULT 5,
            metrics_json MEDIUMTEXT,
            INDEX idx_factor_run (factor_id, run_at DESC)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
    )SQL";

    const char* kCreateDailySnapshots = R"SQL(
        CREATE TABLE IF NOT EXISTS daily_equity_snapshots (
            id          VARCHAR(36) PRIMARY KEY,
            strategy_id VARCHAR(64) NOT NULL,
            snap_date   DATE NOT NULL,
            total_asset DOUBLE DEFAULT 0,
            daily_return DOUBLE DEFAULT 0,
            INDEX idx_strategy_date (strategy_id, snap_date DESC),
            UNIQUE KEY uk_strategy_date (strategy_id, snap_date)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
    )SQL";

    m_db.executeQuery(kCreateStrategyBacktest);
    m_db.executeQuery(kCreateFactorBacktest);
    m_db.executeQuery(kCreateDailySnapshots);
    m_tablesEnsured = true;
    return true;
}

// ── 策略回测 ──

bool BacktestResultRepository::saveStrategyBacktest(const StoredStrategyBacktest& r)
{
    ensureTables();
    std::ostringstream sql;
    sql << "REPLACE INTO strategy_backtest_results "
           "(id, strategy_id, run_at, behavior_kind, metrics_json, time_series_json, trade_stats_json) "
           "VALUES ('"
        << r.id << "','" << r.strategyId << "','" << r.runAt << "',"
        << r.behaviorKind << ",'"
        << QJsonDocument(QJsonObject::fromVariantMap(
               QJsonDocument::fromJson(QByteArray::fromStdString(r.metricsJson)).object().toVariantMap()))
               .toJson(QJsonDocument::Compact).toStdString()
        << "','" << r.timeSeriesJson << "','" << r.tradeStatsJson << "')";
    m_db.executeQuery(sql.str()); return true;
}

std::vector<StoredStrategyBacktest> BacktestResultRepository::loadStrategyBacktests(
    const std::string& strategyId, int limit)
{
    ensureTables();
    std::ostringstream sql;
    sql << "SELECT id, strategy_id, run_at, behavior_kind, metrics_json, time_series_json, trade_stats_json "
           "FROM strategy_backtest_results WHERE strategy_id='"
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
    sql << "REPLACE INTO factor_backtest_results "
           "(id, factor_id, run_at, num_groups, metrics_json) VALUES ('"
        << r.id << "','" << r.factorId << "','" << r.runAt << "',"
        << r.numGroups << ",'" << r.metricsJson << "')";
    m_db.executeQuery(sql.str()); return true;
}

std::vector<StoredFactorBacktest> BacktestResultRepository::loadFactorBacktests(
    const std::string& factorId, int limit)
{
    ensureTables();
    std::ostringstream sql;
    sql << "SELECT id, factor_id, run_at, num_groups, metrics_json "
           "FROM factor_backtest_results WHERE factor_id='"
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
    sql << "REPLACE INTO daily_equity_snapshots "
           "(id, strategy_id, snap_date, total_asset, daily_return) VALUES ('"
        << snap.id << "','" << snap.strategyId << "','" << snap.date << "',"
        << snap.totalAsset << "," << snap.dailyReturn << ")";
    m_db.executeQuery(sql.str()); return true;
}

std::vector<DailyEquitySnapshot> BacktestResultRepository::loadDailySnapshots(
    const std::string& strategyId, int limit)
{
    ensureTables();
    std::ostringstream sql;
    sql << "SELECT id, strategy_id, snap_date, total_asset, daily_return "
           "FROM daily_equity_snapshots WHERE strategy_id='"
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
