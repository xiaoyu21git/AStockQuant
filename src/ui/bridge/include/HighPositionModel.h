// HighPositionModel.h
#pragma once

#include <QAbstractListModel>
#include <QObject>

namespace astock { namespace database {
class ConnectionPool;
class MarketDataRepository;
} }

class HighPositionModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    enum RoleNames {
        SymbolRole = Qt::UserRole + 1,
        NameRole,
        LastPriceRole,
        PreviousChangeRole,
        RiseFromLowRole,
        DrawdownFromHighRole,
        ExtremeGapRole,      // 距离严重异动阈值的涨幅差
        NetMainInflow5dRole,
        RecentOnLhbRole
    };
    Q_ENUM(RoleNames)

    explicit HighPositionModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 从数据库刷新高位股列表，lookbackDays 默认 60 天
    Q_INVOKABLE void refresh(int lookbackDays = 60);

    bool busy() const { return m_busy; }

signals:
    void countChanged();
    void busyChanged();

private:
    struct HighRow {
        QString symbol;
        QString name;
        double lastPrice{0.0};
        double previousChange{0.0};
        double riseFromLow{0.0};
        double drawdownFromHigh{0.0};
        double extremeGap{0.0};    // 离严重异动阈值还差多少涨幅（%）
        double netMainInflow5d{0.0}; // 近5日主力净流入总额
        bool   recentOnLhb{false};   // 近期是否上过龙虎榜
    };

    QVector<HighRow> m_rows;
    QHash<int, QByteArray> m_roleNames;

    // 复用数据库连接池和仓储，避免每次刷新都重新初始化 MySQL
    std::shared_ptr<astock::database::ConnectionPool> m_pool;
    std::shared_ptr<astock::database::MarketDataRepository> m_repo;
    bool m_busy{false};
};
