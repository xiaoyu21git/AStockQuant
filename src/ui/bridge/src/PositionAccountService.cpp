#include "PositionAccountService.h"

#include "MarketDataService.h"
#include "OrderRecordUtils.h"
#include "OrderRuntimeUtils.h"
#include "TradingConnectionConfigService.h"
#include "TradingRuntimeManager.h"
#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"

#include <QDate>
#include <QDateTime>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QDebug>

#include <cmath>
#include <optional>
#include <thread>

#if defined(ASTOCK_ENABLE_JUJIN_MARKET)
#include "JujinApi.h"
#endif

namespace {

constexpr bool kDisablePositionAccountUiSignals = false;
constexpr bool kDisablePositionAccountEventBusSubscriptions = false;
constexpr order_runtime::EmptyStatusPolicy kOrderStatusPolicy = order_runtime::EmptyStatusPolicy::TreatAsPending;

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

QString normalizedTradingDate(const QString& rawValue)
{
    const QString trimmedValue = rawValue.trimmed();
    if (trimmedValue.isEmpty()) {
        return QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    }

    const QString leadingDate = trimmedValue.left(10);
    const QDate candidate = QDate::fromString(leadingDate, QStringLiteral("yyyy-MM-dd"));
    if (candidate.isValid()) {
        return candidate.toString(QStringLiteral("yyyy-MM-dd"));
    }

    const QDateTime timestamp = QDateTime::fromString(trimmedValue, Qt::ISODate);
    if (timestamp.isValid()) {
        return timestamp.date().toString(QStringLiteral("yyyy-MM-dd"));
    }

    return QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
}

QString eventTradingDate(const engine::EventFormat& event)
{
    static const std::vector<std::string> keys = {
        "business_date",
        "trading_day",
        "trade_date",
        "created_at",
        "updated_at"
    };

    for (const std::string& key : keys) {
        const QString rawValue = eventStringValue(event, key);
        if (!rawValue.isEmpty()) {
            return normalizedTradingDate(rawValue);
        }
    }

    return normalizedTradingDate({});
}

QString configuredTradingAccountId()
{
    TradingConnectionConfigService* configService = TradingConnectionConfigService::instance();
    if (!configService) {
        return {};
    }

    const QVariantMap configuration = configService->currentConfiguration();
    for (const QString& key : {QStringLiteral("accountId"),
                               QStringLiteral("liveAccountId"),
                               QStringLiteral("simAccountId")}) {
        const QString value = configuration.value(key).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }

    return {};
}

bool hasMeaningfulBrokerSnapshot(const thirdparty::AccountInfo& accountInfo,
                                const std::vector<thirdparty::Position>& positions)
{
    return !positions.empty()
        || accountInfo.total_asset > 0.0
        || accountInfo.cash > 0.0
        || accountInfo.available > 0.0
        || accountInfo.market_value > 0.0
        || accountInfo.pnl != 0.0
        || !QString::fromStdString(accountInfo.update_time).trimmed().isEmpty();
}

void ensureDailyTurnoverSnapshot(QVariantMap* accountSnapshot, const QString& tradingDate)
{
    if (!accountSnapshot) {
        return;
    }

    const QString normalizedDate = normalizedTradingDate(tradingDate);
    const QString existingDate = accountSnapshot->value(QStringLiteral("dailyTurnoverDate")).toString();
    if (existingDate != normalizedDate) {
        accountSnapshot->insert(QStringLiteral("dailyTurnoverDate"), normalizedDate);
        accountSnapshot->insert(QStringLiteral("dailyTurnoverNotional"), 0.0);
        return;
    }

    if (!accountSnapshot->contains(QStringLiteral("dailyTurnoverNotional"))) {
        accountSnapshot->insert(QStringLiteral("dailyTurnoverNotional"), 0.0);
    }
}

QVariantMap defaultAccountSnapshot()
{
    QVariantMap accountSnapshot;
    const QString configuredAccountId = configuredTradingAccountId();
    accountSnapshot.insert(QStringLiteral("accountId"),
                           configuredAccountId.isEmpty() ? QStringLiteral("SIM_ACCOUNT") : configuredAccountId);
    accountSnapshot.insert(QStringLiteral("availableCash"), 1000000.0);
    accountSnapshot.insert(QStringLiteral("marketValue"), 0.0);
    accountSnapshot.insert(QStringLiteral("realizedPnl"), 0.0);
    accountSnapshot.insert(QStringLiteral("unrealizedPnl"), 0.0);
    accountSnapshot.insert(QStringLiteral("totalAsset"), 1000000.0);
    ensureDailyTurnoverSnapshot(&accountSnapshot, QString());
    accountSnapshot.insert(QStringLiteral("updatedAt"),
                           QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    return accountSnapshot;
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

enum class CanonicalOrderSide {
    Buy,
    Sell,
};

enum class CanonicalPositionSide {
    Long,
    Short,
};

enum class CanonicalPositionEffect {
    Open,
    Close,
};

enum class CanonicalPositionType {
    Stock,
    MarginBuy,
    MarginSell,
    Futures,
    Options,
};

enum class CanonicalTradeAction {
    MarginSell,
    CloseShort,
};

QString serializeOrderSide(CanonicalOrderSide side)
{
    switch (side) {
    case CanonicalOrderSide::Buy:
        return QStringLiteral("BUY");
    case CanonicalOrderSide::Sell:
        return QStringLiteral("SELL");
    }

    return {};
}

QString serializePositionSide(CanonicalPositionSide side)
{
    switch (side) {
    case CanonicalPositionSide::Long:
        return QStringLiteral("LONG");
    case CanonicalPositionSide::Short:
        return QStringLiteral("SHORT");
    }

    return {};
}

QString serializePositionEffect(CanonicalPositionEffect effect)
{
    switch (effect) {
    case CanonicalPositionEffect::Open:
        return QStringLiteral("OPEN");
    case CanonicalPositionEffect::Close:
        return QStringLiteral("CLOSE");
    }

    return {};
}

QString serializePositionType(CanonicalPositionType type)
{
    switch (type) {
    case CanonicalPositionType::Stock:
        return QStringLiteral("stock");
    case CanonicalPositionType::MarginBuy:
        return QStringLiteral("margin_buy");
    case CanonicalPositionType::MarginSell:
        return QStringLiteral("margin_sell");
    case CanonicalPositionType::Futures:
        return QStringLiteral("futures");
    case CanonicalPositionType::Options:
        return QStringLiteral("options");
    }

    return {};
}

QString serializeTradeAction(CanonicalTradeAction action)
{
    switch (action) {
    case CanonicalTradeAction::MarginSell:
        return QStringLiteral("marginSell");
    case CanonicalTradeAction::CloseShort:
        return QStringLiteral("closeShort");
    }

    return {};
}

std::optional<CanonicalOrderSide> parseCanonicalOrderSide(QString side)
{
    side = side.trimmed().toUpper();
    if (side == QStringLiteral("BUY")) {
        return CanonicalOrderSide::Buy;
    }
    if (side == QStringLiteral("SELL")) {
        return CanonicalOrderSide::Sell;
    }
    return std::nullopt;
}

std::optional<CanonicalPositionSide> parseCanonicalPositionSide(QString side)
{
    side = side.trimmed().toUpper();
    if (side == QStringLiteral("LONG")) {
        return CanonicalPositionSide::Long;
    }
    if (side == QStringLiteral("SHORT")) {
        return CanonicalPositionSide::Short;
    }
    return std::nullopt;
}

std::optional<CanonicalPositionEffect> parseCanonicalPositionEffect(QString effect)
{
    effect = effect.trimmed().toUpper();
    if (effect == QStringLiteral("OPEN")) {
        return CanonicalPositionEffect::Open;
    }
    if (effect == QStringLiteral("CLOSE")) {
        return CanonicalPositionEffect::Close;
    }
    return std::nullopt;
}

std::optional<CanonicalPositionType> parseCanonicalPositionType(QString type)
{
    type = type.trimmed().toLower();
    if (type == QStringLiteral("stock")) {
        return CanonicalPositionType::Stock;
    }
    if (type == QStringLiteral("margin_buy")) {
        return CanonicalPositionType::MarginBuy;
    }
    if (type == QStringLiteral("margin_sell")) {
        return CanonicalPositionType::MarginSell;
    }
    if (type == QStringLiteral("futures")) {
        return CanonicalPositionType::Futures;
    }
    if (type == QStringLiteral("options")) {
        return CanonicalPositionType::Options;
    }
    return std::nullopt;
}

std::optional<CanonicalTradeAction> parseCanonicalTradeAction(QString action)
{
    action = action.trimmed();
    if (action == QStringLiteral("marginSell")) {
        return CanonicalTradeAction::MarginSell;
    }
    if (action == QStringLiteral("closeShort")) {
        return CanonicalTradeAction::CloseShort;
    }
    return std::nullopt;
}

QString normalizeOrderSide(QString side)
{
    const std::optional<CanonicalOrderSide> normalizedSide = parseCanonicalOrderSide(side);
    return normalizedSide.has_value() ? serializeOrderSide(*normalizedSide) : QString{};
}

QString normalizePositionSide(QString side)
{
    const std::optional<CanonicalPositionSide> normalizedSide = parseCanonicalPositionSide(side);
    return normalizedSide.has_value() ? serializePositionSide(*normalizedSide) : QString{};
}

QString normalizePositionEffect(QString effect)
{
    const std::optional<CanonicalPositionEffect> normalizedEffect = parseCanonicalPositionEffect(effect);
    return normalizedEffect.has_value() ? serializePositionEffect(*normalizedEffect) : QString{};
}

CanonicalPositionType resolvePositionType(QString rawType,
                                          QString exchange,
                                          QString optionType,
                                          QString underlying,
                                          QString accountType,
                                          std::optional<CanonicalPositionSide> positionSide)
{
    const QString normalizedExchange = exchange.trimmed().toUpper();
    const QString normalizedOptionType = optionType.trimmed().toLower();
    const QString normalizedUnderlying = underlying.trimmed().toUpper();
    const QString normalizedAccountType = accountType.trimmed().toLower();
    const std::optional<CanonicalPositionType> explicitType = parseCanonicalPositionType(rawType);

    if (explicitType.has_value()) {
        return *explicitType;
    }

    if (!normalizedOptionType.isEmpty() || !normalizedUnderlying.isEmpty()) {
        return CanonicalPositionType::Options;
    }

    if (normalizedExchange == QStringLiteral("CFFEX") || normalizedExchange == QStringLiteral("SHFE")
        || normalizedExchange == QStringLiteral("DCE") || normalizedExchange == QStringLiteral("CZCE")
        || normalizedExchange == QStringLiteral("INE") || normalizedExchange == QStringLiteral("GFEX")) {
        return CanonicalPositionType::Futures;
    }

    if (normalizedAccountType.contains(QStringLiteral("margin"))
        || normalizedAccountType.contains(QStringLiteral("credit"))
        || normalizedAccountType.contains(QStringLiteral("融资"))
        || normalizedAccountType.contains(QStringLiteral("融券"))) {
        return positionSide.has_value() && *positionSide == CanonicalPositionSide::Short
            ? CanonicalPositionType::MarginSell
            : CanonicalPositionType::MarginBuy;
    }

    if (positionSide.has_value() && *positionSide == CanonicalPositionSide::Short) {
        return CanonicalPositionType::MarginSell;
    }

    return CanonicalPositionType::Stock;
}

bool isMarginShortOpenFill(CanonicalPositionType resolvedType,
                           CanonicalOrderSide side,
                           const std::optional<CanonicalPositionEffect>& positionEffect,
                           const std::optional<CanonicalTradeAction>& action)
{
    return resolvedType == CanonicalPositionType::MarginSell
        && side == CanonicalOrderSide::Sell
        && ((positionEffect.has_value() && *positionEffect == CanonicalPositionEffect::Open)
            || (action.has_value() && *action == CanonicalTradeAction::MarginSell));
}

bool isMarginShortCoverFill(CanonicalPositionType resolvedType,
                            CanonicalOrderSide side,
                            const std::optional<CanonicalPositionEffect>& positionEffect,
                            const std::optional<CanonicalTradeAction>& action)
{
    return resolvedType == CanonicalPositionType::MarginSell
        && side == CanonicalOrderSide::Buy
        && ((positionEffect.has_value() && *positionEffect == CanonicalPositionEffect::Close)
            || (action.has_value() && *action == CanonicalTradeAction::CloseShort));
}

double unrealizedPnlForPosition(std::optional<CanonicalPositionSide> positionSide,
                                double costBasis,
                                double lastPrice,
                                qint64 quantity)
{
    if (quantity <= 0 || !std::isfinite(costBasis) || !std::isfinite(lastPrice)) {
        return 0.0;
    }

    if (positionSide.has_value() && *positionSide == CanonicalPositionSide::Short) {
        return (costBasis - lastPrice) * static_cast<double>(quantity);
    }

    return (lastPrice - costBasis) * static_cast<double>(quantity);
}

double netMarketValueContribution(const QVariantMap& position)
{
    const double marketValue = std::abs(position.value(QStringLiteral("marketValue")).toDouble());
    const std::optional<CanonicalPositionSide> positionSide = parseCanonicalPositionSide(
        position.value(QStringLiteral("positionSide")).toString());
    return positionSide.has_value() && *positionSide == CanonicalPositionSide::Short
        ? -marketValue
        : marketValue;
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
    , m_initialSnapshotInFlight(false)
    , m_initialSnapshotLoaded(false)
{
    m_accountSnapshot = defaultAccountSnapshot();
}

void PositionAccountService::initialize()
{
    QMutexLocker locker(&m_mutex);
    if (m_initialized) {
        return;
    }

    initializeEventBusIntegration();
    m_initialized = true;
    locker.unlock();
    emit initializedChanged();
}

void PositionAccountService::requestInitialSnapshot()
{
#if !defined(ASTOCK_ENABLE_JUJIN_MARKET)
    return;
#else
    thirdparty::JujinApi* sharedApi = engine::get_shared_jujin_api();
    if (!sharedApi || !sharedApi->is_connected()) {
        emit errorOccurred(QStringLiteral("交易会话未连接，无法刷新持仓快照"));
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        if (m_initialSnapshotInFlight) {
            return;
        }
        m_initialSnapshotInFlight = true;
    }

    QPointer<PositionAccountService> safeService(this);
    std::thread([safeService, sharedApi]() {
        try {
            const QString configuredAccountId = configuredTradingAccountId();
            std::vector<thirdparty::Position> brokerPositions;
            thirdparty::AccountInfo brokerAccount;

            if (!configuredAccountId.isEmpty()) {
                const std::shared_ptr<thirdparty::GmStrategySession> runtimeSession =
                    thirdparty::TradingRuntimeManager::instance().get_session(configuredAccountId.toStdString());
                if (runtimeSession) {
                    brokerPositions = runtimeSession->snapshot_positions();
                    brokerAccount = runtimeSession->snapshot_account();
                }
            }

            if (!hasMeaningfulBrokerSnapshot(brokerAccount, brokerPositions)) {
                brokerPositions = sharedApi->query_positions();
                brokerAccount = sharedApi->query_account();
            }

            const bool hasBrokerSnapshot = hasMeaningfulBrokerSnapshot(brokerAccount, brokerPositions);

            QVariantMap accountData;
            if (!configuredAccountId.isEmpty()) {
                accountData.insert(QStringLiteral("accountId"), configuredAccountId);
            }
            accountData.insert(QStringLiteral("availableCash"), brokerAccount.available > 0.0 ? brokerAccount.available : brokerAccount.cash);
            accountData.insert(QStringLiteral("marketValue"), brokerAccount.market_value);
            accountData.insert(QStringLiteral("realizedPnl"), 0.0);
            accountData.insert(QStringLiteral("unrealizedPnl"), brokerAccount.pnl);
            accountData.insert(QStringLiteral("totalAsset"), brokerAccount.total_asset > 0.0
                ? brokerAccount.total_asset
                : ((brokerAccount.available > 0.0 ? brokerAccount.available : brokerAccount.cash) + brokerAccount.market_value));
            ensureDailyTurnoverSnapshot(&accountData, QString());
            accountData.insert(QStringLiteral("updatedAt"), QString::fromStdString(brokerAccount.update_time).trimmed().isEmpty()
                ? QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                : QString::fromStdString(brokerAccount.update_time).trimmed());

            QHash<QString, QVariantMap> positionsBySymbol;
            for (const thirdparty::Position& rawPosition : brokerPositions) {
                const QString symbol = normalizeOrderSymbol(QString::fromStdString(rawPosition.symbol));
                if (symbol.isEmpty()) {
                    continue;
                }

                const QVariantMap instrumentInfo = MarketDataService::instance()
                    ? MarketDataService::instance()->resolveInstrument(symbol)
                    : QVariantMap{};
                const std::optional<CanonicalPositionSide> positionSide = parseCanonicalPositionSide(
                    QString::fromStdString(rawPosition.direction));
                if (!positionSide.has_value()) {
                    qWarning() << "PositionAccountService: unsupported broker position side"
                               << symbol
                               << QString::fromStdString(rawPosition.direction);
                    continue;
                }
                const QString exchange = instrumentInfo.value(QStringLiteral("exchange")).toString();
                const CanonicalPositionType positionType = resolvePositionType(QString(),
                                                                               exchange,
                                                                               QString(),
                                                                               QString(),
                                                                               QString(),
                                                                               positionSide);
                const double quantity = static_cast<double>(rawPosition.quantity);
                const double positionMarketValue = rawPosition.market_value;
                const double lastPrice = rawPosition.price > 0.0
                    ? rawPosition.price
                    : ((quantity > 0.0 && positionMarketValue > 0.0) ? positionMarketValue / quantity : 0.0);

                QVariantMap position;
                position.insert(QStringLiteral("symbol"), symbol);
                position.insert(QStringLiteral("name"), QString::fromStdString(rawPosition.name).trimmed().isEmpty()
                    ? instrumentInfo.value(QStringLiteral("name")).toString()
                    : QString::fromStdString(rawPosition.name).trimmed());
                position.insert(QStringLiteral("exchange"), exchange);
                position.insert(QStringLiteral("type"), serializePositionType(positionType));
                position.insert(QStringLiteral("positionSide"), serializePositionSide(*positionSide));
                position.insert(QStringLiteral("quantity"), rawPosition.quantity);
                position.insert(QStringLiteral("availableQuantity"), rawPosition.quantity);
                position.insert(QStringLiteral("closeableQuantity"), rawPosition.quantity);
                position.insert(QStringLiteral("costBasis"), rawPosition.price);
                position.insert(QStringLiteral("avgPrice"), rawPosition.price);
                position.insert(QStringLiteral("lastPrice"), lastPrice);
                position.insert(QStringLiteral("marketValue"), positionMarketValue);
                position.insert(QStringLiteral("unrealizedPnl"), rawPosition.pnl);
                position.insert(QStringLiteral("updatedAt"), QString::fromStdString(rawPosition.update_time).trimmed().isEmpty()
                    ? QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                    : QString::fromStdString(rawPosition.update_time).trimmed());
                positionsBySymbol.insert(symbol, position);
            }

            if (!safeService) {
                return;
            }

            QMetaObject::invokeMethod(safeService.data(), [safeService, positionsBySymbol, accountData, hasBrokerSnapshot]() {
                if (!safeService) {
                    return;
                }

                QVariantMap nextAccountData = accountData;
                {
                    QMutexLocker locker(&safeService->m_mutex);
                    safeService->m_initialSnapshotInFlight = false;
                    safeService->m_initialSnapshotLoaded = hasBrokerSnapshot;
                    if (hasBrokerSnapshot) {
                        safeService->m_positionsBySymbol = positionsBySymbol;
                        if (!nextAccountData.contains(QStringLiteral("accountId"))) {
                            nextAccountData.insert(QStringLiteral("accountId"), safeService->m_accountSnapshot.value(QStringLiteral("accountId")));
                        }
                        safeService->m_accountSnapshot = nextAccountData;
                    }
                }

                if (hasBrokerSnapshot) {
                    emit safeService->positionsChanged();
                    emit safeService->accountSnapshotChanged();
                }
            }, Qt::QueuedConnection);
        } catch (const std::exception& error) {
            if (!safeService) {
                return;
            }
            const QString message = QStringLiteral("刷新持仓快照失败: %1").arg(QString::fromUtf8(error.what()));
            QMetaObject::invokeMethod(safeService.data(), [safeService, message]() {
                if (!safeService) {
                    return;
                }
                {
                    QMutexLocker locker(&safeService->m_mutex);
                    safeService->m_initialSnapshotInFlight = false;
                }
                qWarning() << "PositionAccountService:" << message;
                emit safeService->errorOccurred(message);
            }, Qt::QueuedConnection);
        } catch (...) {
            if (!safeService) {
                return;
            }
            const QString message = QStringLiteral("刷新持仓快照失败: 未知异常");
            QMetaObject::invokeMethod(safeService.data(), [safeService, message]() {
                if (!safeService) {
                    return;
                }
                {
                    QMutexLocker locker(&safeService->m_mutex);
                    safeService->m_initialSnapshotInFlight = false;
                }
                qWarning() << "PositionAccountService:" << message;
                emit safeService->errorOccurred(message);
            }, Qt::QueuedConnection);
        }
    }).detach();
#endif
}

bool PositionAccountService::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized;
}

bool PositionAccountService::initialSnapshotLoaded() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialSnapshotLoaded;
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

void PositionAccountService::resetStateForTesting()
{
    bool initializationStateChanged = false;
    engine::EventBus* bus = engine::get_engine_event_bus();

    {
        QMutexLocker locker(&m_mutex);
        initializationStateChanged = m_initialized;

        if (bus && m_eventBusIntegrated) {
            if (m_orderStatusSubscription) {
                bus->unsubscribe(m_orderStatusSubscription);
            }
            if (m_tradeFillSubscription) {
                bus->unsubscribe(m_tradeFillSubscription);
            }
            if (m_executionReportSubscription) {
                bus->unsubscribe(m_executionReportSubscription);
            }
            if (m_positionSubscription) {
                bus->unsubscribe(m_positionSubscription);
            }
            if (m_accountSubscription) {
                bus->unsubscribe(m_accountSubscription);
            }
        }

        m_initialized = false;
        m_eventBusIntegrated = false;
        m_initialSnapshotInFlight = false;
        m_initialSnapshotLoaded = false;
        m_orderStatusSubscription = foundation::utils::Uuid();
        m_tradeFillSubscription = foundation::utils::Uuid();
        m_executionReportSubscription = foundation::utils::Uuid();
        m_positionSubscription = foundation::utils::Uuid();
        m_accountSubscription = foundation::utils::Uuid();
        m_positionsBySymbol.clear();
        m_recentOrderStatuses.clear();
        m_accountSnapshot = defaultAccountSnapshot();
    }

    if (initializationStateChanged) {
        emit initializedChanged();
    }
    emit positionsChanged();
    emit accountSnapshotChanged();
    emit recentOrderStatusesChanged();
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

    m_orderStatusSubscription = bus->subscribe(engine::EventTypes::TRADING_ORDER_UPDATED,
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

    m_executionReportSubscription = bus->subscribe(engine::EventTypes::TRADING_EXECUTION_REPORT,
        [this](const engine::EventFormat& event) {
            const engine::EventFormat queuedEvent = event;
            invokeOnMainThread(this,
                [queuedEvent](PositionAccountService* service) {
                    service->handleTradeFill(queuedEvent);
                });
        });

    m_positionSubscription = bus->subscribe(engine::EventTypes::TRADING_POSITION_UPDATED,
        [this](const engine::EventFormat& event) {
            const engine::EventFormat queuedEvent = event;
            invokeOnMainThread(this,
                [queuedEvent](PositionAccountService* service) {
                    service->handlePositionEvent(queuedEvent);
                });
        });

    m_accountSubscription = bus->subscribe(engine::EventTypes::TRADING_ACCOUNT_UPDATED,
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
    const QString statusOrigin = eventStringValue(event, "status_origin").trimmed().toLower();
    if (statusOrigin == QStringLiteral("local_request")) {
        return;
    }

    const QString explicitClientOrderId = eventStringValue(event, "client_order_id");
    const QString explicitBrokerOrderId = eventStringValue(event, "broker_order_id");
    QString orderId = eventStringValue(event, "order_id");
    if (orderId.isEmpty()) {
        orderId = !explicitClientOrderId.isEmpty() ? explicitClientOrderId : explicitBrokerOrderId;
    }
    QVariantMap existingOrderStatus;
    {
        QMutexLocker locker(&m_mutex);
        existingOrderStatus = order_runtime::findOrderRecord(m_recentOrderStatuses,
                                                             QStringList{ orderId, explicitClientOrderId, explicitBrokerOrderId });
    }

    QVariantMap orderStatus = existingOrderStatus;
    const qint64 quantity = static_cast<qint64>(eventDoubleValue(event, "quantity", 0.0));
    qint64 filledQuantity = static_cast<qint64>(eventDoubleValue(event, "filled_quantity", 0.0));
    const QString normalizedStatus = order_runtime::normalizeOrderStatus(eventStringValue(event, "status"), kOrderStatusPolicy);
    if (normalizedStatus == QStringLiteral("FILLED") && filledQuantity <= 0 && quantity > 0) {
        filledQuantity = quantity;
    }
    const QString resolvedStatus = order_runtime::resolveOrderStatusFromProgress(normalizedStatus,
                                                                                quantity,
                                                                                filledQuantity,
                                                                                kOrderStatusPolicy);

    orderStatus.insert("orderId", orderId);
    orderStatus.insert("clientOrderId", explicitClientOrderId.isEmpty() ? orderId : explicitClientOrderId);
    if (!explicitBrokerOrderId.isEmpty()) {
        orderStatus.insert("brokerOrderId", explicitBrokerOrderId);
    }
    const QString businessStrategyId = eventStringValue(event, "business_strategy_id");
    const QString strategyId = businessStrategyId.isEmpty()
        ? eventStringValue(event, "strategy_id")
        : businessStrategyId;
    orderStatus.insert("strategyId", strategyId);
    const QString runtimeStrategyId = eventStringValue(event, "runtime_strategy_id");
    if (!runtimeStrategyId.isEmpty()) {
        orderStatus.insert("runtimeStrategyId", runtimeStrategyId);
    }
    const QString symbol = normalizeOrderSymbol(eventStringValue(event, "symbol"));
    const QVariantMap instrumentInfo = MarketDataService::instance() ? MarketDataService::instance()->resolveInstrument(symbol) : QVariantMap{};
    orderStatus.insert("symbol", symbol);
    orderStatus.insert("name", eventStringValue(event, "name").isEmpty() ? instrumentInfo.value(QStringLiteral("name")).toString() : eventStringValue(event, "name"));
    orderStatus.insert("exchange", eventStringValue(event, "exchange").isEmpty() ? instrumentInfo.value(QStringLiteral("exchange")).toString() : eventStringValue(event, "exchange"));
    const QString side = normalizeOrderSide(eventStringValue(event, "side"));
    if (!side.isEmpty()) {
        orderStatus.insert("side", side);
    } else if (!eventStringValue(event, "side").trimmed().isEmpty()) {
        qWarning() << "PositionAccountService: unsupported order side"
                   << eventStringValue(event, "side");
    }
    const QString orderType = eventStringValue(event, "type").trimmed().toLower();
    if (!orderType.isEmpty()) {
        orderStatus.insert("type", orderType);
    }
    const QString action = eventStringValue(event, "action").trimmed();
    if (!action.isEmpty()) {
        orderStatus.insert("action", action);
    }
    const QString positionEffect = normalizePositionEffect(eventStringValue(event, "position_effect_text"));
    if (!positionEffect.isEmpty()) {
        orderStatus.insert("positionEffect", positionEffect);
    } else if (!eventStringValue(event, "position_effect_text").trimmed().isEmpty()) {
        qWarning() << "PositionAccountService: unsupported position effect"
                   << eventStringValue(event, "position_effect_text");
    }
    const QString underlying = eventStringValue(event, "underlying").trimmed().toUpper();
    if (!underlying.isEmpty()) {
        orderStatus.insert("underlying", underlying);
    }
    const QString optionType = eventStringValue(event, "option_type").trimmed().toLower();
    if (!optionType.isEmpty()) {
        orderStatus.insert("optionType", optionType);
    }
    const QString expiry = eventStringValue(event, "expiry").trimmed();
    if (!expiry.isEmpty()) {
        orderStatus.insert("expiry", expiry);
    }
    orderStatus.insert("price", eventDoubleValue(event, "price", 0.0));
    orderStatus.insert("quantity", quantity);
    orderStatus.insert("filledQuantity", filledQuantity);
    orderStatus.insert("filledNotional", eventDoubleValue(event, "filled_notional", 0.0));
    orderStatus.insert("status", resolvedStatus);
    if (!statusOrigin.isEmpty()) {
        orderStatus.insert("statusOrigin", statusOrigin);
    }
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
    const QString statusOrigin = eventStringValue(event, "status_origin").trimmed().toLower();
    if (statusOrigin == QStringLiteral("local_request")) {
        return;
    }

    const QString explicitClientOrderId = eventStringValue(event, "client_order_id");
    const QString explicitBrokerOrderId = eventStringValue(event, "broker_order_id");
    QString orderId = eventStringValue(event, "order_id");
    if (orderId.isEmpty()) {
        orderId = !explicitClientOrderId.isEmpty() ? explicitClientOrderId : explicitBrokerOrderId;
    }
    const QString execId = eventStringValue(event, "exec_id");
    QString symbol = normalizeOrderSymbol(eventStringValue(event, "symbol"));
    QString side = normalizeOrderSide(eventStringValue(event, "side"));
    double fillPrice = eventDoubleValue(event, "fill_price", eventDoubleValue(event, "price", 0.0));
    const qint64 fillQuantity = static_cast<qint64>(eventDoubleValue(event, "fill_quantity", eventDoubleValue(event, "volume", 0.0)));
    double filledNotional = eventDoubleValue(event, "filled_notional", eventDoubleValue(event, "amount", fillPrice * static_cast<double>(fillQuantity)));

    QVariantMap existingOrderStatus;
    {
        QMutexLocker locker(&m_mutex);
        existingOrderStatus = order_runtime::findOrderRecord(m_recentOrderStatuses,
                                                             QStringList{ orderId, explicitClientOrderId, explicitBrokerOrderId });
    }

    if (symbol.isEmpty()) {
        symbol = normalizeOrderSymbol(existingOrderStatus.value("symbol").toString());
    }
    if (side.isEmpty()) {
        side = normalizeOrderSide(existingOrderStatus.value("side").toString());
    }
    if (side.isEmpty()) {
        qWarning() << "PositionAccountService: trade fill missing canonical side"
                   << orderId
                   << eventStringValue(event, "side")
                   << existingOrderStatus.value("side").toString();
        return;
    }
    if (fillPrice <= 0.0) {
        fillPrice = existingOrderStatus.value("price").toDouble();
    }
    if (fillPrice <= 0.0 && fillQuantity > 0 && filledNotional > 0.0) {
        fillPrice = filledNotional / static_cast<double>(fillQuantity);
    }
    if (filledNotional <= 0.0 && fillPrice > 0.0 && fillQuantity > 0) {
        filledNotional = fillPrice * static_cast<double>(fillQuantity);
    }
    if (symbol.isEmpty() || side.isEmpty() || fillQuantity <= 0) {
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
             << "status=" << order_runtime::normalizeOrderStatus(eventStringValue(event, "status"), kOrderStatusPolicy);

    QVariantMap orderStatus;
    bool shouldAppendOrderStatus = false;
    QVariantMap positionData;
    QVariantMap accountData;
    {
        QMutexLocker locker(&m_mutex);

        ensureDailyTurnoverSnapshot(&m_accountSnapshot, eventTradingDate(event));

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

        const QString reportedStatus = order_runtime::normalizeOrderStatus(eventStringValue(event, "status"), kOrderStatusPolicy);
        const QString resolvedStatus = order_runtime::resolveOrderStatusFromProgress(reportedStatus,
                                                totalQuantity,
                                                cumulativeFilledQuantity,
                                                kOrderStatusPolicy);
        const QString timestamp = eventStringValue(event, "created_at").isEmpty()
            ? QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
            : eventStringValue(event, "created_at");

        orderStatus = existingOrderStatus;
        orderStatus.insert("orderId", orderId);
        orderStatus.insert("clientOrderId", explicitClientOrderId.isEmpty()
            ? (existingOrderStatus.value("clientOrderId").toString().isEmpty() ? orderId : existingOrderStatus.value("clientOrderId").toString())
            : explicitClientOrderId);
        if (!explicitBrokerOrderId.isEmpty()) {
            orderStatus.insert("brokerOrderId", explicitBrokerOrderId);
        }
        const QString businessStrategyId = eventStringValue(event, "business_strategy_id");
        const QString strategyId = businessStrategyId.isEmpty()
            ? eventStringValue(event, "strategy_id")
            : businessStrategyId;
        orderStatus.insert("strategyId", strategyId.isEmpty()
            ? existingOrderStatus.value("strategyId").toString()
            : strategyId);
        const QString runtimeStrategyId = eventStringValue(event, "runtime_strategy_id");
        if (!runtimeStrategyId.isEmpty()) {
            orderStatus.insert("runtimeStrategyId", runtimeStrategyId);
        }
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
        if (!statusOrigin.isEmpty()) {
            orderStatus.insert("statusOrigin", statusOrigin);
        }
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
        const QString storedPositionSide = position.value("positionSide").toString();
        const std::optional<CanonicalPositionSide> storedPositionSideValue = parseCanonicalPositionSide(storedPositionSide);
        const CanonicalPositionType resolvedType = resolvePositionType(
            existingOrderStatus.value("type").toString(),
            existingOrderStatus.value("exchange").toString(),
            existingOrderStatus.value("optionType").toString(),
            existingOrderStatus.value("underlying").toString(),
            existingOrderStatus.value("accountType").toString(),
            storedPositionSideValue);
        const QString fillAction = existingOrderStatus.value("action").toString().trimmed();
        const std::optional<CanonicalTradeAction> fillActionValue = parseCanonicalTradeAction(fillAction);
        const std::optional<CanonicalOrderSide> fillSide = parseCanonicalOrderSide(side);
        const QString fillPositionEffectText = normalizePositionEffect(existingOrderStatus.value("positionEffect").toString());
        const std::optional<CanonicalPositionEffect> fillPositionEffect = parseCanonicalPositionEffect(
            fillPositionEffectText);
        const bool shortOpenFill = isMarginShortOpenFill(resolvedType, *fillSide, fillPositionEffect, fillActionValue);
        const bool shortCoverFill = isMarginShortCoverFill(resolvedType, *fillSide, fillPositionEffect, fillActionValue);
        const qint64 signedDelta = shortOpenFill
            ? fillQuantity
            : shortCoverFill
                ? -fillQuantity
                : *fillSide == CanonicalOrderSide::Sell
                    ? -fillQuantity
                    : fillQuantity;
        const qint64 newQuantity = previousQuantity + signedDelta;
        const qint64 normalizedQuantity = newQuantity > 0 ? newQuantity : 0;

        double newCostBasis = previousCostBasis;
        const bool openingExposureFill = shortOpenFill || (*fillSide == CanonicalOrderSide::Buy && !shortCoverFill);
        if (openingExposureFill) {
            const double previousNotional = previousCostBasis * static_cast<double>(previousQuantity);
            const double newNotional = previousNotional + filledNotional;
            newCostBasis = newQuantity > 0 ? newNotional / static_cast<double>(newQuantity) : 0.0;
        } else if (newQuantity <= 0) {
            newCostBasis = 0.0;
        }

        position.insert("symbol", symbol);
        position.insert("type", serializePositionType(resolvedType));
        std::optional<CanonicalPositionSide> nextPositionSide = storedPositionSideValue;
        if ((resolvedType == CanonicalPositionType::Futures || resolvedType == CanonicalPositionType::Options)
            && fillPositionEffect.has_value() && *fillPositionEffect == CanonicalPositionEffect::Open) {
            nextPositionSide = *fillSide == CanonicalOrderSide::Buy
                ? CanonicalPositionSide::Long
                : CanonicalPositionSide::Short;
        }
        if (resolvedType == CanonicalPositionType::MarginSell
            && (shortOpenFill || (nextPositionSide.has_value() && *nextPositionSide == CanonicalPositionSide::Short))) {
            nextPositionSide = CanonicalPositionSide::Short;
        }
        if (!nextPositionSide.has_value()) {
            nextPositionSide = *fillSide == CanonicalOrderSide::Sell && resolvedType == CanonicalPositionType::MarginSell
                ? CanonicalPositionSide::Short
                : CanonicalPositionSide::Long;
        }
        position.insert("positionSide", serializePositionSide(*nextPositionSide));
        position.insert("quantity", normalizedQuantity);
        position.insert("availableQuantity", normalizedQuantity);
        position.insert("closeableQuantity", normalizedQuantity);
        position.insert("costBasis", newCostBasis);
        position.insert("lastPrice", fillPrice);
        position.insert("marketValue", static_cast<double>(normalizedQuantity) * fillPrice);
        position.insert("unrealizedPnl", unrealizedPnlForPosition(nextPositionSide, newCostBasis, fillPrice, normalizedQuantity));
        if (!existingOrderStatus.value("name").toString().isEmpty()) {
            position.insert("name", existingOrderStatus.value("name").toString());
        }
        if (!existingOrderStatus.value("exchange").toString().isEmpty()) {
            position.insert("exchange", existingOrderStatus.value("exchange").toString());
        }
        if (fillPositionEffect.has_value()) {
            position.insert("positionEffect", serializePositionEffect(*fillPositionEffect));
        }
        if (!existingOrderStatus.value("underlying").toString().isEmpty()) {
            position.insert("underlying", existingOrderStatus.value("underlying").toString());
        }
        if (!existingOrderStatus.value("optionType").toString().isEmpty()) {
            position.insert("optionType", existingOrderStatus.value("optionType").toString());
        }
        if (!existingOrderStatus.value("expiry").toString().isEmpty()) {
            position.insert("expiry", existingOrderStatus.value("expiry").toString());
        }
        position.insert("updatedAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
        m_positionsBySymbol.insert(symbol, position);
        positionData = position;

        double availableCash = m_accountSnapshot.value("availableCash").toDouble();
        double realizedPnl = m_accountSnapshot.value("realizedPnl").toDouble();
        if (*fillSide == CanonicalOrderSide::Buy) {
            availableCash -= filledNotional;
        } else {
            availableCash += filledNotional;
        }
        if (shortCoverFill) {
            realizedPnl += (previousCostBasis - fillPrice) * static_cast<double>(fillQuantity);
        } else if (*fillSide == CanonicalOrderSide::Sell && !shortOpenFill) {
            realizedPnl += (fillPrice - previousCostBasis) * static_cast<double>(fillQuantity);
        }
        const double nextDailyTurnoverNotional = m_accountSnapshot.value("dailyTurnoverNotional").toDouble()
            + (filledNotional > 0.0 ? filledNotional : 0.0);
        m_accountSnapshot.insert("dailyTurnoverNotional", nextDailyTurnoverNotional);

        double marketValue = 0.0;
        double netMarketValue = 0.0;
        double unrealizedPnl = 0.0;
        for (auto it = m_positionsBySymbol.constBegin(); it != m_positionsBySymbol.constEnd(); ++it) {
            const double positionMarketValue = std::abs(it.value().value("marketValue").toDouble());
            marketValue += positionMarketValue;
            netMarketValue += netMarketValueContribution(it.value());
            unrealizedPnl += it.value().value("unrealizedPnl").toDouble();
        }

        m_accountSnapshot.insert("availableCash", availableCash);
        m_accountSnapshot.insert("marketValue", marketValue);
        m_accountSnapshot.insert("realizedPnl", realizedPnl);
        m_accountSnapshot.insert("unrealizedPnl", unrealizedPnl);
        m_accountSnapshot.insert("totalAsset", availableCash + netMarketValue);
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
        const double closeableQuantity = eventDoubleValue(event, "closeable_quantity", eventDoubleValue(event, "closable_quantity", availableQuantity));
        const QString optionType = eventStringValue(event, "option_type").trimmed().toLower();
        const QString underlying = eventStringValue(event, "underlying").trimmed().toUpper();
        const QString expiry = eventStringValue(event, "expiry").trimmed();
        const QString eventPositionSide = normalizePositionSide(eventStringValue(event, "position_side"));
        if (eventPositionSide.isEmpty() && !eventStringValue(event, "position_side").trimmed().isEmpty()) {
            qWarning() << "PositionAccountService: unsupported position side"
                       << symbol
                       << eventStringValue(event, "position_side");
        }
        const std::optional<CanonicalPositionSide> eventPositionSideValue = parseCanonicalPositionSide(eventPositionSide);
        const CanonicalPositionType resolvedType = resolvePositionType(
            eventStringValue(event, "type"),
            eventStringValue(event, "exchange"),
            optionType,
            underlying,
            eventStringValue(event, "account_type"),
            eventPositionSide.isEmpty()
                ? parseCanonicalPositionSide(position.value("positionSide").toString())
                : eventPositionSideValue);

        position.insert("symbol", symbol);
        const QVariantMap instrumentInfo = MarketDataService::instance() ? MarketDataService::instance()->resolveInstrument(symbol) : QVariantMap{};
        position.insert("name", eventStringValue(event, "name").isEmpty() ? instrumentInfo.value(QStringLiteral("name")).toString() : eventStringValue(event, "name"));
        position.insert("exchange", eventStringValue(event, "exchange").isEmpty() ? instrumentInfo.value(QStringLiteral("exchange")).toString() : eventStringValue(event, "exchange"));
        position.insert("type", serializePositionType(resolvedType));
        if (!eventPositionSide.isEmpty()) {
            position.insert("positionSide", eventPositionSide);
        } else if (!position.value("positionSide").toString().isEmpty()) {
            position.insert("positionSide", position.value("positionSide").toString());
        }
        position.insert("quantity", static_cast<qint64>(quantity));
        position.insert("availableQuantity", static_cast<qint64>(availableQuantity));
        position.insert("closeableQuantity", static_cast<qint64>(closeableQuantity));
        position.insert("costBasis", costBasis);
        position.insert("lastPrice", lastPrice);
        position.insert("marketValue", marketValue);
        position.insert("unrealizedPnl", unrealizedPnl);
        const QString eventPositionEffect = normalizePositionEffect(eventStringValue(event, "position_effect_text"));
        if (!eventPositionEffect.isEmpty()) {
            position.insert("positionEffect", eventPositionEffect);
        } else if (!eventStringValue(event, "position_effect_text").trimmed().isEmpty()) {
            qWarning() << "PositionAccountService: unsupported position effect"
                       << symbol
                       << eventStringValue(event, "position_effect_text");
        }
        if (!underlying.isEmpty()) {
            position.insert("underlying", underlying);
        }
        if (!optionType.isEmpty()) {
            position.insert("optionType", optionType);
        }
        if (!expiry.isEmpty()) {
            position.insert("expiry", expiry);
        }
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

        ensureDailyTurnoverSnapshot(&m_accountSnapshot, eventTradingDate(event));

        const QString accountId = eventStringValue(event, "account_id");
        const double availableCash = eventDoubleValue(event, "available_cash", eventDoubleValue(event, "available", m_accountSnapshot.value("availableCash").toDouble()));
        const double marketValue = eventDoubleValue(event, "market_value", m_accountSnapshot.value("marketValue").toDouble());
        const double totalAsset = eventDoubleValue(event, "total_asset", eventDoubleValue(event, "nav", m_accountSnapshot.value("totalAsset").toDouble()));
        const double unrealizedPnl = eventDoubleValue(event, "float_profit", m_accountSnapshot.value("unrealizedPnl").toDouble());
        const double realizedPnl = eventDoubleValue(event, "realized_pnl", eventDoubleValue(event, "total_profit", m_accountSnapshot.value("realizedPnl").toDouble()));
        const double dailyTurnoverNotional = eventDoubleValue(
            event,
            "daily_turnover_notional",
            eventDoubleValue(event, "daily_traded_notional", m_accountSnapshot.value("dailyTurnoverNotional").toDouble()));
        const double normalizedDailyTurnoverNotional = dailyTurnoverNotional > 0.0 ? dailyTurnoverNotional : 0.0;

        // Treat the first canonical account update as a usable initial snapshot so
        // downstream sizing and live risk checks can proceed without broker pull.
        m_initialSnapshotInFlight = false;
        m_initialSnapshotLoaded = true;

        if (!accountId.isEmpty()) {
            m_accountSnapshot.insert("accountId", accountId);
        }
        m_accountSnapshot.insert("availableCash", availableCash);
        m_accountSnapshot.insert("marketValue", marketValue);
        m_accountSnapshot.insert("realizedPnl", realizedPnl);
        m_accountSnapshot.insert("unrealizedPnl", unrealizedPnl);
        m_accountSnapshot.insert("totalAsset", totalAsset);
        m_accountSnapshot.insert("dailyTurnoverNotional", normalizedDailyTurnoverNotional);
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
        engine::EventTypes::TRADING_POSITION_UPDATED,
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
    event.metadata["event_contract"] = "canonical";

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
        engine::EventTypes::TRADING_ACCOUNT_UPDATED,
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
    event.metadata["event_contract"] = "canonical";

    const auto result = bus->publish(event, static_cast<int>(engine::EventPriority::NORMAL));
    if (!result) {
        qWarning() << "PositionAccountService: failed to publish account update" << QString::fromStdString(result.message);
        return;
    }

    emit accountUpdated(accountData);
}

void PositionAccountService::appendOrderStatus(const QVariantMap& orderStatus)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        changed = order_runtime::upsertOrderRecord(&m_recentOrderStatuses,
                                                   orderStatus,
                                                   order_runtime::replaceOrderRecord,
                                                   [](const QVariantMap& existingStatus, const QVariantMap& nextStatus) {
                                                       return order_runtime::shouldIgnoreOrderStatusRegression(existingStatus, nextStatus, kOrderStatusPolicy);
                                                   },
                                                   [](const QVariantMap& existingStatus, const QVariantMap& nextStatus) {
                                                       const bool sameStatus = existingStatus.value(QStringLiteral("status")) == nextStatus.value(QStringLiteral("status"));
                                                       const bool sameQuantity = existingStatus.value(QStringLiteral("quantity")) == nextStatus.value(QStringLiteral("quantity"));
                                                       const bool sameFilledQuantity = existingStatus.value(QStringLiteral("filledQuantity")) == nextStatus.value(QStringLiteral("filledQuantity"));
                                                       return sameStatus && sameQuantity && sameFilledQuantity;
                                                   });
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


