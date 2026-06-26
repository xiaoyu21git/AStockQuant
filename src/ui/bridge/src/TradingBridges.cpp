#include "TradingBridges.h"
#include "TradingConnectionConfigService.h"
#include "TradingRuntimeStatusService.h"
#include "StockNameResolver.h"
#include "../../engine/include/GmSessionEngine.h"
#include "../../engine/include/AccountEngine.h"
#include "../../../app/system/TradingSystem.h"

#include "../../../engine/include/GlobalEventBusRegistry.h"
#include "../../../domain/strategy/include/RiskEvaluator.h"
#include "foundation/log/logging.hpp"

#include <QVariantMap>
#include <QVariantList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
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
    return m_initialized && app::system::TradingSystem::instance().initialized();
}
bool TradeExecutionBridge::isLiveBridgeReady() {
    const_cast<TradeExecutionBridge*>(this)->ensureInitialized();
    return liveBridgeReady();
}
QVariantList TradeExecutionBridge::recentRuleHits() const { return {}; }
QVariantList TradeExecutionBridge::recentOrders() const { return m_recentOrders; }
QString TradeExecutionBridge::lastErrorMessage() const { return m_lastErrorMessage; }

void TradeExecutionBridge::ensureInitialized() {
    if (m_initialized) return;
    auto& sys = app::system::TradingSystem::instance();

    if (!sys.initialized()) {
        // 通过 TradingConnectionConfigService 统一读取交易配置
        auto* cfgSvc = TradingConnectionConfigService::instance();
        QVariantMap cfg = cfgSvc->loadConfiguration();
        if (cfg.isEmpty()) {
            INTERNAL_ERROR_STREAM << "[Live] config file not found";
            return;
        }
        if (cfg.value("token").toString().isEmpty()) {
            INTERNAL_ERROR_STREAM << "[Live] token not configured";
            return;
        }
        sys.initialize();

        // 从 SDK 同步账户/持仓到 PositionAccountEngine（QML 从这里读数据）
        {
            auto& eng = engine::AccountEngine::instance();
            auto acc = eng.account();
            INTERNAL_ERROR_STREAM << "[TradingBridge] SDK account: total=" << acc.totalAsset
                      << " available=" << acc.availableCash << " mv=" << acc.marketValue;
            domain::trading::AccountSnapshot snap;
            snap.setAccountId(cfg.value("accountId",
                               cfg.value("liveAccountId",
                               cfg.value("simAccountId"))).toString().toStdString());
            snap.setAvailableCash(acc.availableCash);
            snap.setTotalAsset(acc.totalAsset);
            snap.setMarketValue(acc.marketValue);
            if (auto* pe = sys.positionEngine()) {
                pe->applyAccountEvent(snap);
                INTERNAL_ERROR_STREAM << "[TradingBridge] account synced to PositionEngine";
            }
            for (auto& p : eng.positions()) {
                domain::trading::Position pos;
                pos.setSymbol(p.symbol);
                pos.setLastPrice(p.lastPrice);
                pos.setQuantity(p.quantity);
                pos.setSide(p.quantity >= 0 ? domain::trading::PositionSide::Long
                                            : domain::trading::PositionSide::Short);
                if (auto* pe = sys.positionEngine()) {
                    pe->applyPositionEvent(p.symbol, pos);
                }
            }
        }

        bridge::TradingRuntimeStatusService::instance()->refresh();
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

    // ── 策略订单产生通知 → QML ──
    sys.setOnOrderGenerated([this](const domain::trading::TradeOrder& order) {
        QVariantMap entry;
        entry["symbol"]     = QString::fromStdString(order.symbol());
        entry["side"]       = order.side() == domain::strategy::OrderDirection::Buy
                                  ? "BUY" : "SELL";
        entry["price"]      = order.price();
        entry["quantity"]   = static_cast<double>(order.quantity());
        entry["strategyId"] = QString::fromStdString(order.strategyId());
        QMetaObject::invokeMethod(this, [this, entry]() {
            emit orderGenerated(entry);
        }, Qt::QueuedConnection);
    });

    // ── 订单提交结果通知 → QML ──
    sys.setOnOrderSubmitResult([this](const domain::trading::TradeOrder& order,
                                        const domain::trading::SubmitResult& result) {
        QVariantMap entry;
        entry["symbol"]   = QString::fromStdString(order.symbol());
        entry["accepted"] = result.succeeded();
        entry["reason"]   = QString::fromStdString(result.message());
        QMetaObject::invokeMethod(this, [this, entry]() {
            emit orderSubmitResult(entry);
        }, Qt::QueuedConnection);
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
    QString strategyId = orderMap.value("strategyId").toString().trimmed();
    if (strategyId.isEmpty()) {
        // QML 没传就从底层配置服务取 boundStrategyId
        strategyId = TradingConnectionConfigService::instance()
                         ->currentConfiguration()
                         .value("boundStrategyId").toString().trimmed();
    }
    order.setStrategyId(strategyId.toStdString());
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
    // 不在此处调 TradingSystem::initialize()——gateway 可能还没设置
    // TradeExecutionBridge::ensureInitialized() 是唯一正确的初始化入口
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
        const QString sym = QString::fromStdString(pos.symbol());
        item["symbol"] = sym;
        item["name"]   = StockNameResolver::name(sym);
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
    // 不在此处调 TradingSystem::initialize()——gateway 可能还没设置
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
        const QString sym = QString::fromStdString(pos.symbol());
        item["symbol"] = sym;
        item["name"]   = StockNameResolver::name(sym);
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

// ═══════════════════════════════════════════════════════════════════
// 掘金实盘网关 (bridge 层，有 Qt)
// ═══════════════════════════════════════════════════════════════════

} // namespace bridge
