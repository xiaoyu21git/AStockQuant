#include "TradingRuntimeStatusService.h"
#include "../../../engine/include/GmSessionEngine.h"
#include "../../../domain/strategy/include/StrategyManager.h"

#include <QDebug>
#include <QTimer>

namespace bridge {

TradingRuntimeStatusService::TradingRuntimeStatusService(QObject* parent)
    : QObject(parent) {}

QVariantMap TradingRuntimeStatusService::sessionSnapshotForStrategy(const QString& strategyId) {
    QVariantMap snapshot = buildTradingSystemSnapshot();
    snapshot["strategyId"] = strategyId;
    snapshot["sessionId"] = QStringLiteral("strategy_") + strategyId;

    if (engine::GmSessionEngine::instance().initialized()) {
        snapshot["initialized"] = true;
        snapshot["connected"] = true;
        auto* e = domain::strategy::StrategyManager::instance().get(strategyId.toStdString());
        snapshot["state"] = (e && e->isLiveLoopRunning()) ? "Running" : "Ready";
        snapshot["stateLabel"] = (e && e->isLiveLoopRunning()) ? QStringLiteral("运行中") : QStringLiteral("已创建");
    }
    return snapshot;
}

QVariantMap TradingRuntimeStatusService::sessionSnapshotForAccount(const QString& accountId) {
    QVariantMap snapshot = buildTradingSystemSnapshot();
    snapshot["accountId"] = accountId;
    snapshot["sessionId"] = QStringLiteral("account_") + accountId;

    if (engine::GmSessionEngine::instance().initialized()) {
        snapshot["initialized"] = true;
        snapshot["connected"] = true;
        snapshot["state"] = "Ready";
        snapshot["stateLabel"] = QStringLiteral("已创建");
    }
    return snapshot;
}

void TradingRuntimeStatusService::refresh() {
    m_sessionSnapshots.clear();
    if (engine::GmSessionEngine::instance().initialized()) {
        QVariantMap sysSnapshot = buildTradingSystemSnapshot();
        sysSnapshot["sessionId"] = "trading_system";
        m_sessionSnapshots.append(sysSnapshot);
    }
    emit sessionSnapshotsChanged();
}

void TradingRuntimeStatusService::refreshAsync() {
    QTimer::singleShot(0, this, [this]() { refresh(); });
}

QVariantList TradingRuntimeStatusService::sessionSnapshots() const {
    return m_sessionSnapshots;
}

QVariantMap TradingRuntimeStatusService::buildDefaultSnapshot(const QString& sessionId) const {
    QVariantMap snap;
    snap["sessionId"] = sessionId;
    snap["state"] = "Created";
    snap["stateLabel"] = QStringLiteral("已创建");
    snap["connected"] = false;
    snap["initialized"] = false;
    snap["lastError"] = QString();
    snap["subscriptions"] = QVariantList();
    snap["hasError"] = false;
    return snap;
}

QVariantMap TradingRuntimeStatusService::buildTradingSystemSnapshot() const {
    QVariantMap snap;
    snap["state"] = "Created";
    snap["stateLabel"] = QStringLiteral("已创建");
    snap["connected"] = false;
    snap["initialized"] = false;
    snap["lastError"] = QString();
    snap["subscriptions"] = QVariantList();
    snap["hasError"] = false;

    if (engine::GmSessionEngine::instance().initialized()) {
        snap["state"] = "Ready";
        snap["stateLabel"] = QStringLiteral("已创建");
        snap["connected"] = true;
        snap["initialized"] = true;
    }
    return snap;
}

TradingRuntimeStatusService* TradingRuntimeStatusService::instance() {
    static TradingRuntimeStatusService s;
    return &s;
}

} // namespace bridge
