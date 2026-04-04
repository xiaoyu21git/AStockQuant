#include "TradeExecutionService.h"

#include "OrderRecordUtils.h"
#include "OrderRuntimeUtils.h"
#include "RiskConfigService.h"
#include "TradingConnectionConfigService.h"

#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"

#include <QDateTime>
#include <QDebug>
#include <QMutexLocker>
#include <QTimeZone>

#include <cmath>
#include <cstdlib>

#if defined(ASTOCK_ENABLE_JUJIN_MARKET)
#include "JujinApi.h"
#endif

namespace {

constexpr order_runtime::EmptyStatusPolicy kRecentOrderStatusPolicy = order_runtime::EmptyStatusPolicy::KeepEmpty;

QString eventStringValue(const engine::EventFormat& event, const std::string& key)
{
    const auto metadataIt = event.metadata.find(key);
    if (metadataIt != event.metadata.end()) {
        return QString::fromStdString(metadataIt->second).trimmed();
    }

    auto stringValue = event.get<std::string>(key);
    if (stringValue.has_value()) {
        return QString::fromStdString(*stringValue).trimmed();
    }

    auto doubleValue = event.get<double>(key);
    if (doubleValue.has_value()) {
        return QString::number(*doubleValue, 'f', 6);
    }

    return {};
}

double eventDoubleValue(const engine::EventFormat& event, const std::string& key, double fallback = 0.0)
{
    auto doubleValue = event.get<double>(key);
    if (doubleValue.has_value()) {
        return *doubleValue;
    }

    const QString textValue = eventStringValue(event, key);
    if (textValue.isEmpty()) {
        return fallback;
    }

    bool ok = false;
    const double value = textValue.toDouble(&ok);
    return ok ? value : fallback;
}

double configDoubleValue(const QVariantMap& configuration, const QStringList& keys, double fallback)
{
    for (const QString& key : keys) {
        if (!configuration.contains(key)) {
            continue;
        }

        bool ok = false;
        const double value = configuration.value(key).toDouble(&ok);
        if (ok) {
            return value;
        }
    }

    return fallback;
}

qint64 deriveOrderQuantity(double orderSizeLimitWan, double price)
{
    if (price <= 0.0) {
        return 0;
    }

    const double normalizedOrderSizeLimitWan = orderSizeLimitWan > 1.0 ? orderSizeLimitWan : 1.0;
    const double notional = normalizedOrderSizeLimitWan * 10000.0;
    const double boardLots = std::floor(notional / price / 100.0);
    const qint64 quantity = static_cast<qint64>(boardLots) * 100;
    return quantity > 0 ? quantity : 100;
}

bool isPlaceholderAccountId(const QString& accountId)
{
    const QString normalized = accountId.trimmed().toUpper();
    return normalized.isEmpty() || normalized == QStringLiteral("SIM_ACCOUNT");
}

QString readEnvironmentText(const char* name)
{
    if (const char* value = std::getenv(name)) {
        return QString::fromLocal8Bit(value).trimmed();
    }
    return {};
}

bool isDeferredBrokerSubmissionError(const QString& errorText)
{
    const QString normalized = errorText.trimmed().toLower();
    return normalized.contains(QStringLiteral("timed out waiting for sdk callback thread to place order"));
}

bool isPendingConfirmationOrderId(const std::string& orderId)
{
    return orderId.rfind("pending-confirmation-", 0) == 0;
}

QString normalizeManualOrderType(QString orderType)
{
    orderType = orderType.trimmed().toUpper();
    if (orderType == QStringLiteral("MARKET")) {
        return QStringLiteral("MARKET");
    }
    return QStringLiteral("LIMIT");
}

QString normalizeOrderMode(QString mode)
{
    mode = mode.trimmed().toLower();
    if (mode == QStringLiteral("futures") ||
        mode == QStringLiteral("options") ||
        mode == QStringLiteral("margin_buy") ||
        mode == QStringLiteral("margin_sell")) {
        return mode;
    }
    return QStringLiteral("stock");
}

QString normalizeOrderSideText(QString side)
{
    side = side.trimmed().toUpper();
    if (side == QStringLiteral("BUY") || side == QStringLiteral("LONG")) {
        return QStringLiteral("BUY");
    }
    if (side == QStringLiteral("SELL") || side == QStringLiteral("SHORT")) {
        return QStringLiteral("SELL");
    }
    return {};
}

QString normalizePositionEffectText(QString positionEffect)
{
    positionEffect = positionEffect.trimmed().toUpper();
    if (positionEffect == QStringLiteral("1") || positionEffect == QStringLiteral("OPEN")) {
        return QStringLiteral("OPEN");
    }
    if (positionEffect == QStringLiteral("2") || positionEffect == QStringLiteral("CLOSE")) {
        return QStringLiteral("CLOSE");
    }
    return {};
}

QString positionEffectMetadataText(const QString& positionEffect)
{
    if (positionEffect == QStringLiteral("OPEN")) {
        return QStringLiteral("1");
    }
    if (positionEffect == QStringLiteral("CLOSE")) {
        return QStringLiteral("2");
    }
    return {};
}

bool isBoardLotOrderMode(const QString& mode)
{
    return mode == QStringLiteral("stock") ||
        mode == QStringLiteral("margin_buy") ||
        mode == QStringLiteral("margin_sell");
}

void applyOrderContext(QVariantMap* target, const QVariantMap& orderContext)
{
    if (!target) {
        return;
    }

    for (auto it = orderContext.constBegin(); it != orderContext.constEnd(); ++it) {
        const QString textValue = it.value().toString().trimmed();
        if (textValue.isEmpty()) {
            continue;
        }
        target->insert(it.key(), textValue);
    }
}

void applyEventString(engine::EventFormat* event, const char* eventKey, const char* metadataKey, const QString& value)
{
    if (!event) {
        return;
    }

    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    event->set(eventKey, trimmed.toStdString());
    event->metadata[metadataKey] = trimmed.toStdString();
}

void applyOrderContextToEvent(engine::EventFormat* event, const QVariantMap& orderRecord)
{
    applyEventString(event, "client_order_id", "client_order_id", orderRecord.value(QStringLiteral("clientOrderId")).toString());
    applyEventString(event, "broker_order_id", "broker_order_id", orderRecord.value(QStringLiteral("brokerOrderId")).toString());
    applyEventString(event, "type", "type", orderRecord.value(QStringLiteral("type")).toString());
    applyEventString(event, "action", "action", orderRecord.value(QStringLiteral("action")).toString());
    applyEventString(event, "position_effect", "position_effect", orderRecord.value(QStringLiteral("position_effect")).toString());
    applyEventString(event, "position_effect_text", "position_effect_text", orderRecord.value(QStringLiteral("positionEffect")).toString());
    applyEventString(event, "underlying", "underlying", orderRecord.value(QStringLiteral("underlying")).toString());
    applyEventString(event, "option_type", "option_type", orderRecord.value(QStringLiteral("optionType")).toString());
    applyEventString(event, "expiry", "expiry", orderRecord.value(QStringLiteral("expiry")).toString());
}

QVariantMap buildRecentOrderRecordFromEvent(const engine::EventFormat& event)
{
    QVariantMap orderRecord;
    const QString statusOrigin = eventStringValue(event, "status_origin").trimmed().toLower();

    const QString clientOrderId = eventStringValue(event, "client_order_id");
    const QString brokerOrderId = eventStringValue(event, "broker_order_id");
    QString orderId = eventStringValue(event, "order_id");
    if (orderId.isEmpty()) {
        orderId = !clientOrderId.isEmpty() ? clientOrderId : brokerOrderId;
    }

    if (orderId.isEmpty() && clientOrderId.isEmpty() && brokerOrderId.isEmpty()) {
        return {};
    }

    orderRecord.insert(QStringLiteral("orderId"), orderId);
    if (!clientOrderId.isEmpty()) {
        orderRecord.insert(QStringLiteral("clientOrderId"), clientOrderId);
    }
    if (!brokerOrderId.isEmpty()) {
        orderRecord.insert(QStringLiteral("brokerOrderId"), brokerOrderId);
    }
    if (!statusOrigin.isEmpty()) {
        orderRecord.insert(QStringLiteral("statusOrigin"), statusOrigin);
    }

    const QString strategyId = eventStringValue(event, "strategy_id");
    if (!strategyId.isEmpty()) {
        orderRecord.insert(QStringLiteral("strategyId"), strategyId);
    }

    const QString symbol = eventStringValue(event, "symbol").trimmed().toUpper();
    if (!symbol.isEmpty()) {
        orderRecord.insert(QStringLiteral("symbol"), symbol);
    }

    const QString exchange = eventStringValue(event, "exchange").trimmed().toUpper();
    if (!exchange.isEmpty()) {
        orderRecord.insert(QStringLiteral("exchange"), exchange);
    }

    const QString side = eventStringValue(event, "side").trimmed().toUpper();
    if (!side.isEmpty()) {
        orderRecord.insert(QStringLiteral("side"), side);
    }

    const QString orderType = eventStringValue(event, "order_type").trimmed().toUpper();
    if (!orderType.isEmpty()) {
        orderRecord.insert(QStringLiteral("orderType"), orderType);
    }

    const double price = eventDoubleValue(event, "price", 0.0);
    if (price > 0.0) {
        orderRecord.insert(QStringLiteral("price"), price);
    }

    const qint64 quantity = static_cast<qint64>(eventDoubleValue(event, "quantity", eventDoubleValue(event, "total_quantity", 0.0)));
    if (quantity > 0) {
        orderRecord.insert(QStringLiteral("quantity"), quantity);
    }

    const qint64 filledQuantity = static_cast<qint64>(eventDoubleValue(
        event,
        "filled_quantity",
        eventDoubleValue(event, "cumulative_filled_quantity", eventDoubleValue(event, "fill_quantity", 0.0))));
    if (filledQuantity > 0) {
        orderRecord.insert(QStringLiteral("filledQuantity"), filledQuantity);
    }

    const double filledNotional = eventDoubleValue(event, "filled_notional", 0.0);
    if (filledNotional > 0.0) {
        orderRecord.insert(QStringLiteral("filledNotional"), filledNotional);
    }

    const QString status = order_runtime::resolveOrderStatusFromProgress(eventStringValue(event, "status"),
                                                                        quantity,
                                                                        filledQuantity,
                                                                        kRecentOrderStatusPolicy);
    if (!status.isEmpty()) {
        orderRecord.insert(QStringLiteral("status"), status);
    }

    const QString message = eventStringValue(event, "message");
    if (!message.isEmpty()) {
        orderRecord.insert(QStringLiteral("message"), message);
    }

    const QString createdAt = eventStringValue(event, "created_at");
    if (!createdAt.isEmpty()) {
        orderRecord.insert(QStringLiteral("createdAt"), createdAt);
    }

    const QString updatedAt = eventStringValue(event, "updated_at");
    if (!updatedAt.isEmpty()) {
        orderRecord.insert(QStringLiteral("updatedAt"), updatedAt);
    } else if (!createdAt.isEmpty()) {
        orderRecord.insert(QStringLiteral("updatedAt"), createdAt);
    }

    const QString type = eventStringValue(event, "type").trimmed().toLower();
    if (!type.isEmpty()) {
        orderRecord.insert(QStringLiteral("type"), type);
    }

    const QString action = eventStringValue(event, "action").trimmed();
    if (!action.isEmpty()) {
        orderRecord.insert(QStringLiteral("action"), action);
    }

    const QString positionEffect = eventStringValue(event, "position_effect_text").trimmed().toUpper();
    if (!positionEffect.isEmpty()) {
        orderRecord.insert(QStringLiteral("positionEffect"), positionEffect);
    }

    const QString underlying = eventStringValue(event, "underlying").trimmed().toUpper();
    if (!underlying.isEmpty()) {
        orderRecord.insert(QStringLiteral("underlying"), underlying);
    }

    const QString optionType = eventStringValue(event, "option_type").trimmed().toLower();
    if (!optionType.isEmpty()) {
        orderRecord.insert(QStringLiteral("optionType"), optionType);
    }

    const QString expiry = eventStringValue(event, "expiry").trimmed();
    if (!expiry.isEmpty()) {
        orderRecord.insert(QStringLiteral("expiry"), expiry);
    }

    return orderRecord;
}

QString exchangeFromSymbol(const QString& symbol)
{
    const QString normalized = symbol.trimmed().toUpper();
    const int dot = normalized.lastIndexOf('.');
    if (dot >= 0 && dot + 1 < normalized.length()) {
        const QString suffix = normalized.mid(dot + 1);
        if (suffix == QStringLiteral("SH")) {
            return QStringLiteral("SHSE");
        }
        if (suffix == QStringLiteral("SZ")) {
            return QStringLiteral("SZSE");
        }
        if (suffix == QStringLiteral("BJ")) {
            return QStringLiteral("BSE");
        }
        return suffix;
    }
    if (normalized.startsWith(QStringLiteral("SHSE."))) {
        return QStringLiteral("SHSE");
    }
    if (normalized.startsWith(QStringLiteral("SZSE."))) {
        return QStringLiteral("SZSE");
    }
    if (normalized.startsWith(QStringLiteral("BSE."))) {
        return QStringLiteral("BSE");
    }
    if (normalized.startsWith(QStringLiteral("CFFEX."))) {
        return QStringLiteral("CFFEX");
    }
    if (normalized.startsWith(QStringLiteral("SHFE."))) {
        return QStringLiteral("SHFE");
    }
    if (normalized.startsWith(QStringLiteral("DCE."))) {
        return QStringLiteral("DCE");
    }
    if (normalized.startsWith(QStringLiteral("CZCE."))) {
        return QStringLiteral("CZCE");
    }
    if (normalized.startsWith(QStringLiteral("INE."))) {
        return QStringLiteral("INE");
    }
    if (normalized.startsWith(QStringLiteral("GFEX."))) {
        return QStringLiteral("GFEX");
    }
    return {};
}

} // namespace

TradeExecutionService* TradeExecutionService::m_instance = nullptr;
QMutex TradeExecutionService::m_instanceMutex;

TradeExecutionService* TradeExecutionService::instance()
{
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        m_instance = new TradeExecutionService();
    }
    return m_instance;
}

TradeExecutionService::TradeExecutionService(QObject* parent)
    : QObject(parent)
    , m_initialized(false)
    , m_eventBusIntegrated(false)
{
}

void TradeExecutionService::initialize()
{
    QMutexLocker locker(&m_mutex);
    if (m_initialized) {
        return;
    }

    initializeEventBusIntegration();
    m_initialized = true;
    emit initializedChanged();
}

bool TradeExecutionService::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized;
}

QString TradeExecutionService::lastErrorMessage() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastErrorMessage;
}

void TradeExecutionService::updateLastErrorMessage(const QString& message)
{
    const QString normalized = message.trimmed();
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_lastErrorMessage == normalized) {
            return;
        }
        m_lastErrorMessage = normalized;
        changed = true;
    }

    if (changed) {
        emit lastErrorMessageChanged();
    }
}

bool TradeExecutionService::submitBridgeOrder(const QVariantMap& request)
{
    const QString normalizedSymbol = request.value(QStringLiteral("symbol")).toString().trimmed().toUpper();
    const QString normalizedSide = normalizeOrderSideText(request.value(QStringLiteral("side")).toString());
    const QString normalizedOrderType = normalizeManualOrderType(request.value(QStringLiteral("orderType")).toString());
    const QString normalizedMode = normalizeOrderMode(
        request.value(QStringLiteral("mode")).toString().trimmed().isEmpty()
            ? request.value(QStringLiteral("type")).toString()
            : request.value(QStringLiteral("mode")).toString());
    const QString normalizedAction = request.value(QStringLiteral("action")).toString().trimmed();
    const QString normalizedPositionEffect = normalizePositionEffectText(request.value(QStringLiteral("positionEffect")).toString());
    const double normalizedPrice = request.value(QStringLiteral("price")).toDouble();
    const bool isOptionExerciseAction = normalizedAction.compare(QStringLiteral("optionExercise"), Qt::CaseInsensitive) == 0
        || normalizedAction.compare(QStringLiteral("exercise"), Qt::CaseInsensitive) == 0;
    qint64 normalizedQuantity = request.value(QStringLiteral("quantity")).toLongLong();
    if (normalizedQuantity <= 0) {
        normalizedQuantity = isBoardLotOrderMode(normalizedMode) ? 100 : 1;
    }

    const bool invalidBoardLotQuantity = isBoardLotOrderMode(normalizedMode)
        && (normalizedQuantity < 100 || normalizedQuantity % 100 != 0);
    if (normalizedSymbol.isEmpty() ||
        normalizedSide.isEmpty() ||
        (!isOptionExerciseAction && normalizedPrice <= 0.0) ||
        normalizedQuantity <= 0 ||
        invalidBoardLotQuantity) {
        updateLastErrorMessage(QStringLiteral("无效的委托参数"));
        qWarning() << "TradeExecutionService: invalid bridge order"
                   << normalizedSymbol
                   << normalizedMode
                   << normalizedSide
                   << normalizedPrice
                   << normalizedQuantity;
        return false;
    }

    initialize();

    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        updateLastErrorMessage(QStringLiteral("交易事件总线未就绪"));
        qWarning() << "TradeExecutionService: EventBus not ready for bridge order";
        return false;
    }

    QVariantMap orderContext;
    orderContext.insert(QStringLiteral("type"), normalizedMode);
    if (!normalizedAction.isEmpty()) {
        orderContext.insert(QStringLiteral("action"), normalizedAction);
    }
    if (!normalizedPositionEffect.isEmpty()) {
        orderContext.insert(QStringLiteral("positionEffect"), normalizedPositionEffect);
        orderContext.insert(QStringLiteral("position_effect"), positionEffectMetadataText(normalizedPositionEffect));
    }

    const QString underlying = request.value(QStringLiteral("underlying")).toString().trimmed().toUpper();
    if (!underlying.isEmpty()) {
        orderContext.insert(QStringLiteral("underlying"), underlying);
    }
    const QString optionType = request.value(QStringLiteral("optionType")).toString().trimmed().toLower();
    if (!optionType.isEmpty()) {
        orderContext.insert(QStringLiteral("optionType"), optionType);
    }
    const QString expiry = request.value(QStringLiteral("expiry")).toString().trimmed();
    if (!expiry.isEmpty()) {
        orderContext.insert(QStringLiteral("expiry"), expiry);
    }

    std::map<std::string, std::string> runtimeMetadata;
    for (auto it = orderContext.constBegin(); it != orderContext.constEnd(); ++it) {
        const QString textValue = it.value().toString().trimmed();
        if (textValue.isEmpty()) {
            continue;
        }
        if (it.key() == QStringLiteral("optionType")) {
            runtimeMetadata["option_type"] = textValue.toStdString();
            continue;
        }
        runtimeMetadata[it.key().toStdString()] = textValue.toStdString();
    }

    const QString correlationId = QString::fromStdString(foundation::utils::Uuid::generate_v4().to_string());
    const QString strategyId = request.value(QStringLiteral("strategyId")).toString().trimmed().isEmpty()
        ? QStringLiteral("manual_test")
        : request.value(QStringLiteral("strategyId")).toString().trimmed();
    const QString strategyName = request.value(QStringLiteral("strategyName")).toString().trimmed().isEmpty()
        ? QStringLiteral("Manual Test")
        : request.value(QStringLiteral("strategyName")).toString().trimmed();

    return submitBrokerOrder(strategyId,
                             strategyName,
                             normalizedSymbol,
                             normalizedSide,
                             normalizedOrderType,
                             normalizedPrice,
                             normalizedQuantity,
                             correlationId,
                             1.0,
                             orderContext,
                             runtimeMetadata);
}

bool TradeExecutionService::submitManualTestOrder(const QString& symbol,
                                                  const QString& side,
                                                  double price,
                                                  qint64 quantity,
                                                  const QString& orderType,
                                                  const QString& strategyId,
                                                  const QString& strategyName)
{
    QVariantMap request;
    request.insert(QStringLiteral("symbol"), symbol);
    request.insert(QStringLiteral("side"), side);
    request.insert(QStringLiteral("price"), price);
    request.insert(QStringLiteral("quantity"), quantity);
    request.insert(QStringLiteral("orderType"), orderType);
    request.insert(QStringLiteral("mode"), QStringLiteral("stock"));
    request.insert(QStringLiteral("strategyId"), strategyId);
    request.insert(QStringLiteral("strategyName"), strategyName);
    return submitBridgeOrder(request);
}

bool TradeExecutionService::cancelManualTestOrder(const QString& orderId)
{
    const QString normalizedOrderId = orderId.trimmed();
    if (normalizedOrderId.isEmpty()) {
        updateLastErrorMessage(QStringLiteral("撤单请求缺少订单编号"));
        qWarning() << "TradeExecutionService: invalid cancel request with empty orderId";
        return false;
    }

    initialize();

    QVariantMap baseOrder;
    {
        QMutexLocker locker(&m_mutex);
        baseOrder = order_runtime::findOrderRecord(m_recentOrders, normalizedOrderId);
    }

    if (order_runtime::isClosedOrderStatus(baseOrder.value(QStringLiteral("status")).toString(), kRecentOrderStatusPolicy)) {
        updateLastErrorMessage(QStringLiteral("当前订单已结束，不能重复撤单"));
        qWarning() << "TradeExecutionService: order is already closed" << normalizedOrderId;
        return false;
    }

    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        updateLastErrorMessage(QStringLiteral("交易事件总线未就绪"));
        qWarning() << "TradeExecutionService: EventBus not ready for cancel request";
        return false;
    }

    if (baseOrder.isEmpty()) {
        baseOrder.insert(QStringLiteral("orderId"), normalizedOrderId);
        baseOrder.insert(QStringLiteral("clientOrderId"), normalizedOrderId);
        baseOrder.insert(QStringLiteral("strategyId"), QStringLiteral("manual_test"));
        baseOrder.insert(QStringLiteral("strategyName"), QStringLiteral("Manual Test"));
    }

    if (baseOrder.value(QStringLiteral("createdAt")).toString().isEmpty()) {
        baseOrder.insert(QStringLiteral("createdAt"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    }

#if !defined(ASTOCK_ENABLE_JUJIN_MARKET)
    baseOrder.insert(QStringLiteral("status"), QStringLiteral("CANCELLED"));
    baseOrder.insert(QStringLiteral("message"), QStringLiteral("Local test order cancelled"));
    baseOrder.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    publishOrderStatus(baseOrder, normalizedOrderId);
    return true;
#else
    QString errorMessage;
    if (!ensureBrokerApiReady(&errorMessage)) {
        if (isPendingConfirmationOrderId(normalizedOrderId.toStdString())) {
            baseOrder.insert(QStringLiteral("status"), QStringLiteral("CANCELLED"));
            baseOrder.insert(QStringLiteral("message"), QStringLiteral("Queued test order cancelled locally"));
            baseOrder.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
            publishOrderStatus(baseOrder, normalizedOrderId);
            return true;
        }

        qWarning() << "TradeExecutionService:" << errorMessage;
        updateLastErrorMessage(errorMessage);
        return false;
    }

    const QString cancelOrderId = baseOrder.value(QStringLiteral("clientOrderId")).toString().trimmed().isEmpty()
        ? normalizedOrderId
        : baseOrder.value(QStringLiteral("clientOrderId")).toString().trimmed();

    if (!m_brokerApi->cancel_order(cancelOrderId.toStdString())) {
        updateLastErrorMessage(QString::fromStdString(m_brokerApi->last_error_message()));
        qWarning() << "TradeExecutionService: broker cancel request failed" << cancelOrderId
                   << QString::fromStdString(m_brokerApi->last_error_message());
        return false;
    }

    updateLastErrorMessage(QString());
    baseOrder.insert(QStringLiteral("status"), QStringLiteral("PENDING_CANCEL"));
    baseOrder.insert(QStringLiteral("message"), QStringLiteral("Cancel request submitted to broker runtime"));
    baseOrder.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    publishOrderStatus(baseOrder, normalizedOrderId);
    return true;
#endif
}

QVariantList TradeExecutionService::recentOrders() const
{
    QMutexLocker locker(&m_mutex);
    return m_recentOrders;
}

void TradeExecutionService::clearRecentOrders()
{
    {
        QMutexLocker locker(&m_mutex);
        m_recentOrders.clear();
    }

    emit recentOrdersChanged();
}

void TradeExecutionService::initializeEventBusIntegration()
{
    if (m_eventBusIntegrated) {
        return;
    }

    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        qWarning() << "TradeExecutionService: EventBus not ready, skip event integration";
        return;
    }

    m_orderUpdateSubscription = bus->subscribe(engine::EventTypes::TRADING_ORDER_UPDATED,
        [this](const engine::EventFormat& event) {
            handleRuntimeOrderUpdate(event);
        });

    m_tradeFillSubscription = bus->subscribe(engine::EventTypes::ORDER_FILL,
        [this](const engine::EventFormat& event) {
            handleRuntimeTradeFill(event);
        });

    m_riskApprovalSubscription = bus->subscribe(engine::EventTypes::RISK_APPROVAL,
        [this](const engine::EventFormat& event) {
            handleRiskApproval(event);
        });

    m_eventBusIntegrated = true;
    qDebug() << "TradeExecutionService: EventBus integration initialized";
}

void TradeExecutionService::handleRuntimeOrderUpdate(const engine::EventFormat& event)
{
    if (eventStringValue(event, "status_origin").trimmed().compare(QStringLiteral("local_request"), Qt::CaseInsensitive) == 0) {
        return;
    }

    const QVariantMap orderRecord = buildRecentOrderRecordFromEvent(event);
    if (orderRecord.isEmpty()) {
        return;
    }

    appendRecentOrder(orderRecord);
}

void TradeExecutionService::handleRuntimeTradeFill(const engine::EventFormat& event)
{
    if (eventStringValue(event, "status_origin").trimmed().compare(QStringLiteral("local_request"), Qt::CaseInsensitive) == 0) {
        return;
    }

    const QVariantMap orderRecord = buildRecentOrderRecordFromEvent(event);
    if (orderRecord.isEmpty()) {
        return;
    }

    appendRecentOrder(orderRecord);
}

void TradeExecutionService::handleRiskApproval(const engine::EventFormat& event)
{
    const QString strategyId = eventStringValue(event, "strategy_id");
    const QString strategyName = eventStringValue(event, "strategy_name");
    const QString symbol = eventStringValue(event, "symbol").trimmed().toUpper();
    const QString side = eventStringValue(event, "action").trimmed().toUpper();
    const double price = eventDoubleValue(event, "price", 0.0);
    const double strength = eventDoubleValue(event, "strength", 0.0);

    if (strategyId.isEmpty() || symbol.isEmpty() || side.isEmpty() || price <= 0.0) {
        qWarning() << "TradeExecutionService: skip invalid risk approval event";
        return;
    }

    RiskConfigService* riskConfigService = RiskConfigService::instance();
    QVariantMap riskConfiguration = riskConfigService->appliedConfiguration();
    if (riskConfiguration.isEmpty()) {
        riskConfiguration = riskConfigService->currentConfiguration();
    }

    const double orderSizeLimitWan = configDoubleValue(
        riskConfiguration,
        {QStringLiteral("orderSizeLimit"), QStringLiteral("maxOrderSize")},
        100.0);
    const qint64 quantity = deriveOrderQuantity(orderSizeLimitWan, price);
    const QString correlationId = !event.correlation_id.empty()
        ? QString::fromStdString(event.correlation_id)
        : QString::fromStdString(event.id);

    submitBrokerOrder(strategyId,
                      strategyName,
                      symbol,
                      side,
                      QStringLiteral("LIMIT"),
                      price,
                      quantity,
                      correlationId,
                      strength);
}

bool TradeExecutionService::submitBrokerOrder(const QString& strategyId,
                                              const QString& strategyName,
                                              const QString& symbol,
                                              const QString& side,
                                              const QString& orderType,
                                              double price,
                                              qint64 quantity,
                                              const QString& correlationId,
                                              double strength,
                                              const QVariantMap& orderContext,
                                              const std::map<std::string, std::string>& runtimeMetadata)
{
    Q_UNUSED(strength);

    const QString action = orderContext.value(QStringLiteral("action")).toString().trimmed();
    const bool isOptionExerciseAction = action.compare(QStringLiteral("optionExercise"), Qt::CaseInsensitive) == 0
        || action.compare(QStringLiteral("exercise"), Qt::CaseInsensitive) == 0;

    if (strategyId.isEmpty() || symbol.isEmpty() || side.isEmpty() || (!isOptionExerciseAction && price <= 0.0) || quantity <= 0) {
        qWarning() << "TradeExecutionService: skip invalid broker order";
        return false;
    }

#if !defined(ASTOCK_ENABLE_JUJIN_MARKET)
    qWarning() << "TradeExecutionService: real trading is unavailable because JUJIN support is not compiled";
    return submitLocalPendingOrder(strategyId,
                                   strategyName,
                                   symbol,
                                   side,
                                   orderType,
                                   price,
                                   quantity,
                                   correlationId,
                                   QStringLiteral("JUJIN support unavailable, queued as local pending order"),
                                   orderContext);
#else
    QString errorMessage;
    if (!ensureBrokerApiReady(&errorMessage)) {
        qWarning() << "TradeExecutionService:" << errorMessage;
        updateLastErrorMessage(errorMessage);
        return submitLocalPendingOrder(strategyId,
                                       strategyName,
                                       symbol,
                                       side,
                                       orderType,
                                       price,
                                       quantity,
                                       correlationId,
                                       errorMessage.isEmpty()
                                           ? QStringLiteral("Broker unavailable, queued as local pending order")
                                           : QStringLiteral("%1，已回退为本地待处理委托").arg(errorMessage),
                                       orderContext);
    }

    const auto brokerSide = side.trimmed().toUpper() == QStringLiteral("SELL")
        ? thirdparty::OrderSide::SELL
        : thirdparty::OrderSide::BUY;
    const QString normalizedOrderType = normalizeManualOrderType(orderType);
    const auto brokerOrderType = normalizedOrderType == QStringLiteral("MARKET")
        ? thirdparty::OrderType::MARKET
        : thirdparty::OrderType::LIMIT;

    const QString requestOrderId = correlationId.isEmpty()
        ? QString::fromStdString(foundation::utils::Uuid::generate_v4().to_string())
        : correlationId;

    QVariantMap orderRequest;
    orderRequest.insert("orderId", requestOrderId);
    orderRequest.insert("clientOrderId", requestOrderId);
    orderRequest.insert("strategyId", strategyId);
    orderRequest.insert("strategyName", strategyName);
    orderRequest.insert("symbol", symbol.trimmed().toUpper());
    orderRequest.insert("exchange", exchangeFromSymbol(symbol));
    orderRequest.insert("side", side.trimmed().toUpper());
    orderRequest.insert("price", price);
    orderRequest.insert("quantity", quantity);
    orderRequest.insert("orderType", normalizedOrderType);
    orderRequest.insert("requestedNotional", price * static_cast<double>(quantity));
    orderRequest.insert("status", QStringLiteral("REQUESTED"));
    orderRequest.insert("createdAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    applyOrderContext(&orderRequest, orderContext);

    publishOrderRequest(orderRequest, correlationId);

    const std::string orderId = m_brokerApi->place_order(
        symbol.trimmed().toStdString(),
        brokerSide,
        brokerOrderType,
        price,
        static_cast<double>(quantity),
        requestOrderId.toStdString(),
        runtimeMetadata);

    QVariantMap orderStatus = orderRequest;
    orderStatus.insert("updatedAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));

    if (orderId.empty()) {
        const QString brokerError = QString::fromStdString(m_brokerApi->last_error_message());
        if (isDeferredBrokerSubmissionError(brokerError)) {
            updateLastErrorMessage(QString());
            orderStatus.insert("status", QStringLiteral("PENDING"));
            orderStatus.insert("message", QStringLiteral("Order accepted by runtime queue"));
            publishOrderStatus(orderStatus, correlationId);
            return true;
        }

        qWarning() << "TradeExecutionService: broker place_order failed"
                   << "requestOrderId=" << requestOrderId
                   << "symbol=" << symbol
                   << "side=" << side
                   << "orderType=" << normalizedOrderType
                   << "price=" << price
                   << "quantity=" << quantity
                   << "error=" << (brokerError.isEmpty()
                       ? QStringLiteral("Broker rejected order request")
                       : brokerError);

        orderStatus.insert("status", QStringLiteral("REJECTED"));
        orderStatus.insert("message", brokerError.isEmpty()
            ? QStringLiteral("Broker rejected order request")
            : brokerError);
        updateLastErrorMessage(orderStatus.value("message").toString());
        publishOrderStatus(orderStatus, correlationId);
        return false;
    }

    orderRequest.insert("brokerOrderId", QString::fromStdString(orderId));
    orderStatus = orderRequest;
    orderStatus.insert("updatedAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));

    if (isPendingConfirmationOrderId(orderId)) {
        orderStatus.insert("status", QStringLiteral("PENDING"));
        orderStatus.insert("message", QStringLiteral("Order queued in runtime session"));
    } else {
        orderStatus.insert("status", QStringLiteral("SUBMITTED"));
        orderStatus.insert("message", QStringLiteral("Order submitted to broker runtime"));
    }

    updateLastErrorMessage(QString());
    publishOrderStatus(orderStatus, correlationId);
    return true;
#endif
}

bool TradeExecutionService::submitLocalPendingOrder(const QString& strategyId,
                                                    const QString& strategyName,
                                                    const QString& symbol,
                                                    const QString& side,
                                                    const QString& orderType,
                                                    double price,
                                                    qint64 quantity,
                                                    const QString& correlationId,
                                                    const QString& message,
                                                    const QVariantMap& orderContext)
{
    const QString action = orderContext.value(QStringLiteral("action")).toString().trimmed();
    const bool isOptionExerciseAction = action.compare(QStringLiteral("optionExercise"), Qt::CaseInsensitive) == 0
        || action.compare(QStringLiteral("exercise"), Qt::CaseInsensitive) == 0;
    if (strategyId.isEmpty() || symbol.isEmpty() || side.isEmpty() || (!isOptionExerciseAction && price <= 0.0) || quantity <= 0) {
        qWarning() << "TradeExecutionService: skip invalid local pending order";
        return false;
    }

    const QString resolvedCorrelationId = correlationId.isEmpty()
        ? QString::fromStdString(foundation::utils::Uuid::generate_v4().to_string())
        : correlationId;

    QVariantMap orderRequest;
    orderRequest.insert("orderId", resolvedCorrelationId);
    orderRequest.insert("clientOrderId", resolvedCorrelationId);
    orderRequest.insert("strategyId", strategyId);
    orderRequest.insert("strategyName", strategyName);
    orderRequest.insert("symbol", symbol.trimmed().toUpper());
    orderRequest.insert("exchange", exchangeFromSymbol(symbol));
    orderRequest.insert("side", side.trimmed().toUpper());
    orderRequest.insert("price", price);
    orderRequest.insert("quantity", quantity);
    orderRequest.insert("orderType", normalizeManualOrderType(orderType));
    orderRequest.insert("requestedNotional", price * static_cast<double>(quantity));
    orderRequest.insert("status", QStringLiteral("REQUESTED"));
    orderRequest.insert("createdAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    applyOrderContext(&orderRequest, orderContext);

    publishOrderRequest(orderRequest, resolvedCorrelationId);

    QVariantMap orderStatus = orderRequest;
    orderStatus.insert("status", QStringLiteral("SUBMITTED"));
    orderStatus.insert("message", message.isEmpty()
        ? QStringLiteral("Local pending order created")
        : message);
    orderStatus.insert("updatedAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    publishOrderStatus(orderStatus, resolvedCorrelationId);
    return true;
}

bool TradeExecutionService::submitSimulatedOrder(const QString& strategyId,
                                                 const QString& strategyName,
                                                 const QString& symbol,
                                                 const QString& side,
                                                 double price,
                                                 qint64 quantity,
                                                 const QString& correlationId,
                                                 double strength)
{
    if (strategyId.isEmpty() || symbol.isEmpty() || side.isEmpty() || price <= 0.0 || quantity <= 0) {
        qWarning() << "TradeExecutionService: skip invalid simulated order";
        return false;
    }

    RiskConfigService* riskConfigService = RiskConfigService::instance();
    QVariantMap riskConfiguration = riskConfigService->appliedConfiguration();
    if (riskConfiguration.isEmpty()) {
        riskConfiguration = riskConfigService->currentConfiguration();
    }

    const double orderSizeLimitWan = configDoubleValue(
        riskConfiguration,
        {QStringLiteral("orderSizeLimit"), QStringLiteral("maxOrderSize")},
        100.0);
    const double maxPositionPercent = configDoubleValue(
        riskConfiguration,
        {QStringLiteral("maxPositionPercent"), QStringLiteral("maxSinglePositionRatio"), QStringLiteral("positionPercent")},
        15.0);
    const QString orderId = QString::fromStdString(foundation::utils::Uuid::generate_v4().to_string());

    QVariantMap orderRequest;
    orderRequest.insert("orderId", orderId);
    orderRequest.insert("clientOrderId", orderId);
    orderRequest.insert("strategyId", strategyId);
    orderRequest.insert("strategyName", strategyName);
    orderRequest.insert("symbol", symbol.trimmed().toUpper());
    orderRequest.insert("exchange", exchangeFromSymbol(symbol));
    orderRequest.insert("side", side.trimmed().toUpper());
    orderRequest.insert("price", price);
    orderRequest.insert("quantity", quantity);
    orderRequest.insert("orderType", QStringLiteral("LIMIT"));
    orderRequest.insert("requestedNotional", price * static_cast<double>(quantity));
    orderRequest.insert("orderSizeLimitWan", orderSizeLimitWan);
    orderRequest.insert("maxPositionPercent", maxPositionPercent);
    orderRequest.insert("signalStrength", strength);
    orderRequest.insert("status", QStringLiteral("REQUESTED"));
    orderRequest.insert("createdAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));

    publishOrderRequest(orderRequest, correlationId);

    QVariantMap orderStatus = orderRequest;
    orderStatus.insert("status", QStringLiteral("SUBMITTED"));
    orderStatus.insert("message", QStringLiteral("Simulated order request accepted"));
    orderStatus.insert("updatedAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    publishOrderStatus(orderStatus, correlationId);

    QVariantMap tradeFill = orderStatus;
    tradeFill.insert("fillId", QString::fromStdString(foundation::utils::Uuid::generate_v4().to_string()));
    tradeFill.insert("fillPrice", price);
    tradeFill.insert("fillQuantity", quantity);
    tradeFill.insert("filledNotional", price * static_cast<double>(quantity));
    tradeFill.insert("status", QStringLiteral("FILLED"));
    tradeFill.insert("message", QStringLiteral("Simulated trade fill completed"));
    tradeFill.insert("filledAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    publishTradeFill(tradeFill, correlationId);

    return true;
}

void TradeExecutionService::publishOrderRequest(const QVariantMap& orderRequest, const QString& correlationId)
{
    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        return;
    }

    engine::EventFormat event = engine::EventFormat::create_from_strings(
        engine::EventTypes::TRADING_ORDER_SUBMIT_REQUEST,
        "TRADE_EXECUTION_SERVICE",
        0);
    event.correlation_id = correlationId.toStdString();
    event.set("order_id", orderRequest.value("orderId").toString().toStdString());
    event.set("strategy_id", orderRequest.value("strategyId").toString().toStdString());
    event.set("strategy_name", orderRequest.value("strategyName").toString().toStdString());
    event.set("symbol", orderRequest.value("symbol").toString().toStdString());
    event.set("side", orderRequest.value("side").toString().toStdString());
    event.set("price", orderRequest.value("price").toDouble());
    event.set("quantity", static_cast<int64_t>(orderRequest.value("quantity").toLongLong()));
    event.set("order_type", orderRequest.value("orderType").toString().toStdString());
    event.metadata["order_id"] = orderRequest.value("orderId").toString().toStdString();
    event.metadata["strategy_id"] = orderRequest.value("strategyId").toString().toStdString();
    event.metadata["symbol"] = orderRequest.value("symbol").toString().toStdString();
    event.metadata["side"] = orderRequest.value("side").toString().toStdString();
    event.metadata["order_type"] = orderRequest.value("orderType").toString().toStdString();
    event.metadata["status"] = orderRequest.value("status").toString().toStdString();
    event.metadata["event_contract"] = "canonical";
    applyOrderContextToEvent(&event, orderRequest);

    const auto result = bus->publish(event, static_cast<int>(engine::EventPriority::HIGH));
    if (!result) {
        qWarning() << "TradeExecutionService: failed to publish order request" << QString::fromStdString(result.message);
        return;
    }

    emit orderRequestPublished(orderRequest);
    appendRecentOrder(orderRequest);
}

void TradeExecutionService::publishOrderStatus(const QVariantMap& orderStatus, const QString& correlationId)
{
    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        return;
    }

    const QString statusOrigin = orderStatus.value(QStringLiteral("statusOrigin")).toString().trimmed().isEmpty()
        ? QStringLiteral("local_request")
        : orderStatus.value(QStringLiteral("statusOrigin")).toString().trimmed();

    engine::EventFormat event = engine::EventFormat::create_from_strings(
        engine::EventTypes::TRADING_ORDER_UPDATED,
        "TRADE_EXECUTION_SERVICE",
        0);
    event.correlation_id = correlationId.toStdString();
    event.set("order_id", orderStatus.value("orderId").toString().toStdString());
    event.set("strategy_id", orderStatus.value("strategyId").toString().toStdString());
    event.set("symbol", orderStatus.value("symbol").toString().toStdString());
    event.set("side", orderStatus.value("side").toString().toStdString());
    event.set("price", orderStatus.value("price").toDouble());
    event.set("quantity", static_cast<int64_t>(orderStatus.value("quantity").toLongLong()));
    event.set("filled_quantity", static_cast<int64_t>(orderStatus.value("filledQuantity").toLongLong()));
    event.set("filled_notional", orderStatus.value("filledNotional").toDouble());
    event.set("order_type", orderStatus.value("orderType").toString().toStdString());
    event.set("status", orderStatus.value("status").toString().toStdString());
    event.set("message", orderStatus.value("message").toString().toStdString());
    event.set("created_at", orderStatus.value("createdAt").toString().toStdString());
    event.set("updated_at", orderStatus.value("updatedAt").toString().toStdString());
    event.metadata["order_id"] = orderStatus.value("orderId").toString().toStdString();
    event.metadata["strategy_id"] = orderStatus.value("strategyId").toString().toStdString();
    event.metadata["symbol"] = orderStatus.value("symbol").toString().toStdString();
    event.metadata["side"] = orderStatus.value("side").toString().toStdString();
    event.metadata["order_type"] = orderStatus.value("orderType").toString().toStdString();
    event.metadata["status"] = orderStatus.value("status").toString().toStdString();
    event.metadata["message"] = orderStatus.value("message").toString().toStdString();
    event.metadata["status_origin"] = statusOrigin.toStdString();
    event.metadata["event_contract"] = "canonical";
    applyOrderContextToEvent(&event, orderStatus);

    const auto result = bus->publish(event, static_cast<int>(engine::EventPriority::HIGH));
    if (!result) {
        qWarning() << "TradeExecutionService: failed to publish order status" << QString::fromStdString(result.message);
        return;
    }

    QVariantMap recentOrder = orderStatus;
    recentOrder.insert(QStringLiteral("statusOrigin"), statusOrigin);
    emit orderStatusPublished(recentOrder);
    appendRecentOrder(recentOrder);
}

void TradeExecutionService::publishTradeFill(const QVariantMap& tradeFill, const QString& correlationId)
{
    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        return;
    }

    const QString statusOrigin = tradeFill.value(QStringLiteral("statusOrigin")).toString().trimmed().isEmpty()
        ? QStringLiteral("local_request")
        : tradeFill.value(QStringLiteral("statusOrigin")).toString().trimmed();

    engine::EventFormat event = engine::EventFormat::create_from_strings(
        engine::EventTypes::ORDER_FILL,
        "TRADE_EXECUTION_SERVICE",
        0);
    event.correlation_id = correlationId.toStdString();
    event.set("fill_id", tradeFill.value("fillId").toString().toStdString());
    event.set("order_id", tradeFill.value("orderId").toString().toStdString());
    event.set("strategy_id", tradeFill.value("strategyId").toString().toStdString());
    event.set("symbol", tradeFill.value("symbol").toString().toStdString());
    event.set("side", tradeFill.value("side").toString().toStdString());
    event.set("fill_price", tradeFill.value("fillPrice").toDouble());
    event.set("fill_quantity", static_cast<int64_t>(tradeFill.value("fillQuantity").toLongLong()));
    event.set("filled_notional", tradeFill.value("filledNotional").toDouble());
    event.set("status", tradeFill.value("status").toString().toStdString());
    event.metadata["fill_id"] = tradeFill.value("fillId").toString().toStdString();
    event.metadata["order_id"] = tradeFill.value("orderId").toString().toStdString();
    event.metadata["strategy_id"] = tradeFill.value("strategyId").toString().toStdString();
    event.metadata["symbol"] = tradeFill.value("symbol").toString().toStdString();
    event.metadata["side"] = tradeFill.value("side").toString().toStdString();
    event.metadata["status"] = tradeFill.value("status").toString().toStdString();
    event.metadata["status_origin"] = statusOrigin.toStdString();
    applyOrderContextToEvent(&event, tradeFill);

    const auto result = bus->publish(event, static_cast<int>(engine::EventPriority::HIGH));
    if (!result) {
        qWarning() << "TradeExecutionService: failed to publish trade fill" << QString::fromStdString(result.message);
        return;
    }

    QVariantMap recentOrder = tradeFill;
    recentOrder.insert(QStringLiteral("statusOrigin"), statusOrigin);
    appendRecentOrder(recentOrder);
}

void TradeExecutionService::appendRecentOrder(const QVariantMap& orderRecord)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        changed = order_runtime::upsertOrderRecord(&m_recentOrders,
                                                   orderRecord,
                                                   order_runtime::overlayOrderRecord,
                                                   [](const QVariantMap& existingRecord, const QVariantMap& nextRecord) {
                                                       return order_runtime::shouldIgnoreOrderStatusRegression(existingRecord, nextRecord, kRecentOrderStatusPolicy);
                                                   },
                                                   [](const QVariantMap& existingRecord, const QVariantMap& nextRecord) {
                                                       const bool sameStatus = order_runtime::normalizeOrderStatus(existingRecord.value(QStringLiteral("status")).toString(), kRecentOrderStatusPolicy)
                                                           == order_runtime::normalizeOrderStatus(nextRecord.value(QStringLiteral("status")).toString(), kRecentOrderStatusPolicy);
                                                       const bool sameQuantity = existingRecord.value(QStringLiteral("quantity")) == nextRecord.value(QStringLiteral("quantity"));
                                                       const bool sameFilledQuantity = existingRecord.value(QStringLiteral("filledQuantity")) == nextRecord.value(QStringLiteral("filledQuantity"));
                                                       const bool sameMessage = existingRecord.value(QStringLiteral("message")) == nextRecord.value(QStringLiteral("message"));
                                                       const bool sameBrokerOrderId = existingRecord.value(QStringLiteral("brokerOrderId")) == nextRecord.value(QStringLiteral("brokerOrderId"));
                                                       return sameStatus && sameQuantity && sameFilledQuantity && sameMessage && sameBrokerOrderId;
                                                   });
    }

    if (!changed) {
        return;
    }

    emit recentOrdersChanged();
}

#if defined(ASTOCK_ENABLE_JUJIN_MARKET)
bool TradeExecutionService::ensureBrokerApiReady(QString* errorMessage)
{
    TradingConnectionConfigService* configService = TradingConnectionConfigService::instance();
    if (!configService) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Trading connection config service unavailable");
        }
        return false;
    }

    configService->refreshClientProcessStatus();
    const QVariantMap configuration = configService->loadConfiguration();

    if (!configuration.value(QStringLiteral("enabled")).toBool()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Jujin trading connection is disabled");
        }
        return false;
    }

    if (configuration.value(QStringLiteral("readOnly"), true).toBool()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Trading connection is in read-only mode");
        }
        return false;
    }

    const QString token = !configuration.value(QStringLiteral("token")).toString().trimmed().isEmpty()
        ? configuration.value(QStringLiteral("token")).toString().trimmed()
        : readEnvironmentText("ASTOCK_GM_TOKEN");
    if (token.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Jujin token is empty");
        }
        return false;
    }

    const QString configuredAccountId = configuration.value(QStringLiteral("accountId")).toString().trimmed();
    const QString envAccountId = readEnvironmentText("ASTOCK_GM_ACCOUNT_ID");
    const QString accountId = !envAccountId.isEmpty()
        ? envAccountId
        : (isPlaceholderAccountId(configuredAccountId) ? QString() : configuredAccountId);
    Q_UNUSED(accountId);

    if (!configService->clientProcessRunning()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Jujin client process is not running");
        }
        return false;
    }

    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("EventBus is not ready");
        }
        return false;
    }

    thirdparty::JujinApi* sharedApi = engine::get_shared_jujin_api();
    if (!sharedApi) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Shared Jujin trading session is unavailable");
        }
        return false;
    }

    if (!sharedApi->is_connected()) {
        if (errorMessage) {
            const QString brokerDetail = QString::fromStdString(sharedApi->last_error_message()).trimmed();
            *errorMessage = brokerDetail.isEmpty()
                ? QStringLiteral("Shared Jujin trading session is not connected")
                : QStringLiteral("Shared Jujin trading session is not connected: %1").arg(brokerDetail);
        }
        return false;
    }

    m_brokerApi = sharedApi;
    return true;
}
#endif






