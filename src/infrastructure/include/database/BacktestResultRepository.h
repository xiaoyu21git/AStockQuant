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
    // 市场数据
    std::string dataStartDate;
    std::string dataEndDate;
    // 策略参数
    std::string combineMode;
    int targetPositionCount = 0;
    int maxPositions = 0;
    int fastPeriod = 0;
    int slowPeriod = 0;
    int signalPeriod = 0;
    // 因子
    int factorCount = 0;
    std::string factorIds;
    std::string factorWeights;
    // 绩效
    double totalReturn = 0;
    double annualizedReturn = 0;
    double sharpeRatio = 0;
    double maxDrawdown = 0;
    double winRate = 0;
    double profitFactor = 0;
    double sortinoRatio = 0;
    double calmarRatio = 0;
    double volatility = 0;
    double alpha = 0;
    double beta = 0;
    // 交易统计
    int totalTrades = 0;
    int winningTrades = 0;
    int losingTrades = 0;
    double totalProfit = 0;
    double totalLoss = 0;
    double maxWin = 0;
    double maxLoss = 0;
    double avgHoldingDays = 0;
    double avgPositions = 0;
    // 风控
    int stopLossFills = 0;
    int ruleExitFills = 0;
    int normalSellFills = 0;
    int riskRejected = 0;
    // 净值曲线
    std::string equityCurveJson;
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
    std::string date;
    double totalAsset = 0.0;
    double dailyReturn = 0.0;
};

struct StoredStrategyTrade {
    std::string runId;
    std::string tradeDate;
    std::string symbol;
    bool isBuy{true};
    std::int64_t quantity{0};
    double price{0.0};
    double realizedPnl{0.0};
};

class BacktestResultRepository {
public:
    explicit BacktestResultRepository(astock::database::ISqlDatabase& db);
    bool ensureTables();

    bool saveStrategyBacktest(const StoredStrategyBacktest& record);
    std::vector<StoredStrategyBacktest> loadStrategyBacktests(const std::string& strategyId,
                                                               int limit = 50);
    bool saveStrategyTrades(const std::vector<StoredStrategyTrade>& trades);

    bool saveFactorBacktest(const StoredFactorBacktest& record);
    std::vector<StoredFactorBacktest> loadFactorBacktests(const std::string& factorId, int limit = 50);

    bool saveDailySnapshot(const DailyEquitySnapshot& snap);
    std::vector<DailyEquitySnapshot> loadDailySnapshots(const std::string& strategyId, int limit = 50);
    std::vector<double> loadRecentDailyReturns(const std::string& strategyId, int lookback = 30);

private:
    astock::database::ISqlDatabase& m_db;
    bool m_tablesEnsured{false};
};

} // namespace domain::backtest
