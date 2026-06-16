#pragma once
// ═════════════════════════════════════════════════════════════════════════
// BacktestResultRepository — 回测结果持久化
// 按已有策略/因子回测结果 + 每日实盘净值快照
// ═════════════════════════════════════════════════════════════════════════

#include <QString>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <memory>
#include <string>
#include <vector>
#include <optional>

namespace astock { namespace database {
class ISqlDatabase;
} }

namespace domain::backtest {

struct StoredStrategyBacktest {
    std::string id;
    std::string strategyId;
    std::string runAt;
    int behaviorKind = 0;
    std::string metricsJson;
    std::string timeSeriesJson;
    std::string tradeStatsJson;
};

struct StoredFactorBacktest {
    std::string id;
    std::string factorId;
    std::string runAt;
    int numGroups = 5;
    std::string metricsJson;
};

struct DailyEquitySnapshot {
    std::string id;
    std::string strategyId;
    std::string date;          // YYYY-MM-DD
    double totalAsset = 0.0;
    double dailyReturn = 0.0;
};

class BacktestResultRepository final {
public:
    explicit BacktestResultRepository(astock::database::ISqlDatabase& db);

    /// @brief 初始化表结构（首次调用时创建）
    bool ensureTables();

    // ── 策略回测结果 ──
    bool saveStrategyBacktest(const StoredStrategyBacktest& record);
    std::vector<StoredStrategyBacktest> loadStrategyBacktests(const std::string& strategyId,
                                                               int limit = 20);

    // ── 因子回测结果 ──
    bool saveFactorBacktest(const StoredFactorBacktest& record);
    std::vector<StoredFactorBacktest> loadFactorBacktests(const std::string& factorId,
                                                           int limit = 20);

    // ── 每日净值快照 ──
    bool saveDailySnapshot(const DailyEquitySnapshot& snap);
    std::vector<DailyEquitySnapshot> loadDailySnapshots(const std::string& strategyId,
                                                         int limit = 252);
    /// @brief 获取最近 ~252 个交易日的收益率序列（供指标计算）
    std::vector<double> loadRecentDailyReturns(const std::string& strategyId, int lookback = 252);

private:
    astock::database::ISqlDatabase& m_db;
    bool m_tablesEnsured{false};
};

} // namespace domain::backtest
