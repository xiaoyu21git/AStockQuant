#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <QString>

// 读取 data/live_account_snapshot.json 中的 positions 数组，展示当前持仓明细
class LivePositionModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum RoleNames {
        SymbolRole = Qt::UserRole + 1,
        QuantityRole,
        PriceRole,
        MarketValueRole,
        DirectionRole
    };
    Q_ENUM(RoleNames)

    explicit LivePositionModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh(); // 重新从快照文件加载最新持仓

private:
    struct PositionRecord {
        QString symbol;
        double  quantity{0.0};
        double  price{0.0};
        double  marketValue{0.0};
        QString direction;
    };

    QVector<PositionRecord> m_records;
    QHash<int, QByteArray> m_roleNames;

    void loadFromFile();
};
