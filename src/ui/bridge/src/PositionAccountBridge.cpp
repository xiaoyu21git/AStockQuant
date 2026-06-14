#include "../include/PositionAccountBridge.h"
#include "domain/trading/PositionAccountEngine.h"

#include <QMutexLocker>

PositionAccountBridge* PositionAccountBridge::s_instance = nullptr;
QMutex PositionAccountBridge::s_mutex;

PositionAccountBridge* PositionAccountBridge::instance() {
    QMutexLocker locker(&s_mutex);
    if (!s_instance) s_instance = new PositionAccountBridge();
    return s_instance;
}

PositionAccountBridge::PositionAccountBridge(QObject* p) : QObject(p) {}

void PositionAccountBridge::setEngine(domain::trading::PositionAccountEngine* e) { m_engine = e; }

void PositionAccountBridge::initialize() {
    QMutexLocker l(&m_mutex);
    if (m_initialized) return;
    m_initialized = true;
    emit initializedChanged();
}

bool PositionAccountBridge::isInitialized() const {
    QMutexLocker l(&m_mutex);
    return m_initialized;
}

QVariantList PositionAccountBridge::positions() const {
    QVariantList r;
    if (!m_engine) return r;
    QMutexLocker l(&m_mutex);
    for (auto& [k, p] : m_engine->positions()) {
        QVariantMap m;
        m["symbol"] = QString::fromStdString(p.symbol());
        m["positionSide"] = (p.side() == domain::trading::PositionSide::Long)
            ? QStringLiteral("LONG") : QStringLiteral("SHORT");
        m["quantity"] = static_cast<qint64>(p.quantity());
        m["availableQuantity"] = static_cast<qint64>(p.availableQuantity());
        m["closeableQuantity"] = static_cast<qint64>(p.closeableQuantity());
        m["costBasis"] = p.costBasis(); m["lastPrice"] = p.lastPrice();
        m["marketValue"] = p.marketValue(); m["unrealizedPnl"] = p.unrealizedPnl();
        r.append(m);
    }
    return r;
}

QVariantMap PositionAccountBridge::accountSnapshot() const {
    if (!m_engine) return {};
    QMutexLocker l(&m_mutex);
    auto& a = m_engine->account();
    QVariantMap m;
    m["accountId"] = QString::fromStdString(a.accountId());
    m["availableCash"] = a.availableCash(); m["marketValue"] = a.marketValue();
    m["realizedPnl"] = a.realizedPnl(); m["unrealizedPnl"] = a.unrealizedPnl();
    m["totalAsset"] = a.totalAsset(); m["dailyTurnoverNotional"] = a.dailyTurnoverNotional();
    return m;
}

void PositionAccountBridge::resetStateForTesting() {
    if (m_engine) m_engine->reset();
    emit positionsChanged(); emit accountSnapshotChanged();
}