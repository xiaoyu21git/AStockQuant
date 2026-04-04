#include "TradeExecutionService.h"

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

QDateTime currentChinaDateTime()
{
    static const QTimeZone chinaTimeZone("Asia/Shanghai");
    if (chinaTimeZone.isValid()) {
        return QDateTime::currentDateTimeUtc().toTimeZone(chinaTimeZone);
    }
    return QDateTime::currentDateTime();
}

bool isLikelyChinaAStockSymbol(const QString& symbol)
{
    const QString normalized = symbol.trimmed().toUpper();
    return normalized.endsWith(QStringLiteral(".SH"))
        || normalized.endsWith(QStringLiteral(".SZ"))
        || normalized.endsWith(QStringLiteral(".BJ"));
}

bool isLikelyChinaAStockTradingSession()
{
    const QDateTime now = currentChinaDateTime();
    if (!now.isValid()) {
        return true;
    }

    const int dayOfWeek = now.date().dayOfWeek();
    if (dayOfWeek < 1 || dayOfWeek > 5) {
        return false;
    }

    const QTime currentTime = now.time();
    const bool morningSession = currentTime >= QTime(9, 15) && currentTime < QTime(11, 30);
    const bool afternoonSession = currentTime >= QTime(13, 0) && currentTime < QTime(15, 0);
    return morningSession || afternoonSession;
}

bool shouldTreatSubmittedOrderAsPending(const QString& symbol, const QString& status)
{
    return status.trimmed().compare(QStringLiteral("SUBMITTED"), Qt::CaseInsensitive) == 0
        && isLikelyChinaAStockSymbol(symbol)
        && !isLikelyChinaAStockTradingSession();
}

QString outsideTradingSessionMessage()
{
    return QStringLiteral("Order accepted outside trading session and waiting for market open");
}

bool isClosedOrderStatus(const QString& status)
{
    const QString normalized = status.trimmed().toUpper();
    return normalized == QStringLiteral("FILLED") ||
        normalized == QStringLiteral("CANCELLED") ||
        normalized == QStringLiteral("REJECTED");
}

QString normalizeManualOrderType(QString orderType)
{
    orderType = orderType.trimmed().toUpper();
    if (orderType == QStringLiteral("MARKET")) {
        return QStringLiteral("MARKET");
    }
    return QStringLiteral("LIMIT");
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
    return {};
}

QVariantMap findRecentOrderRecord(const QVariantList& recentOrders, const QString& orderId)
{
    for (const QVariant& entry : recentOrders) {
        const QVariantMap orderRecord = entry.toMap();
        if (orderRecord.value(QStringLiteral("orderId")).toString().trimmed() == orderId) {
            return orderRecord;
        }
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

bool TradeExecutionService::submitManualTestOrder(const QString& symbol,
                                                  const QString& side,
                                                  double price,
                                                  qint64 quantity,
                                                  const QString& orderType,
                                                  const QString& strategyId,
                                                  const QString& strategyName)
{
    const QString normalizedSymbol = symbol.trimmed().toUpper();
    const QString normalizedSide = side.trimmed().toUpper();
    const QString normalizedOrderType = normalizeManualOrderType(orderType);
    const qint64 normalizedQuantity = quantity > 0 ? quantity : 100;

    if (normalizedSymbol.isEmpty() ||
        (normalizedSide != QStringLiteral("BUY") && normalizedSide != QStringLiteral("SELL")) ||
        price <= 0.0 ||
        normalizedQuantity < 100 ||
        normalizedQuantity % 100 != 0) {
        qWarning() << "TradeExecutionService: invalid manual test order" << normalizedSymbol << normalizedSide << price << normalizedQuantity;
        return false;
    }

    initialize();

    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        qWarning() << "TradeExecutionService: EventBus not ready for manual test order";
        return false;
    }

    const QString correlationId = QString::fromStdString(foundation::utils::Uuid::generate_v4().to_string());
    return submitBrokerOrder(strategyId,
                             strategyName,
                             normalizedSymbol,
                             normalizedSide,
                             normalizedOrderType,
                             price,
                             normalizedQuantity,
                             correlationId,
                             1.0);
}

bool TradeExecutionService::cancelManualTestOrder(const QString& orderId)
{
    const QString normalizedOrderId = orderId.trimmed();
    if (normalizedOrderId.isEmpty()) {
        qWarning() << "TradeExecutionService: invalid cancel request with empty orderId";
        return false;
    }

    initialize();

    QVariantMap baseOrder;
    {
        QMutexLocker locker(&m_mutex);
        baseOrder = findRecentOrderRecord(m_recentOrders, normalizedOrderId);
    }

    if (isClosedOrderStatus(baseOrder.value(QStringLiteral("status")).toString())) {
        qWarning() << "TradeExecutionService: order is already closed" << normalizedOrderId;
        return false;
    }

    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        qWarning() << "TradeExecutionService: EventBus not ready for cancel request";
        return false;
    }

    if (baseOrder.isEmpty()) {
        baseOrder.insert(QStringLiteral("orderId"), normalizedOrderId);
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
        return false;
    }

    if (!m_brokerApi->cancel_order(normalizedOrderId.toStdString())) {
        qWarning() << "TradeExecutionService: broker cancel request failed" << normalizedOrderId
                   << QString::fromStdString(m_brokerApi->last_error_message());
        return false;
    }

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

    m_riskApprovalSubscription = bus->subscribe("risk.approval",
        [this](const engine::EventFormat& event) {
            handleRiskApproval(event);
        });

    m_eventBusIntegrated = true;
    qDebug() << "TradeExecutionService: EventBus integration initialized";
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
                                              double strength)
{
    Q_UNUSED(strength);

    if (strategyId.isEmpty() || symbol.isEmpty() || side.isEmpty() || price <= 0.0 || quantity <= 0) {
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
                                   QStringLiteral("JUJIN support unavailable, queued as local pending order"));
#else
    QString errorMessage;
    if (!ensureBrokerApiReady(&errorMessage)) {
        qWarning() << "TradeExecutionService:" << errorMessage;
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
                                           : QStringLiteral("%1，已回退为本地待处理委托").arg(errorMessage));
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

    publishOrderRequest(orderRequest, correlationId);

    const std::string orderId = m_brokerApi->place_order(
        symbol.trimmed().toStdString(),
        brokerSide,
        brokerOrderType,
        price,
        static_cast<double>(quantity),
        requestOrderId.toStdString());

    QVariantMap orderStatus = orderRequest;
    orderStatus.insert("updatedAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));

    if (orderId.empty()) {
        const QString brokerError = QString::fromStdString(m_brokerApi->last_error_message());
        if (isDeferredBrokerSubmissionError(brokerError)) {
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
        const QString submittedStatus = shouldTreatSubmittedOrderAsPending(symbol, QStringLiteral("SUBMITTED"))
            ? QStringLiteral("PENDING")
            : QStringLiteral("SUBMITTED");
        orderStatus.insert("status", submittedStatus);
        orderStatus.insert("message", submittedStatus == QStringLiteral("PENDING")
            ? outsideTradingSessionMessage()
            : QStringLiteral("Order submitted to broker runtime"));
    }

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
                                                    const QString& message)
{
    if (strategyId.isEmpty() || symbol.isEmpty() || side.isEmpty() || price <= 0.0 || quantity <= 0) {
        qWarning() << "TradeExecutionService: skip invalid local pending order";
        return false;
    }

    const QString resolvedCorrelationId = correlationId.isEmpty()
        ? QString::fromStdString(foundation::utils::Uuid::generate_v4().to_string())
        : correlationId;

    QVariantMap orderRequest;
    orderRequest.insert("orderId", resolvedCorrelationId);
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
        "order.submit.request",
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

    engine::EventFormat event = engine::EventFormat::create_from_strings(
        engine::EventTypes::ORDER_STATUS,
        "TRADE_EXECUTION_SERVICE",
        0);
    event.correlation_id = correlationId.toStdString();
    event.set("order_id", orderStatus.value("orderId").toString().toStdString());
    event.set("strategy_id", orderStatus.value("strategyId").toString().toStdString());
    event.set("symbol", orderStatus.value("symbol").toString().toStdString());
    event.set("side", orderStatus.value("side").toString().toStdString());
    event.set("price", orderStatus.value("price").toDouble());
    event.set("quantity", static_cast<int64_t>(orderStatus.value("quantity").toLongLong()));
    event.set("order_type", orderStatus.value("orderType").toString().toStdString());
    event.set("status", orderStatus.value("status").toString().toStdString());
    event.set("message", orderStatus.value("message").toString().toStdString());
    event.metadata["order_id"] = orderStatus.value("orderId").toString().toStdString();
    event.metadata["strategy_id"] = orderStatus.value("strategyId").toString().toStdString();
    event.metadata["symbol"] = orderStatus.value("symbol").toString().toStdString();
    event.metadata["side"] = orderStatus.value("side").toString().toStdString();
    event.metadata["order_type"] = orderStatus.value("orderType").toString().toStdString();
    event.metadata["status"] = orderStatus.value("status").toString().toStdString();
    event.metadata["message"] = orderStatus.value("message").toString().toStdString();

    const auto result = bus->publish(event, static_cast<int>(engine::EventPriority::HIGH));
    if (!result) {
        qWarning() << "TradeExecutionService: failed to publish order status" << QString::fromStdString(result.message);
        return;
    }

    emit orderStatusPublished(orderStatus);
    appendRecentOrder(orderStatus);
}

void TradeExecutionService::publishTradeFill(const QVariantMap& tradeFill, const QString& correlationId)
{
    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        return;
    }

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

    const auto result = bus->publish(event, static_cast<int>(engine::EventPriority::HIGH));
    if (!result) {
        qWarning() << "TradeExecutionService: failed to publish trade fill" << QString::fromStdString(result.message);
        return;
    }

    appendRecentOrder(tradeFill);
}

void TradeExecutionService::appendRecentOrder(const QVariantMap& orderRecord)
{
    {
        QMutexLocker locker(&m_mutex);
        const QString orderId = orderRecord.value(QStringLiteral("orderId")).toString().trimmed();
        QVariantMap mergedRecord = orderRecord;

        if (!orderId.isEmpty()) {
            for (int index = 0; index < m_recentOrders.size(); ++index) {
                const QVariantMap existingRecord = m_recentOrders.at(index).toMap();
                if (existingRecord.value(QStringLiteral("orderId")).toString().trimmed() != orderId) {
                    continue;
                }

                mergedRecord = existingRecord;
                for (auto it = orderRecord.constBegin(); it != orderRecord.constEnd(); ++it) {
                    mergedRecord.insert(it.key(), it.value());
                }
                m_recentOrders.removeAt(index);
                break;
            }
        }

        m_recentOrders.push_front(mergedRecord);
        while (m_recentOrders.size() > 50) {
            m_recentOrders.removeLast();
        }
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






