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

    m_db.executeUpdate("SET client_min_messages = WARNING");

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

    // 旧版 JSONB/TEXT 表已在上面按需重建，不再每次回测无条件 DROP CASCADE

    const char* kCreateStrategyBacktest = R"SQL(
        CREATE TABLE IF NOT EXISTS live.strategy_backtest_results (
            id              VARCHAR(36) PRIMARY KEY,
            strategy_id     VARCHAR(64) NOT NULL,
            run_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
            behavior_kind   INT DEFAULT 0,
            data_start_date DATE,
            data_end_date   DATE,
            combine_mode        VARCHAR(16),
            target_position_count INT,
            max_positions       INT,
            fast_period         INT,
            slow_period         INT,
            signal_period       INT,
            factor_count        INT,
            factor_ids          TEXT,
            factor_weights      TEXT,
            total_return        DOUBLE PRECISION,
            annualized_return   DOUBLE PRECISION,
            sharpe_ratio        DOUBLE PRECISION,
            max_drawdown        DOUBLE PRECISION,
            win_rate            DOUBLE PRECISION,
            profit_factor       DOUBLE PRECISION,
            sortino_ratio       DOUBLE PRECISION,
            calmar_ratio        DOUBLE PRECISION,
            volatility          DOUBLE PRECISION,
            alpha               DOUBLE PRECISION,
            beta                DOUBLE PRECISION,
            total_trades        INT,
            winning_trades      INT,
            losing_trades       INT,
            total_profit        DOUBLE PRECISION,
            total_loss          DOUBLE PRECISION,
            max_win             DOUBLE PRECISION,
            max_loss            DOUBLE PRECISION,
            avg_holding_days    DOUBLE PRECISION,
            avg_positions       DOUBLE PRECISION,
            stop_loss_fills     INT DEFAULT 0,
            rule_exit_fills     INT DEFAULT 0,
            normal_sell_fills   INT DEFAULT 0,
            risk_rejected       INT DEFAULT 0,
            equity_curve_json   TEXT
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
    const int affected = m_db.executeUpdate(
        "INSERT INTO live.strategy_backtest_results ("
        "id, strategy_id, run_at, behavior_kind, "
        "data_start_date, data_end_date, "
        "combine_mode, target_position_count, max_positions, fast_period, slow_period, signal_period, "
        "factor_count, factor_ids, factor_weights, "
        "total_return, annualized_return, sharpe_ratio, max_drawdown, win_rate, profit_factor, "
        "sortino_ratio, calmar_ratio, volatility, alpha, beta, "
        "total_trades, winning_trades, losing_trades, total_profit, total_loss, max_win, max_loss, "
        "avg_holding_days, avg_positions, "
        "stop_loss_fills, rule_exit_fills, normal_sell_fills, risk_rejected, "
        "equity_curve_json"
        ") VALUES (?,?,NOW(),?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "strategy_id=EXCLUDED.strategy_id, run_at=NOW(), behavior_kind=EXCLUDED.behavior_kind, "
        "data_start_date=EXCLUDED.data_start_date, data_end_date=EXCLUDED.data_end_date, "
        "combine_mode=EXCLUDED.combine_mode, target_position_count=EXCLUDED.target_position_count, "
        "max_positions=EXCLUDED.max_positions, fast_period=EXCLUDED.fast_period, "
        "slow_period=EXCLUDED.slow_period, signal_period=EXCLUDED.signal_period, "
        "factor_count=EXCLUDED.factor_count, factor_ids=EXCLUDED.factor_ids, factor_weights=EXCLUDED.factor_weights, "
        "total_return=EXCLUDED.total_return, annualized_return=EXCLUDED.annualized_return, "
        "sharpe_ratio=EXCLUDED.sharpe_ratio, max_drawdown=EXCLUDED.max_drawdown, "
        "win_rate=EXCLUDED.win_rate, profit_factor=EXCLUDED.profit_factor, "
        "sortino_ratio=EXCLUDED.sortino_ratio, calmar_ratio=EXCLUDED.calmar_ratio, "
        "volatility=EXCLUDED.volatility, alpha=EXCLUDED.alpha, beta=EXCLUDED.beta, "
        "total_trades=EXCLUDED.total_trades, winning_trades=EXCLUDED.winning_trades, "
        "losing_trades=EXCLUDED.losing_trades, total_profit=EXCLUDED.total_profit, "
        "total_loss=EXCLUDED.total_loss, max_win=EXCLUDED.max_win, max_loss=EXCLUDED.max_loss, "
        "avg_holding_days=EXCLUDED.avg_holding_days, avg_positions=EXCLUDED.avg_positions, "
        "stop_loss_fills=EXCLUDED.stop_loss_fills, rule_exit_fills=EXCLUDED.rule_exit_fills, "
        "normal_sell_fills=EXCLUDED.normal_sell_fills, risk_rejected=EXCLUDED.risk_rejected, "
        "equity_curve_json=EXCLUDED.equity_curve_json",
        {P{r.id}, P{r.strategyId}, P{r.behaviorKind},
         P{r.dataStartDate}, P{r.dataEndDate},
         P{r.combineMode}, P{r.targetPositionCount}, P{r.maxPositions}, P{r.fastPeriod}, P{r.slowPeriod}, P{r.signalPeriod},
         P{r.factorCount}, P{r.factorIds}, P{r.factorWeights},
         P{r.totalReturn}, P{r.annualizedReturn}, P{r.sharpeRatio}, P{r.maxDrawdown}, P{r.winRate}, P{r.profitFactor},
         P{r.sortinoRatio}, P{r.calmarRatio}, P{r.volatility}, P{r.alpha}, P{r.beta},
         P{r.totalTrades}, P{r.winningTrades}, P{r.losingTrades}, P{r.totalProfit}, P{r.totalLoss}, P{r.maxWin}, P{r.maxLoss},
         P{r.avgHoldingDays}, P{r.avgPositions},
         P{r.stopLossFills}, P{r.ruleExitFills}, P{r.normalSellFills}, P{r.riskRejected},
         P{r.equityCurveJson}});
    if (affected <= 0) {
        auto err = m_db.lastError();
        INTERNAL_ERROR_STREAM << "[BacktestRepo] saveStrategyBacktest FAILED id=" << r.id
                              << " affected=" << affected << " error=" << err;
        std::cerr << "[BacktestRepo] SAVE FAILED: " << err << std::endl;
        return false;
    }
    INTERNAL_INFO_STREAM << "[BacktestRepo] saveStrategyBacktest OK id=" << r.id;
    return true;
}

bool BacktestResultRepository::saveStrategyTrades(const std::vector<StoredStrategyTrade>& trades)
{
    ensureTables();
    using P = astock::database::SqlParam;
    m_db.beginTransaction();
    for (const auto& trade : trades) {
        const int affected = m_db.executeUpdate(
            "INSERT INTO live.strategy_backtest_trades "
            "(run_id, trade_date, symbol, side, quantity, price, realized_pnl) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)",
            {P{trade.runId}, P{trade.tradeDate}, P{trade.symbol},
             P{std::string(trade.isBuy ? "B" : "S")},
             P{static_cast<std::int64_t>(trade.quantity)}, P{trade.price}, P{trade.realizedPnl}});
        if (affected <= 0) {
            INTERNAL_ERROR_STREAM << "[BacktestRepo] saveStrategyTrades failed run=" << trade.runId
                                  << " error=" << m_db.lastError();
            m_db.rollbackTransaction();
            return false;
        }
    }
    m_db.commitTransaction();
    return true;
}

std::vector<StoredStrategyBacktest> BacktestResultRepository::loadStrategyBacktests(
    const std::string& strategyId, int limit)
{
    ensureTables();
    std::ostringstream sql;
    sql << "SELECT * FROM live.strategy_backtest_results WHERE strategy_id='"
        << strategyId << "' ORDER BY run_at DESC LIMIT " << limit;

    auto result = m_db.executeQuery(sql.str());
    std::vector<StoredStrategyBacktest> records;
    for (int i = 0; i < result.rowCount(); ++i) {
        const auto& row = result.getRow(i);
        StoredStrategyBacktest r;
        r.id                = row.getString("id");
        r.strategyId        = row.getString("strategy_id");
        r.runAt             = row.getString("run_at");
        r.behaviorKind      = row.getInt("behavior_kind");
        r.dataStartDate     = row.getString("data_start_date");
        r.dataEndDate       = row.getString("data_end_date");
        r.combineMode       = row.getString("combine_mode");
        r.targetPositionCount = row.getInt("target_position_count");
        r.maxPositions      = row.getInt("max_positions");
        r.fastPeriod        = row.getInt("fast_period");
        r.slowPeriod        = row.getInt("slow_period");
        r.signalPeriod      = row.getInt("signal_period");
        r.factorCount       = row.getInt("factor_count");
        r.factorIds         = row.getString("factor_ids");
        r.factorWeights     = row.getString("factor_weights");
        r.totalReturn       = row.getDouble("total_return");
        r.annualizedReturn  = row.getDouble("annualized_return");
        r.sharpeRatio       = row.getDouble("sharpe_ratio");
        r.maxDrawdown       = row.getDouble("max_drawdown");
        r.winRate           = row.getDouble("win_rate");
        r.profitFactor      = row.getDouble("profit_factor");
        r.sortinoRatio      = row.getDouble("sortino_ratio");
        r.calmarRatio       = row.getDouble("calmar_ratio");
        r.volatility        = row.getDouble("volatility");
        r.alpha             = row.getDouble("alpha");
        r.beta              = row.getDouble("beta");
        r.totalTrades       = row.getInt("total_trades");
        r.winningTrades     = row.getInt("winning_trades");
        r.losingTrades      = row.getInt("losing_trades");
        r.totalProfit       = row.getDouble("total_profit");
        r.totalLoss         = row.getDouble("total_loss");
        r.maxWin            = row.getDouble("max_win");
        r.maxLoss           = row.getDouble("max_loss");
        r.avgHoldingDays    = row.getDouble("avg_holding_days");
        r.avgPositions      = row.getDouble("avg_positions");
        r.stopLossFills     = row.getInt("stop_loss_fills");
        r.ruleExitFills     = row.getInt("rule_exit_fills");
        r.normalSellFills   = row.getInt("normal_sell_fills");
        r.riskRejected      = row.getInt("risk_rejected");
        r.equityCurveJson   = row.getString("equity_curve_json");
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
