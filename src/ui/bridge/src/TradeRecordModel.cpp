// TradeRecordModel.cpp
#include "TradeRecordModel.h"
#include <QDateTime>
#include <QDebug>
#include <QPointer>

#include <QtConcurrent>

TradeRecordModel::TradeRecordModel(QObject* parent) 
    : QAbstractListModel(parent) {
    // 初始化角色名
    m_roleNames[TimeRole] = "time";
    m_roleNames[SymbolRole] = "symbol";
    m_roleNames[PriceRole] = "price";
    m_roleNames[VolumeRole] = "volume";
    m_roleNames[SideRole] = "side";
    m_roleNames[ProfitRole] = "profit";
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
    case ProfitRole:
        return trade.profit;
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
    // 不带收益信息的版本，默认 profit 为 0
    addTrade(symbol, price, volume, side, 0.0);
}

void TradeRecordModel::addTrade(const QString& symbol, double price,
                               double volume, const QString& side, double profit) {
    beginInsertRows(QModelIndex(), m_trades.size(), m_trades.size());
    
    TradeRecord record;
    record.time = QDateTime::currentDateTime().toString("hh:mm:ss");
    record.symbol = symbol;
    record.price = price;
    record.volume = volume;
    record.side = side;
    record.profit = profit;
    
    m_trades.append(record);
    endInsertRows();

    // 聚合统计更新：简单认为带有 profit 的记录为平仓记录
    if (side == QStringLiteral("买入")) {
        m_buyTrades++;
        m_currentPosition += volume;
    } else {
        m_sellTrades++;
        m_currentPosition -= volume;
        if (profit > 0.0) {
            m_winningTrades++;
            m_totalProfit += profit;
        } else if (profit < 0.0) {
            m_losingTrades++;
            m_totalLoss += profit;
        }
    }

    emit countChanged();
    emit statsChanged();
    qDebug() << "Trade added:" << symbol << price << volume << side << "profit:" << profit;
}

void TradeRecordModel::clear() {
    if (m_trades.isEmpty())
        return;
    
    beginRemoveRows(QModelIndex(), 0, m_trades.size() - 1);
    m_trades.clear();
    endRemoveRows();

    // 重置聚合统计
    m_buyTrades = 0;
    m_sellTrades = 0;
    m_winningTrades = 0;
    m_losingTrades = 0;
    m_totalProfit = 0.0;
    m_totalLoss = 0.0;
    m_currentPosition = 0.0;

    emit countChanged();
    emit statsChanged();
}

void TradeRecordModel::refresh() {
    if (m_busy) {
        qWarning() << "[TradeRecordModel] refresh already running, ignore";
        return;
    }

    m_busy = true;
    emit busyChanged();

    QPointer<TradeRecordModel> self(this);

    // 目前没有真实的数据源，这里只预留异步刷新框架，未来可以在此加载数据库/文件
    QtConcurrent::run([self]() {
        if (!self)
            return;

        // TODO: 在这里添加从数据库或文件加载交易记录的逻辑，填充 self->m_trades

        QMetaObject::invokeMethod(self, [self]() {
            if (!self)
                return;

            // 如果将来刷新逻辑修改了 m_trades，这里应当包裹 beginResetModel/endResetModel
            self->m_busy = false;
            emit self->busyChanged();
            emit self->countChanged();
        }, Qt::QueuedConnection);
    });
}

#include "moc_TradeRecordModel.cpp"  // 生成的元对象代码