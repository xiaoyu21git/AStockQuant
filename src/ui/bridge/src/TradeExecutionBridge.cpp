// TradeExecutionBridge.cpp - 桥接层 (暂缓实现，等待新 TradingTypes 架构适配)
#include "../include/TradeExecutionBridge.h"
#include <QMutexLocker>

TradeExecutionBridge* TradeExecutionBridge::s_instance = nullptr;
QMutex TradeExecutionBridge::s_mutex;

TradeExecutionBridge* TradeExecutionBridge::instance() {
    QMutexLocker locker(&s_mutex);
    if (!s_instance) s_instance = new TradeExecutionBridge();
    return s_instance;
}

TradeExecutionBridge::TradeExecutionBridge(QObject* parent) : QObject(parent) {}
void TradeExecutionBridge::setEngine(domain::trading::TradeExecutionEngine*) {}
void TradeExecutionBridge::initialize() { m_initialized = true; emit initializedChanged(); }
bool TradeExecutionBridge::isInitialized() const { return m_initialized; }
QString TradeExecutionBridge::lastErrorMessage() const { return m_lastErrorMessage; }
bool TradeExecutionBridge::submitBridgeOrder(const QVariantMap&) { return false; }
bool TradeExecutionBridge::submitManualTestOrder(const QString&, const QString&, double, qint64, const QString&, const QString&, const QString&) { return false; }
bool TradeExecutionBridge::cancelManualTestOrder(const QString&) { return false; }
bool TradeExecutionBridge::approveExecutionCheckpoint(const QString&, const QString&) { return false; }
bool TradeExecutionBridge::resumeExecutionPause(const QString&, const QString&) { return false; }
QVariantList TradeExecutionBridge::recentOrders() const { return {}; }
void TradeExecutionBridge::clearRecentOrders() { emit recentOrdersChanged(); }