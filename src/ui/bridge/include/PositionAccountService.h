#pragma once

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QVariantList>
#include <QVariantMap>

#include "foundation/Utils/Uuid.h"

namespace engine {
struct EventFormat;
}

class PositionAccountService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)
    Q_PROPERTY(QVariantList positions READ positions NOTIFY positionsChanged)
    Q_PROPERTY(QVariantMap accountSnapshot READ accountSnapshot NOTIFY accountSnapshotChanged)
    Q_PROPERTY(QVariantList recentOrderStatuses READ recentOrderStatuses NOTIFY recentOrderStatusesChanged)

public:
    static PositionAccountService* instance();

    PositionAccountService(const PositionAccountService&) = delete;
    PositionAccountService& operator=(const PositionAccountService&) = delete;

    Q_INVOKABLE void initialize();
    Q_INVOKABLE void requestInitialSnapshot();
    Q_INVOKABLE bool isInitialized() const;
    Q_INVOKABLE bool initialSnapshotLoaded() const;
    Q_INVOKABLE QVariantList positions() const;
    Q_INVOKABLE QVariantMap accountSnapshot() const;
    Q_INVOKABLE QVariantList recentOrderStatuses() const;
    Q_INVOKABLE void resetStateForTesting();

signals:
    void initializedChanged();
    void positionsChanged();
    void accountSnapshotChanged();
    void recentOrderStatusesChanged();
    void positionUpdated(const QVariantMap& positionData);
    void accountUpdated(const QVariantMap& accountData);
    void errorOccurred(const QString& message);

private:
    explicit PositionAccountService(QObject* parent = nullptr);

    void initializeEventBusIntegration();
    void handleOrderStatus(const engine::EventFormat& event);
    void handleTradeFill(const engine::EventFormat& event);
    void handlePositionEvent(const engine::EventFormat& event);
    void handleAccountEvent(const engine::EventFormat& event);
    void publishPositionUpdate(const QVariantMap& positionData, const QString& correlationId);
    void publishAccountUpdate(const QVariantMap& accountData, const QString& correlationId);
    void appendOrderStatus(const QVariantMap& orderStatus);

    static PositionAccountService* m_instance;
    static QMutex m_instanceMutex;

    mutable QMutex m_mutex;
    bool m_initialized;
    bool m_eventBusIntegrated;
    bool m_initialSnapshotInFlight;
    bool m_initialSnapshotLoaded;
    foundation::utils::Uuid m_orderStatusSubscription;
    foundation::utils::Uuid m_tradeFillSubscription;
    foundation::utils::Uuid m_executionReportSubscription;
    foundation::utils::Uuid m_positionSubscription;
    foundation::utils::Uuid m_accountSubscription;
    QHash<QString, QVariantMap> m_positionsBySymbol;
    QVariantMap m_accountSnapshot;
    QVariantList m_recentOrderStatuses;
};