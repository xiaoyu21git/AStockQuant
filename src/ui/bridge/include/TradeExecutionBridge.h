#pragma once
// ═══════════════════════════════════════════════════════════════════
// TradeExecutionBridge — thin QML bridge (pure conversion layer)
//
// Responsibilities (ONLY):
//   1. QVariantMap → domain::trading::TradeOrder conversion
//   2. Call domain::trading::TradeExecutionEngine methods
//   3. Convert domain results → QVariantMap for QML signals
//
// NOT responsible for:
//   - Order validation, scheduling, risk, gateway — in domain engine
//   - #if ASTOCK_ENABLE_JUJIN — in BrokerGatewayFactory
//   - QVariantMap string matching — all replaced by strong types
// ═══════════════════════════════════════════════════════════════════

#include <QObject>
#include <QMutex>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

namespace domain::trading {
class TradeExecutionEngine;
class TradeOrder;
}

class TradeExecutionBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)
    Q_PROPERTY(QString lastErrorMessage READ lastErrorMessage NOTIFY lastErrorMessageChanged)
    Q_PROPERTY(QVariantList recentOrders READ recentOrders NOTIFY recentOrdersChanged)

public:
    static TradeExecutionBridge* instance();

    TradeExecutionBridge(const TradeExecutionBridge&) = delete;
    TradeExecutionBridge& operator=(const TradeExecutionBridge&) = delete;

    void setEngine(domain::trading::TradeExecutionEngine* engine);

    Q_INVOKABLE void initialize();
    Q_INVOKABLE bool isInitialized() const;
    Q_INVOKABLE QString lastErrorMessage() const;

    Q_INVOKABLE bool submitBridgeOrder(const QVariantMap& request);
    Q_INVOKABLE bool submitManualTestOrder(const QString& symbol,
                                            const QString& side,
                                            double price,
                                            qint64 quantity = 100,
                                            const QString& orderType = QStringLiteral("LIMIT"),
                                            const QString& strategyId = QStringLiteral("manual_test"),
                                            const QString& strategyName = QStringLiteral("Manual Test"));
    Q_INVOKABLE bool cancelManualTestOrder(const QString& orderId);
    Q_INVOKABLE bool approveExecutionCheckpoint(const QString& executionScopeId,
                                                const QString& batchId);
    Q_INVOKABLE bool resumeExecutionPause(const QString& executionScopeId,
                                          const QString& pausedBatchId = QString());

    Q_INVOKABLE QVariantList recentOrders() const;
    Q_INVOKABLE void clearRecentOrders();

signals:
    void initializedChanged();
    void lastErrorMessageChanged();
    void recentOrdersChanged();
    void orderRequestPublished(const QVariantMap& orderRequest);
    void orderStatusPublished(const QVariantMap& orderStatus);
    void tradeFillPublished(const QVariantMap& tradeFill);

private:
    explicit TradeExecutionBridge(QObject* parent = nullptr);

    void updateLastErrorMessage(const QString& message);

    // ── Type conversions (static, pure) ──
    static domain::trading::TradeOrder qvariantToTradeOrder(const QVariantMap& m);
    static QVariantMap tradeOrderToQvariant(const domain::trading::TradeOrder& o);

    static TradeExecutionBridge* s_instance;
    static QMutex s_mutex;

    mutable QMutex m_mutex;
    bool m_initialized{false};
    QString m_lastErrorMessage;
    QVariantList m_recentOrders;
    domain::trading::TradeExecutionEngine* m_engine{nullptr};
};