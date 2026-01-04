// TradeRecordModel.cpp
#include "TradeRecordModel.h"
#include <QDateTime>
#include <QDebug>

TradeRecordModel::TradeRecordModel(QObject* parent) 
    : QAbstractListModel(parent) {
    // 初始化角色名
    m_roleNames[TimeRole] = "time";
    m_roleNames[SymbolRole] = "symbol";
    m_roleNames[PriceRole] = "price";
    m_roleNames[VolumeRole] = "volume";
    m_roleNames[SideRole] = "side";
}

int TradeRecordModel::rowCount(const QModelIndex& parent) const {
    Q_UNUSED(parent)
    return m_trades.size();
}

QVariant TradeRecordModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_trades.size())
        return QVariant();
    
    const auto& trade = m_trades.at(index.row());
    
    switch (role) {
    case TimeRole:
        return trade.time;
    case SymbolRole:
        return trade.symbol;
    case PriceRole:
        return trade.price;
    case VolumeRole:
        return trade.volume;
    case SideRole:
        return trade.side;
    case Qt::DisplayRole:
        return QString("%1 %2 @ %3").arg(trade.symbol).arg(trade.volume).arg(trade.price);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> TradeRecordModel::roleNames() const {
    return m_roleNames;
}

void TradeRecordModel::addTrade(const QString& symbol, double price, 
                               double volume, const QString& side) {
    beginInsertRows(QModelIndex(), m_trades.size(), m_trades.size());
    
    TradeRecord record;
    record.time = QDateTime::currentDateTime().toString("hh:mm:ss");
    record.symbol = symbol;
    record.price = price;
    record.volume = volume;
    record.side = side;
    
    m_trades.append(record);
    endInsertRows();
    
    emit countChanged();
    qDebug() << "Trade added:" << symbol << price << volume << side;
}

void TradeRecordModel::clear() {
    if (m_trades.isEmpty())
        return;
    
    beginRemoveRows(QModelIndex(), 0, m_trades.size() - 1);
    m_trades.clear();
    endRemoveRows();
    
    emit countChanged();
}

#include "moc_TradeRecordModel.cpp"  // 生成的元对象代码