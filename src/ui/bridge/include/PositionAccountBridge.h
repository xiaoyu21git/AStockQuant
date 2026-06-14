#pragma once
// ═══════════════════════════════════════════════════════════════
// PositionAccountBridge — thin QML bridge (zero EventBus)
// Responsibilities: QVariantMap ↔ domain types only
// EventBus wiring is in AppBootstrap — this layer never touches it
// ═══════════════════════════════════════════════════════════════

#include <QObject>
#include <QMutex>
#include <QVariantList>
#include <QVariantMap>

namespace domain::trading {
class PositionAccountEngine;
}

class PositionAccountBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)
    Q_PROPERTY(QVariantList positions READ positions NOTIFY positionsChanged)
    Q_PROPERTY(QVariantMap accountSnapshot READ accountSnapshot NOTIFY accountSnapshotChanged)

public:
    static PositionAccountBridge* instance();

    PositionAccountBridge(const PositionAccountBridge&) = delete;
    PositionAccountBridge& operator=(const PositionAccountBridge&) = delete;

    void setEngine(domain::trading::PositionAccountEngine* engine);

    Q_INVOKABLE void initialize();
    Q_INVOKABLE bool isInitialized() const;
    Q_INVOKABLE QVariantList positions() const;
    Q_INVOKABLE QVariantMap accountSnapshot() const;
    Q_INVOKABLE void resetStateForTesting();

signals:
    void initializedChanged();
    void positionsChanged();
    void accountSnapshotChanged();
    void positionUpdated(const QVariantMap& positionData);
    void accountUpdated(const QVariantMap& accountData);
    void errorOccurred(const QString& message);

private:
    explicit PositionAccountBridge(QObject* parent = nullptr);

    static PositionAccountBridge* s_instance;
    static QMutex s_mutex;

    mutable QMutex m_mutex;
    bool m_initialized{false};
    domain::trading::PositionAccountEngine* m_engine{nullptr};
};