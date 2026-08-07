#include "TradingBridges.h"
#include "TradingConnectionConfigService.h"
#include "TradingRuntimeStatusService.h"
#include "StockNameResolver.h"
#include "OrderFieldKeys.h"
#include "../../engine/include/GmSessionEngine.h"
#include "../../engine/include/AccountEngine.h"
#include "../../engine/include/TradeEngine.h"
#include "../../../thirdparty/gmsdk/strategy.h"
#include "../../../domain/trading/TradeExecutionEngine.h"
#include "../../../domain/trading/include/PositionUtils.h"
#include "../../../domain/trading/include/OrderUtils.h"


#include "../../../engine/include/GlobalEventBusRegistry.h"
#include "../../../domain/strategy/include/RiskEvaluator.h"
#include "foundation/market/AStockSymbol.h"

#include <sstream>
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

#include <atomic>
#include <chrono>

// ── 工具函数 ──
namespace {
/// @brief OrderDirection → QML 侧边字符串
inline QString toQmlSide(domain::strategy::OrderDirection d) {
    return d == domain::strategy::OrderDirection::Buy
        ? QStringLiteral("BUY") : QStringLiteral("SELL");
}
} // anonymous namespace

namespace {

std::string generateClOrdId() {
    static std::atomic<uint64_t> s_counter{0};
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    uint64_t seq = s_counter.fetch_add(1, std::memory_order_relaxed);
    std::ostringstream oss;
    oss << std::hex << now << "_" << seq;
    return oss.str();
}

} // anonymous namespace

namespace bridge {
using namespace bridge::keys;

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
QVariantList TradeExecutionBridge::recentOrders() const {
    const_cast<TradeExecutionBridge*>(this)->ensureInitialized();
    QMutexLocker lock(&m_recentOrdersMutex);
    return m_recentOrders;
}
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
        entry[kBrokerOrderId] = QString::fromStdString(fill.brokerOrderId().text());
        entry["fillId"]      = QString::fromStdString(fill.fillId().text());
        entry[kPrice]       = fill.price();
        entry[kQuantity]    = static_cast<double>(fill.quantity());
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
            statusEntry[kBrokerOrderId] = brokerIdStr;
        const auto& symbol = updated.symbol();
        if (!symbol.empty())
            statusEntry[kSymbol] = QString::fromStdString(symbol);
        statusEntry[kSide]   = toQmlSide(updated.side());
        statusEntry[kPrice]    = updated.price();
        statusEntry[kQuantity] = static_cast<double>(updated.quantity());
        const QString rawStatus = orderStatusValueToString(updated.status());
        statusEntry[kRawStatus] = rawStatus;
        statusEntry[kStatus]    = rawStatus;
        statusEntry["filledQty"]   = static_cast<double>(updated.filledQuantity());
        statusEntry["filledPrice"] = updated.filledPrice();
        const auto& msg = updated.statusMessage();
        if (!msg.empty())
            statusEntry["message"] = QString::fromStdString(msg);
        const auto& sid = updated.strategyId();
        if (!sid.empty())
            statusEntry[kStrategyId] = QString::fromStdString(sid);

        // ── 同步更新 m_recentOrders — QML syncPendingOrders 从这里读数据 ──
        bool didUpdateRecent = false;
        if (!brokerIdStr.isEmpty()) {
            QMutexLocker lock(&m_recentOrdersMutex);
            for (int i = 0; i < m_recentOrders.size(); ++i) {
                QVariantMap item = m_recentOrders[i].toMap();
                if (item.value(kBrokerOrderId).toString() == brokerIdStr) {
                    item[kStatus]      = rawStatus;
                    item[kRawStatus]   = rawStatus;
                    item["filledQty"]   = static_cast<double>(updated.filledQuantity());
                    item["filledPrice"] = updated.filledPrice();
                    if (!msg.empty())
                        item["message"] = QString::fromStdString(msg);
                    m_recentOrders[i] = item;
                    didUpdateRecent = true;
                    break;
                }
            }
        } // 锁释放

        // 信号发射延迟到主线程（回调可能来自 gmsdk 线程）
        QMetaObject::invokeMethod(this, [this, statusEntry, didUpdateRecent]() {
            if (didUpdateRecent) emit recentOrdersChanged();
            emit orderStatusChanged(statusEntry);
            emit orderStatusPublished(statusEntry);
        }, Qt::QueuedConnection);
    });

    engine.setOnOrderAccepted([this, orderStatusValueToString](const domain::trading::TradeOrder& accepted) {
        const QString rawStatus = orderStatusValueToString(accepted.status());
        const QString symbol    = QString::fromStdString(accepted.symbol());
        const QString side      = toQmlSide(accepted.side());
        const QString brokerId  = QString::fromStdString(accepted.brokerOrderId());
        const QString strategyId = QString::fromStdString(accepted.strategyId());

        // ── 写入 recentOrders (QML syncPendingOrders 的数据源) ──
        QVariantMap recentEntry;
        recentEntry[kBrokerOrderId] = brokerId;
        recentEntry[kSymbol]        = symbol;
        recentEntry[kSide]          = side;
        recentEntry[kPrice]         = accepted.price();
        recentEntry[kQuantity]      = static_cast<double>(accepted.quantity());
        recentEntry[kStatus]        = rawStatus;
        recentEntry[kRawStatus]     = rawStatus;
        recentEntry["message"]       = QString::fromStdString(accepted.statusMessage());
        recentEntry[kStrategyId]    = strategyId;
        recentEntry["filledQty"]     = static_cast<double>(accepted.filledQuantity());
        recentEntry["filledPrice"]   = accepted.filledPrice();
        recentEntry["submittedAt"]   = QDateTime::currentDateTime().toString(Qt::ISODate);
        const auto& clientOid = accepted.clientOrderId();
        if (!clientOid.empty())
            recentEntry["clientOrderId"] = QString::fromStdString(clientOid);
        appendRecentOrder(recentEntry);

        // 信号发射延迟到主线程（回调可能来自 gmsdk 线程）
        QMetaObject::invokeMethod(this, [this, recentEntry]() {
            emit orderStatusChanged(recentEntry);
            emit orderStatusPublished(recentEntry);
        }, Qt::QueuedConnection);
    });

    engine.setOnOrderGenerated([this](const domain::trading::TradeOrder& order) {
        QVariantMap entry;
        entry[kSymbol]     = QString::fromStdString(order.symbol());
        entry[kSide]       = toQmlSide(order.side());
        entry[kPrice]      = order.price();
        entry[kQuantity]   = static_cast<double>(order.quantity());
        entry[kStrategyId] = QString::fromStdString(order.strategyId());
        QMetaObject::invokeMethod(this, [this, entry]() {
            emit orderGenerated(entry);
        }, Qt::QueuedConnection);
    });
    engine.setOnOrderSubmitResult([this](const domain::trading::TradeOrder& order,
                                        const domain::trading::SubmitResult& result) {
        const QString symbol = QString::fromStdString(order.symbol());
        const QString side   = toQmlSide(order.side());

        QVariantMap entry;
        entry[kSymbol]     = symbol;
        entry[kSide]       = side;
        entry[kPrice]      = order.price();
        entry[kQuantity]   = static_cast<double>(order.quantity());
        entry[kStrategyId] = QString::fromStdString(order.strategyId());
        entry["accepted"]   = result.succeeded();
        entry["reason"]     = QString::fromStdString(result.message());

        // ── 失败时写入 recentOrders, 确保 QML 能看到被拒绝的策略订单 ──
        if (!result.succeeded()) {
            QVariantMap recentEntry;
            recentEntry[kBrokerOrderId] = QString::fromStdString(result.brokerOrderId().text());
            recentEntry[kSymbol]        = symbol;
            recentEntry[kSide]          = side;
            recentEntry[kPrice]         = order.price();
            recentEntry[kQuantity]      = static_cast<double>(order.quantity());
            recentEntry[kStatus]        = QStringLiteral("REJECTED");
            recentEntry[kRawStatus]     = QStringLiteral("REJECTED");
            recentEntry["message"]       = QString::fromStdString(result.message());
            recentEntry[kStrategyId]    = QString::fromStdString(order.strategyId());
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
        auto bus = engine::get_engine_event_bus();
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

                    QVariantMap capturedItem;
                    bool foundItem = false;
                    {
                        QMutexLocker lock(&m_recentOrdersMutex);
                        for (int i = 0; i < m_recentOrders.size(); ++i) {
                            QVariantMap item = m_recentOrders[i].toMap();
                            if (item.value(kBrokerOrderId).toString() == brokerId) {
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
                                    item[kStatus]    = QString::fromLatin1(statusStr);
                                    item[kRawStatus] = QString::fromLatin1(statusStr);
                                }
                                if (fp)  item["filledPrice"] = *fp;
                                if (fq)  item["filledQty"]   = static_cast<double>(*fq);
                                if (msg) item["message"]     = QString::fromStdString(*msg);
                                m_recentOrders[i] = item;
                                capturedItem = item;
                                foundItem = true;
                                break;
                            }
                        }
                    } // 锁释放 — emit 在锁外避免与 recentOrders() getter 死锁

                    if (foundItem) {
                        emit orderStatusChanged(capturedItem);
                        emit orderStatusPublished(capturedItem);
                        emit recentOrdersChanged();
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
    QString strategyId = orderMap.value(kStrategyId).toString().trimmed();
    if (strategyId.isEmpty()) {
        // QML 没传就从底层配置服务取 boundStrategyId
        strategyId = TradingConnectionConfigService::instance()
                         ->currentConfiguration()
                         .value("boundStrategyId").toString().trimmed();
    }
    order.setStrategyId(strategyId.toStdString());
    order.setSymbol(orderMap.value(kSymbol).toString().toStdString());
    order.setPrice(orderMap.value(kPrice).toDouble());
    order.setQuantity(static_cast<std::int64_t>(orderMap.value(kQuantity).toDouble()));

    // ── 方向：使用 RiskEvaluator 枚举转换（领域层纯 C++，无字符串比较） ──
    const std::string sideRaw = orderMap.value(kSide).toString().toUpper().toStdString();
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
            order.setPrice(orderMap.value(kLastPrice, orderMap.value(kPrice)).toDouble());
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

    // 构建 OrderRequest（风控在 Trading 引擎 submitOrder 内部统一完成）
    engine::OrderRequest engineReq;
    auto& accEng = engine::AccountEngine::instance();
    auto account   = accEng.account();

    domain::trading::OrderBuilder manualBuilder;
    bool isBuy = (order.side() == domain::strategy::OrderDirection::Buy);
    auto pe = isBuy ? domain::trading::PositionEffect::Open
                    : domain::trading::PositionEffect::Close;
    engineReq = manualBuilder.buildManualOrder(
        order.symbol(),
        isBuy ? engine::OrderSide::Buy : engine::OrderSide::Sell,
        order.price(), order.quantity(), pe,
        strategyId.toStdString(), account.accountId);

    if (!engineReq.isValid()) {
        QVariantMap out;
        out["accepted"] = false;
        out["message"]  = QStringLiteral("订单构建失败: 价格/数量无效");
        setLastError(out["message"].toString());
        return out;
    }

    // 篮子ID — 手动单也标记以便审计追溯
    {
        static std::atomic<uint64_t> s_manualBasketSeq{0};
        engineReq.setExtension(domain::trading::ExtKey::kBasketId,
                               s_manualBasketSeq.fetch_add(1));
    }

    // TradeOrder 字段对齐 OrderBuilder 输出
    order.setClOrdId(engineReq.clOrdId());
    order.setAccountId(engineReq.accountId());
    order.setCurrency(engineReq.currency());
    order.setExchange(engineReq.exchange());
    order.setPositionEffect(static_cast<domain::strategy::PositionEffect>(engineReq.positionEffect()));

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
        recentEntry[kBrokerOrderId] = QString::fromStdString(result.brokerOrderId().text());
        recentEntry[kSymbol] = orderMap.value(kSymbol);
        recentEntry[kSide] = orderMap.value(kSide);
        recentEntry[kPrice] = orderMap.value(kPrice);
        recentEntry[kQuantity] = orderMap.value(kQuantity);
        recentEntry[kStatus] = QStringLiteral("REJECTED");
        recentEntry[kRawStatus] = QStringLiteral("REJECTED");
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
    QVariantMap capturedItem;
    bool foundItem = false;
    {
        QMutexLocker lock(&m_recentOrdersMutex);
        for (int i = 0; i < m_recentOrders.size(); ++i) {
            QVariantMap item = m_recentOrders[i].toMap();
            if (item.value(kBrokerOrderId).toString() == orderId
                    || item.value("clientOrderId").toString() == orderId
                    || item.value("id").toString() == orderId) {
                item[kStatus]    = status;
                item[kRawStatus] = status;
                if (!message.isEmpty())
                    item["message"] = message;
                m_recentOrders[i] = item;
                capturedItem = item;
                foundItem = true;
                break;
            }
        }
    } // 锁释放

    if (foundItem) {
        emit orderStatusChanged(capturedItem);
        emit orderStatusPublished(capturedItem);
        emit recentOrdersChanged();
    }
}

bool TradeExecutionBridge::cancelOrder(const QString& brokerOrderId) {
    ensureInitialized();
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
        double limitRatio = domain::trading::boardLimitRatio(found->symbol);
        price = (side == "SELL") ? (preClose * (1.0 - limitRatio))
                                 : (preClose * (1.0 + limitRatio));
        price = domain::trading::roundPriceForBoard(price, m.toStdString());
    }
    if (price <= 0) price = found->lastPrice > 0 ? found->lastPrice : found->costPrice;
    if (price <= 0) price = 0.01;

    // 3. 直连网关, 不做本地风控/T+1/数量校验, 全部由网关裁决
    auto* strategy = engine::GmSessionEngine::instance().strategy();
    if (!strategy) {
        out["message"] = QStringLiteral("交易会话未就绪");
        return out;
    }

    std::string gmSym = foundation::market::AStockSymbol::fromString(found->symbol).gmSymbol();
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
    recentEntry[kBrokerOrderId] = QString::fromStdString(brokerId);
    recentEntry[kSymbol]        = searchSym;
    recentEntry[kSide]          = side;
    recentEntry[kPrice]         = price;
    recentEntry[kQuantity]      = static_cast<double>(closeQty);
    recentEntry[kStatus]        = accepted ? QStringLiteral("SUBMITTED") : QStringLiteral("REJECTED");
    recentEntry[kRawStatus]     = recentEntry[kStatus];
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
    out[kStatus]   = recentEntry[kStatus];
    out["message"]  = recentEntry["message"];
    if (!gwError.empty())
        out["gwError"] = QString::fromStdString(gwError);
    return out;
}

void TradeExecutionBridge::clearRecentOrders() {
    {
        QMutexLocker lock(&m_recentOrdersMutex);
        m_recentOrders.clear();
    }
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
    {
        QMutexLocker lock(&m_recentOrdersMutex);
        m_recentOrders.prepend(order);
        if (m_recentOrders.size() > 50) m_recentOrders.removeLast();
    }
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
    const_cast<PositionAccountBridge*>(this)->initialize();
    QVariantMap map;
    if (!m_initialized) return map;
    auto snap = engine::AccountEngine::instance().snapshot();
    auto& acc = snap.account;
    map["totalAsset"]    = acc.totalAsset;
    map["marketValue"]   = acc.marketValue;
    map["availableCash"] = acc.availableCash;
    map["realizedPnl"]   = acc.realizedPnl;
    // 浮动盈亏优先用 gmsdk 推送值，否则汇总各持仓
    double unrealized = acc.unrealizedPnl;
    if (unrealized == 0.0) {
        for (const auto& p : snap.positions)
            unrealized += p.unrealizedPnl;
    }
    map["unrealizedPnl"] = unrealized;
    map[kAccountId]     = QString::fromStdString(acc.accountId);
    return map;
}

QVariantList PositionAccountBridge::positions() const {
    const_cast<PositionAccountBridge*>(this)->initialize();
    QVariantList list;
    if (!m_initialized) return list;
    for (auto& p : engine::AccountEngine::instance().positions()) {
        QVariantMap item;
        const QString sym = QString::fromStdString(p.symbol);
        item[kSymbol] = sym;
        item["name"]   = StockNameResolver::name(sym);
        item[kSide]   = p.quantity >= 0 ? "LONG" : "SHORT";
        item["type"]   = "stock";
        item[kQuantity]          = static_cast<double>(p.quantity);
        item["availableQuantity"] = static_cast<double>(p.availableQty);
        item["closeableQuantity"] = static_cast<double>(p.availableQty);
        item["costBasis"]         = p.costPrice;
        item[kLastPrice]         = p.lastPrice;
        item["marketValue"]       = p.marketValue;
        item["unrealizedPnl"]     = p.unrealizedPnl;
        list.append(item);
    }
    return list;
}

QVariantList PositionAccountBridge::recentOrderStatuses() const {
    QMutexLocker lock(&m_orderStatusesMutex);
    return m_recentOrderStatuses;
}

void PositionAccountBridge::appendOrderStatus(const QVariantMap& status) {
    bool updated = false;
    {
        QMutexLocker lock(&m_orderStatusesMutex);
        // 去重：按 brokerOrderId 更新已有条目
        const QString id = status.value(kBrokerOrderId).toString();
        for (int i = 0; i < m_recentOrderStatuses.size(); ++i) {
            QVariantMap item = m_recentOrderStatuses[i].toMap();
            if (item.value(kBrokerOrderId).toString() == id) {
                m_recentOrderStatuses[i] = status;
                updated = true;
                break;
            }
        }
        if (!updated) {
            m_recentOrderStatuses.prepend(status);
            if (m_recentOrderStatuses.size() > 50) m_recentOrderStatuses.removeLast();
        }
    }
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
    double varUsage = domain::strategy::RiskManager::estimateVar(exposurePct);

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
    snapshot[kStrategyId] = strategy.value(kStrategyId);

    if (!engine::AccountEngine::instance().initialized()) {
        snapshot[kStatus] = "unavailable";
        snapshot["positions"] = QVariantList();
        snapshot["diagnostics"] = QVariantMap();
        return snapshot;
    }

    auto snap = engine::AccountEngine::instance().snapshot();
    auto& acc = snap.account;
    snapshot["totalAsset"] = acc.totalAsset;
    snapshot["marketValue"] = acc.marketValue;
    snapshot["availableCash"] = acc.availableCash;
    snapshot["realizedPnl"] = acc.realizedPnl;
    snapshot["unrealizedPnl"] = acc.unrealizedPnl;
    snapshot[kStatus] = "ok";

    auto& positions = snap.positions;
    QVariantList posList;
    for (auto& p : positions) {
        QVariantMap item;
        const QString sym = QString::fromStdString(p.symbol);
        item[kSymbol] = sym;
        item["name"]   = StockNameResolver::name(sym);
        item[kSide]     = p.quantity >= 0 ? "LONG" : "SHORT";
        item["type"]     = "stock";
        item[kQuantity]   = static_cast<double>(p.quantity);
        item["costBasis"]  = p.costPrice;
        item[kLastPrice]  = p.lastPrice;
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
