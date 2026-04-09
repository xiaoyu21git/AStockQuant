#pragma once

#include <QObject>
#include <QMutex>
#include <QVariantMap>
#include <QStringList>

#include "foundation/Utils/Uuid.h"

namespace engine {
struct EventFormat;
}

class RiskMonitorService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)
    Q_PROPERTY(double currentDrawdownPercent READ currentDrawdownPercent NOTIFY currentDrawdownPercentChanged)
    Q_PROPERTY(double varUsagePercent READ varUsagePercent NOTIFY varUsagePercentChanged)
    Q_PROPERTY(double currentTotalExposurePercent READ currentTotalExposurePercent NOTIFY currentTotalExposurePercentChanged)
    Q_PROPERTY(double varBudgetAmount READ varBudgetAmount NOTIFY varUsagePercentChanged)
    Q_PROPERTY(double estimatedVarAmount READ estimatedVarAmount NOTIFY varUsagePercentChanged)

public:
    static RiskMonitorService* instance();

    RiskMonitorService(const RiskMonitorService&) = delete;
    RiskMonitorService& operator=(const RiskMonitorService&) = delete;

    Q_INVOKABLE void initialize();
    Q_INVOKABLE QVariantMap buildPortfolioSnapshot(const QVariantMap& strategy,
                                                  const QVariantMap& latestBacktest = QVariantMap());
    Q_INVOKABLE QVariantMap reviewTradeSignal(const QVariantMap& signalData,
                                             bool publishDecisionEvent = true);
    Q_INVOKABLE void resetStateForTesting();

    bool isInitialized() const;
    double currentDrawdownPercent() const;
    double varUsagePercent() const;
    double currentTotalExposurePercent() const;
    double varBudgetAmount() const;
    double estimatedVarAmount() const;

signals:
    void initializedChanged();
    void currentDrawdownPercentChanged();
    void varUsagePercentChanged();
    void currentTotalExposurePercentChanged();
    void riskDecisionPublished(const QVariantMap& decisionData);

private:
    explicit RiskMonitorService(QObject* parent = nullptr);
    void initializeEventBusIntegration();
    void handleStrategySignal(const engine::EventFormat& event);
    void handleTradingAccountUpdate(const engine::EventFormat& event);
    void publishRiskDecision(const QVariantMap& decision, const QString& eventType, const QString& correlationId);
    void syncObservedTotalAssetPeak(double totalAsset);
    void refreshLiveMetrics();
    void updateLiveMetricsFromAccountSnapshot(const QVariantMap& accountSnapshot);
    void resetBreakerStateIfNeeded(const QString& tradingDate);
    void evaluateBreakerActions(const QVariantMap& accountSnapshot, const QString& tradingDate);
    void dispatchBreakerOrders(int breakerStage, const QVariantList& positions, const QString& tradingDate);
    void submitBreakerOrder(const QVariantMap& request);

    static RiskMonitorService* m_instance;
    static QMutex m_instanceMutex;

    mutable QMutex m_mutex;
    bool m_initialized;
    bool m_eventBusIntegrated;
    double m_peakObservedTotalAsset;
    double m_currentDrawdownPercent;
    double m_varUsagePercent;
    double m_currentTotalExposurePercent;
    double m_varBudgetAmount;
    double m_estimatedVarAmount;
    QString m_breakerTradingDate;
    int m_lastBreakerAutoActionStage;
    bool m_level3TradingHaltActive;
    foundation::utils::Uuid m_strategySignalSubscription;
    foundation::utils::Uuid m_accountUpdateSubscription;
};