#include "TradingBridges.h"
#include "../../../app/system/TradingSystem.h"
#include "../../../domain/strategy/include/RiskEvaluator.h"

#include <QVariantMap>
#include <QVariantList>
#include <QDateTime>
#include <QDebug>

namespace bridge {

// ═══════════════════════════════════════════════════════════════════
// TradeExecutionBridge
// ═══════════════════════════════════════════════════════════════════
TradeExecutionBridge::TradeExecutionBridge(QObject* parent)
    : QObject(parent) {}

bool TradeExecutionBridge::initialized() const { return m_initialized; }
bool TradeExecutionBridge::liveBridgeReady() const {
    return app::system::TradingSystem::instance().initialized();
}
bool TradeExecutionBridge::isLiveBridgeReady() const { return liveBridgeReady(); }
QVariantList TradeExecutionBridge::recentRuleHits() const { return {}; }
QVariantList TradeExecutionBridge::recentOrders() const { return m_recentOrders; }
QString TradeExecutionBridge::lastErrorMessage() const { return m_lastErrorMessage; }

void TradeExecutionBridge::ensureInitialized() {
    if (m_initialized) return;
    auto& sys = app::system::TradingSystem::instance();

    if (!sys.initialized()) {
        // ── 读取交易配置：实盘/掘金模式通过配置切换 ──
        // TODO: 掘金实盘适配器应在 bridge 层实现 (桥接层有 Qt)，包装 JujinApi 为 IBrokerGatewayEx
        // 然后通过 sys.setBrokerGateway() 注入。当前默认使用模拟网关。
        sys.initialize();
    }

    // ── 注册引擎回调：将领域层成交/状态事件转发到 QML 信号 ──
    sys.setOnTradeFill([this](const domain::trading::TradeFill& fill) {
        QVariantMap entry;
        entry["brokerOrderId"] = QString::fromStdString(fill.brokerOrderId().text());
        entry["fillId"]      = QString::fromStdString(fill.fillId().text());
        entry["price"]       = fill.price();
        entry["quantity"]    = static_cast<double>(fill.quantity());
        entry["commission"]  = fill.commission();
        entry["tradeTime"]   = QString::fromStdString(fill.tradeTime().to_string());
        emit tradeFilled(entry);
        emit tradeFillPublished(entry);
    });

    sys.setOnOrderUpdate([this](const domain::trading::TradeOrder& updated) {
        QVariantMap statusEntry;
        statusEntry["symbol"]   = QString::fromStdString(updated.symbol());
        statusEntry["side"]     = updated.side() == domain::strategy::OrderDirection::Buy
                                    ? "BUY" : "SELL";
        statusEntry["price"]    = updated.price();
        statusEntry["quantity"] = static_cast<double>(updated.quantity());
        statusEntry["status"]   = "updated";
        emit orderStatusChanged(statusEntry);
        emit orderStatusPublished(statusEntry);
    });

    m_initialized = true;
    emit initializedChanged();
    emit liveBridgeReadyChanged();
}

QString TradeExecutionBridge::liveBridgeStatusMessage() const {
    if (!m_initialized) return QStringLiteral("未初始化");
    return app::system::TradingSystem::instance().initialized()
        ? QStringLiteral("可执行") : QStringLiteral("待连接");
}

QVariantMap TradeExecutionBridge::submitOrder(const QVariantMap& orderMap) {
    ensureInitialized();

    domain::trading::TradeOrder order;
    order.setStrategyId(orderMap.value("strategyId").toString().toStdString());
    order.setSymbol(orderMap.value("symbol").toString().toStdString());
    order.setPrice(orderMap.value("price").toDouble());
    order.setQuantity(static_cast<std::int64_t>(orderMap.value("quantity").toDouble()));

    // ── 方向：使用 RiskEvaluator 枚举转换（领域层纯 C++，无字符串比较） ──
    const std::string sideRaw = orderMap.value("side").toString().toUpper().toStdString();
    order.setSide(domain::strategy::RiskEvaluator::directionFromString(sideRaw));

    // ── 仓位效应 ──
    if (orderMap.contains("positionEffect")) {
        const std::string peRaw = orderMap.value("positionEffect").toString().toUpper().toStdString();
        order.setPositionEffect(domain::strategy::RiskEvaluator::positionEffectFromString(peRaw));
    }

    // ── 订单类型 (LIMIT/MARKET) ──
    if (orderMap.contains("orderType")) {
        const QString ot = orderMap.value("orderType").toString().toUpper();
        if (ot == "MARKET") {
            // 市价单：使用最近价作为参考价格，标记为市价
            order.setPrice(orderMap.value("lastPrice", orderMap.value("price")).toDouble());
        }
    }

    // ── 特殊操作类型 (repay/returnStock) ──
    if (orderMap.contains("action")) {
        const std::string actionRaw = orderMap.value("action").toString().toStdString();
        using SA = domain::strategy::SpecialAction;
        SA action = domain::strategy::RiskEvaluator::specialActionFromString(actionRaw);
        if (action == SA::CashRepay) {
            order.setActionKind(domain::trading::ActionKind::CashRepay);
            order.setBoardLotMode(false);
        } else if (action == SA::ShareReturn) {
            order.setActionKind(domain::trading::ActionKind::ShareReturn);
            order.setBoardLotMode(false);
        }
    }

    // ── 可选字段 ──
    if (orderMap.contains("signalStrength"))
        order.setSignalStrength(orderMap.value("signalStrength").toDouble());
    if (orderMap.contains("cashAmount"))
        order.setCashAmount(orderMap.value("cashAmount").toDouble());

    emit orderRequested(orderMap);
    emit orderRequestPublished(orderMap);

    auto result = app::system::TradingSystem::instance().submitOrder(order);

    // ── 记录结果 ──
    QVariantMap out;
    out["accepted"] = result.succeeded();
    out["message"] = QString::fromStdString(result.message());

    if (!result.succeeded()) {
        setLastError(QString::fromStdString(result.message()));
    }

    // 添加到最近订单列表
    QVariantMap recentEntry;
    recentEntry["brokerOrderId"] = QString::fromStdString(result.brokerOrderId().text());
    recentEntry["symbol"] = orderMap.value("symbol");
    recentEntry["side"] = orderMap.value("side");
    recentEntry["price"] = orderMap.value("price");
    recentEntry["quantity"] = orderMap.value("quantity");
    recentEntry["status"] = result.succeeded() ? "submitted" : "rejected";
    recentEntry["message"] = QString::fromStdString(result.message());
    recentEntry["submittedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    appendRecentOrder(recentEntry);

    // 发布订单状态
    QVariantMap statusEntry;
    statusEntry["brokerOrderId"] = recentEntry["brokerOrderId"];
    statusEntry["status"] = recentEntry["status"];
    statusEntry["message"] = recentEntry["message"];
    statusEntry["symbol"] = recentEntry["symbol"];
    statusEntry["side"] = recentEntry["side"];
    statusEntry["price"] = recentEntry["price"];
    statusEntry["quantity"] = recentEntry["quantity"];
    emit orderStatusChanged(statusEntry);
    emit orderStatusPublished(statusEntry);

    return out;
}

bool TradeExecutionBridge::submitBridgeOrder(const QVariantMap& request) {
    QVariantMap result = submitOrder(request);
    return result.value("accepted").toBool();
}

bool TradeExecutionBridge::cancelOrder(const QString& brokerOrderId) {
    if (!m_initialized) return false;
    domain::trading::BrokerOrderId id(brokerOrderId.toStdString());
    auto* engine = app::system::TradingSystem::instance().tradeEngine();
    if (!engine) {
        setLastError(QStringLiteral("交易引擎未初始化"));
        return false;
    }
    bool ok = engine->cancelOrder(id);
    if (!ok) setLastError(QStringLiteral("撤单失败"));
    return ok;
}

bool TradeExecutionBridge::resumeExecutionPause(const QString& executionScopeId,
                                                  const QString& pausedBatchId) {
    auto* engine = app::system::TradingSystem::instance().tradeEngine();
    if (!engine) {
        setLastError(QStringLiteral("交易引擎未初始化"));
        return false;
    }
    bool ok = engine->resumeExecutionPause(executionScopeId.toStdString(),
                                            pausedBatchId.toStdString());
    if (!ok) setLastError(QStringLiteral("恢复执行暂停失败"));
    return ok;
}

bool TradeExecutionBridge::approveExecutionCheckpoint(const QString& executionScopeId,
                                                        const QString& batchId) {
    auto* engine = app::system::TradingSystem::instance().tradeEngine();
    if (!engine) {
        setLastError(QStringLiteral("交易引擎未初始化"));
        return false;
    }
    bool ok = engine->approveExecutionCheckpoint(executionScopeId.toStdString(),
                                                   batchId.toStdString());
    if (!ok) setLastError(QStringLiteral("批准执行检查点失败"));
    return ok;
}

bool TradeExecutionBridge::cancelManualTestOrder(const QString& orderId) {
    domain::trading::BrokerOrderId id(orderId.toStdString());
    auto* engine = app::system::TradingSystem::instance().tradeEngine();
    if (!engine) {
        setLastError(QStringLiteral("交易引擎未初始化"));
        return false;
    }
    bool ok = engine->cancelOrder(id);
    if (!ok) setLastError(QStringLiteral("取消手动测试订单失败"));
    return ok;
}

void TradeExecutionBridge::clearRecentOrders() {
    m_recentOrders.clear();
    emit recentOrdersChanged();
}

void TradeExecutionBridge::appendRecentOrder(const QVariantMap& order) {
    m_recentOrders.prepend(order);
    if (m_recentOrders.size() > 50) m_recentOrders.removeLast();
    emit recentOrdersChanged();
}

void TradeExecutionBridge::setLastError(const QString& message) {
    if (m_lastErrorMessage == message) return;
    m_lastErrorMessage = message;
    emit lastErrorMessageChanged();
}

// ═══════════════════════════════════════════════════════════════════
// PositionAccountBridge
// ═══════════════════════════════════════════════════════════════════
PositionAccountBridge::PositionAccountBridge(QObject* parent)
    : QObject(parent) {}

bool PositionAccountBridge::initialized() const { return m_initialized; }

void PositionAccountBridge::initialize() {
    if (m_initialized) return;
    auto& sys = app::system::TradingSystem::instance();
    if (!sys.initialized()) sys.initialize();
    m_initialized = true;
    emit initializedChanged();
    refresh();
}

void PositionAccountBridge::requestInitialSnapshot() {
    if (!m_initialized) initialize();
    refresh();
}

bool PositionAccountBridge::initialSnapshotLoaded() const {
    return m_initialized;
}

void PositionAccountBridge::refresh() {
    emit accountSnapshotChanged();
    emit positionsChanged();
    emit dataChanged();
}

QVariantMap PositionAccountBridge::accountSnapshot() const {
    QVariantMap map;
    if (!m_initialized) return map;
    const auto& acc = app::system::TradingSystem::instance().accountSnapshot();
    map["totalAsset"] = acc.totalAsset();
    map["marketValue"] = acc.marketValue();
    map["availableCash"] = acc.availableCash();
    map["realizedPnl"] = acc.realizedPnl();
    map["unrealizedPnl"] = acc.unrealizedPnl();
    map["accountId"] = QString::fromStdString(acc.accountId());
    return map;
}

QVariantList PositionAccountBridge::positions() const {
    QVariantList list;
    if (!m_initialized) return list;
    const auto& posMap = app::system::TradingSystem::instance().positions();
    for (const auto& [key, pos] : posMap) {
        QVariantMap item;
        item["symbol"] = QString::fromStdString(pos.symbol());
        item["side"] = pos.side() == domain::trading::PositionSide::Long ? "LONG" : "SHORT";
        item["type"] = pos.type() == domain::trading::PositionType::Stock ? "stock"
            : pos.type() == domain::trading::PositionType::MarginBuy ? "margin_buy"
            : pos.type() == domain::trading::PositionType::MarginSell ? "margin_sell"
            : pos.type() == domain::trading::PositionType::Futures ? "futures"
            : "options";
        item["quantity"] = static_cast<double>(pos.quantity());
        item["availableQuantity"] = static_cast<double>(pos.availableQuantity());
        item["closeableQuantity"] = static_cast<double>(pos.closeableQuantity());
        item["costBasis"] = pos.costBasis();
        item["lastPrice"] = pos.lastPrice();
        item["marketValue"] = pos.marketValue();
        item["unrealizedPnl"] = pos.unrealizedPnl();
        item["underlying"] = QString::fromStdString(pos.underlying());
        item["optionType"] = QString::fromStdString(pos.optionType());
        item["expiry"] = QString::fromStdString(pos.expiry());
        list.append(item);
    }
    return list;
}

QVariantList PositionAccountBridge::recentOrderStatuses() const {
    return m_recentOrderStatuses;
}

void PositionAccountBridge::appendOrderStatus(const QVariantMap& status) {
    // 去重：按 brokerOrderId 更新已有条目
    const QString id = status.value("brokerOrderId").toString();
    for (int i = 0; i < m_recentOrderStatuses.size(); ++i) {
        QVariantMap item = m_recentOrderStatuses[i].toMap();
        if (item.value("brokerOrderId").toString() == id) {
            m_recentOrderStatuses[i] = status;
            emit recentOrderStatusesChanged();
            return;
        }
    }
    m_recentOrderStatuses.prepend(status);
    if (m_recentOrderStatuses.size() > 50) m_recentOrderStatuses.removeLast();
    emit recentOrderStatusesChanged();
}

// ═══════════════════════════════════════════════════════════════════
// RiskControlBridge
// ═══════════════════════════════════════════════════════════════════
RiskControlBridge::RiskControlBridge(QObject* parent)
    : QObject(parent) {
    m_timer.setInterval(3000);
    connect(&m_timer, &QTimer::timeout, this, &RiskControlBridge::refresh);
}

double RiskControlBridge::varUsagePercent() const { return m_varUsagePct; }
double RiskControlBridge::currentDrawdownPercent() const { return m_drawdownPct; }
double RiskControlBridge::currentTotalExposurePercent() const { return m_exposurePct; }
double RiskControlBridge::varBudgetAmount() const { return m_varBudget; }
double RiskControlBridge::estimatedVarAmount() const { return m_estimatedVar; }

void RiskControlBridge::initializeAsync() {
    auto& sys = app::system::TradingSystem::instance();
    if (!sys.initialized()) sys.initialize();
    refresh();
    m_timer.start();
}

void RiskControlBridge::initialize() {
    initializeAsync();
}

void RiskControlBridge::refresh() {
    if (!app::system::TradingSystem::instance().initialized()) return;

    const auto& acc = app::system::TradingSystem::instance().accountSnapshot();
    double totalAsset = acc.totalAsset();
    double marketValue = acc.marketValue();

    bool changed = false;
    auto check = [&](double& oldVal, double newVal, double eps = 0.01) {
        if (std::abs(oldVal - newVal) > eps) { oldVal = newVal; changed = true; }
    };

    double exposurePct = totalAsset > 0.0 ? (marketValue / totalAsset) * 100.0 : 0.0;
    double varUsage = exposurePct * 1.49;  // 简化的VaR使用率

    check(m_exposurePct, exposurePct);
    check(m_varUsagePct, varUsage);

    if (changed) {
        emit varUsagePercentChanged();
        emit currentDrawdownPercentChanged();
        emit currentTotalExposurePercentChanged();
        emit varBudgetAmountChanged();
        emit estimatedVarAmountChanged();
    }
}

QVariantMap RiskControlBridge::buildPortfolioSnapshot(const QVariantMap& strategy,
                                                        const QVariantMap& backtestRecord) {
    QVariantMap snapshot;
    snapshot["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    snapshot["strategyId"] = strategy.value("strategyId");

    if (!app::system::TradingSystem::instance().initialized()) {
        snapshot["status"] = "unavailable";
        snapshot["positions"] = QVariantList();
        snapshot["diagnostics"] = QVariantMap();
        return snapshot;
    }

    const auto& acc = app::system::TradingSystem::instance().accountSnapshot();
    snapshot["totalAsset"] = acc.totalAsset();
    snapshot["marketValue"] = acc.marketValue();
    snapshot["availableCash"] = acc.availableCash();
    snapshot["realizedPnl"] = acc.realizedPnl();
    snapshot["unrealizedPnl"] = acc.unrealizedPnl();
    snapshot["status"] = "ok";

    const auto& posMap = app::system::TradingSystem::instance().positions();
    QVariantList posList;
    for (const auto& [key, pos] : posMap) {
        QVariantMap item;
        item["symbol"] = QString::fromStdString(pos.symbol());
        item["side"] = pos.side() == domain::trading::PositionSide::Long ? "LONG" : "SHORT";
        item["type"] = pos.type() == domain::trading::PositionType::Stock ? "stock"
            : pos.type() == domain::trading::PositionType::MarginBuy ? "margin_buy"
            : pos.type() == domain::trading::PositionType::MarginSell ? "margin_sell"
            : "options";
        item["quantity"] = static_cast<double>(pos.quantity());
        item["costBasis"] = pos.costBasis();
        item["lastPrice"] = pos.lastPrice();
        item["marketValue"] = pos.marketValue();
        item["unrealizedPnl"] = pos.unrealizedPnl();
        posList.append(item);
    }
    snapshot["positions"] = posList;

    // 回测上下文诊断（简化）
    QVariantMap diagnostics;
    diagnostics["backtestRecordId"] = backtestRecord.value("recordId");
    diagnostics["exposurePercent"] = m_exposurePct;
    diagnostics["varUsagePercent"] = m_varUsagePct;
    diagnostics["drawdownPercent"] = m_drawdownPct;
    snapshot["diagnostics"] = diagnostics;

    return snapshot;
}

} // namespace bridge
