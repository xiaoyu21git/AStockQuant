// TradeRecordModel.h
#pragma once
#include <QAbstractListModel>
#include <QObject>

class TradeRecordModel : public QAbstractListModel {
    Q_OBJECT  // 这个宏会生成额外的元对象代码
    
public:
    enum RoleNames {
        TimeRole = Qt::UserRole + 1,
        SymbolRole,
        PriceRole,
        VolumeRole,
        SideRole
    };
    Q_ENUM(RoleNames)
    
    explicit TradeRecordModel(QObject* parent = nullptr);
    
    // QAbstractListModel 接口
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    
    // 添加数据的方法
    Q_INVOKABLE void addTrade(const QString& symbol, double price, 
                              double volume, const QString& side);
    Q_INVOKABLE void clear();
    
signals:
    void countChanged();
    
private:
    struct TradeRecord {
        QString time;
        QString symbol;
        double price;
        double volume;
        QString side;
    };
    
    QVector<TradeRecord> m_trades;
    QHash<int, QByteArray> m_roleNames;
};