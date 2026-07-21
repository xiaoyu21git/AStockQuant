#include "StrategyPerformanceModel.h"
#include "../../../infrastructure/include/database/BacktestResultRepository.h"
#include "../../../infrastructure/include/database/NativePgConnectionPool.h"
#include "../../../infrastructure/include/database/ISqlDatabase.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <sstream>

StrategyPerformanceModel::StrategyPerformanceModel(QObject* parent)
    : QAbstractListModel(parent) {}

StrategyPerformanceModel::~StrategyPerformanceModel() = default;

void StrategyPerformanceModel::setStrategyId(const QString& id) {
    if (m_strategyId == id) return;
    m_strategyId = id;
    emit strategyIdChanged();
    refresh();
}

int StrategyPerformanceModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant StrategyPerformanceModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const auto& row = m_rows[index.row()];
    switch (role) {
    case RunAtRole:            return row.value("runAt");
    case TotalReturnRole:      return row.value("totalReturn");
    case AnnualizedReturnRole: return row.value("annualizedReturn");
    case SharpeRatioRole:      return row.value("sharpeRatio");
    case MaxDrawdownRole:      return row.value("maxDrawdown");
    case VolatilityRole:       return row.value("volatility");
    case WinRateRole:          return row.value("winRate");
    case SortinoRatioRole:     return row.value("sortinoRatio");
    case CalmarRatioRole:      return row.value("calmarRatio");
    case ProfitFactorRole:     return row.value("profitFactor");
    case BehaviorKindRole:     return row.value("behaviorKind");
    case ResultIdRole:         return row.value("id");
    case ParametersRole:       return row.value("parameters");
    case Qt::DisplayRole:      return row.value("runAt");
    }
    return {};
}

QHash<int, QByteArray> StrategyPerformanceModel::roleNames() const {
    return {
        {RunAtRole,            "runAt"},
        {TotalReturnRole,      "totalReturn"},
        {AnnualizedReturnRole, "annualizedReturn"},
        {SharpeRatioRole,      "sharpeRatio"},
        {MaxDrawdownRole,      "maxDrawdown"},
        {VolatilityRole,       "volatility"},
        {WinRateRole,          "winRate"},
        {SortinoRatioRole,     "sortinoRatio"},
        {CalmarRatioRole,      "calmarRatio"},
        {ProfitFactorRole,     "profitFactor"},
        {BehaviorKindRole,     "behaviorKind"},
        {ResultIdRole,         "resultId"},
        {ParametersRole,       "parameters"}
    };
}

bool StrategyPerformanceModel::ensureRepo() {
    if (m_repo) return true;
    auto& pool = astock::database::NativePgConnectionPool::instance();
    if (!pool.isInitialized()) { emit errorOccurred("数据库未初始化"); return false; }
    auto db = pool.getConnection();
    if (!db) { emit errorOccurred("数据库连接失败"); return false; }
    m_repo = std::make_unique<domain::backtest::BacktestResultRepository>(*db);
    m_repo->ensureTables();
    return true;
}

void StrategyPerformanceModel::refresh() {
    beginResetModel();
    m_rows.clear();
    if (!m_strategyId.isEmpty() && ensureRepo()) {
        auto records = m_repo->loadStrategyBacktests(m_strategyId.toStdString(), 50);
        for (const auto& r : records) {
            QVariantMap item;
            item["id"]              = QString::fromStdString(r.id);
            item["runAt"]           = QString::fromStdString(r.runAt);
            item["behaviorKind"]    = r.behaviorKind;
            item["totalReturn"]     = r.totalReturn;
            item["annualizedReturn"]= r.annualizedReturn;
            item["sharpeRatio"]     = r.sharpeRatio;
            item["maxDrawdown"]     = r.maxDrawdown;
            item["volatility"]      = r.volatility;
            item["winRate"]         = r.winRate;
            item["sortinoRatio"]    = r.sortinoRatio;
            item["calmarRatio"]     = r.calmarRatio;
            item["profitFactor"]    = r.profitFactor;
            // 策略参数快照
            QVariantMap params;
            params["dataStartDate"]       = QString::fromStdString(r.dataStartDate);
            params["dataEndDate"]         = QString::fromStdString(r.dataEndDate);
            params["combineMode"]         = QString::fromStdString(r.combineMode);
            params["targetPositionCount"] = r.targetPositionCount;
            params["maxPositions"]        = r.maxPositions;
            params["fastPeriod"]          = r.fastPeriod;
            params["slowPeriod"]          = r.slowPeriod;
            params["signalPeriod"]        = r.signalPeriod;
            params["factorCount"]         = r.factorCount;
            params["factorIds"]           = QString::fromStdString(r.factorIds);
            params["factorWeights"]       = QString::fromStdString(r.factorWeights);
            item["parameters"] = params;
            m_rows.append(item);
        }
    }
    endResetModel();
}

QVariantMap StrategyPerformanceModel::loadResultDetail(int row) {
    QVariantMap detail;
    if (row < 0 || row >= m_rows.size() || !ensureRepo()) return detail;

    QString resultId = m_rows[row].value("id").toString();
    auto records = m_repo->loadStrategyBacktests(m_strategyId.toStdString(), 100);
    for (const auto& r : records) {
        if (QString::fromStdString(r.id) != resultId) continue;
        detail["id"]   = resultId;
        detail["runAt"] = QString::fromStdString(r.runAt);
        // 策略参数
        QVariantMap params;
        params["dataStartDate"]       = QString::fromStdString(r.dataStartDate);
        params["dataEndDate"]         = QString::fromStdString(r.dataEndDate);
        params["combineMode"]         = QString::fromStdString(r.combineMode);
        params["targetPositionCount"] = r.targetPositionCount;
        params["maxPositions"]        = r.maxPositions;
        params["fastPeriod"]          = r.fastPeriod;
        params["slowPeriod"]          = r.slowPeriod;
        params["signalPeriod"]        = r.signalPeriod;
        params["factorCount"]         = r.factorCount;
        params["factorIds"]           = QString::fromStdString(r.factorIds);
        params["factorWeights"]       = QString::fromStdString(r.factorWeights);
        detail["parameters"] = params;
        // 交易统计
        QVariantMap ts;
        ts["totalTrades"]   = r.totalTrades;
        ts["winningTrades"] = r.winningTrades;
        ts["losingTrades"]  = r.losingTrades;
        ts["totalProfit"]   = r.totalProfit;
        ts["totalLoss"]     = r.totalLoss;
        ts["largestWin"]    = r.maxWin;
        ts["largestLoss"]   = r.maxLoss;
        ts["avgHoldingDays"] = r.avgHoldingDays;
        ts["avgPositions"]   = r.avgPositions;
        detail["tradeStats"] = ts;
        // 风控分类
        QVariantMap rc;
        rc["stopLossFills"]   = r.stopLossFills;
        rc["ruleExitFills"]   = r.ruleExitFills;
        rc["normalSellFills"] = r.normalSellFills;
        rc["riskRejected"]    = r.riskRejected;
        detail["riskControl"] = rc;
        // 净值曲线
        QJsonParseError err;
        auto eqDoc = QJsonDocument::fromJson(QByteArray::fromStdString(r.equityCurveJson), &err);
        if (err.error == QJsonParseError::NoError && eqDoc.isObject())
            detail["timeSeries"] = eqDoc.object().toVariantMap();
        break;
    }
    return detail;
}

QVariantList StrategyPerformanceModel::loadTrades(const QString& runId, int offset, int limit) {
    QVariantList list;
    if (!ensureRepo()) return list;

    auto& pool = astock::database::NativePgConnectionPool::instance();
    auto db = pool.getConnection();
    if (!db || !db->isOpen()) return list;

    std::ostringstream sql;
    sql << "SELECT trade_date, symbol, side, quantity, price, realized_pnl "
           "FROM live.strategy_backtest_trades WHERE run_id='"
        << runId.toStdString() << "' ORDER BY trade_date ASC OFFSET "
        << offset << " LIMIT " << limit;
    auto result = db->executeQuery(sql.str());
    for (int i = 0; i < result.rowCount(); ++i) {
        const auto& row = result.getRow(i);
        QVariantMap t;
        t["date"]   = QString::fromStdString(row.getString("trade_date")).left(10);
        t["symbol"] = QString::fromStdString(row.getString("symbol"));
        t["side"]   = QString::fromStdString(row.getString("side"));
        t["qty"]    = row.getInt("quantity");
        t["price"]  = row.getDouble("price");
        t["pnl"]    = row.getDouble("realized_pnl");
        list.append(t);
    }
    return list;
}

QVariantList StrategyPerformanceModel::loadDailyPositions(const QString& runId) {
    QVariantList list;
    if (!ensureRepo()) return list;

    auto& pool = astock::database::NativePgConnectionPool::instance();
    auto db = pool.getConnection();
    if (!db || !db->isOpen()) return list;

    std::ostringstream sql;
    sql << "SELECT trade_date, symbol, "
           "SUM(CASE WHEN side='B' THEN quantity ELSE -quantity END) AS net_qty, "
           "SUM(CASE WHEN side='S' THEN realized_pnl ELSE 0 END) AS total_pnl "
           "FROM live.strategy_backtest_trades WHERE run_id='"
        << runId.toStdString() << "' GROUP BY trade_date, symbol ORDER BY trade_date, symbol";
    auto result = db->executeQuery(sql.str());
    for (int i = 0; i < result.rowCount(); ++i) {
        const auto& row = result.getRow(i);
        QVariantMap p;
        p["date"]    = QString::fromStdString(row.getString("trade_date")).left(10);
        p["symbol"]  = QString::fromStdString(row.getString("symbol"));
        p["netQty"]  = row.getInt("net_qty");
        p["totalPnl"] = row.getDouble("total_pnl");
        list.append(p);
    }
    return list;
}
