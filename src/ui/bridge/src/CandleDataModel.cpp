// CandleDataModel.cpp
#include "CandleDataModel.h"

#include <algorithm>

namespace bridge {

CandleDataModel::CandleDataModel(QObject* parent) : QAbstractTableModel(parent) {}

int CandleDataModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_data.size());
}

int CandleDataModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant CandleDataModel::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid() || idx.row() >= static_cast<int>(m_data.size()))
        return QVariant();
    const auto& c = m_data[idx.row()];
    switch (role) {
        case TimestampRole: return c.timestamp;
        case OpenRole:      return c.open;
        case HighRole:      return c.high;
        case LowRole:       return c.low;
        case CloseRole:     return c.close;
        case VolumeRole:    return c.volume;
        case Qt::DisplayRole: {
            switch (idx.column()) {
                case TimestampCol: return c.timestamp;
                case OpenCol:      return c.open;
                case HighCol:      return c.high;
                case LowCol:       return c.low;
                case CloseCol:     return c.close;
                case VolumeCol:    return c.volume;
                default: return QVariant();
            }
        }
        default: return QVariant();
    }
}

QHash<int, QByteArray> CandleDataModel::roleNames() const {
    static const QHash<int, QByteArray> roles = {
        {TimestampRole, "timestamp"}, {OpenRole, "open"}, {HighRole, "high"},
        {LowRole, "low"}, {CloseRole, "close"}, {VolumeRole, "volume"}
    };
    return roles;
}

void CandleDataModel::appendCandle(qint64 ts, double o, double h, double l, double c, double v) {
    int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    m_data.push_back({ts, o, h, l, c, v});
    endInsertRows();
    emit countChanged();
    emit lastPriceChanged();
    emit lastVolumeChanged();
}

void CandleDataModel::updateLastCandle(double close, double high, double low, double volume) {
    if (m_data.empty()) return;
    auto& last = m_data.back();
    last.close = close;
    last.high  = std::max(last.high, high);
    last.low   = std::min(last.low, low);
    last.volume += volume;
    int r = static_cast<int>(m_data.size()) - 1;
    emit dataChanged(index(r, 0), index(r, ColumnCount - 1));
    emit lastPriceChanged();
    emit lastVolumeChanged();
}

void CandleDataModel::setCandles(const QVariantList& candles) {
    beginResetModel();
    m_data.clear();
    m_data.reserve(candles.size());
    for (const auto& item : candles) {
        auto m = item.toMap();
        m_data.push_back({
            static_cast<qint64>(m["timestamp"].toLongLong()),
            m["open"].toDouble(), m["high"].toDouble(), m["low"].toDouble(),
            m["close"].toDouble(), m["volume"].toDouble()
        });
    }
    endResetModel();
    emit countChanged();
    emit lastPriceChanged();
    emit lastVolumeChanged();
}

void CandleDataModel::clear() {
    beginResetModel();
    m_data.clear();
    endResetModel();
    emit countChanged();
}

double CandleDataModel::lastPrice()  const { return m_data.empty() ? 0.0 : m_data.back().close; }
double CandleDataModel::lastVolume() const { return m_data.empty() ? 0.0 : m_data.back().volume; }
void   CandleDataModel::setPreClose(double v) { m_preClose = v; emit preCloseChanged(); }

const CandleItem* CandleDataModel::candleAt(int row) const {
    return (row >= 0 && row < static_cast<int>(m_data.size())) ? &m_data[row] : nullptr;
}

} // namespace bridge
