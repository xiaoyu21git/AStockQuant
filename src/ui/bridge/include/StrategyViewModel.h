// StrategyViewModel.h
// 策略视图模型 - 负责策略数据的视图展示，不包含业务逻辑
#pragma once

#include "StrategyLifecycleStatus.h"
#include "../../../domain/backtest/include/ResolvedStrategyBehavior.h"

#include <QAbstractListModel>
#include <QDateTime>
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
        SubTypeRole,
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
    Q_INVOKABLE QVariantList filterStrategiesByType(int strategyTypeIndex) const;
    Q_INVOKABLE QVariantList filterStrategiesByStatus(int statusIndex) const;
    
    // 单个策略操作
    Q_INVOKABLE void updateStrategy(const QString& strategyId, const QVariantMap& strategyData);
    Q_INVOKABLE void removeStrategy(const QString& strategyId);
    
    // 状态更新
    Q_INVOKABLE bool updateStrategyStatus(const QString& strategyId, int statusIndex);
    Q_INVOKABLE bool updateStrategyPerformance(const QString& strategyId, const QVariantMap& performance);
    
signals:
    void countChanged();
    void dataUpdated();
    
private:
    enum class StrategyAssetCategory {
        Unknown = 0,
        Stock,
        Future,
        Option,
        Fund,
        Index,
        MultiAsset,
    };

    enum class StrategyTimeFrameKind {
        Unknown = 0,
        Tick,
        Minute1,
        Minute5,
        Minute15,
        Minute30,
        Hour1,
        Daily,
        Weekly,
        Monthly,
        Custom,
    };

    enum class StrategyRiskLevelKind {
        Unknown = 0,
        Low,
        Medium,
        High,
        VeryHigh,
    };

    struct StrategyPerformanceSnapshot {
        double returns{0.0};
        double maxDrawdown{0.0};
        double sharpeRatio{0.0};
        double winRate{0.0};
        int runningDays{0};
        int tradesCount{0};
        double position{0.0};
        double dailyPnL{0.0};
    };

    // 策略数据结构
    struct StrategyViewData {
        QString strategyId;
        QString strategyName;
        domain::backtest::ResolvedStrategyIdentity strategyIdentity;
        QString description;
        strategy_view::StrategyLifecycleStatus status{strategy_view::StrategyLifecycleStatus::Unknown};
        StrategyPerformanceSnapshot performance;
        StrategyAssetCategory assetType{StrategyAssetCategory::Unknown};
        StrategyTimeFrameKind timeFrame{StrategyTimeFrameKind::Unknown};
        StrategyRiskLevelKind riskLevel{StrategyRiskLevelKind::Unknown};
        QStringList tags;
        QDateTime createdAt;
        QDateTime updatedAt;
        QString author;
        QString version;
        
        QVariantMap toVariantMap() const;
        static StrategyViewData fromVariantMap(const QVariantMap& map);
    };

    static StrategyAssetCategory assetCategoryFromIndex(const QVariant& value);
    static int assetCategoryIndex(StrategyAssetCategory assetType);
    static StrategyTimeFrameKind timeFrameFromIndex(const QVariant& value);
    static int timeFrameIndex(StrategyTimeFrameKind timeFrame);
    static StrategyRiskLevelKind riskLevelFromIndex(const QVariant& value);
    static int riskLevelIndex(StrategyRiskLevelKind riskLevel);
    static double parseNumericValue(const QVariant& value);
    static QDateTime parseDateTimeValue(const QVariant& value);
    
    // 数据查找
    int findIndexById(const QString& strategyId) const;
    
private:
    QVector<StrategyViewData> m_strategies;
};