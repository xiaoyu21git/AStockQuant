#include "TradingBridges.h"
#include "TradingConnectionConfigService.h"
#include "TradingRuntimeStatusService.h"
#include "StockNameResolver.h"
#include "../../engine/include/GmSessionEngine.h"
#include "../../engine/include/AccountEngine.h"
#include "../../engine/include/TradeEngine.h"
#include "../../../thirdparty/gmsdk/strategy.h"
#include "../../../domain/trading/TradeExecutionEngine.h"
#include "../../../domain/trading/include/PositionUtils.h"
#include "../../../domain/trading/include/OrderUtils.h"


#include "../../../engine/include/GlobalEventBusRegistry.h"
#include "../../../domain/strategy/include/RiskEvaluator.h"
#include "../../../domain/strategy/include/RiskManager.h"
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
    return m_initialized && domain::trading::TradeExecutionEngine::instance().initialized();
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
    auto& engine = domain::trading::TradeExecutionEngine::instance();

    if (!engine.initialized()) {
        INTERNAL_ERROR_STREAM << "[Live] TradeExecutionEngine not initialized, waiting for GmSessionEngine startup";
        return;
    }

    // ── 注册回调：TradeExecutionEngine → QML 信号 ──
    engine.setOnTradeFill([this](const domain::trading::TradeFill& fill) {
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
    // ── 状态字符串转换 (OrderStatusValue → QML OrderUtils 期望的大写格式) ──
    auto orderStatusValueToString = [](domain::trading::OrderStatusValue v) -> QString {
        using domain::trading::OrderStatusValue;
        switch (v) {
            case OrderStatusValue::New:              return QStringLiteral("SUBMITTED");
            case OrderStatusValue::PartiallyFilled:  return QStringLiteral("PARTIAL_FILLED");
            case OrderStatusValue::Filled:           return QStringLiteral("FILLED");
            case OrderStatusValue::Cancelled:        return QStringLiteral("CANCELLED");
            case OrderStatusValue::Rejected:         return QStringLiteral("REJECTED");
            case OrderStatusValue::Expired:          return QStringLiteral("EXPIRED");
            default:                                 return QStringLiteral("PENDING");
        }
    };

    engine.setOnOrderUpdate([this, orderStatusValueToString](const domain::trading::TradeOrder& updated) {
        QVariantMap statusEntry;
        const auto& brokerId = updated.brokerOrderId();
        const QString brokerIdStr = QString::fromStdString(brokerId);
        if (!brokerId.empty())
            statusEntry["brokerOrderId"] = brokerIdStr;
        const auto& symbol = updated.symbol();
        if (!symbol.empty())
            statusEntry["symbol"] = QString::fromStdString(symbol);
        statusEntry["side"]   = updated.side() == domain::strategy::OrderDirection::Buy
                                    ? QStringLiteral("BUY") : QStringLiteral("SELL");
        statusEntry["price"]    = updated.price();
        statusEntry["quantity"] = static_cast<double>(updated.quantity());
        const QString rawStatus = orderStatusValueToString(updated.status());
        statusEntry["rawStatus"] = rawStatus;
        statusEntry["status"]    = rawStatus;
        statusEntry["filledQty"]   = static_cast<double>(updated.filledQuantity());
        statusEntry["filledPrice"] = updated.filledPrice();
        const auto& msg = updated.statusMessage();
        if (!msg.empty())
            statusEntry["message"] = QString::fromStdString(msg);
        const auto& sid = updated.strategyId();
        if (!sid.empty())
            statusEntry["strategyId"] = QString::fromStdString(sid);

        // ── 同步更新 m_recentOrders — QML syncPendingOrders 从这里读数据 ──
        if (!brokerIdStr.isEmpty()) {
            for (int i = 0; i < m_recentOrders.size(); ++i) {
                QVariantMap item = m_recentOrders[i].toMap();
                if (item.value("brokerOrderId").toString() == brokerIdStr) {
                    item["status"]      = rawStatus;
                    item["rawStatus"]   = rawStatus;
                    item["filledQty"]   = static_cast<double>(updated.filledQuantity());
                    item["filledPrice"] = updated.filledPrice();
                    if (!msg.empty())
                        item["message"] = QString::fromStdString(msg);
                    m_recentOrders[i] = item;
                    break;
                }
            }
            emit recentOrdersChanged();
        }

        emit orderStatusChanged(statusEntry);
        emit orderStatusPublished(statusEntry);
    });

    engine.setOnOrderAccepted([this, orderStatusValueToString](const domain::trading::TradeOrder& accepted) {
        const QString rawStatus = orderStatusValueToString(accepted.status());
        const QString symbol    = QString::fromStdString(accepted.symbol());
        const QString side      = accepted.side() == domain::strategy::OrderDirection::Buy
                                    ? QStringLiteral("BUY") : QStringLiteral("SELL");
        const QString brokerId  = QString::fromStdString(accepted.brokerOrderId());
        const QString strategyId = QString::fromStdString(accepted.strategyId());

        // ── 写入 recentOrders (QML syncPendingOrders 的数据源) ──
        QVariantMap recentEntry;
        recentEntry["brokerOrderId"] = brokerId;
        recentEntry["symbol"]        = symbol;
        recentEntry["side"]          = side;
        recentEntry["price"]         = accepted.price();
        recentEntry["quantity"]      = static_cast<double>(accepted.quantity());
        recentEntry["status"]        = rawStatus;
        recentEntry["rawStatus"]     = rawStatus;
        recentEntry["message"]       = QString::fromStdString(accepted.statusMessage());
        recentEntry["strategyId"]    = strategyId;
        recentEntry["filledQty"]     = static_cast<double>(accepted.filledQuantity());
        recentEntry["filledPrice"]   = accepted.filledPrice();
        recentEntry["submittedAt"]   = QDateTime::currentDateTime().toString(Qt::ISODate);
        const auto& clientOid = accepted.clientOrderId();
        if (!clientOid.empty())
            recentEntry["clientOrderId"] = QString::fromStdString(clientOid);
        appendRecentOrder(recentEntry);

        // ── 发射信号 → QML 更新日志 + 刷新列表 ──
        emit orderStatusChanged(recentEntry);
        emit orderStatusPublished(recentEntry);
    });

    engine.setOnOrderGenerated([this](const domain::trading::TradeOrder& order) {
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
    engine.setOnOrderSubmitResult([this](const domain::trading::TradeOrder& order,
                                        const domain::trading::SubmitResult& result) {
        const QString symbol = QString::fromStdString(order.symbol());
        const QString side   = order.side() == domain::strategy::OrderDirection::Buy
                                    ? QStringLiteral("BUY") : QStringLiteral("SELL");

        QVariantMap entry;
        entry["symbol"]     = symbol;
        entry["side"]       = side;
        entry["price"]      = order.price();
        entry["quantity"]   = static_cast<double>(order.quantity());
        entry["strategyId"] = QString::fromStdString(order.strategyId());
        entry["accepted"]   = result.succeeded();
        entry["reason"]     = QString::fromStdString(result.message());

        // ── 失败时写入 recentOrders, 确保 QML 能看到被拒绝的策略订单 ──
        if (!result.succeeded()) {
            QVariantMap recentEntry;
            recentEntry["brokerOrderId"] = QString::fromStdString(result.brokerOrderId().text());
            recentEntry["symbol"]        = symbol;
            recentEntry["side"]          = side;
            recentEntry["price"]         = order.price();
            recentEntry["quantity"]      = static_cast<double>(order.quantity());
            recentEntry["status"]        = QStringLiteral("REJECTED");
            recentEntry["rawStatus"]     = QStringLiteral("REJECTED");
            recentEntry["message"]       = QString::fromStdString(result.message());
            recentEntry["strategyId"]    = QString::fromStdString(order.strategyId());
            recentEntry["submittedAt"]   = QDateTime::currentDateTime().toString(Qt::ISODate);
            appendRecentOrder(recentEntry);

            emit orderStatusChanged(recentEntry);
            emit orderStatusPublished(recentEntry);
        }

        QMetaObject::invokeMethod(this, [this, entry]() {
            emit orderSubmitResult(entry);
        }, Qt::QueuedConnection);
    });

    // ── 直接订阅 EventBus, 不依赖 TradeExecutionEngine 的 recentOrders ──
    //    确保 quickClosePosition 等直连网关的订单也能收到状态回调
    {
        auto* bus = engine::get_engine_event_bus();
        if (bus) {
            m_orderUpdateSub = bus->subscribe("trading.order.updated",
                [this](const engine::EventFormat& e) {
                    auto id = e.get<std::string>("broker_order_id");
                    if (!id) return;
                    QString brokerId = QString::fromStdString(*id);
                    auto st = e.get<std::int64_t>("status");
                    auto fp = e.get<double>("filled_price");
                    auto fq = e.get<std::int64_t>("filled_quantity");
                    auto msg = e.get<std::string>("message");

                    for (int i = 0; i < m_recentOrders.size(); ++i) {
                        QVariantMap item = m_recentOrders[i].toMap();
                        if (item.value("brokerOrderId").toString() == brokerId) {
                            if (st) {
                                // EventBus status = OrderUpdate::Status (0-based)
                                // Map to QML rawStatus
                                const char* statusStr = "SUBMITTED";
                                switch (static_cast<int>(*st)) {
                                    case 0: statusStr = "SUBMITTED";        break;
                                    case 1: statusStr = "PARTIAL_FILLED";  break;
                                    case 2: statusStr = "FILLED";          break;
                                    case 3: statusStr = "CANCELLED";       break;
                                    case 4: statusStr = "REJECTED";        break;
                                    case 5: statusStr = "EXPIRED";         break;
                                }
                                item["status"]    = QString::fromLatin1(statusStr);
                                item["rawStatus"] = QString::fromLatin1(statusStr);
                            }
                            if (fp)  item["filledPrice"] = *fp;
                            if (fq)  item["filledQty"]   = static_cast<double>(*fq);
                            if (msg) item["message"]     = QString::fromStdString(*msg);
                            m_recentOrders[i] = item;

                            emit orderStatusChanged(item);
                            emit orderStatusPublished(item);
                            emit recentOrdersChanged();
                            return;
                        }
                    }
                });
        }
    }

    m_initialized = true;
    emit initializedChanged();
    emit liveBridgeReadyChanged();
}

QString TradeExecutionBridge::liveBridgeStatusMessage() const {
    if (!m_initialized) return QStringLiteral("未初始化");
    return domain::trading::TradeExecutionEngine::instance().initialized()
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
    if (orderMap.contains("clientOrderId"))
        order.setClientOrderId(orderMap.value("clientOrderId").toString().toStdString());

    emit orderRequested(orderMap);
    emit orderRequestPublished(orderMap);

    // ── 手动单账户风控 ──
    {
        auto& accEng = engine::AccountEngine::instance();
        auto account   = accEng.account();
        auto positions = accEng.positions();

        engine::OrderRequest engineReq;
        engineReq.setSymbol(order.symbol());
        engineReq.setPrice(order.price());
        engineReq.setQuantity(order.quantity());
        engineReq.setSide((order.side() == domain::strategy::OrderDirection::Buy)
                          ? engine::OrderSide::Buy : engine::OrderSide::Sell);
        engineReq.setOrderType(engine::OrderType::Limit);

        auto riskResult = domain::strategy::RiskManager::instance()
            .checkManualOrder(engineReq, account, positions, order.price());
        if (!riskResult.approved()) {
            QVariantMap out;
            out["accepted"] = false;
            out["message"]  = QString::fromStdString(riskResult.description());
            setLastError(QString::fromStdString(riskResult.description()));
            QVariantMap rejectEntry;
            rejectEntry["status"]     = QStringLiteral("REJECTED");
            rejectEntry["rawStatus"]  = QStringLiteral("REJECTED");
            rejectEntry["message"]    = out["message"];
            rejectEntry["symbol"]     = orderMap.value("symbol");
            rejectEntry["side"]       = orderMap.value("side");
            rejectEntry["price"]      = orderMap.value("price");
            rejectEntry["quantity"]   = orderMap.value("quantity");
            rejectEntry["reasonCode"] = QString::fromStdString(
                domain::strategy::RiskEvaluator::descriptionForCode(riskResult.code()));
            rejectEntry["statusOrigin"] = QStringLiteral("risk_reject");
            emit orderStatusChanged(rejectEntry);
            emit orderStatusPublished(rejectEntry);
            return out;
        }
    }

    auto result = domain::trading::TradeExecutionEngine::instance().submitOrder(order);

    // ── 记录结果 ──
    QVariantMap out;
    out["accepted"] = result.succeeded();
    out["message"] = QString::fromStdString(result.message());

    if (!result.succeeded()) {
        setLastError(QString::fromStdString(result.message()));
    }

    // ── 引擎提前拒绝时(校验/调度/风控/未初始化), 回调未触发, 桥接层负责写入 + 通知 ──
    // 成功路径上 TradeExecutionEngine::submitOrder() 内部已通过 setOnOrderAccepted
    // 回调完成 appendRecentOrder + 信号发射, 桥接层不重复处理。
    if (!result.brokerOrderId().valid()) {
        QVariantMap recentEntry;
        recentEntry["brokerOrderId"] = QString::fromStdString(result.brokerOrderId().text());
        recentEntry["symbol"] = orderMap.value("symbol");
        recentEntry["side"] = orderMap.value("side");
        recentEntry["price"] = orderMap.value("price");
        recentEntry["quantity"] = orderMap.value("quantity");
        recentEntry["status"] = QStringLiteral("REJECTED");
        recentEntry["rawStatus"] = QStringLiteral("REJECTED");
        recentEntry["message"] = QString::fromStdString(result.message());
        recentEntry["submittedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        appendRecentOrder(recentEntry);

        emit orderStatusChanged(recentEntry);
        emit orderStatusPublished(recentEntry);
    }

    return out;
}

bool TradeExecutionBridge::submitBridgeOrder(const QVariantMap& request) {
    QVariantMap result = submitOrder(request);
    return result.value("accepted").toBool();
}

void TradeExecutionBridge::publishOrderStatusUpdate(const QString& orderId,
                                                     const QString& status,
                                                     const QString& message) {
    for (int i = 0; i < m_recentOrders.size(); ++i) {
        QVariantMap item = m_recentOrders[i].toMap();
        if (item.value("brokerOrderId").toString() == orderId
                || item.value("clientOrderId").toString() == orderId
                || item.value("id").toString() == orderId) {
            item["status"]    = status;
            item["rawStatus"] = status;
            if (!message.isEmpty())
                item["message"] = message;
            m_recentOrders[i] = item;
            emit orderStatusChanged(item);
            emit orderStatusPublished(item);
            emit recentOrdersChanged();
            return;
        }
    }
}

bool TradeExecutionBridge::cancelOrder(const QString& brokerOrderId) {
    if (!m_initialized) return false;
    domain::trading::BrokerOrderId id(brokerOrderId.toStdString());
    auto* engine = &domain::trading::TradeExecutionEngine::instance();
    if (!engine) {
        setLastError(QStringLiteral("交易引擎未初始化"));
        return false;
    }
    bool ok = engine->cancelOrder(id);
    if (ok)
        publishOrderStatusUpdate(brokerOrderId, QStringLiteral("PENDING_CANCEL"), QStringLiteral("撤单已提交"));
    else
        setLastError(QStringLiteral("撤单失败"));
    return ok;
}

bool TradeExecutionBridge::resumeExecutionPause(const QString& executionScopeId,
                                                  const QString& pausedBatchId) {
    auto* engine = &domain::trading::TradeExecutionEngine::instance();
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
    auto* engine = &domain::trading::TradeExecutionEngine::instance();
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
    auto* engine = &domain::trading::TradeExecutionEngine::instance();
    if (!engine) {
        setLastError(QStringLiteral("交易引擎未初始化"));
        return false;
    }
    bool ok = engine->cancelOrder(id);
    if (ok)
        publishOrderStatusUpdate(orderId, QStringLiteral("PENDING_CANCEL"), QStringLiteral("撤单已提交"));
    else
        setLastError(QStringLiteral("取消手动测试订单失败"));
    return ok;
}

QVariantMap TradeExecutionBridge::quickClosePosition(const QString& symbol, const QString& mode) {
    QVariantMap out;
    out["accepted"] = false;

    ensureInitialized();

    // 1. 查持仓 — 只做最基本的判空
    auto positions = engine::AccountEngine::instance().positions();
    QString searchSym = symbol.trimmed().toUpper();
    const engine::Position* found = nullptr;
    for (auto& p : positions) {
        if (QString::fromStdString(p.symbol).trimmed().toUpper() == searchSym) {
            found = &p; break;
        }
    }
    if (!found || found->quantity <= 0) {
        out["message"] = QStringLiteral("未找到持仓 ") + searchSym;
        return out;
    }

    QString m = mode.trimmed().toLower();
    QString side = (found->quantity >= 0) ? QStringLiteral("SELL") : QStringLiteral("BUY");
    std::int64_t closeQty = found->availableQty > 0 ? found->availableQty : found->quantity;

    // 2. 价格 — 跌停价卖出, 取不到用持仓成本
    double price = 0;
    auto quote = engine::GmSessionEngine::instance().fetchQuote(found->symbol);
    if (quote && quote->valid && quote->preClose > 0) {
        double preClose = quote->preClose;
        double limitRatio = 0.10;
        std::string code = found->symbol;
        auto dot = code.find('.');
        if (dot != std::string::npos) code = code.substr(0, dot);
        if (code.rfind("300", 0) == 0 || code.rfind("301", 0) == 0 || code.rfind("688", 0) == 0)
            limitRatio = 0.20;
        else if (code.rfind("8", 0) == 0 || code.rfind("4", 0) == 0)
            limitRatio = 0.30;
        price = (side == "SELL") ? (preClose * (1.0 - limitRatio))
                                 : (preClose * (1.0 + limitRatio));
        if (m == "futures") price = qRound(price);
        else if (m == "options") price = qRound(price * 10000.0) / 10000.0;
        else price = qRound(price * 100.0) / 100.0;
    }
    if (price <= 0) price = found->lastPrice > 0 ? found->lastPrice : found->costPrice;
    if (price <= 0) price = 0.01;

    // 3. 直连网关, 不做本地风控/T+1/数量校验, 全部由网关裁决
    auto* strategy = static_cast<::Strategy*>(
        engine::GmSessionEngine::instance().strategy());
    if (!strategy) {
        out["message"] = QStringLiteral("交易会话未就绪");
        return out;
    }

    std::string gmSym;
    {
        auto dot = found->symbol.find('.');
        if (dot != std::string::npos) {
            std::string code = found->symbol.substr(0, dot);
            std::string exch = found->symbol.substr(dot + 1);
            if (exch == "SH") gmSym = "SHSE." + code;
            else if (exch == "SZ") gmSym = "SZSE." + code;
            else if (exch == "BJ") gmSym = "BSE." + code;
        }
    }
    if (gmSym.empty()) {
        out["message"] = QStringLiteral("无效代码: ") + searchSym;
        return out;
    }

    int gmSide = (side == "BUY") ? 1 : 2;
    Order gmOrder = strategy->place_order(gmSym.c_str(),
                                          static_cast<int>(closeQty),
                                          gmSide, 1 /*Limit*/,
                                          2 /*平仓*/, price, 0, 0, 0.0, 0, NULL);

    bool accepted = (gmOrder.cl_ord_id[0] != '\0');
    std::string brokerId = accepted ? gmOrder.cl_ord_id : "";

    // 4. 网关原始错误
    std::string gwError;
    if (!accepted) {
        auto err = strategy->get_last_error_detail();
        gwError = (err && err[0]) ? err : "place_order rejected";
    }

    // 5. 记录到 recentOrders
    QVariantMap recentEntry;
    recentEntry["brokerOrderId"] = QString::fromStdString(brokerId);
    recentEntry["symbol"]        = searchSym;
    recentEntry["side"]          = side;
    recentEntry["price"]         = price;
    recentEntry["quantity"]      = static_cast<double>(closeQty);
    recentEntry["status"]        = accepted ? QStringLiteral("SUBMITTED") : QStringLiteral("REJECTED");
    recentEntry["rawStatus"]     = recentEntry["status"];
    recentEntry["message"]       = accepted
        ? QStringLiteral("平仓已提交") : QString::fromStdString(gwError);
    recentEntry["submittedAt"]   = QDateTime::currentDateTime().toString(Qt::ISODate);
    appendRecentOrder(recentEntry);
    emit orderStatusChanged(recentEntry);
    emit orderStatusPublished(recentEntry);

    // 6. 注册到引擎追踪成交状态
    if (accepted) {
        domain::trading::TradeOrder trackOrder;
        trackOrder.setSymbol(found->symbol);
        trackOrder.setBrokerOrderId(brokerId);
        trackOrder.setQuantity(closeQty);
        trackOrder.setPrice(price);
        trackOrder.setSide(side == "BUY" ? domain::strategy::OrderDirection::Buy
                                         : domain::strategy::OrderDirection::Sell);
        trackOrder.setStatus(domain::trading::OrderStatusValue::New);
        trackOrder.setStatusMessage("accepted");
        domain::trading::TradeExecutionEngine::instance().registerOrder(trackOrder);
    }

    out["accepted"] = accepted;
    out["status"]   = recentEntry["status"];
    out["message"]  = recentEntry["message"];
    if (!gwError.empty())
        out["gwError"] = QString::fromStdString(gwError);
    return out;
}

void TradeExecutionBridge::clearRecentOrders() {
    m_recentOrders.clear();
    emit recentOrdersChanged();
}

// ── Domain 工具方法 (薄转发到 OrderUtils.h) ──

QString TradeExecutionBridge::resolveLiveOrderType(
    const QString& rawType, const QString& optionType,
    const QString& underlying, const QString& exchange) const {
    return QString::fromUtf8(domain::trading::resolveLiveOrderType(
        rawType.toStdString(), optionType.toStdString(),
        underlying.toStdString(), exchange.toStdString()));
}

QString TradeExecutionBridge::resolveLiveOrderAction(
    const QString& rawType, const QString& side,
    const QString& positionEffect, const QString& action) const {
    return QString::fromUtf8(domain::trading::resolveLiveOrderAction(
        rawType.toStdString(), side.toStdString(),
        positionEffect.toStdString(), action.toStdString()));
}

QString TradeExecutionBridge::translateOrderSide(const QString& side) const {
    return QString::fromUtf8(domain::trading::translateOrderSide(side.toStdString()));
}

bool TradeExecutionBridge::isFuturesExchange(const QString& exchange) const {
    return domain::trading::isFuturesExchange(exchange.toStdString());
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
    m_initialized = true;

    // 订阅 AccountEngine 数据变更 — 成交后 GmSdk 回调 → AccountEngine → 触发 refresh
    engine::AccountEngine::instance().setOnDataChanged([this]() {
        refresh();
    });

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
    auto acc = engine::AccountEngine::instance().account();
    map["totalAsset"]    = acc.totalAsset;
    map["marketValue"]   = acc.marketValue;
    map["availableCash"] = acc.availableCash;
    map["realizedPnl"]   = 0.0;
    map["unrealizedPnl"] = 0.0;
    map["accountId"]     = QString::fromStdString(acc.accountId);
    return map;
}

QVariantList PositionAccountBridge::positions() const {
    QVariantList list;
    if (!m_initialized) return list;
    for (auto& p : engine::AccountEngine::instance().positions()) {
        QVariantMap item;
        const QString sym = QString::fromStdString(p.symbol);
        item["symbol"] = sym;
        item["name"]   = StockNameResolver::name(sym);
        item["side"]   = p.quantity >= 0 ? "LONG" : "SHORT";
        item["type"]   = "stock";
        item["quantity"]          = static_cast<double>(p.quantity);
        item["availableQuantity"] = static_cast<double>(p.availableQty);
        item["closeableQuantity"] = static_cast<double>(p.availableQty);
        item["costBasis"]         = p.costPrice;
        item["lastPrice"]         = p.lastPrice;
        item["marketValue"]       = p.marketValue;
        item["unrealizedPnl"]     = p.unrealizedPnl;
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

// ── Domain 工具方法 (薄转发到 PositionUtils.h 自由函数) ──

double PositionAccountBridge::normalizePositionQuantity(double rawQty, const QString& type) const {
    return static_cast<double>(
        domain::trading::normalizePositionQuantity(rawQty, type.toStdString()));
}

double PositionAccountBridge::calculatePositionMarketValue(const QVariantList& rawPositions) const {
    double total = 0.0;
    for (const auto& item : rawPositions) {
        total += item.toMap().value("marketValue", 0.0).toDouble();
    }
    return total;
}

QString PositionAccountBridge::positionTypeTitle(const QString& type) const {
    return QString::fromUtf8(domain::trading::positionTypeTitle(type.toStdString()));
}

QString PositionAccountBridge::positionUnit(const QString& type) const {
    return QString::fromUtf8(domain::trading::positionUnit(type.toStdString()));
}

QString PositionAccountBridge::positionSideLabel(const QString& side) const {
    return QString::fromUtf8(domain::trading::positionSideLabel(side.toStdString()));
}

QString PositionAccountBridge::closeableLabel(const QString& type, const QString& side) const {
    return QString::fromUtf8(domain::trading::closeableLabel(type.toStdString(), side.toStdString()));
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
    if (!engine::AccountEngine::instance().initialized()) return;

    auto acc = engine::AccountEngine::instance().account();
    double totalAsset = acc.totalAsset;
    double marketValue = acc.marketValue;

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

    if (!engine::AccountEngine::instance().initialized()) {
        snapshot["status"] = "unavailable";
        snapshot["positions"] = QVariantList();
        snapshot["diagnostics"] = QVariantMap();
        return snapshot;
    }

    auto acc = engine::AccountEngine::instance().account();
    snapshot["totalAsset"] = acc.totalAsset;
    snapshot["marketValue"] = acc.marketValue;
    snapshot["availableCash"] = acc.availableCash;
    snapshot["realizedPnl"] = 0.0;
    snapshot["unrealizedPnl"] = 0.0;
    snapshot["status"] = "ok";

    auto positions = engine::AccountEngine::instance().positions();
    QVariantList posList;
    for (auto& p : positions) {
        QVariantMap item;
        const QString sym = QString::fromStdString(p.symbol);
        item["symbol"] = sym;
        item["name"]   = StockNameResolver::name(sym);
        item["side"]     = p.quantity >= 0 ? "LONG" : "SHORT";
        item["type"]     = "stock";
        item["quantity"]   = static_cast<double>(p.quantity);
        item["costBasis"]  = p.costPrice;
        item["lastPrice"]  = p.lastPrice;
        item["marketValue"] = p.marketValue;
        item["unrealizedPnl"] = p.unrealizedPnl;
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
