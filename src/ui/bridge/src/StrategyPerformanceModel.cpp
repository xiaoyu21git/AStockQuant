#include "StrategyPerformanceModel.h"
#include "../../../infrastructure/include/database/BacktestResultRepository.h"
#include "../../../infrastructure/include/database/NativePgConnectionPool.h"
#include "../../../infrastructure/include/database/ISqlDatabase.h"

#include <QJsonDocument>
#include <QJsonObject>

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
        {ResultIdRole,         "resultId"}
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
            item["id"]           = QString::fromStdString(r.id);
            item["runAt"]        = QString::fromStdString(r.runAt);
            item["behaviorKind"] = r.behaviorKind;

            QJsonParseError err;
            auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(r.metricsJson), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject m = doc.object();
                item["totalReturn"]      = m.value("totalReturn").toDouble();
                item["annualizedReturn"] = m.value("annualizedReturn").toDouble();
                item["sharpeRatio"]      = m.value("sharpeRatio").toDouble();
                item["maxDrawdown"]      = m.value("maxDrawdown").toDouble();
                item["volatility"]       = m.value("volatility").toDouble();
                item["winRate"]          = m.value("winRate").toDouble();
                item["sortinoRatio"]     = m.value("sortinoRatio").toDouble();
                item["calmarRatio"]      = m.value("calmarRatio").toDouble();
                item["profitFactor"]     = m.value("profitFactor").toDouble();
            }
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
        detail["id"]    = resultId;
        detail["runAt"] = QString::fromStdString(r.runAt);

        QJsonParseError err;
        auto tsDoc = QJsonDocument::fromJson(QByteArray::fromStdString(r.timeSeriesJson), &err);
        if (err.error == QJsonParseError::NoError && tsDoc.isObject())
            detail["timeSeries"] = tsDoc.object().toVariantMap();
        auto tradeDoc = QJsonDocument::fromJson(QByteArray::fromStdString(r.tradeStatsJson), &err);
        if (err.error == QJsonParseError::NoError && tradeDoc.isObject())
            detail["tradeStats"] = tradeDoc.object().toVariantMap();
        break;
    }
    return detail;
}
