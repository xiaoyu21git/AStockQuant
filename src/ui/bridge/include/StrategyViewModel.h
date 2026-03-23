// StrategyViewModel.h
// 策略视图模型 - 负责策略数据的视图展示，不包含业务逻辑
#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <QString>
#include <QVariant>
#include <QStringList>

class StrategyViewModel : public QAbstractListModel {
    Q_OBJECT
    
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    
public:
    // 角色枚举
    enum RoleNames {
        StrategyIdRole = Qt::UserRole + 1,
        StrategyNameRole,
        StrategyTypeRole,
        DescriptionRole,
        StatusRole,
        ReturnsRole,
        MaxDrawdownRole,
        SharpeRatioRole,
        WinRateRole,
        RunningDaysRole,
        TradesCountRole,
        PositionRole,
        DailyPnLRole,
        AssetTypeRole,
        TimeFrameRole,
        RiskLevelRole,
        TagsRole,
        CreatedAtRole,
        UpdatedAtRole,
        AuthorRole,
        VersionRole
    };
    Q_ENUM(RoleNames)
    
    explicit StrategyViewModel(QObject* parent = nullptr);
    ~StrategyViewModel();
    
    // QAbstractListModel接口
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    
    // 数据操作方法 - QML调用这些方法更新数据
    Q_INVOKABLE void updateData(const QVariantList& strategies);
    Q_INVOKABLE void clearData();
    Q_INVOKABLE void appendData(const QVariantMap& strategyData);
    Q_INVOKABLE void addDataBatch(const QVariantList& strategies);
    
    // 数据访问方法
    Q_INVOKABLE QVariantMap getRow(int index) const;
    Q_INVOKABLE QVariantMap getStrategyById(const QString& strategyId) const;
    Q_INVOKABLE QVariantList getAllStrategies() const;
    Q_INVOKABLE QVariantList searchStrategies(const QString& keyword) const;
    Q_INVOKABLE QVariantList filterStrategiesByType(const QString& strategyType) const;
    Q_INVOKABLE QVariantList filterStrategiesByStatus(const QString& status) const;
    
    // 单个策略操作
    Q_INVOKABLE void updateStrategy(const QString& strategyId, const QVariantMap& strategyData);
    Q_INVOKABLE void removeStrategy(const QString& strategyId);
    
    // 状态更新
    Q_INVOKABLE bool updateStrategyStatus(const QString& strategyId, const QString& status);
    Q_INVOKABLE bool updateStrategyPerformance(const QString& strategyId, const QVariantMap& performance);
    
signals:
    void countChanged();
    void dataUpdated();
    
private:
    // 策略数据结构
    struct StrategyViewData {
        QString strategyId;
        QString strategyName;
        QString strategyType;
        QString description;
        QString status;  // "running", "paused", "stopped", "DRAFT", "ACTIVE", etc.
        QString returns;
        QString maxDrawdown;
        QString sharpeRatio;
        QString winRate;
        int runningDays;
        int tradesCount;
        double position;
        double dailyPnL;
        QString assetType;
        QString timeFrame;
        QString riskLevel;
        QStringList tags;
        QString createdAt;
        QString updatedAt;
        QString author;
        QString version;
        
        QVariantMap toVariantMap() const;
        static StrategyViewData fromVariantMap(const QVariantMap& map);
    };
    
    // 数据查找
    int findIndexById(const QString& strategyId) const;
    
private:
    QVector<StrategyViewData> m_strategies;
};