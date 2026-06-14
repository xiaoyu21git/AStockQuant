// TradeExecutionService.cpp — 桥接层
// 自身持有 Q_PROPERTY，QML 直接绑定订阅

#include "TradeExecutionService.h"
#include "OrderSubmissionFacade.h"

#include "Event/EventBus.hpp"

#include <QMutexLocker>

namespace {

app::facade::OrderSubmissionFacade& getFacade() {
    static app::facade::OrderSubmissionFacade facade;
    return facade;
}

} // anonymous namespace

TradeExecutionService* TradeExecutionService::m_instance = nullptr;
QMutex TradeExecutionService::m_instanceMutex;

TradeExecutionService* TradeExecutionService::instance() {
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) m_instance = new TradeExecutionService();
    return m_instance;
}

TradeExecutionService::TradeExecutionService(QObject* parent) : QObject(parent) {}

void TradeExecutionService::initialize() {
    QMutexLocker locker(&m_mutex);
    if (m_initialized) return;
    m_initialized = true;
    locker.unlock();
    emit initializedChanged();
}

bool TradeExecutionService::isInitialized() const {
    QMutexLocker locker(&m_mutex);
    return m_initialized;
}

QString TradeExecutionService::lastErrorMessage() const {
    QMutexLocker locker(&m_mutex);
    return m_lastErrorMessage;
}

void TradeExecutionService::updateLastErrorMessage(const QString& msg) {
    QMutexLocker locker(&m_mutex);
    if (m_lastErrorMessage == msg) return;
    m_lastErrorMessage = msg;
    locker.unlock();
    emit lastErrorMessageChanged();
}

bool TradeExecutionService::submitBridgeOrder(const QVariantMap& request) {
    initialize();

    domain::trading::OrderSubmissionRequest req;
    req.symbol = request.value("symbol").toString().trimmed().toUpper().toStdString();
    req.side = domain::trading::orderSideFromString(request.value("side").toString().toStdString());
    req.price = request.value("price").toDouble();
    req.quantity = request.value("quantity").toLongLong();
    req.orderType = request.value("orderType").toString().toStdString();
    req.strategyId = request.value("strategyId").toString().toStdString();
    req.strategyName = request.value("strategyName").toString().toStdString();
    req.cashAmount = request.value("cashAmount").toDouble();
    req.signalStrength = request.value("signalStrength").toDouble();
    req.batchId = request.value("batchId").toString().toStdString();
    req.batchIndex = request.value("batchIndex").toInt();
    req.executionScopeId = request.value("executionScopeId").toString().toStdString();
    req.previousBatchId = request.value("previousBatchId").toString().toStdString();
    req.requiresPreviousBatchFilled = request.value("requiresPreviousBatchFilled").toBool();
    req.requiresManualCheckpoint = request.value("requiresManualCheckpoint").toBool();
    req.pauseOnAbnormalReject = request.value("pauseOnAbnormalReject").toBool();

    auto result = getFacade().submitOrder(req);

    if (!result.accepted) {
        updateLastErrorMessage(QString::fromStdString(result.message));
        return false;
    }

    updateLastErrorMessage(QString());
    return true;
}

bool TradeExecutionService::submitManualTestOrder(const QString& symbol, const QString& side,
                                                    double price, qint64 quantity,
                                                    const QString& orderType, const QString& strategyId,
                                                    const QString& strategyName) {
    QVariantMap req;
    req["symbol"] = symbol;
    req["side"] = side;
    req["price"] = price;
    req["quantity"] = quantity;
    req["orderType"] = orderType;
    req["strategyId"] = strategyId;
    req["strategyName"] = strategyName;
    return submitBridgeOrder(req);
}

bool TradeExecutionService::cancelManualTestOrder(const QString&) {
    return false;
}

bool TradeExecutionService::approveExecutionCheckpoint(const QString& scopeId, const QString& batchId) {
    getFacade().approveCheckpoint(scopeId.toStdString(), batchId.toStdString());
    return true;
}

bool TradeExecutionService::resumeExecutionPause(const QString& scopeId, const QString&) {
    getFacade().resumePause(scopeId.toStdString());
    return true;
}

QVariantList TradeExecutionService::recentOrders() const {
    QMutexLocker locker(&m_mutex);
    return m_recentOrders;
}

void TradeExecutionService::clearRecentOrders() {
    QMutexLocker locker(&m_mutex);
    m_recentOrders.clear();
    locker.unlock();
    emit recentOrdersChanged();
}

void TradeExecutionService::resetStateForTesting() {
    QMutexLocker locker(&m_mutex);
    m_initialized = false;
    m_lastErrorMessage.clear();
    m_recentOrders.clear();
    locker.unlock();
    emit initializedChanged();
}
