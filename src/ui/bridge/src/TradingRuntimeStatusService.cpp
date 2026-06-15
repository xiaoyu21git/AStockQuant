#include "TradingRuntimeStatusService.h"
#include "../../../app/system/TradingSystem.h"

#include <QDebug>
#include <QTimer>

namespace bridge {

TradingRuntimeStatusService::TradingRuntimeStatusService(QObject* parent)
    : QObject(parent) {}

QVariantMap TradingRuntimeStatusService::sessionSnapshotForStrategy(const QString& strategyId) {
    // 委托给 TradingSystem 获取策略相关的运行时会话状态
    QVariantMap snapshot = buildTradingSystemSnapshot();
    snapshot["strategyId"] = strategyId;
    snapshot["sessionId"] = QStringLiteral("strategy_") + strategyId;

    // 检查交易系统是否已初始化
    const auto& sys = app::system::TradingSystem::instance();
    if (sys.initialized()) {
        snapshot["initialized"] = true;
        snapshot["connected"] = true;
        snapshot["state"] = "Running";
        snapshot["stateLabel"] = QStringLiteral("运行中");
    }

    return snapshot;
}

QVariantMap TradingRuntimeStatusService::sessionSnapshotForAccount(const QString& accountId) {
    QVariantMap snapshot = buildTradingSystemSnapshot();
    snapshot["accountId"] = accountId;
    snapshot["sessionId"] = QStringLiteral("account_") + accountId;

    const auto& sys = app::system::TradingSystem::instance();
    if (sys.initialized()) {
        snapshot["initialized"] = true;
        snapshot["connected"] = true;
        snapshot["state"] = "Running";
        snapshot["stateLabel"] = QStringLiteral("运行中");
    }

    return snapshot;
}

void TradingRuntimeStatusService::refresh() {
    m_sessionSnapshots.clear();

    const auto& sys = app::system::TradingSystem::instance();
    if (sys.initialized()) {
        QVariantMap sysSnapshot = buildTradingSystemSnapshot();
        sysSnapshot["sessionId"] = "trading_system";
        sysSnapshot["accountId"] = QString::fromStdString(
            sys.accountSnapshot().accountId());
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

    const auto& sys = app::system::TradingSystem::instance();
    if (sys.initialized()) {
        snap["state"] = "Running";
        snap["stateLabel"] = QStringLiteral("运行中");
        snap["connected"] = true;
        snap["initialized"] = true;
    }

    return snap;
}

} // namespace bridge
