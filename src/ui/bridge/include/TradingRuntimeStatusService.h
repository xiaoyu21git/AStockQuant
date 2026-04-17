#pragma once

#include <QObject>
#include <QMutex>
#include <QVariantList>
#include <QVariantMap>

class QTimer;

class TradingRuntimeStatusService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)
    Q_PROPERTY(QVariantList sessionSnapshots READ sessionSnapshots NOTIFY sessionSnapshotsChanged)

public:
    static TradingRuntimeStatusService* instance();

    TradingRuntimeStatusService(const TradingRuntimeStatusService&) = delete;
    TradingRuntimeStatusService& operator=(const TradingRuntimeStatusService&) = delete;

    Q_INVOKABLE void initialize();
    Q_INVOKABLE void initializeAsync();
    Q_INVOKABLE bool isInitialized() const;
    Q_INVOKABLE QVariantList sessionSnapshots() const;
    Q_INVOKABLE QVariantMap sessionSnapshotForStrategy(const QString& strategyId) const;
    Q_INVOKABLE QVariantMap sessionSnapshotForAccount(const QString& accountId) const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshAsync();

signals:
    void initializedChanged();
    void sessionSnapshotsChanged();

private:
    explicit TradingRuntimeStatusService(QObject* parent = nullptr);

    static TradingRuntimeStatusService* m_instance;
    static QMutex m_instanceMutex;

    mutable QMutex m_mutex;
    bool m_initialized;
    QVariantList m_sessionSnapshots;
    QTimer* m_refreshTimer;
};