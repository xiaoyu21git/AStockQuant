#include "TradeExecutionService.h"

#include "OrderRecordUtils.h"
#include "OrderRuntimeUtils.h"
#include "RiskConfigService.h"
#include "RiskMonitorService.h"
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

    auto intValue = event.get<int64_t>(key);
    if (intValue.has_value()) {
        return QString::number(*intValue);
    }

    return {};
}

double eventDoubleValue(const engine::EventFormat& event, const std::string& key, double fallback = 0.0)
{
    auto doubleValue = event.get<double>(key);
    if (doubleValue.has_value()) {
        return *doubleValue;
    }

    auto intValue = event.get<int64_t>(key);
    if (intValue.has_value()) {
        return static_cast<double>(*intValue);
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

QVariantMap loadTradingConnectionConfiguration()
{
    TradingConnectionConfigService* configService = TradingConnectionConfigService::instance();
    if (!configService) {
        return {};
    }

    return configService->loadConfiguration();
}

QString resolvedBusinessStrategyId(const QVariantMap& request, const QVariantMap& configuration)
{
    const QString requested = request.value(QStringLiteral("strategyId")).toString().trimmed();
    if (!requested.isEmpty()) {
        return requested;
    }

    const QString configured = configuration.value(QStringLiteral("boundStrategyId")).toString().trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }

    return QStringLiteral("manual_test");
}

QString resolvedBusinessStrategyName(const QVariantMap& request,
                                    const QVariantMap& configuration,
                                    const QString& strategyId)
{
    const QString requested = request.value(QStringLiteral("strategyName")).toString().trimmed();
    if (!requested.isEmpty()) {
        return requested;
    }

    const QString configured = configuration.value(QStringLiteral("boundStrategyName")).toString().trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }

    if (strategyId == QStringLiteral("manual_test")) {
        return QStringLiteral("Manual Test");
    }

    return strategyId;
}

QString resolvedGmStrategyId(const QVariantMap& request, const QVariantMap& configuration)
{
    const QString requestedGmStrategyId = request.value(QStringLiteral("gmStrategyId")).toString().trimmed();
    if (!requestedGmStrategyId.isEmpty()) {
        return requestedGmStrategyId;
    }
    const QString requested = request.value(QStringLiteral("runtimeStrategyId")).toString().trimmed();
    if (!requested.isEmpty()) {
        return requested;
    }

    const QString configuredGmStrategyId = configuration.value(QStringLiteral("gmStrategyId")).toString().trimmed();
    if (!configuredGmStrategyId.isEmpty()) {
        return configuredGmStrategyId;
    }

    const QString configured = configuration.value(QStringLiteral("runtimeStrategyId")).toString().trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }

    const QString legacyStrategyId = configuration.value(QStringLiteral("strategyId")).toString().trimmed();
    if (!legacyStrategyId.isEmpty()) {
        return legacyStrategyId;
    }

    return {};
}

QString eventStrategyId(const engine::EventFormat& event)
{
    const QString businessStrategyId = eventStringValue(event, "business_strategy_id");
    return businessStrategyId.isEmpty() ? eventStringValue(event, "strategy_id") : businessStrategyId;
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

bool isCashRepayAction(const QString& action)
{
    const QString normalized = action.trimmed().toLower();
    return normalized == QStringLiteral("repay")
        || normalized == QStringLiteral("cashrepay")
        || normalized == QStringLiteral("creditrepaycash");
}

bool isShareReturnAction(const QString& action)
{
    const QString normalized = action.trimmed().toLower();
    return normalized == QStringLiteral("returnstock")
        || normalized == QStringLiteral("repayshare")
        || normalized == QStringLiteral("creditrepayshare");
}

bool isPriceOptionalAction(const QString& action)
{
    return isCashRepayAction(action) || isShareReturnAction(action);
}

bool isQuantityOptionalAction(const QString& action)
{
    return isCashRepayAction(action);
}

double requestedNotionalValue(double price, qint64 quantity, double cashAmount)
{
    if (cashAmount > 0.0) {
        return cashAmount;
    }
    if (price > 0.0 && quantity > 0) {
        return price * static_cast<double>(quantity);
    }
    return 0.0;
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

void applyEventNumber(engine::EventFormat* event, const char* eventKey, const char* metadataKey, double value)
{
    if (!event || !std::isfinite(value) || value <= 0.0) {
        return;
    }

    event->set(eventKey, value);
    event->metadata[metadataKey] = QString::number(value, 'f', 6).toStdString();
}

void applyOrderContextToEvent(engine::EventFormat* event, const QVariantMap& orderRecord)
{
    applyEventString(event, "client_order_id", "client_order_id", orderRecord.value(QStringLiteral("clientOrderId")).toString());
    applyEventString(event, "broker_order_id", "broker_order_id", orderRecord.value(QStringLiteral("brokerOrderId")).toString());
    applyEventString(event, "business_strategy_id", "business_strategy_id", orderRecord.value(QStringLiteral("strategyId")).toString());
    applyEventString(event, "runtime_strategy_id", "runtime_strategy_id", orderRecord.value(QStringLiteral("runtimeStrategyId")).toString());
    applyEventString(event, "type", "type", orderRecord.value(QStringLiteral("type")).toString());
    applyEventString(event, "action", "action", orderRecord.value(QStringLiteral("action")).toString());
    applyEventString(event, "position_effect", "position_effect", orderRecord.value(QStringLiteral("position_effect")).toString());
    applyEventString(event, "position_effect_text", "position_effect_text", orderRecord.value(QStringLiteral("positionEffect")).toString());
    applyEventString(event, "underlying", "underlying", orderRecord.value(QStringLiteral("underlying")).toString());
    applyEventString(event, "option_type", "option_type", orderRecord.value(QStringLiteral("optionType")).toString());
    applyEventString(event, "expiry", "expiry", orderRecord.value(QStringLiteral("expiry")).toString());
    applyEventNumber(event, "cash_amount", "cash_amount", orderRecord.value(QStringLiteral("cashAmount")).toDouble());
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

    const QString strategyId = eventStrategyId(event);
    if (!strategyId.isEmpty()) {
        orderRecord.insert(QStringLiteral("strategyId"), strategyId);
    }

    const QString gmStrategyId = eventStringValue(event, "runtime_strategy_id");
    if (!gmStrategyId.isEmpty()) {
        orderRecord.insert(QStringLiteral("runtimeStrategyId"), gmStrategyId);
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

    const double cashAmount = eventDoubleValue(event, "cash_amount", 0.0);
    if (cashAmount > 0.0) {
        orderRecord.insert(QStringLiteral("cashAmount"), cashAmount);
    }

    const double requestedNotional = eventDoubleValue(event, "requested_notional", cashAmount);
    if (requestedNotional > 0.0) {
        orderRecord.insert(QStringLiteral("requestedNotional"), requestedNotional);
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
    const bool riskBypassTradingHalt = request.value(QStringLiteral("riskBypassTradingHalt")).toBool();
    const bool isOptionExerciseAction = normalizedAction.compare(QStringLiteral("optionExercise"), Qt::CaseInsensitive) == 0
        || normalizedAction.compare(QStringLiteral("exercise"), Qt::CaseInsensitive) == 0;
    const bool isCashRepayBridgeAction = isCashRepayAction(normalizedAction);
    const double cashAmount = request.value(QStringLiteral("cashAmount")).toDouble();
    qint64 normalizedQuantity = request.value(QStringLiteral("quantity")).toLongLong();
    if (normalizedQuantity <= 0 && !isQuantityOptionalAction(normalizedAction)) {
        normalizedQuantity = isBoardLotOrderMode(normalizedMode) ? 100 : 1;
    }

    const bool invalidBoardLotQuantity = !isCashRepayBridgeAction
        && normalizedPositionEffect != QStringLiteral("CLOSE")
        && isBoardLotOrderMode(normalizedMode)
        && (normalizedQuantity < 100 || normalizedQuantity % 100 != 0);
    if (normalizedSymbol.isEmpty() ||
        normalizedSide.isEmpty() ||
        (!isOptionExerciseAction && !isPriceOptionalAction(normalizedAction) && normalizedPrice <= 0.0) ||
        (!isQuantityOptionalAction(normalizedAction) && normalizedQuantity <= 0) ||
        (isCashRepayBridgeAction && cashAmount <= 0.0) ||
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
    if (cashAmount > 0.0) {
        orderContext.insert(QStringLiteral("cashAmount"), cashAmount);
    }
    if (riskBypassTradingHalt) {
        orderContext.insert(QStringLiteral("riskBypassTradingHalt"), QStringLiteral("true"));
    }
    const QString riskActionSource = request.value(QStringLiteral("riskActionSource")).toString().trimmed();
    if (!riskActionSource.isEmpty()) {
        orderContext.insert(QStringLiteral("riskActionSource"), riskActionSource);
    }
    if (request.contains(QStringLiteral("riskBreakerStage"))) {
        orderContext.insert(QStringLiteral("riskBreakerStage"), request.value(QStringLiteral("riskBreakerStage")).toString());
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
    const QString tradingDate = request.value(QStringLiteral("tradingDate")).toString().trimmed();
    if (!tradingDate.isEmpty()) {
        orderContext.insert(QStringLiteral("tradingDate"), tradingDate);
    }

    const QString correlationId = QString::fromStdString(foundation::utils::Uuid::generate_v4().to_string());
    const QVariantMap tradingConfiguration = loadTradingConnectionConfiguration();
    const QString strategyId = resolvedBusinessStrategyId(request, tradingConfiguration);
    const QString strategyName = resolvedBusinessStrategyName(request, tradingConfiguration, strategyId);
    const QString gmStrategyId = resolvedGmStrategyId(request, tradingConfiguration);

    const double signalStrength = request.value(QStringLiteral("strength")).toDouble() > 0.0
        ? request.value(QStringLiteral("strength")).toDouble()
        : 1.0;
    const QString requestOrderId = correlationId;
    const QString now = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));

    QVariantMap orderRequest;
    orderRequest.insert(QStringLiteral("orderId"), requestOrderId);
    orderRequest.insert(QStringLiteral("clientOrderId"), requestOrderId);
    orderRequest.insert(QStringLiteral("strategyId"), strategyId);
    orderRequest.insert(QStringLiteral("strategyName"), strategyName);
    if (!gmStrategyId.isEmpty()) {
        orderRequest.insert(QStringLiteral("runtimeStrategyId"), gmStrategyId);
    }
    orderRequest.insert(QStringLiteral("symbol"), normalizedSymbol);
    orderRequest.insert(QStringLiteral("exchange"), exchangeFromSymbol(normalizedSymbol));
    orderRequest.insert(QStringLiteral("side"), normalizedSide);
    orderRequest.insert(QStringLiteral("price"), normalizedPrice);
    orderRequest.insert(QStringLiteral("quantity"), normalizedQuantity);
    orderRequest.insert(QStringLiteral("orderType"), normalizedOrderType);
    orderRequest.insert(QStringLiteral("requestedNotional"), requestedNotionalValue(normalizedPrice, normalizedQuantity, cashAmount));
    if (cashAmount > 0.0) {
        orderRequest.insert(QStringLiteral("cashAmount"), cashAmount);
    }
    orderRequest.insert(QStringLiteral("signalStrength"), signalStrength);
    orderRequest.insert(QStringLiteral("status"), QStringLiteral("REQUESTED"));
    orderRequest.insert(QStringLiteral("createdAt"), now);
    applyOrderContext(&orderRequest, orderContext);

    publishOrderRequest(orderRequest, correlationId);

    QVariantMap pendingRiskStatus = orderRequest;
    pendingRiskStatus.insert(QStringLiteral("status"), QStringLiteral("PENDING_RISK"));
    pendingRiskStatus.insert(QStringLiteral("message"), QStringLiteral("委托已进入风控审批"));
    pendingRiskStatus.insert(QStringLiteral("updatedAt"), now);
    pendingRiskStatus.insert(QStringLiteral("statusOrigin"), QStringLiteral("risk_pending"));
    publishOrderStatus(pendingRiskStatus, correlationId);

    QVariantMap riskSignalPayload;
    riskSignalPayload.insert(QStringLiteral("correlationId"), correlationId);
    riskSignalPayload.insert(QStringLiteral("orderId"), requestOrderId);
    riskSignalPayload.insert(QStringLiteral("clientOrderId"), requestOrderId);
    riskSignalPayload.insert(QStringLiteral("strategyId"), strategyId);
    riskSignalPayload.insert(QStringLiteral("businessStrategyId"), strategyId);
    riskSignalPayload.insert(QStringLiteral("strategyName"), strategyName);
    riskSignalPayload.insert(QStringLiteral("runtimeStrategyId"), gmStrategyId);
    riskSignalPayload.insert(QStringLiteral("symbol"), normalizedSymbol);
    riskSignalPayload.insert(QStringLiteral("side"), normalizedSide);
    riskSignalPayload.insert(QStringLiteral("action"), normalizedAction.isEmpty() ? normalizedSide : normalizedAction);
    riskSignalPayload.insert(QStringLiteral("price"), normalizedPrice);
    riskSignalPayload.insert(QStringLiteral("quantity"), normalizedQuantity);
    riskSignalPayload.insert(QStringLiteral("orderType"), normalizedOrderType);
    riskSignalPayload.insert(QStringLiteral("strength"), signalStrength);
    riskSignalPayload.insert(QStringLiteral("riskBypassTradingHalt"), riskBypassTradingHalt);
    if (!riskActionSource.isEmpty()) {
        riskSignalPayload.insert(QStringLiteral("riskActionSource"), riskActionSource);
    }
    if (request.contains(QStringLiteral("riskBreakerStage"))) {
        riskSignalPayload.insert(QStringLiteral("riskBreakerStage"), request.value(QStringLiteral("riskBreakerStage")));
    }
    if (!tradingDate.isEmpty()) {
        riskSignalPayload.insert(QStringLiteral("tradingDate"), tradingDate);
    }
    if (cashAmount > 0.0) {
        riskSignalPayload.insert(QStringLiteral("cashAmount"), cashAmount);
        riskSignalPayload.insert(QStringLiteral("requestedNotional"), cashAmount);
    }
    applyOrderContext(&riskSignalPayload, orderContext);

    RiskMonitorService* riskMonitorService = RiskMonitorService::instance();
    if (!riskMonitorService) {
        const QString errorMessage = QStringLiteral("风控服务未就绪");
        qWarning() << "TradeExecutionService: risk monitor service unavailable for manual order";
        updateLastErrorMessage(errorMessage);

        QVariantMap rejectStatus = orderRequest;
        rejectStatus.insert(QStringLiteral("status"), QStringLiteral("REJECTED"));
        rejectStatus.insert(QStringLiteral("message"), errorMessage);
        rejectStatus.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
        rejectStatus.insert(QStringLiteral("statusOrigin"), QStringLiteral("risk_reject"));
        publishOrderStatus(rejectStatus, correlationId);
        return false;
    }

    const QVariantMap riskDecision = riskMonitorService->reviewTradeSignal(riskSignalPayload, true);
    if (!riskDecision.value(QStringLiteral("approved")).toBool()) {
        const QString reason = riskDecision.value(QStringLiteral("reason")).toString().trimmed();
        updateLastErrorMessage(reason.isEmpty() ? QStringLiteral("风控拒绝委托") : reason);
        return false;
    }

    updateLastErrorMessage(QString());
    return true;
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

    m_riskRejectSubscription = bus->subscribe(engine::EventTypes::RISK_REJECT,
        [this](const engine::EventFormat& event) {
            handleRiskReject(event);
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
    const QString strategyId = eventStrategyId(event);
    const QString strategyName = eventStringValue(event, "strategy_name");
    const QString symbol = eventStringValue(event, "symbol").trimmed().toUpper();
    const QString side = normalizeOrderSideText(
        eventStringValue(event, "side").trimmed().isEmpty()
            ? eventStringValue(event, "action")
            : eventStringValue(event, "side"));
    const QString action = eventStringValue(event, "action").trimmed();
    const double price = eventDoubleValue(event, "price", 0.0);
    const double cashAmount = eventDoubleValue(event, "cash_amount", 0.0);
    const double strength = eventDoubleValue(event, "strength", 0.0);
    qint64 quantity = static_cast<qint64>(eventDoubleValue(event, "quantity", eventDoubleValue(event, "total_quantity", 0.0)));
    const QString orderType = normalizeManualOrderType(eventStringValue(event, "order_type"));
    const bool isOptionExerciseAction = action.compare(QStringLiteral("optionExercise"), Qt::CaseInsensitive) == 0
        || action.compare(QStringLiteral("exercise"), Qt::CaseInsensitive) == 0;
    const bool isCashRepayBridgeAction = isCashRepayAction(action);
    const bool isShareReturnBridgeAction = isShareReturnAction(action);

    if (strategyId.isEmpty()
        || symbol.isEmpty()
        || side.isEmpty()
        || (!isOptionExerciseAction && !isPriceOptionalAction(action) && price <= 0.0)
        || (!isQuantityOptionalAction(action) && quantity <= 0)
        || (isCashRepayBridgeAction && cashAmount <= 0.0)) {
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
    if (quantity <= 0 && !isCashRepayBridgeAction && !isShareReturnBridgeAction) {
        quantity = deriveOrderQuantity(orderSizeLimitWan, price);
    }
    const QString correlationId = !event.correlation_id.empty()
        ? QString::fromStdString(event.correlation_id)
        : QString::fromStdString(event.id);

    const QVariantMap tradingConfiguration = loadTradingConnectionConfiguration();
    const QString boundStrategyId = tradingConfiguration.value(QStringLiteral("boundStrategyId")).toString().trimmed();
    if (!boundStrategyId.isEmpty() && boundStrategyId != strategyId) {
        qWarning() << "TradeExecutionService: skip risk approval for unbound strategy"
                   << strategyId << "bound=" << boundStrategyId;
        return;
    }

    QString gmStrategyId = eventStringValue(event, "runtime_strategy_id");
    if (gmStrategyId.isEmpty()) {
        gmStrategyId = resolvedGmStrategyId(QVariantMap{}, tradingConfiguration);
    }

    QVariantMap orderContext;
    const QString mode = eventStringValue(event, "type").trimmed().toLower();
    if (!mode.isEmpty()) {
        orderContext.insert(QStringLiteral("type"), mode);
    }
    if (!action.isEmpty()) {
        orderContext.insert(QStringLiteral("action"), action);
    }
    const QString positionEffect = normalizePositionEffectText(
        eventStringValue(event, "position_effect_text").trimmed().isEmpty()
            ? eventStringValue(event, "position_effect")
            : eventStringValue(event, "position_effect_text"));
    if (!positionEffect.isEmpty()) {
        orderContext.insert(QStringLiteral("positionEffect"), positionEffect);
        orderContext.insert(QStringLiteral("position_effect"), positionEffectMetadataText(positionEffect));
    }
    const QString underlying = eventStringValue(event, "underlying").trimmed().toUpper();
    if (!underlying.isEmpty()) {
        orderContext.insert(QStringLiteral("underlying"), underlying);
    }
    const QString optionType = eventStringValue(event, "option_type").trimmed().toLower();
    if (!optionType.isEmpty()) {
        orderContext.insert(QStringLiteral("optionType"), optionType);
    }
    const QString expiry = eventStringValue(event, "expiry").trimmed();
    if (!expiry.isEmpty()) {
        orderContext.insert(QStringLiteral("expiry"), expiry);
    }
    if (cashAmount > 0.0) {
        orderContext.insert(QStringLiteral("cashAmount"), cashAmount);
    }

    submitBrokerOrder(strategyId,
                      strategyName,
                      gmStrategyId,
                      symbol,
                      side,
                      orderType,
                      price,
                      quantity,
                      correlationId,
                      strength,
                      orderContext);
}

void TradeExecutionService::handleRiskReject(const engine::EventFormat& event)
{
    const QString correlationId = !event.correlation_id.empty()
        ? QString::fromStdString(event.correlation_id)
        : QString::fromStdString(event.id);
    const QString reason = eventStringValue(event, "reason").trimmed().isEmpty()
        ? QStringLiteral("风控拒绝委托")
        : eventStringValue(event, "reason").trimmed();

    QVariantMap orderStatus = buildRecentOrderRecordFromEvent(event);
    if (orderStatus.isEmpty()) {
        orderStatus.insert(QStringLiteral("orderId"), correlationId);
        orderStatus.insert(QStringLiteral("clientOrderId"), correlationId);
    }

    if (orderStatus.value(QStringLiteral("strategyId")).toString().trimmed().isEmpty()) {
        orderStatus.insert(QStringLiteral("strategyId"), eventStrategyId(event));
    }
    if (orderStatus.value(QStringLiteral("strategyName")).toString().trimmed().isEmpty()) {
        orderStatus.insert(QStringLiteral("strategyName"), eventStringValue(event, "strategy_name"));
    }
    if (orderStatus.value(QStringLiteral("runtimeStrategyId")).toString().trimmed().isEmpty()) {
        orderStatus.insert(QStringLiteral("runtimeStrategyId"), eventStringValue(event, "runtime_strategy_id"));
    }
    if (orderStatus.value(QStringLiteral("symbol")).toString().trimmed().isEmpty()) {
        orderStatus.insert(QStringLiteral("symbol"), eventStringValue(event, "symbol").trimmed().toUpper());
    }
    if (orderStatus.value(QStringLiteral("side")).toString().trimmed().isEmpty()) {
        orderStatus.insert(QStringLiteral("side"), normalizeOrderSideText(
            eventStringValue(event, "side").trimmed().isEmpty()
                ? eventStringValue(event, "action")
                : eventStringValue(event, "side")));
    }
    if (!orderStatus.contains(QStringLiteral("price"))) {
        orderStatus.insert(QStringLiteral("price"), eventDoubleValue(event, "price", 0.0));
    }
    if (!orderStatus.contains(QStringLiteral("quantity"))) {
        orderStatus.insert(QStringLiteral("quantity"), static_cast<qint64>(eventDoubleValue(event, "quantity", 0.0)));
    }
    const double cashAmount = eventDoubleValue(event, "cash_amount", 0.0);
    if (cashAmount > 0.0) {
        orderStatus.insert(QStringLiteral("cashAmount"), cashAmount);
        orderStatus.insert(QStringLiteral("requestedNotional"), cashAmount);
    }
    if (orderStatus.value(QStringLiteral("orderType")).toString().trimmed().isEmpty()) {
        orderStatus.insert(QStringLiteral("orderType"), normalizeManualOrderType(eventStringValue(event, "order_type")));
    }
    if (orderStatus.value(QStringLiteral("createdAt")).toString().trimmed().isEmpty()) {
        orderStatus.insert(QStringLiteral("createdAt"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    }

    orderStatus.insert(QStringLiteral("status"), QStringLiteral("REJECTED"));
    orderStatus.insert(QStringLiteral("message"), reason);
    orderStatus.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    orderStatus.insert(QStringLiteral("statusOrigin"), QStringLiteral("risk_reject"));

    const QString mode = eventStringValue(event, "type").trimmed().toLower();
    if (!mode.isEmpty()) {
        orderStatus.insert(QStringLiteral("type"), mode);
    }
    const QString action = eventStringValue(event, "action").trimmed();
    if (!action.isEmpty()) {
        orderStatus.insert(QStringLiteral("action"), action);
    }
    const QString positionEffect = normalizePositionEffectText(
        eventStringValue(event, "position_effect_text").trimmed().isEmpty()
            ? eventStringValue(event, "position_effect")
            : eventStringValue(event, "position_effect_text"));
    if (!positionEffect.isEmpty()) {
        orderStatus.insert(QStringLiteral("positionEffect"), positionEffect);
        orderStatus.insert(QStringLiteral("position_effect"), positionEffectMetadataText(positionEffect));
    }
    const QString underlying = eventStringValue(event, "underlying").trimmed().toUpper();
    if (!underlying.isEmpty()) {
        orderStatus.insert(QStringLiteral("underlying"), underlying);
    }
    const QString optionType = eventStringValue(event, "option_type").trimmed().toLower();
    if (!optionType.isEmpty()) {
        orderStatus.insert(QStringLiteral("optionType"), optionType);
    }
    const QString expiry = eventStringValue(event, "expiry").trimmed();
    if (!expiry.isEmpty()) {
        orderStatus.insert(QStringLiteral("expiry"), expiry);
    }

    publishOrderStatus(orderStatus, correlationId);
    updateLastErrorMessage(reason);
}

bool TradeExecutionService::submitBrokerOrder(const QString& strategyId,
                                              const QString& strategyName,
                                              const QString& gmStrategyId,
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
    const bool isCashRepayBridgeAction = isCashRepayAction(action);
    const double cashAmount = orderContext.value(QStringLiteral("cashAmount")).toDouble();

    if (strategyId.isEmpty()
        || symbol.isEmpty()
        || side.isEmpty()
        || (!isOptionExerciseAction && !isPriceOptionalAction(action) && price <= 0.0)
        || (!isQuantityOptionalAction(action) && quantity <= 0)
        || (isCashRepayBridgeAction && cashAmount <= 0.0)) {
        qWarning() << "TradeExecutionService: skip invalid broker order";
        return false;
    }

#if !defined(ASTOCK_ENABLE_JUJIN_MARKET)
    qWarning() << "TradeExecutionService: real trading is unavailable because JUJIN support is not compiled";
    return submitLocalPendingOrder(strategyId,
                                   strategyName,
                                   gmStrategyId,
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
                                       gmStrategyId,
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
    if (!gmStrategyId.isEmpty()) {
        orderRequest.insert("runtimeStrategyId", gmStrategyId);
    }
    orderRequest.insert("symbol", symbol.trimmed().toUpper());
    orderRequest.insert("exchange", exchangeFromSymbol(symbol));
    orderRequest.insert("side", side.trimmed().toUpper());
    orderRequest.insert("price", price);
    orderRequest.insert("quantity", quantity);
    orderRequest.insert("orderType", normalizedOrderType);
    orderRequest.insert("requestedNotional", requestedNotionalValue(price, quantity, cashAmount));
    if (cashAmount > 0.0) {
        orderRequest.insert("cashAmount", cashAmount);
    }
    orderRequest.insert("status", QStringLiteral("REQUESTED"));
    orderRequest.insert("createdAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    applyOrderContext(&orderRequest, orderContext);

    publishOrderRequest(orderRequest, correlationId);

    std::map<std::string, std::string> brokerMetadata = runtimeMetadata;
    for (auto it = orderContext.constBegin(); it != orderContext.constEnd(); ++it) {
        const QString textValue = it.value().toString().trimmed();
        if (!textValue.isEmpty()) {
            brokerMetadata[it.key().toStdString()] = textValue.toStdString();
        }
    }
    brokerMetadata["strategy_id"] = strategyId.toStdString();
    brokerMetadata["business_strategy_id"] = strategyId.toStdString();
    if (!gmStrategyId.isEmpty()) {
        brokerMetadata["runtime_strategy_id"] = gmStrategyId.toStdString();
    }

    const std::string orderId = m_brokerApi->place_order(
        symbol.trimmed().toStdString(),
        brokerSide,
        brokerOrderType,
        price,
        static_cast<double>(quantity),
        requestOrderId.toStdString(),
        brokerMetadata);

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
                                                    const QString& gmStrategyId,
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
    const double cashAmount = orderContext.value(QStringLiteral("cashAmount")).toDouble();
    if (strategyId.isEmpty()
        || symbol.isEmpty()
        || side.isEmpty()
        || (!isOptionExerciseAction && !isPriceOptionalAction(action) && price <= 0.0)
        || (!isQuantityOptionalAction(action) && quantity <= 0)
        || (isCashRepayAction(action) && cashAmount <= 0.0)) {
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
    if (!gmStrategyId.isEmpty()) {
        orderRequest.insert("runtimeStrategyId", gmStrategyId);
    }
    orderRequest.insert("symbol", symbol.trimmed().toUpper());
    orderRequest.insert("exchange", exchangeFromSymbol(symbol));
    orderRequest.insert("side", side.trimmed().toUpper());
    orderRequest.insert("price", price);
    orderRequest.insert("quantity", quantity);
    orderRequest.insert("orderType", normalizeManualOrderType(orderType));
    orderRequest.insert("requestedNotional", requestedNotionalValue(price, quantity, cashAmount));
    if (cashAmount > 0.0) {
        orderRequest.insert("cashAmount", cashAmount);
    }
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
                                                 const QString& gmStrategyId,
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
    if (!gmStrategyId.isEmpty()) {
        orderRequest.insert("runtimeStrategyId", gmStrategyId);
    }
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
    if (!orderRequest.value("runtimeStrategyId").toString().trimmed().isEmpty()) {
        event.set("runtime_strategy_id", orderRequest.value("runtimeStrategyId").toString().toStdString());
        event.metadata["runtime_strategy_id"] = orderRequest.value("runtimeStrategyId").toString().toStdString();
    }
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
    if (!orderStatus.value("runtimeStrategyId").toString().trimmed().isEmpty()) {
        event.set("runtime_strategy_id", orderStatus.value("runtimeStrategyId").toString().toStdString());
        event.metadata["runtime_strategy_id"] = orderStatus.value("runtimeStrategyId").toString().toStdString();
    }
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
    if (!tradeFill.value("runtimeStrategyId").toString().trimmed().isEmpty()) {
        event.set("runtime_strategy_id", tradeFill.value("runtimeStrategyId").toString().toStdString());
        event.metadata["runtime_strategy_id"] = tradeFill.value("runtimeStrategyId").toString().toStdString();
    }
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






