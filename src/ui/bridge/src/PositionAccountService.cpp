#include "PositionAccountService.h"

#include "MarketDataService.h"
#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"

#include <QDateTime>
#include <QMetaObject>
#include <QPointer>
#include <QMutexLocker>
#include <QDebug>

namespace {

constexpr bool kDisablePositionAccountUiSignals = false;
constexpr bool kDisablePositionAccountEventBusSubscriptions = false;

template <typename Service, typename Func>
void invokeOnMainThread(Service* service, Func&& func)
{
    QPointer<Service> safeService(service);
    QMetaObject::invokeMethod(service, [safeService, fn = std::forward<Func>(func)]() mutable {
        if (safeService) {
            fn(safeService.data());
        }
    }, Qt::QueuedConnection);
}

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

QVariantList hashValuesToList(const QHash<QString, QVariantMap>& positions)
{
    QVariantList result;
    result.reserve(positions.size());
    for (auto it = positions.constBegin(); it != positions.constEnd(); ++it) {
        result.push_back(it.value());
    }
    return result;
}

QString normalizeOrderSymbol(QString symbol)
{
    symbol = symbol.trimmed().toUpper();
    if (symbol.startsWith(QStringLiteral("SHSE."))) {
        return symbol.mid(5) + QStringLiteral(".SH");
    }
    if (symbol.startsWith(QStringLiteral("SZSE."))) {
        return symbol.mid(5) + QStringLiteral(".SZ");
    }
    if (symbol.startsWith(QStringLiteral("BSE."))) {
        return symbol.mid(4) + QStringLiteral(".BJ");
    }
    return symbol;
}

QString normalizeOrderSide(QString side)
{
    side = side.trimmed().toUpper();
    if (side == QStringLiteral("1") || side == QStringLiteral("BUY") || side == QStringLiteral("LONG")) {
        return QStringLiteral("BUY");
    }
    if (side == QStringLiteral("2") || side == QStringLiteral("SELL") || side == QStringLiteral("SHORT")) {
        return QStringLiteral("SELL");
    }
    return side;
}

QString normalizeOrderStatus(QString status)
{
    status = status.trimmed().toUpper();
    if (status.isEmpty()) {
        return QStringLiteral("PENDING");
    }

    if (status == QStringLiteral("PARTIALLY_FILLED")) {
        return QStringLiteral("PARTIAL_FILLED");
    }

    bool ok = false;
    const double numericStatus = status.toDouble(&ok);
    if (ok) {
        const int code = static_cast<int>(numericStatus);
        switch (code) {
        case 0:
            return QStringLiteral("PENDING");
        case 1:
        case 10:
        case 13:
            return QStringLiteral("SUBMITTED");
        case 2:
            return QStringLiteral("PARTIAL_FILLED");
        case 3:
            return QStringLiteral("FILLED");
        case 4:
        case 5:
        case 12:
            return QStringLiteral("CANCELLED");
        case 8:
            return QStringLiteral("REJECTED");
        default:
            return QStringLiteral("PENDING");
        }
    }

    if (status == QStringLiteral("NEW") || status == QStringLiteral("PENDINGNEW")) {
        return QStringLiteral("SUBMITTED");
    }
    return status;
}

int orderStatusPhase(const QString& status)
{
    const QString normalized = normalizeOrderStatus(status);
    if (normalized == QStringLiteral("REQUESTED")) {
        return 0;
    }
    if (normalized == QStringLiteral("PENDING") || normalized == QStringLiteral("SUBMITTED")) {
        return 1;
    }
    if (normalized == QStringLiteral("PARTIAL_FILLED")) {
        return 2;
    }
    if (normalized == QStringLiteral("CANCELLED") || normalized == QStringLiteral("REJECTED")) {
        return 3;
    }
    if (normalized == QStringLiteral("FILLED")) {
        return 4;
    }
    return 0;
}

bool shouldIgnoreOrderStatusRegression(const QVariantMap& existingStatus, const QVariantMap& nextStatus)
{
    const QString existingNormalized = normalizeOrderStatus(existingStatus.value("status").toString());
    const QString nextNormalized = normalizeOrderStatus(nextStatus.value("status").toString());
    const qint64 existingFilledQuantity = existingStatus.value("filledQuantity").toLongLong();
    const qint64 nextFilledQuantity = nextStatus.value("filledQuantity").toLongLong();
    const int existingPhase = orderStatusPhase(existingNormalized);
    const int nextPhase = orderStatusPhase(nextNormalized);

    if (existingFilledQuantity > nextFilledQuantity) {
        return true;
    }

    if (existingPhase > nextPhase && existingFilledQuantity >= nextFilledQuantity) {
        return true;
    }

    return false;
}

} // namespace

PositionAccountService* PositionAccountService::m_instance = nullptr;
QMutex PositionAccountService::m_instanceMutex;

PositionAccountService* PositionAccountService::instance()
{
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        m_instance = new PositionAccountService();
    }
    return m_instance;
}

PositionAccountService::PositionAccountService(QObject* parent)
    : QObject(parent)
    , m_initialized(false)
    , m_eventBusIntegrated(false)
{
    m_accountSnapshot.insert("accountId", QStringLiteral("SIM_ACCOUNT"));
    m_accountSnapshot.insert("availableCash", 1000000.0);
    m_accountSnapshot.insert("marketValue", 0.0);
    m_accountSnapshot.insert("realizedPnl", 0.0);
    m_accountSnapshot.insert("unrealizedPnl", 0.0);
    m_accountSnapshot.insert("totalAsset", 1000000.0);
    m_accountSnapshot.insert("updatedAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
}

void PositionAccountService::initialize()
{
    QMutexLocker locker(&m_mutex);
    if (m_initialized) {
        return;
    }

    initializeEventBusIntegration();
    m_initialized = true;
    emit initializedChanged();
}

bool PositionAccountService::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized;
}

QVariantList PositionAccountService::positions() const
{
    QMutexLocker locker(&m_mutex);
    return hashValuesToList(m_positionsBySymbol);
}

QVariantMap PositionAccountService::accountSnapshot() const
{
    QMutexLocker locker(&m_mutex);
    return m_accountSnapshot;
}

QVariantList PositionAccountService::recentOrderStatuses() const
{
    QMutexLocker locker(&m_mutex);
    return m_recentOrderStatuses;
}

void PositionAccountService::initializeEventBusIntegration()
{
    if (m_eventBusIntegrated) {
        return;
    }

    if (kDisablePositionAccountEventBusSubscriptions) {
        qWarning() << "PositionAccountService: EventBus subscriptions suppressed for diagnostics";
        m_eventBusIntegrated = true;
        return;
    }

    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        qWarning() << "PositionAccountService: EventBus not ready, skip event integration";
        return;
    }

    m_orderStatusSubscription = bus->subscribe("trading.order.updated",
        [this](const engine::EventFormat& event) {
            const engine::EventFormat queuedEvent = event;
            invokeOnMainThread(this,
                [queuedEvent](PositionAccountService* service) {
                    service->handleOrderStatus(queuedEvent);
                });
        });

    m_tradeFillSubscription = bus->subscribe(engine::EventTypes::ORDER_FILL,
        [this](const engine::EventFormat& event) {
            const engine::EventFormat queuedEvent = event;
            invokeOnMainThread(this,
                [queuedEvent](PositionAccountService* service) {
                    service->handleTradeFill(queuedEvent);
                });
        });

    m_positionSubscription = bus->subscribe("trading.position.updated",
        [this](const engine::EventFormat& event) {
            const engine::EventFormat queuedEvent = event;
            invokeOnMainThread(this,
                [queuedEvent](PositionAccountService* service) {
                    service->handlePositionEvent(queuedEvent);
                });
        });

    m_accountSubscription = bus->subscribe("trading.account.updated",
        [this](const engine::EventFormat& event) {
            const engine::EventFormat queuedEvent = event;
            invokeOnMainThread(this,
                [queuedEvent](PositionAccountService* service) {
                    service->handleAccountEvent(queuedEvent);
                });
        });

    m_eventBusIntegrated = true;
    qDebug() << "PositionAccountService: EventBus integration initialized";
}

void PositionAccountService::handleOrderStatus(const engine::EventFormat& event)
{
    QVariantMap orderStatus;
    const qint64 quantity = static_cast<qint64>(eventDoubleValue(event, "quantity", 0.0));
    qint64 filledQuantity = static_cast<qint64>(eventDoubleValue(event, "filled_quantity", 0.0));
    const QString normalizedStatus = normalizeOrderStatus(eventStringValue(event, "status"));
    if (normalizedStatus == QStringLiteral("FILLED") && filledQuantity <= 0 && quantity > 0) {
        filledQuantity = quantity;
    }

    orderStatus.insert("orderId", eventStringValue(event, "order_id"));
    orderStatus.insert("strategyId", eventStringValue(event, "strategy_id"));
    const QString symbol = normalizeOrderSymbol(eventStringValue(event, "symbol"));
    const QVariantMap instrumentInfo = MarketDataService::instance() ? MarketDataService::instance()->resolveInstrument(symbol) : QVariantMap{};
    orderStatus.insert("symbol", symbol);
    orderStatus.insert("name", eventStringValue(event, "name").isEmpty() ? instrumentInfo.value(QStringLiteral("name")).toString() : eventStringValue(event, "name"));
    orderStatus.insert("exchange", eventStringValue(event, "exchange").isEmpty() ? instrumentInfo.value(QStringLiteral("exchange")).toString() : eventStringValue(event, "exchange"));
    orderStatus.insert("side", normalizeOrderSide(eventStringValue(event, "side")));
    orderStatus.insert("price", eventDoubleValue(event, "price", 0.0));
    orderStatus.insert("quantity", quantity);
    orderStatus.insert("filledQuantity", filledQuantity);
    orderStatus.insert("filledNotional", eventDoubleValue(event, "filled_notional", 0.0));
    orderStatus.insert("status", normalizedStatus);
    orderStatus.insert("message", eventStringValue(event, "message"));
    const QString createdAtText = eventStringValue(event, "created_at");
    const QString updatedAtText = eventStringValue(event, "updated_at");
    orderStatus.insert("createdAt", createdAtText);
    orderStatus.insert("updatedAt", updatedAtText);

    if (orderStatus.value("createdAt").toString().isEmpty()) {
        orderStatus.insert("createdAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    }
    if (orderStatus.value("updatedAt").toString().isEmpty()) {
        orderStatus.insert("updatedAt", orderStatus.value("createdAt"));
    }

    if (orderStatus.value("status").toString() == QStringLiteral("REJECTED")) {
        qWarning() << "PositionAccountService: rejected order status"
                   << orderStatus.value("orderId").toString()
                   << orderStatus.value("symbol").toString()
                   << orderStatus.value("message").toString();
    }

    appendOrderStatus(orderStatus);
}

void PositionAccountService::handleTradeFill(const engine::EventFormat& event)
{
    const QString orderId = eventStringValue(event, "order_id");
    const QString execId = eventStringValue(event, "exec_id");
    const QString symbol = normalizeOrderSymbol(eventStringValue(event, "symbol"));
    const QString side = normalizeOrderSide(eventStringValue(event, "side"));
    const double fillPrice = eventDoubleValue(event, "fill_price", eventDoubleValue(event, "price", 0.0));
    const qint64 fillQuantity = static_cast<qint64>(eventDoubleValue(event, "fill_quantity", eventDoubleValue(event, "volume", 0.0)));
    const double filledNotional = eventDoubleValue(event, "filled_notional", fillPrice * static_cast<double>(fillQuantity));
    if (symbol.isEmpty() || side.isEmpty() || fillPrice <= 0.0 || fillQuantity <= 0) {
        qWarning() << "PositionAccountService: invalid trade fill event";
        return;
    }

    qDebug() << "PositionAccountService: handleTradeFill"
             << "orderId=" << orderId
             << "execId=" << execId
             << "symbol=" << symbol
             << "side=" << side
             << "fillQuantity=" << fillQuantity
             << "fillPrice=" << fillPrice
             << "filledNotional=" << filledNotional
             << "status=" << normalizeOrderStatus(eventStringValue(event, "status"));

    QVariantMap orderStatus;
    bool shouldAppendOrderStatus = false;
    QVariantMap positionData;
    QVariantMap accountData;
    {
        QMutexLocker locker(&m_mutex);

        QVariantMap existingOrderStatus;
        if (!orderId.isEmpty()) {
            for (const QVariant& entry : std::as_const(m_recentOrderStatuses)) {
                const QVariantMap candidate = entry.toMap();
                if (candidate.value("orderId").toString() == orderId) {
                    existingOrderStatus = candidate;
                    break;
                }
            }
        }

        if (!execId.isEmpty() && existingOrderStatus.value("lastExecId").toString() == execId) {
            return;
        }

        const QVariantMap instrumentInfo = MarketDataService::instance() ? MarketDataService::instance()->resolveInstrument(symbol) : QVariantMap{};
        qint64 totalQuantity = static_cast<qint64>(eventDoubleValue(event, "quantity", existingOrderStatus.value("quantity").toLongLong()));
        qint64 cumulativeFilledQuantity = static_cast<qint64>(eventDoubleValue(event, "filled_quantity", 0.0));
        if (cumulativeFilledQuantity <= 0) {
            cumulativeFilledQuantity = existingOrderStatus.value("filledQuantity").toLongLong();
        }
        if (cumulativeFilledQuantity <= 0) {
            cumulativeFilledQuantity = fillQuantity;
        }
        if (cumulativeFilledQuantity < fillQuantity) {
            cumulativeFilledQuantity = fillQuantity;
        }
        if (totalQuantity > 0 && cumulativeFilledQuantity > totalQuantity) {
            cumulativeFilledQuantity = totalQuantity;
        }

        const QString reportedStatus = normalizeOrderStatus(eventStringValue(event, "status"));
        const QString resolvedStatus = reportedStatus == QStringLiteral("PENDING")
            ? ((totalQuantity > 0 && cumulativeFilledQuantity >= totalQuantity) ? QStringLiteral("FILLED") : QStringLiteral("PARTIAL_FILLED"))
            : reportedStatus;
        const QString timestamp = eventStringValue(event, "created_at").isEmpty()
            ? QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
            : eventStringValue(event, "created_at");

        orderStatus = existingOrderStatus;
        orderStatus.insert("orderId", orderId);
        orderStatus.insert("strategyId", eventStringValue(event, "strategy_id").isEmpty()
            ? existingOrderStatus.value("strategyId").toString()
            : eventStringValue(event, "strategy_id"));
        orderStatus.insert("symbol", symbol);
        orderStatus.insert("name", existingOrderStatus.value("name").toString().isEmpty()
            ? instrumentInfo.value(QStringLiteral("name")).toString()
            : existingOrderStatus.value("name").toString());
        orderStatus.insert("exchange", existingOrderStatus.value("exchange").toString().isEmpty()
            ? instrumentInfo.value(QStringLiteral("exchange")).toString()
            : existingOrderStatus.value("exchange").toString());
        orderStatus.insert("side", side);
        orderStatus.insert("price", existingOrderStatus.value("price").toDouble() > 0.0
            ? existingOrderStatus.value("price").toDouble()
            : fillPrice);
        orderStatus.insert("quantity", totalQuantity > 0 ? totalQuantity : fillQuantity);
        orderStatus.insert("filledQuantity", cumulativeFilledQuantity);
        orderStatus.insert("filledNotional", filledNotional);
        orderStatus.insert("status", resolvedStatus);
        orderStatus.insert("message", QStringLiteral("Execution report received"));
        if (orderStatus.value("createdAt").toString().isEmpty()) {
            orderStatus.insert("createdAt", timestamp);
        }
        orderStatus.insert("updatedAt", timestamp);
        orderStatus.insert("filledAt", timestamp);
        if (!execId.isEmpty()) {
            orderStatus.insert("lastExecId", execId);
        }
        shouldAppendOrderStatus = !orderId.isEmpty();

        QVariantMap position = m_positionsBySymbol.value(symbol);
        const qint64 previousQuantity = position.value("quantity").toLongLong();
        const double previousCostBasis = position.value("costBasis").toDouble();
        const qint64 signedDelta = side == QStringLiteral("SELL") ? -fillQuantity : fillQuantity;
        const qint64 newQuantity = previousQuantity + signedDelta;

        double newCostBasis = previousCostBasis;
        if (side == QStringLiteral("BUY")) {
            const double previousNotional = previousCostBasis * static_cast<double>(previousQuantity);
            const double newNotional = previousNotional + filledNotional;
            newCostBasis = newQuantity > 0 ? newNotional / static_cast<double>(newQuantity) : 0.0;
        } else if (newQuantity <= 0) {
            newCostBasis = 0.0;
        }

        position.insert("symbol", symbol);
        position.insert("quantity", newQuantity > 0 ? newQuantity : 0);
        position.insert("costBasis", newCostBasis);
        position.insert("lastPrice", fillPrice);
        position.insert("marketValue", (newQuantity > 0 ? newQuantity : 0) * fillPrice);
        position.insert("updatedAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
        m_positionsBySymbol.insert(symbol, position);
        positionData = position;

        double availableCash = m_accountSnapshot.value("availableCash").toDouble();
        double realizedPnl = m_accountSnapshot.value("realizedPnl").toDouble();
        if (side == QStringLiteral("BUY")) {
            availableCash -= filledNotional;
        } else {
            availableCash += filledNotional;
            realizedPnl += (fillPrice - previousCostBasis) * static_cast<double>(fillQuantity);
        }

        double marketValue = 0.0;
        for (auto it = m_positionsBySymbol.constBegin(); it != m_positionsBySymbol.constEnd(); ++it) {
            marketValue += it.value().value("marketValue").toDouble();
        }

        m_accountSnapshot.insert("availableCash", availableCash);
        m_accountSnapshot.insert("marketValue", marketValue);
        m_accountSnapshot.insert("realizedPnl", realizedPnl);
        m_accountSnapshot.insert("unrealizedPnl", 0.0);
        m_accountSnapshot.insert("totalAsset", availableCash + marketValue);
        m_accountSnapshot.insert("updatedAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
        accountData = m_accountSnapshot;
    }

    if (shouldAppendOrderStatus) {
        appendOrderStatus(orderStatus);
    }

    if (kDisablePositionAccountUiSignals) {
        return;
    }

    QPointer<PositionAccountService> safeService(this);
    QMetaObject::invokeMethod(this, [safeService, positionData, accountData]() {
        if (!safeService) {
            return;
        }

        emit safeService->positionsChanged();
        emit safeService->accountSnapshotChanged();
        emit safeService->positionUpdated(positionData);
        emit safeService->accountUpdated(accountData);
    }, Qt::QueuedConnection);
}

void PositionAccountService::handlePositionEvent(const engine::EventFormat& event)
{
    const QString symbol = normalizeOrderSymbol(eventStringValue(event, "symbol"));
    if (symbol.isEmpty()) {
        return;
    }

    QVariantMap positionData;
    {
        QMutexLocker locker(&m_mutex);

        QVariantMap position = m_positionsBySymbol.value(symbol);
        const double quantity = eventDoubleValue(event, "quantity", eventDoubleValue(event, "volume", 0.0));
        const double availableQuantity = eventDoubleValue(event, "available_quantity", eventDoubleValue(event, "available_volume", 0.0));
        const double costBasis = eventDoubleValue(event, "cost_basis", eventDoubleValue(event, "cost_price", position.value("costBasis").toDouble()));
        const double lastPrice = eventDoubleValue(event, "last_price", eventDoubleValue(event, "market_price", eventDoubleValue(event, "price", position.value("lastPrice").toDouble())));
        const double marketValue = eventDoubleValue(event, "market_value", position.value("marketValue").toDouble());
        const double unrealizedPnl = eventDoubleValue(event, "float_profit", position.value("unrealizedPnl").toDouble());

        position.insert("symbol", symbol);
        const QVariantMap instrumentInfo = MarketDataService::instance() ? MarketDataService::instance()->resolveInstrument(symbol) : QVariantMap{};
        position.insert("name", eventStringValue(event, "name").isEmpty() ? instrumentInfo.value(QStringLiteral("name")).toString() : eventStringValue(event, "name"));
        position.insert("exchange", eventStringValue(event, "exchange").isEmpty() ? instrumentInfo.value(QStringLiteral("exchange")).toString() : eventStringValue(event, "exchange"));
        position.insert("quantity", static_cast<qint64>(quantity));
        position.insert("availableQuantity", static_cast<qint64>(availableQuantity));
        position.insert("costBasis", costBasis);
        position.insert("lastPrice", lastPrice);
        position.insert("marketValue", marketValue);
        position.insert("unrealizedPnl", unrealizedPnl);
        position.insert("updatedAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
        m_positionsBySymbol.insert(symbol, position);
        positionData = position;
    }

    if (kDisablePositionAccountUiSignals) {
        return;
    }

    QPointer<PositionAccountService> safeService(this);
    QMetaObject::invokeMethod(this, [safeService, positionData]() {
        if (!safeService) {
            return;
        }

        emit safeService->positionsChanged();
        emit safeService->positionUpdated(positionData);
    }, Qt::QueuedConnection);
}

void PositionAccountService::handleAccountEvent(const engine::EventFormat& event)
{
    QVariantMap accountData;
    {
        QMutexLocker locker(&m_mutex);

        const QString accountId = eventStringValue(event, "account_id");
        const double availableCash = eventDoubleValue(event, "available_cash", eventDoubleValue(event, "available", m_accountSnapshot.value("availableCash").toDouble()));
        const double marketValue = eventDoubleValue(event, "market_value", m_accountSnapshot.value("marketValue").toDouble());
        const double totalAsset = eventDoubleValue(event, "total_asset", eventDoubleValue(event, "nav", m_accountSnapshot.value("totalAsset").toDouble()));
        const double unrealizedPnl = eventDoubleValue(event, "float_profit", m_accountSnapshot.value("unrealizedPnl").toDouble());
        const double realizedPnl = eventDoubleValue(event, "realized_pnl", eventDoubleValue(event, "total_profit", m_accountSnapshot.value("realizedPnl").toDouble()));

        if (!accountId.isEmpty()) {
            m_accountSnapshot.insert("accountId", accountId);
        }
        m_accountSnapshot.insert("availableCash", availableCash);
        m_accountSnapshot.insert("marketValue", marketValue);
        m_accountSnapshot.insert("realizedPnl", realizedPnl);
        m_accountSnapshot.insert("unrealizedPnl", unrealizedPnl);
        m_accountSnapshot.insert("totalAsset", totalAsset);
        m_accountSnapshot.insert("updatedAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
        accountData = m_accountSnapshot;
    }

    if (kDisablePositionAccountUiSignals) {
        return;
    }

    QPointer<PositionAccountService> safeService(this);
    QMetaObject::invokeMethod(this, [safeService, accountData]() {
        if (!safeService) {
            return;
        }

        emit safeService->accountSnapshotChanged();
        emit safeService->accountUpdated(accountData);
    }, Qt::QueuedConnection);
}

void PositionAccountService::publishPositionUpdate(const QVariantMap& positionData, const QString& correlationId)
{
    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        return;
    }

    engine::EventFormat event = engine::EventFormat::create_from_strings(
        "position.update",
        "POSITION_ACCOUNT_SERVICE",
        0);
    event.correlation_id = correlationId.toStdString();
    event.set("symbol", positionData.value("symbol").toString().toStdString());
    event.set("quantity", static_cast<int64_t>(positionData.value("quantity").toLongLong()));
    event.set("cost_basis", positionData.value("costBasis").toDouble());
    event.set("last_price", positionData.value("lastPrice").toDouble());
    event.set("market_value", positionData.value("marketValue").toDouble());
    event.metadata["symbol"] = positionData.value("symbol").toString().toStdString();
    event.metadata["quantity"] = QString::number(positionData.value("quantity").toLongLong()).toStdString();

    const auto result = bus->publish(event, static_cast<int>(engine::EventPriority::NORMAL));
    if (!result) {
        qWarning() << "PositionAccountService: failed to publish position update" << QString::fromStdString(result.message);
        return;
    }

    emit positionUpdated(positionData);
}

void PositionAccountService::publishAccountUpdate(const QVariantMap& accountData, const QString& correlationId)
{
    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        return;
    }

    engine::EventFormat event = engine::EventFormat::create_from_strings(
        "account.update",
        "POSITION_ACCOUNT_SERVICE",
        0);
    event.correlation_id = correlationId.toStdString();
    event.set("account_id", accountData.value("accountId").toString().toStdString());
    event.set("available_cash", accountData.value("availableCash").toDouble());
    event.set("market_value", accountData.value("marketValue").toDouble());
    event.set("realized_pnl", accountData.value("realizedPnl").toDouble());
    event.set("total_asset", accountData.value("totalAsset").toDouble());
    event.metadata["account_id"] = accountData.value("accountId").toString().toStdString();
    event.metadata["total_asset"] = QString::number(accountData.value("totalAsset").toDouble(), 'f', 6).toStdString();

    const auto result = bus->publish(event, static_cast<int>(engine::EventPriority::NORMAL));
    if (!result) {
        qWarning() << "PositionAccountService: failed to publish account update" << QString::fromStdString(result.message);
        return;
    }

    emit accountUpdated(accountData);
}

void PositionAccountService::appendOrderStatus(const QVariantMap& orderStatus)
{
    bool changed = true;
    {
        QMutexLocker locker(&m_mutex);

        const QString orderId = orderStatus.value("orderId").toString();
        if (!orderId.isEmpty()) {
            for (int index = 0; index < m_recentOrderStatuses.size(); ++index) {
                const QVariantMap existingStatus = m_recentOrderStatuses.at(index).toMap();
                if (existingStatus.value("orderId").toString() == orderId) {
                    if (shouldIgnoreOrderStatusRegression(existingStatus, orderStatus)) {
                        changed = false;
                        break;
                    }

                    const bool sameStatus = existingStatus.value("status") == orderStatus.value("status");
                    const bool sameQuantity = existingStatus.value("quantity") == orderStatus.value("quantity");
                    const bool sameFilledQuantity = existingStatus.value("filledQuantity") == orderStatus.value("filledQuantity");
                    if (sameStatus && sameQuantity && sameFilledQuantity) {
                        changed = false;
                        break;
                    }
                    m_recentOrderStatuses.removeAt(index);
                    break;
                }
            }
        }

        if (changed) {
            m_recentOrderStatuses.push_front(orderStatus);
            while (m_recentOrderStatuses.size() > 50) {
                m_recentOrderStatuses.removeLast();
            }
        }
    }

    if (!changed) {
        return;
    }

    if (kDisablePositionAccountUiSignals) {
        return;
    }

    QPointer<PositionAccountService> safeService(this);
    QMetaObject::invokeMethod(this, [safeService]() {
        if (!safeService) {
            return;
        }

        emit safeService->recentOrderStatusesChanged();
    }, Qt::QueuedConnection);
}


