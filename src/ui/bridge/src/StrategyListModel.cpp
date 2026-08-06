#include "../include/StrategyListModel.h"
#include "foundation/log/logging.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

StrategyListModel::StrategyListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

StrategyListModel::~StrategyListModel() = default;

int StrategyListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

QVariant StrategyListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }

    const int rowIndex = index.row();
    if (rowIndex < 0 || rowIndex >= m_rows.size()) {
        return {};
    }

    const StrategyRow& row = m_rows.at(rowIndex);
    switch (role) {
    case IdRole:
        return row.strategyId;
    case NameRole:
        return row.name;
    case StatusRole:
        return row.status;
    case StatusTextRole:
        return row.statusText;
    case UpdatedAtRole:
        return row.updatedAt;
    case RunningRole:
        return row.running;
    case DisplayStatusRole:
        return row.displayStatus.isEmpty() ? QStringLiteral("已停止") : row.displayStatus;
    case ReturnsRole:
        return row.returns;
    case SharpeRatioRole:
        return row.sharpeRatio;
    case MaxDrawdownRole:
        return row.maxDrawdown;
    case WinRateRole:
        return row.winRate;
    case TradesRole:
        return row.trades;
    case RunningDaysRole:
        return row.runningDays;
    case DailyPnLRole:
        return row.dailyPnL;
    case PositionRole:
        return row.position;
    default:
        return {};
    }
}

QHash<int, QByteArray> StrategyListModel::roleNames() const
{
    return {
        {IdRole, "strategyId"},
        {NameRole, "name"},
        {StatusRole, "status"},
        {StatusTextRole, "statusText"},
        {UpdatedAtRole, "updatedAt"},
        {RunningRole, "running"},
        {DisplayStatusRole, "displayStatus"},
        {ReturnsRole, "returns"},
        {SharpeRatioRole, "sharpeRatio"},
        {MaxDrawdownRole, "maxDrawdown"},
        {WinRateRole, "winRate"},
        {TradesRole, "trades"},
        {RunningDaysRole, "runningDays"},
        {DailyPnLRole, "dailyPnL"},
        {PositionRole, "position"}
    };
}

void StrategyListModel::replaceAll(const QVariantList& items)
{
    INTERNAL_INFO_STREAM << "[ListModel] replaceAll START rows=" << static_cast<int>(items.size());
    QVector<StrategyRow> nextRows;
    nextRows.reserve(items.size());
    INTERNAL_INFO_STREAM << "[StrategyListModel] replaceAll input rows=" << items.size();

    for (const QVariant& item : items) {
        const QVariantMap map = item.toMap();
        if (map.isEmpty()) {
            INTERNAL_WARN_STREAM << "[StrategyListModel] skip empty map row";
            continue;
        }

        const StrategyRow row = fromVariantMap(map);
        if (row.strategyId.isEmpty()) {
            INTERNAL_WARN_STREAM << QStringLiteral("[StrategyListModel] skip row: empty strategyId raw=%1")
                                        .arg(QString::fromUtf8(
                                            QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(
                                                QJsonDocument::Compact))).toStdString();
            continue;
        }

        INTERNAL_INFO_STREAM << QStringLiteral("[StrategyListModel] mapped row strategyId=%1 name=%2 status=%3 updatedAt=%4")
                                 .arg(row.strategyId,
                                      row.name,
                                      QString::number(row.status),
                                      row.updatedAt).toStdString();
        nextRows.push_back(row);
    }

    INTERNAL_INFO_STREAM << "[ListModel] replaceAll building model, nextRows=" << static_cast<int>(nextRows.size());
    beginResetModel();
    m_rows = std::move(nextRows);
    endResetModel();
    emit countChanged();
    INTERNAL_INFO_STREAM << "[ListModel] replaceAll DONE";
}

void StrategyListModel::upsertOne(const QVariantMap& item)
{
    const StrategyRow row = fromVariantMap(item);
    if (row.strategyId.isEmpty()) {
        return;
    }

    const int existing = findRow(row.strategyId);
    if (existing >= 0) {
        m_rows[existing] = row;
        const QModelIndex changed = index(existing);
        emit dataChanged(changed, changed);
        return;
    }

    const int insertAt = m_rows.size();
    beginInsertRows(QModelIndex(), insertAt, insertAt);
    m_rows.push_back(row);
    endInsertRows();
    emit countChanged();
}

bool StrategyListModel::removeById(const QString& strategyId)
{
    const QString normalized = strategyId.trimmed();
    if (normalized.isEmpty()) {
        return false;
    }

    const int existing = findRow(normalized);
    if (existing < 0) {
        return false;
    }

    beginRemoveRows(QModelIndex(), existing, existing);
    m_rows.remove(existing);
    endRemoveRows();
    emit countChanged();
    return true;
}

void StrategyListModel::clear()
{
    if (m_rows.isEmpty()) {
        return;
    }

    beginResetModel();
    m_rows.clear();
    endResetModel();
    emit countChanged();
}

QVariantMap StrategyListModel::getRow(int index) const
{
    if (index < 0 || index >= m_rows.size()) {
        return {};
    }
    return toVariantMap(m_rows.at(index));
}

StrategyListModel::StrategyRow StrategyListModel::fromVariantMap(const QVariantMap& map)
{
    StrategyRow row;
    row.strategyId = map.value(QStringLiteral("strategyId")).toString().trimmed();

    row.name = map.value(QStringLiteral("strategyName")).toString().trimmed();

    row.status = map.value(QStringLiteral("status")).toInt();
    if (row.status == 0 && map.contains(QStringLiteral("statusIndex"))) {
        row.status = map.value(QStringLiteral("statusIndex")).toInt();
    }

    row.statusText = map.value(QStringLiteral("statusText")).toString().trimmed();
    row.updatedAt = map.value(QStringLiteral("updatedAt")).toString().trimmed();
    row.running = map.value(QStringLiteral("running")).toBool();
    row.displayStatus = map.value(QStringLiteral("displayStatus")).toString();
    row.returns      = map.value(QStringLiteral("returns")).toDouble();
    row.sharpeRatio  = map.value(QStringLiteral("sharpeRatio")).toDouble();
    row.maxDrawdown  = map.value(QStringLiteral("maxDrawdown")).toDouble();
    row.winRate      = map.value(QStringLiteral("winRate")).toDouble();
    row.trades       = map.value(QStringLiteral("trades")).toInt();
    row.runningDays  = map.value(QStringLiteral("runningDays")).toInt();
    row.dailyPnL     = map.value(QStringLiteral("dailyPnL")).toDouble();
    row.position     = map.value(QStringLiteral("position")).toDouble();

    INTERNAL_INFO_STREAM << "[StrategyListModel] fromVariantMap: "
                         << row.strategyId.toStdString()
                         << " returns=" << row.returns
                         << " sharpe=" << row.sharpeRatio
                         << " trades=" << row.trades
                         << " runningDays=" << row.runningDays;

    if (row.name.isEmpty()) {
        INTERNAL_WARN_STREAM << QStringLiteral("[StrategyListModel] mapped empty name for strategyId=%1 raw=%2")
                                    .arg(row.strategyId,
                                         QString::fromUtf8(
                                             QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(
                                                 QJsonDocument::Compact))).toStdString();
    }
    return row;
}

QVariantMap StrategyListModel::toVariantMap(const StrategyRow& row) const
{
    QVariantMap map;
    map.insert(QStringLiteral("strategyId"), row.strategyId);
    map.insert(QStringLiteral("name"), row.name);
    map.insert(QStringLiteral("status"), row.status);
    map.insert(QStringLiteral("statusText"), row.statusText);
    map.insert(QStringLiteral("updatedAt"), row.updatedAt);
    map.insert(QStringLiteral("running"), row.running);
    map.insert(QStringLiteral("displayStatus"), row.displayStatus.isEmpty() ? QStringLiteral("已停止") : row.displayStatus);
    map.insert(QStringLiteral("returns"), row.returns);
    map.insert(QStringLiteral("sharpeRatio"), row.sharpeRatio);
    map.insert(QStringLiteral("maxDrawdown"), row.maxDrawdown);
    map.insert(QStringLiteral("winRate"), row.winRate);
    map.insert(QStringLiteral("trades"), row.trades);
    map.insert(QStringLiteral("runningDays"), row.runningDays);
    map.insert(QStringLiteral("dailyPnL"), row.dailyPnL);
    map.insert(QStringLiteral("position"), row.position);
    return map;
}

void StrategyListModel::updateDisplayStatus(const QString& strategyId, const QString& status)
{
    const int idx = findRow(strategyId);
    if (idx < 0) return;
    m_rows[idx].displayStatus = status;
    const QModelIndex changed = index(idx);
    emit dataChanged(changed, changed, {DisplayStatusRole});
}

int StrategyListModel::findRow(const QString& strategyId) const
{
    for (int index = 0; index < m_rows.size(); ++index) {
        if (m_rows.at(index).strategyId == strategyId) {
            return index;
        }
    }
    return -1;
}
