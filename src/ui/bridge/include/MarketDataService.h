#pragma once

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QStringList>
#include <QVector>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>

#include "foundation/Utils/Uuid.h"

namespace engine {
struct EventFormat;
}

class MarketDataService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)
    Q_PROPERTY(QVariantList marketSnapshots READ marketSnapshots NOTIFY marketSnapshotsChanged)
    Q_PROPERTY(QString primarySymbol READ primarySymbol NOTIFY primarySymbolChanged)
    Q_PROPERTY(bool hasLiveData READ hasLiveData NOTIFY hasLiveDataChanged)
    Q_PROPERTY(int runtimeSubscriptionCount READ runtimeSubscriptionCount NOTIFY runtimeSubscriptionStatusChanged)
    Q_PROPERTY(int runtimeSubscriptionLimit READ runtimeSubscriptionLimit NOTIFY runtimeSubscriptionStatusChanged)

public:
    static MarketDataService* instance();

    MarketDataService(const MarketDataService&) = delete;
    MarketDataService& operator=(const MarketDataService&) = delete;

    Q_INVOKABLE void initialize();
    Q_INVOKABLE void initializeAsync();
    Q_INVOKABLE bool isInitialized() const;
    Q_INVOKABLE QVariantList marketSnapshots() const;
    Q_INVOKABLE QString primarySymbol() const;
    Q_INVOKABLE bool hasLiveData() const;
    Q_INVOKABLE int runtimeSubscriptionCount() const;
    Q_INVOKABLE int runtimeSubscriptionLimit() const;
    Q_INVOKABLE void activateDefaultWatchlist();
    Q_INVOKABLE void setWatchlist(const QStringList& symbols);
    Q_INVOKABLE void ensureWatchSymbol(const QString& symbol);
    Q_INVOKABLE QVariantMap resolveInstrument(const QString& query) const;

signals:
    void initializedChanged();
    void marketSnapshotsChanged();
    void primarySymbolChanged();
    void hasLiveDataChanged();
    void runtimeSubscriptionStatusChanged();
    void marketEventReceived(const QVariantMap& marketSnapshot);

private:
    explicit MarketDataService(QObject* parent = nullptr);

    void initializeEventBusIntegration();
    void queueSnapshotUpdate(const QVariantMap& snapshot, bool liveUpdate);
    void flushPendingSnapshots();
    void publishWatchRequest(const QString& symbol) const;
    void logWatchRequestThrottled(const QString& symbol, const QString& reason) const;
    QVariantMap mergeSnapshotLocked(const QVariantMap& snapshot,
                                    bool liveUpdate,
                                    bool* primaryChanged,
                                    bool* liveStateChanged);
    void upsertSnapshot(const QVariantMap& snapshot, bool liveUpdate);
    QString normalizeSymbol(const QString& symbol) const;
    void handleMarketEvent(const engine::EventFormat& event, const QString& eventType);
    QVariantMap buildSnapshotFromEvent(const engine::EventFormat& event, const QString& eventType) const;
    QVariantList orderedSnapshotsLocked() const;
    QString resolveDisplayName(const QString& symbol) const;

    static MarketDataService* m_instance;
    static QMutex m_instanceMutex;

    mutable QMutex m_mutex;
    bool m_initialized;
    bool m_eventBusIntegrated;
    bool m_hasLiveData;
    bool m_flushScheduled;
    bool m_pendingLiveUpdate;
    QString m_primarySymbol;
    QStringList m_watchlist;
    QHash<QString, QVariantMap> m_snapshotsBySymbol;
    mutable QMutex m_pendingMutex;
    QVector<QVariantMap> m_pendingSnapshots;
    foundation::utils::Uuid m_marketTickSubscription;
    foundation::utils::Uuid m_marketBarSubscription;
    foundation::utils::Uuid m_tradingMarketTickSubscription;
    foundation::utils::Uuid m_tradingMarketBarSubscription;
    foundation::utils::Uuid m_runtimeSubscriptionStatusSubscription;
    int m_runtimeSubscriptionCount;
    int m_runtimeSubscriptionLimit;
    mutable qint64 m_lastWatchRequestLogAtMs;
    mutable QString m_lastWatchRequestLogSymbol;
    mutable int m_suppressedWatchRequestLogs;
};