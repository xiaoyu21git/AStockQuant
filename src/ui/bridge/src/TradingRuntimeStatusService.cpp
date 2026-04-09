#include "TradingRuntimeStatusService.h"

#include "TradingRuntimeManager.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>

#include <algorithm>

namespace {

QString sessionStateCode(thirdparty::TradingSessionState state)
{
    switch (state) {
    case thirdparty::TradingSessionState::Created:
        return QStringLiteral("CREATED");
    case thirdparty::TradingSessionState::Initialized:
        return QStringLiteral("INITIALIZED");
    case thirdparty::TradingSessionState::Starting:
        return QStringLiteral("STARTING");
    case thirdparty::TradingSessionState::Running:
        return QStringLiteral("RUNNING");
    case thirdparty::TradingSessionState::Stopping:
        return QStringLiteral("STOPPING");
    case thirdparty::TradingSessionState::Stopped:
        return QStringLiteral("STOPPED");
    case thirdparty::TradingSessionState::Error:
        return QStringLiteral("ERROR");
    }

    return QStringLiteral("UNKNOWN");
}

QString sessionStateLabel(thirdparty::TradingSessionState state)
{
    switch (state) {
    case thirdparty::TradingSessionState::Created:
        return QStringLiteral("已创建");
    case thirdparty::TradingSessionState::Initialized:
        return QStringLiteral("已初始化");
    case thirdparty::TradingSessionState::Starting:
        return QStringLiteral("启动中");
    case thirdparty::TradingSessionState::Running:
        return QStringLiteral("运行中");
    case thirdparty::TradingSessionState::Stopping:
        return QStringLiteral("停止中");
    case thirdparty::TradingSessionState::Stopped:
        return QStringLiteral("已停止");
    case thirdparty::TradingSessionState::Error:
        return QStringLiteral("异常");
    }

    return QStringLiteral("未知");
}

QString runtimeStrategyIdFromSessionId(const QString& sessionId)
{
    const int separatorIndex = sessionId.indexOf(QChar(':'));
    if (separatorIndex < 0 || separatorIndex + 1 >= sessionId.size()) {
        return {};
    }
    return sessionId.mid(separatorIndex + 1).trimmed();
}

QVariantMap snapshotToVariantMap(const thirdparty::TradingSessionSnapshot& snapshot)
{
    QVariantMap result;
    const QString sessionId = QString::fromStdString(snapshot.session_id).trimmed();
    result.insert(QStringLiteral("sessionId"), sessionId);
    result.insert(QStringLiteral("accountId"), QString::fromStdString(snapshot.account_id).trimmed());
    result.insert(QStringLiteral("strategyId"), QString::fromStdString(snapshot.strategy_id).trimmed());
    result.insert(QStringLiteral("runtimeStrategyId"), runtimeStrategyIdFromSessionId(sessionId));
    result.insert(QStringLiteral("state"), sessionStateCode(snapshot.state));
    result.insert(QStringLiteral("stateLabel"), sessionStateLabel(snapshot.state));
    result.insert(QStringLiteral("initialized"), snapshot.initialized);
    result.insert(QStringLiteral("connected"), snapshot.connected);
    result.insert(QStringLiteral("isRunning"), snapshot.state == thirdparty::TradingSessionState::Running);
    const QString lastError = QString::fromStdString(snapshot.last_error).trimmed();
    result.insert(QStringLiteral("lastError"), lastError);
    result.insert(QStringLiteral("hasError"), !lastError.isEmpty() || snapshot.state == thirdparty::TradingSessionState::Error);

    QStringList subscriptions;
    subscriptions.reserve(static_cast<int>(snapshot.subscriptions.size()));
    for (const std::string& subscription : snapshot.subscriptions) {
        subscriptions.append(QString::fromStdString(subscription));
    }
    result.insert(QStringLiteral("subscriptions"), subscriptions);
    return result;
}

QVariantList loadSessionSnapshots()
{
    std::vector<thirdparty::TradingSessionSnapshot> snapshots = thirdparty::TradingRuntimeManager::instance().session_snapshots();
    std::sort(snapshots.begin(), snapshots.end(), [](const auto& left, const auto& right) {
        if (left.account_id == right.account_id) {
            return left.session_id < right.session_id;
        }
        return left.account_id < right.account_id;
    });

    QVariantList result;
    result.reserve(static_cast<int>(snapshots.size()));
    for (const auto& snapshot : snapshots) {
        result.append(snapshotToVariantMap(snapshot));
    }
    return result;
}

} // namespace

TradingRuntimeStatusService* TradingRuntimeStatusService::m_instance = nullptr;
QMutex TradingRuntimeStatusService::m_instanceMutex;

TradingRuntimeStatusService* TradingRuntimeStatusService::instance()
{
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        QCoreApplication* app = QCoreApplication::instance();
        if (app && QThread::currentThread() != app->thread()) {
            QMetaObject::invokeMethod(app, [app]() {
                if (!m_instance) {
                    m_instance = new TradingRuntimeStatusService(app);
                }
            }, Qt::BlockingQueuedConnection);
        } else {
            m_instance = new TradingRuntimeStatusService(app);
        }
    }
    return m_instance;
}

TradingRuntimeStatusService::TradingRuntimeStatusService(QObject* parent)
    : QObject(parent)
    , m_initialized(false)
    , m_refreshTimer(new QTimer(this))
{
    m_refreshTimer->setInterval(1500);
    connect(m_refreshTimer, &QTimer::timeout, this, &TradingRuntimeStatusService::refresh);
}

void TradingRuntimeStatusService::initialize()
{
    bool needsEmit = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_initialized) {
            m_initialized = true;
            needsEmit = true;
        }
    }

    refresh();
    if (!m_refreshTimer->isActive()) {
        m_refreshTimer->start();
    }

    if (needsEmit) {
        emit initializedChanged();
    }
}

bool TradingRuntimeStatusService::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized;
}

QVariantList TradingRuntimeStatusService::sessionSnapshots() const
{
    QMutexLocker locker(&m_mutex);
    return m_sessionSnapshots;
}

QVariantMap TradingRuntimeStatusService::sessionSnapshotForStrategy(const QString& strategyId) const
{
    const QString normalized = strategyId.trimmed();
    if (normalized.isEmpty()) {
        return {};
    }

    const QVariantList snapshots = sessionSnapshots();
    for (const QVariant& snapshotValue : snapshots) {
        const QVariantMap snapshot = snapshotValue.toMap();
        if (snapshot.value(QStringLiteral("strategyId")).toString().trimmed() == normalized
            || snapshot.value(QStringLiteral("runtimeStrategyId")).toString().trimmed() == normalized) {
            return snapshot;
        }
    }
    return {};
}

QVariantMap TradingRuntimeStatusService::sessionSnapshotForAccount(const QString& accountId) const
{
    const QString normalized = accountId.trimmed();
    if (normalized.isEmpty()) {
        return {};
    }

    const QVariantList snapshots = sessionSnapshots();
    for (const QVariant& snapshotValue : snapshots) {
        const QVariantMap snapshot = snapshotValue.toMap();
        if (snapshot.value(QStringLiteral("accountId")).toString().trimmed() == normalized) {
            return snapshot;
        }
    }
    return {};
}

void TradingRuntimeStatusService::refresh()
{
    const QVariantList snapshots = loadSessionSnapshots();

    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_sessionSnapshots != snapshots) {
            m_sessionSnapshots = snapshots;
            changed = true;
        }
    }

    if (changed) {
        emit sessionSnapshotsChanged();
    }
}