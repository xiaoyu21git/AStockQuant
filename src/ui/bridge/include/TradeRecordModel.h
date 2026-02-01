// TradeRecordModel.h
#pragma once
#include <QAbstractListModel>
#include <QObject>

class TradeRecordModel : public QAbstractListModel {
    Q_OBJECT  // 这个宏会生成额外的元对象代码
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int buyTrades READ buyTrades NOTIFY statsChanged)
    Q_PROPERTY(int sellTrades READ sellTrades NOTIFY statsChanged)
    Q_PROPERTY(double netProfit READ netProfit NOTIFY statsChanged)
    Q_PROPERTY(double totalProfit READ totalProfit NOTIFY statsChanged)
    Q_PROPERTY(double totalLoss READ totalLoss NOTIFY statsChanged)
    Q_PROPERTY(double winRate READ winRate NOTIFY statsChanged)
    Q_PROPERTY(double currentPosition READ currentPosition NOTIFY statsChanged)
    
public:
    enum RoleNames {
        TimeRole = Qt::UserRole + 1,
        SymbolRole,
        PriceRole,
        VolumeRole,
        SideRole,
        ProfitRole
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
    // 带收益信息的重载，供 C++ 直接调用
    void addTrade(const QString& symbol, double price,
                  double volume, const QString& side, double profit);
    Q_INVOKABLE void clear();
    // 异步刷新（预留给将来从数据库/文件加载）
    Q_INVOKABLE void refresh();

    bool busy() const { return m_busy; }

    // 聚合统计只读访问器
    int buyTrades() const { return m_buyTrades; }
    int sellTrades() const { return m_sellTrades; }
    double netProfit() const { return m_totalProfit + m_totalLoss; }
    double totalProfit() const { return m_totalProfit; }
    double totalLoss() const { return m_totalLoss; }
    double winRate() const {
        int totalClosed = m_winningTrades + m_losingTrades;
        return totalClosed > 0 ? static_cast<double>(m_winningTrades) / totalClosed : 0.0;
    }
    double currentPosition() const { return m_currentPosition; }
    
signals:
    void countChanged();
    void busyChanged();
    void statsChanged();
    
private:
    struct TradeRecord {
        QString time;
        QString symbol;
        double price;
        double volume;
        QString side;
        double profit{0.0};
    };
    
    QVector<TradeRecord> m_trades;
    QHash<int, QByteArray> m_roleNames;
    bool m_busy{false};

    // 聚合统计字段
    int m_buyTrades{0};
    int m_sellTrades{0};
    int m_winningTrades{0};
    int m_losingTrades{0};
    double m_totalProfit{0.0};
    double m_totalLoss{0.0};
    double m_currentPosition{0.0};
};