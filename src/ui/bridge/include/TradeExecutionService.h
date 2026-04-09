#pragma once

#include <map>
#include <QObject>
#include <QHash>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "foundation/Utils/Uuid.h"

namespace engine {
struct EventFormat;
}

#if defined(ASTOCK_ENABLE_JUJIN_MARKET)
namespace thirdparty {
class JujinApi;
}
#endif

class TradeExecutionService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)
    Q_PROPERTY(QString lastErrorMessage READ lastErrorMessage NOTIFY lastErrorMessageChanged)
    Q_PROPERTY(QVariantList recentOrders READ recentOrders NOTIFY recentOrdersChanged)
    Q_PROPERTY(QVariantList recentRuleHits READ recentRuleHits NOTIFY recentRuleHitsChanged)

public:
    static TradeExecutionService* instance();

    TradeExecutionService(const TradeExecutionService&) = delete;
    TradeExecutionService& operator=(const TradeExecutionService&) = delete;

    Q_INVOKABLE void initialize();
    Q_INVOKABLE bool isInitialized() const;
    Q_INVOKABLE QString lastErrorMessage() const;
    Q_INVOKABLE QVariantList recentOrders() const;
    Q_INVOKABLE QVariantList recentRuleHits() const;
    Q_INVOKABLE bool isLiveBridgeReady();
    Q_INVOKABLE QString liveBridgeStatusMessage();
    Q_INVOKABLE void clearRecentOrders();
    Q_INVOKABLE void resetStateForTesting();
    Q_INVOKABLE bool approveExecutionCheckpoint(const QString& executionScopeId,
                                               const QString& batchId);
    Q_INVOKABLE bool resumeExecutionPause(const QString& executionScopeId,
                                         const QString& pausedBatchId = QString());
    Q_INVOKABLE bool submitBridgeOrder(const QVariantMap& request);
    Q_INVOKABLE bool submitManualTestOrder(const QString& symbol,
                                           const QString& side,
                                           double price,
                                           qint64 quantity = 100,
                                           const QString& orderType = QStringLiteral("LIMIT"),
                                           const QString& strategyId = QStringLiteral("manual_test"),
                                           const QString& strategyName = QStringLiteral("Manual Test"));
    Q_INVOKABLE bool cancelManualTestOrder(const QString& orderId);

signals:
    void initializedChanged();
    void lastErrorMessageChanged();
    void recentOrdersChanged();
    void recentRuleHitsChanged();
    void orderRequestPublished(const QVariantMap& orderRequest);
    void orderStatusPublished(const QVariantMap& orderStatus);
    void tradeFillPublished(const QVariantMap& tradeFill);

private:
    explicit TradeExecutionService(QObject* parent = nullptr);

    void initializeEventBusIntegration();
    void handleRuntimeOrderUpdate(const engine::EventFormat& event);
    void handleRuntimeTradeFill(const engine::EventFormat& event);
    void handleRiskApproval(const engine::EventFormat& event);
    void handleRiskReject(const engine::EventFormat& event);
    bool submitBrokerOrder(const QString& strategyId,
                           const QString& strategyName,
                           const QString& gmStrategyId,
                           const QString& symbol,
                           const QString& side,
                           const QString& orderType,
                           double price,
                           qint64 quantity,
                           const QString& correlationId,
                       double strength = 0.0,
                       const QVariantMap& orderContext = QVariantMap{},
                       const std::map<std::string, std::string>& runtimeMetadata = {});
    bool submitLocalPendingOrder(const QString& strategyId,
                                 const QString& strategyName,
                                 const QString& gmStrategyId,
                                 const QString& symbol,
                                 const QString& side,
                                 const QString& orderType,
                                 double price,
                                 qint64 quantity,
                                 const QString& correlationId,
                           const QString& message,
                           const QVariantMap& orderContext = QVariantMap{});
    bool submitSimulatedOrder(const QString& strategyId,
                              const QString& strategyName,
                                        const QString& gmStrategyId,
                              const QString& symbol,
                              const QString& side,
                              double price,
                              qint64 quantity,
                              const QString& correlationId,
                              double strength = 0.0);
    void publishOrderRequest(const QVariantMap& orderRequest, const QString& correlationId);
    void publishOrderStatus(const QVariantMap& orderStatus, const QString& correlationId);
    void publishTradeFill(const QVariantMap& tradeFill, const QString& correlationId);
    QVariantMap findExecutionPauseBlock(const QVariantMap& orderRequest) const;
    QVariantMap findManualCheckpointBlock(const QVariantMap& orderRequest) const;
    QVariantMap findPartialFillAdvanceBlock(const QVariantMap& orderRequest) const;
    QVariantMap findPendingOrderConflict(const QString& symbol, const QString& side) const;
    bool updateExecutionPauseLocked(const QVariantMap& orderRecord);
    void appendRecentOrder(const QVariantMap& orderRecord);
    void appendRecentRuleHit(const QVariantMap& orderRecord);
    void updateLastErrorMessage(const QString& message);

    static TradeExecutionService* m_instance;
    static QMutex m_instanceMutex;

    mutable QMutex m_mutex;
    bool m_initialized;
    bool m_eventBusIntegrated;
    QString m_lastErrorMessage;
    foundation::utils::Uuid m_orderUpdateSubscription;
    foundation::utils::Uuid m_tradeFillSubscription;
    foundation::utils::Uuid m_riskApprovalSubscription;
    foundation::utils::Uuid m_riskRejectSubscription;
    QVariantList m_recentOrders;
    QVariantList m_recentRuleHits;
    QHash<QString, QVariantMap> m_pausedExecutionScopes;
    QSet<QString> m_approvedExecutionCheckpoints;

#if defined(ASTOCK_ENABLE_JUJIN_MARKET)
    bool evaluateBrokerReadiness(QString* errorMessage = nullptr, bool bindBrokerApi = false);
    bool ensureBrokerApiReady(QString* errorMessage = nullptr);

    thirdparty::JujinApi* m_brokerApi = nullptr;
#endif
};



