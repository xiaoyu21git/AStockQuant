#include "MarketDataService.h"

#include "MarketSubscriptionStatusRegistry.h"

#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"
#include "DatabaseConnectionManager.h"
#include "DataFetchFieldContractUtils.h"

#include <QDateTime>
#include <QMetaObject>
#include <QPointer>
#include <QMutexLocker>
#include <QDebug>

#include <algorithm>
#include <thread>

namespace {

constexpr auto kRuntimeSubscriptionStatusEvent = "trading.market.subscription.status";
constexpr qint64 kReferenceLookupFailureBackoffMs = 15000;

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

double eventNumericValue(const engine::EventFormat& event, const std::string& key, double fallback = 0.0)
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

std::vector<double> parseNumericListText(const QString& text)
{
    std::vector<double> values;
    const QStringList tokens = text.split(',', Qt::SkipEmptyParts);
    values.reserve(tokens.size());

    for (const QString& token : tokens) {
        bool ok = false;
        const double value = token.trimmed().toDouble(&ok);
        if (ok) {
            values.push_back(value);
        }
    }

    return values;
}

std::vector<double> eventNumericVectorValue(const engine::EventFormat& event, const std::string& key)
{
    auto vectorValue = event.get<std::vector<double>>(key);
    if (vectorValue.has_value()) {
        return *vectorValue;
    }

    const QString textValue = eventStringValue(event, key);
    if (textValue.isEmpty()) {
        return {};
    }

    return parseNumericListText(textValue);
}

QString canonicalSymbolCode(const QString& symbol)
{
    const QString normalized = symbol.trimmed().toUpper();
    if (normalized.isEmpty()) {
        return {};
    }

    const int dotIndex = normalized.indexOf('.');
    if (dotIndex < 0) {
        return normalized;
    }

    const QString firstPart = normalized.left(dotIndex);
    const QString secondPart = normalized.mid(dotIndex + 1);
    static const QStringList prefixedExchanges = {
        QStringLiteral("SHSE"),
        QStringLiteral("SZSE"),
        QStringLiteral("BSE"),
        QStringLiteral("CFFEX"),
        QStringLiteral("SHFE"),
        QStringLiteral("DCE"),
        QStringLiteral("CZCE"),
        QStringLiteral("INE"),
        QStringLiteral("GFEX")
    };

    return prefixedExchanges.contains(firstPart) ? secondPart : firstPart;
}

bool databaseTableHasColumn(const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                            const QString& tableName,
                            const QString& columnName)
{
    if (!database || tableName.trimmed().isEmpty() || columnName.trimmed().isEmpty()) {
        return false;
    }

    const auto result = database->executeQuery(
        QStringLiteral(
            "SELECT COUNT(*) AS count FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = :table_name AND COLUMN_NAME = :column_name"),
        {{QStringLiteral(":table_name"), tableName.trimmed()},
         {QStringLiteral(":column_name"), columnName.trimmed()}});

    return !result.isEmpty() && result.getRow(0).getInt(QStringLiteral("count")) > 0;
}

QString symbolInfoIndustrySelect(const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                                 const QString& alias)
{
    const QString normalizedAlias = alias.trimmed().isEmpty() ? QStringLiteral("si") : alias.trimmed();
    if (databaseTableHasColumn(database, QStringLiteral("symbol_info"), QStringLiteral("industry_code"))) {
        return QStringLiteral("TRIM(COALESCE(%1.industry_code, '')) AS industry_code, ").arg(normalizedAlias);
    }

    return QStringLiteral("'' AS industry_code, ");
}

QString preferredEventTimestamp(const engine::EventFormat& event)
{
    const QString createdAt = eventStringValue(event, "created_at");
    if (!createdAt.isEmpty()) {
        return createdAt;
    }

    const QString updatedAt = eventStringValue(event, "updated_at");
    if (!updatedAt.isEmpty()) {
        return updatedAt;
    }

    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString tickDisplayTime(const QString& timestamp)
{
    if (timestamp.isEmpty()) {
        return QStringLiteral("--");
    }
    if (timestamp.size() >= 8) {
        return timestamp.right(8);
    }
    return timestamp;
}

QVariantMap buildDepthSnapshot(const std::vector<double>& bidPrices,
                               const std::vector<double>& bidVolumes,
                               const std::vector<double>& askPrices,
                               const std::vector<double>& askVolumes,
                               const QString& source)
{
    QVariantList bids;
    QVariantList asks;
    double totalBid = 0.0;
    double totalAsk = 0.0;

    const int bidCount = bidPrices.size() < bidVolumes.size()
        ? static_cast<int>(bidPrices.size())
        : static_cast<int>(bidVolumes.size());
    const int askCount = askPrices.size() < askVolumes.size()
        ? static_cast<int>(askPrices.size())
        : static_cast<int>(askVolumes.size());

    for (int index = 0; index < bidCount; ++index) {
        if (bidPrices[index] <= 0.0 && bidVolumes[index] <= 0.0) {
            continue;
        }

        QVariantMap row;
        row.insert(QStringLiteral("price"), bidPrices[index]);
        row.insert(QStringLiteral("volume"), bidVolumes[index]);
        bids.push_back(row);
        totalBid += bidVolumes[index];
    }

    for (int index = 0; index < askCount; ++index) {
        if (askPrices[index] <= 0.0 && askVolumes[index] <= 0.0) {
            continue;
        }

        QVariantMap row;
        row.insert(QStringLiteral("price"), askPrices[index]);
        row.insert(QStringLiteral("volume"), askVolumes[index]);
        asks.push_back(row);
        totalAsk += askVolumes[index];
    }

    if (bids.isEmpty() && asks.isEmpty()) {
        return {};
    }

    QVariantMap depthSnapshot;
    depthSnapshot.insert(QStringLiteral("bids"), bids);
    depthSnapshot.insert(QStringLiteral("asks"), asks);
    depthSnapshot.insert(QStringLiteral("totalBid"), totalBid);
    depthSnapshot.insert(QStringLiteral("totalAsk"), totalAsk);
    depthSnapshot.insert(QStringLiteral("levelCount"), bidCount > askCount ? bidCount : askCount);
    depthSnapshot.insert(QStringLiteral("live"), true);
    depthSnapshot.insert(QStringLiteral("source"), source);
    return depthSnapshot;
}

QString inferTickDirection(const QVariantMap& previousSnapshot, const QVariantMap& latestTick)
{
    const double previousPrice = previousSnapshot.value(QStringLiteral("price")).toDouble();
    const double currentPrice = latestTick.value(QStringLiteral("price")).toDouble();
    if (currentPrice > previousPrice) {
        return QStringLiteral("buy");
    }
    if (currentPrice < previousPrice) {
        return QStringLiteral("sell");
    }

    const QVariantList recentTicks = previousSnapshot.value(QStringLiteral("recentTicks")).toList();
    if (!recentTicks.isEmpty()) {
        const QString recentDirection = recentTicks.constFirst().toMap().value(QStringLiteral("direction")).toString().trimmed().toLower();
        if (!recentDirection.isEmpty()) {
            return recentDirection;
        }
    }

    return QStringLiteral("buy");
}

QHash<QString, QString> createDefaultNames()
{
    return {
        {QStringLiteral("000001.SH"), QStringLiteral("上证指数")},
        {QStringLiteral("399001.SZ"), QStringLiteral("深证成指")},
        {QStringLiteral("399006.SZ"), QStringLiteral("创业板指")},
        {QStringLiteral("000300.SH"), QStringLiteral("沪深300")}
    };
}

QHash<QString, QVariantMap> createDefaultSnapshots()
{
    return {
        {QStringLiteral("000001.SH"), QVariantMap{{QStringLiteral("symbol"), QStringLiteral("000001.SH")}, {QStringLiteral("name"), QStringLiteral("上证指数")}, {QStringLiteral("price"), 3248.45}, {QStringLiteral("preClose"), 3248.45}, {QStringLiteral("pre_close"), 3248.45}, {QStringLiteral("change"), 0.0}, {QStringLiteral("color"), QStringLiteral("#3b82f6")}, {QStringLiteral("source"), QStringLiteral("seed")}, {QStringLiteral("updatedAt"), QStringLiteral("--")}, {QStringLiteral("live"), false}}},
        {QStringLiteral("399001.SZ"), QVariantMap{{QStringLiteral("symbol"), QStringLiteral("399001.SZ")}, {QStringLiteral("name"), QStringLiteral("深证成指")}, {QStringLiteral("price"), 10112.62}, {QStringLiteral("preClose"), 10112.62}, {QStringLiteral("pre_close"), 10112.62}, {QStringLiteral("change"), 0.0}, {QStringLiteral("color"), QStringLiteral("#3b82f6")}, {QStringLiteral("source"), QStringLiteral("seed")}, {QStringLiteral("updatedAt"), QStringLiteral("--")}, {QStringLiteral("live"), false}}},
        {QStringLiteral("399006.SZ"), QVariantMap{{QStringLiteral("symbol"), QStringLiteral("399006.SZ")}, {QStringLiteral("name"), QStringLiteral("创业板指")}, {QStringLiteral("price"), 2008.31}, {QStringLiteral("preClose"), 2008.31}, {QStringLiteral("pre_close"), 2008.31}, {QStringLiteral("change"), 0.0}, {QStringLiteral("color"), QStringLiteral("#3b82f6")}, {QStringLiteral("source"), QStringLiteral("seed")}, {QStringLiteral("updatedAt"), QStringLiteral("--")}, {QStringLiteral("live"), false}}},
        {QStringLiteral("000300.SH"), QVariantMap{{QStringLiteral("symbol"), QStringLiteral("000300.SH")}, {QStringLiteral("name"), QStringLiteral("沪深300")}, {QStringLiteral("price"), 3786.84}, {QStringLiteral("preClose"), 3786.84}, {QStringLiteral("pre_close"), 3786.84}, {QStringLiteral("change"), 0.0}, {QStringLiteral("color"), QStringLiteral("#3b82f6")}, {QStringLiteral("source"), QStringLiteral("seed")}, {QStringLiteral("updatedAt"), QStringLiteral("--")}, {QStringLiteral("live"), false}}}
    };
}

QStringList defaultWatchSymbols()
{
    return {
        QStringLiteral("000001.SH"),
        QStringLiteral("399001.SZ"),
        QStringLiteral("399006.SZ"),
        QStringLiteral("000300.SH")
    };
}

QString eventDisplayName(const engine::EventFormat& event)
{
    static const std::vector<std::string> keys = {
        "name",
        "stock_name",
        "stockName",
        "sec_name",
        "security_name",
        "symbol_name",
        "instrument_name",
        "display_name",
        "名称"
    };

    for (const std::string& key : keys) {
        const QString value = eventStringValue(event, key);
        if (!value.isEmpty()) {
            return value;
        }
    }

    return {};
}

QString colorForChange(double changePercent);

const QHash<QString, QString>& defaultNameMap()
{
    static const QHash<QString, QString> names = createDefaultNames();
    return names;
}

const QHash<QString, QVariantMap>& defaultSnapshotMap()
{
    static const QHash<QString, QVariantMap> snapshots = createDefaultSnapshots();
    return snapshots;
}

QString lookupDisplayNameFromDatabase(const QString& symbol)
{
    static QMutex cacheMutex;
    static QHash<QString, QString> cachedNames;
    static QHash<QString, qint64> failedLookupAtMs;

    const QString normalizedSymbol = symbol.trimmed().toUpper();
    const QString plainCode = canonicalSymbolCode(normalizedSymbol);
    if (normalizedSymbol.isEmpty()) {
        return {};
    }

    {
        QMutexLocker locker(&cacheMutex);
        if (cachedNames.contains(normalizedSymbol)) {
            return cachedNames.value(normalizedSymbol);
        }
        const qint64 failedAtMs = failedLookupAtMs.value(normalizedSymbol, 0);
        if (failedAtMs > 0
            && QDateTime::currentMSecsSinceEpoch() - failedAtMs < kReferenceLookupFailureBackoffMs) {
            return {};
        }
    }

    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        QMutexLocker locker(&cacheMutex);
        failedLookupAtMs.insert(normalizedSymbol, QDateTime::currentMSecsSinceEpoch());
        return {};
    }

    try {
        const auto result = database->executeQuery(
            QStringLiteral(
                "SELECT name FROM symbol_info "
                "WHERE symbol = :symbol OR (:plain_code <> '' AND SUBSTRING_INDEX(symbol, '.', 1) = :plain_code) "
                "ORDER BY CASE WHEN symbol = :symbol THEN 0 ELSE 1 END LIMIT 1"),
            {{QStringLiteral(":symbol"), normalizedSymbol},
             {QStringLiteral(":plain_code"), plainCode}});
        if (result.isEmpty()) {
            QMutexLocker locker(&cacheMutex);
            failedLookupAtMs.insert(normalizedSymbol, QDateTime::currentMSecsSinceEpoch());
            return {};
        }

        const QString resolvedName = result.getRow(0).getString(QStringLiteral("name")).trimmed();
        if (resolvedName.isEmpty()) {
            QMutexLocker locker(&cacheMutex);
            failedLookupAtMs.insert(normalizedSymbol, QDateTime::currentMSecsSinceEpoch());
            return {};
        }

        {
            QMutexLocker locker(&cacheMutex);
            cachedNames.insert(normalizedSymbol, resolvedName);
            failedLookupAtMs.remove(normalizedSymbol);
        }
        return resolvedName;
    } catch (const std::exception& e) {
        qWarning() << "MarketDataService: lookupDisplayNameFromDatabase failed"
                   << normalizedSymbol << e.what();
    } catch (...) {
        qWarning() << "MarketDataService: lookupDisplayNameFromDatabase failed with unknown error"
                   << normalizedSymbol;
    }

    {
        QMutexLocker locker(&cacheMutex);
        failedLookupAtMs.insert(normalizedSymbol, QDateTime::currentMSecsSinceEpoch());
    }

    return {};
}

QVariantMap lookupLatestDailySnapshotFromDatabase(const QString& symbol)
{
    static QMutex cacheMutex;
    static QHash<QString, QVariantMap> cachedSnapshots;
    static QHash<QString, qint64> failedLookupAtMs;

    const QString normalizedSymbol = symbol.trimmed().toUpper();
    const QString plainCode = canonicalSymbolCode(normalizedSymbol);
    if (normalizedSymbol.isEmpty()) {
        return {};
    }

    {
        QMutexLocker locker(&cacheMutex);
        if (cachedSnapshots.contains(normalizedSymbol)) {
            return cachedSnapshots.value(normalizedSymbol);
        }
        const qint64 failedAtMs = failedLookupAtMs.value(normalizedSymbol, 0);
        if (failedAtMs > 0
            && QDateTime::currentMSecsSinceEpoch() - failedAtMs < kReferenceLookupFailureBackoffMs) {
            return {};
        }
    }

    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        QMutexLocker locker(&cacheMutex);
        failedLookupAtMs.insert(normalizedSymbol, QDateTime::currentMSecsSinceEpoch());
        return {};
    }

    try {
        const QString industrySelect = symbolInfoIndustrySelect(database, QStringLiteral("si"));
        const auto result = database->executeQuery(
            QStringLiteral(
                "SELECT d.symbol AS symbol, COALESCE(si.name, d.symbol) AS name, "
                "%1d.market_cap AS market_cap, d.circulating_market_cap AS circulating_market_cap, "
                "DATE_FORMAT(d.trade_date, '%Y-%m-%d') AS trade_date, d.close AS close, d.pre_close AS pre_close "
                "FROM daily_bar d "
                "LEFT JOIN symbol_info si ON d.symbol = si.symbol "
                "WHERE d.symbol = :symbol OR (:plain_code <> '' AND SUBSTRING_INDEX(d.symbol, '.', 1) = :plain_code) "
                "ORDER BY CASE WHEN d.symbol = :symbol THEN 0 ELSE 1 END, d.trade_date DESC LIMIT 1").arg(industrySelect),
            {{QStringLiteral(":symbol"), normalizedSymbol},
             {QStringLiteral(":plain_code"), plainCode}});
        if (result.isEmpty()) {
            QMutexLocker locker(&cacheMutex);
            failedLookupAtMs.insert(normalizedSymbol, QDateTime::currentMSecsSinceEpoch());
            return {};
        }

        const auto& row = result.getRow(0);
        const double closePrice = row.getDouble(QString(factor::bridge::MarketBarFieldKeys::CLOSE));
        if (closePrice <= 0.0) {
            QMutexLocker locker(&cacheMutex);
            failedLookupAtMs.insert(normalizedSymbol, QDateTime::currentMSecsSinceEpoch());
            return {};
        }

        double preClosePrice = row.getDouble(QString(factor::bridge::MarketBarFieldKeys::PRE_CLOSE));
        if (preClosePrice <= 0.0) {
            preClosePrice = closePrice;
        }

        const double changePercent = preClosePrice > 0.0
            ? (closePrice - preClosePrice) / preClosePrice * 100.0
            : 0.0;

        QVariantMap snapshot;
        snapshot.insert(QString(factor::bridge::CommonFieldKeys::SYMBOL), normalizedSymbol);
        snapshot.insert(QString(factor::bridge::ContextualMetadataFieldKeys::NAME),
                row.getString(QString(factor::bridge::ContextualMetadataFieldKeys::NAME)).trimmed());
        snapshot.insert(QString(factor::bridge::MarketBarFieldKeys::INDUSTRY_CODE),
            row.getString(QString(factor::bridge::MarketBarFieldKeys::INDUSTRY_CODE)).trimmed());
        snapshot.insert(QStringLiteral("price"), closePrice);
        snapshot.insert(QString(factor::bridge::MarketBarFieldKeys::CLOSE), closePrice);
        snapshot.insert(QStringLiteral("preClose"), preClosePrice);
        snapshot.insert(QString(factor::bridge::MarketBarFieldKeys::PRE_CLOSE), preClosePrice);
        snapshot.insert(QStringLiteral("marketCap"), row.getDouble(QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP)));
        snapshot.insert(QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP),
                row.getDouble(QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP)));
        snapshot.insert(QStringLiteral("circulatingMarketCap"), row.getDouble(QString(factor::bridge::MarketBarFieldKeys::CIRCULATING_MARKET_CAP)));
        snapshot.insert(QString(factor::bridge::MarketBarFieldKeys::CIRCULATING_MARKET_CAP),
                row.getDouble(QString(factor::bridge::MarketBarFieldKeys::CIRCULATING_MARKET_CAP)));
        snapshot.insert(QStringLiteral("change"), changePercent);
        snapshot.insert(QStringLiteral("color"), colorForChange(changePercent));
        snapshot.insert(QStringLiteral("source"), QStringLiteral("daily_snapshot"));
        snapshot.insert(QStringLiteral("updatedAt"), row.getString(QString(factor::bridge::CommonFieldKeys::TRADE_DATE)).trimmed());
        snapshot.insert(QStringLiteral("live"), false);

        {
            QMutexLocker locker(&cacheMutex);
            cachedSnapshots.insert(normalizedSymbol, snapshot);
            failedLookupAtMs.remove(normalizedSymbol);
        }

        return snapshot;
    } catch (const std::exception& e) {
        qWarning() << "MarketDataService: lookupLatestDailySnapshotFromDatabase failed"
                   << normalizedSymbol << e.what();
    } catch (...) {
        qWarning() << "MarketDataService: lookupLatestDailySnapshotFromDatabase failed with unknown error"
                   << normalizedSymbol;
    }

    {
        QMutexLocker locker(&cacheMutex);
        failedLookupAtMs.insert(normalizedSymbol, QDateTime::currentMSecsSinceEpoch());
    }

    return {};
}

bool needsReferenceSnapshot(const QVariantMap& snapshot)
{
    const QString source = snapshot.value(QStringLiteral("source")).toString().trimmed().toLower();
    const QString updatedAt = snapshot.value(QStringLiteral("updatedAt")).toString().trimmed();
    return source.isEmpty()
        || source == QStringLiteral("seed")
        || source == QStringLiteral("watchlist")
        || source == QStringLiteral("daily_snapshot")
        || updatedAt.isEmpty()
        || updatedAt == QStringLiteral("--");
}

QVariantMap hydrateDisplaySnapshot(QVariantMap snapshot)
{
    const QString symbol = snapshot.value(QString(factor::bridge::CommonFieldKeys::SYMBOL)).toString().trimmed().toUpper();
    if (symbol.isEmpty() || !needsReferenceSnapshot(snapshot)) {
        return snapshot;
    }

    const QVariantMap referenceSnapshot = lookupLatestDailySnapshotFromDatabase(symbol);
    if (referenceSnapshot.isEmpty()) {
        return snapshot;
    }

    for (auto it = referenceSnapshot.constBegin(); it != referenceSnapshot.constEnd(); ++it) {
        snapshot.insert(it.key(), it.value());
    }
    return snapshot;
}

QString colorForChange(double changePercent)
{
    if (changePercent > 0.0) {
        return QStringLiteral("#10b981");
    }
    if (changePercent < 0.0) {
        return QStringLiteral("#ef4444");
    }
    return QStringLiteral("#3b82f6");
}

} // namespace

MarketDataService* MarketDataService::m_instance = nullptr;
QMutex MarketDataService::m_instanceMutex;

MarketDataService* MarketDataService::instance()
{
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        m_instance = new MarketDataService();
    }
    return m_instance;
}

MarketDataService::MarketDataService(QObject* parent)
    : QObject(parent)
    , m_initialized(false)
    , m_eventBusIntegrated(false)
    , m_hasLiveData(false)
    , m_flushScheduled(false)
    , m_pendingLiveUpdate(false)
    , m_runtimeSubscriptionCount(0)
    , m_runtimeSubscriptionLimit(0)
    , m_lastWatchRequestLogAtMs(0)
    , m_suppressedWatchRequestLogs(0)
    , m_primarySymbol(QStringLiteral("000001.SH"))
{
    m_watchlist = defaultWatchSymbols();
    m_snapshotsBySymbol = defaultSnapshotMap();
}

void MarketDataService::initialize()
{
    QMutexLocker locker(&m_mutex);
    if (m_initialized) {
        return;
    }

    m_runtimeSubscriptionCount = MarketSubscriptionStatusRegistry::subscriptionCount();
    m_runtimeSubscriptionLimit = MarketSubscriptionStatusRegistry::subscriptionLimit();

    for (const QString& symbol : m_watchlist) {
        QVariantMap snapshot = m_snapshotsBySymbol.value(symbol);
        if (snapshot.isEmpty()) {
            snapshot = defaultSnapshotMap().value(symbol);
        }
        if (snapshot.isEmpty()) {
            snapshot.insert(QStringLiteral("symbol"), symbol);
            snapshot.insert(QStringLiteral("name"), defaultNameMap().value(symbol, symbol));
            snapshot.insert(QStringLiteral("price"), 0.0);
            snapshot.insert(QStringLiteral("change"), 0.0);
            snapshot.insert(QStringLiteral("color"), QStringLiteral("#3b82f6"));
            snapshot.insert(QStringLiteral("source"), QStringLiteral("watchlist"));
            snapshot.insert(QStringLiteral("updatedAt"), QStringLiteral("--"));
        }
        m_snapshotsBySymbol.insert(symbol, hydrateDisplaySnapshot(snapshot));
    }

    initializeEventBusIntegration();
    m_initialized = true;
    emit initializedChanged();
    emit marketSnapshotsChanged();
}

void MarketDataService::initializeAsync()
{
    QPointer<MarketDataService> safeService(this);
    std::thread([safeService]() {
        if (safeService) {
            safeService->initialize();
        }
    }).detach();
}

bool MarketDataService::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized;
}

QVariantList MarketDataService::marketSnapshots() const
{
    QMutexLocker locker(&m_mutex);
    return orderedSnapshotsLocked();
}

QString MarketDataService::primarySymbol() const
{
    QMutexLocker locker(&m_mutex);
    return m_primarySymbol;
}

bool MarketDataService::hasLiveData() const
{
    QMutexLocker locker(&m_mutex);
    return m_hasLiveData;
}

int MarketDataService::runtimeSubscriptionCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_runtimeSubscriptionCount;
}

int MarketDataService::runtimeSubscriptionLimit() const
{
    QMutexLocker locker(&m_mutex);
    return m_runtimeSubscriptionLimit;
}

QVariantMap MarketDataService::resolveInstrument(const QString& query) const
{
    try {
        const QString normalizedQuery = normalizeSymbol(query);
        const QString keyword = query.trimmed();
        if (normalizedQuery.isEmpty() && keyword.isEmpty()) {
            return {};
        }

        QHash<QString, QVariantMap> snapshotsBySymbol;
        {
            QMutexLocker locker(&m_mutex);
            snapshotsBySymbol = m_snapshotsBySymbol;
        }

        auto buildResult = [](const QVariantMap& snapshot) {
            QVariantMap result = hydrateDisplaySnapshot(snapshot);
            result.insert(QStringLiteral("matched"), true);
            return result;
        };

        for (auto it = snapshotsBySymbol.constBegin(); it != snapshotsBySymbol.constEnd(); ++it) {
            const QVariantMap snapshot = it.value();
            if (normalizeSymbol(snapshot.value(QStringLiteral("symbol")).toString()) == normalizedQuery) {
                return buildResult(snapshot);
            }
        }

        const QString plainCode = canonicalSymbolCode(normalizedQuery);
        if (!plainCode.isEmpty()) {
            for (auto it = snapshotsBySymbol.constBegin(); it != snapshotsBySymbol.constEnd(); ++it) {
                const QVariantMap snapshot = it.value();
                if (canonicalSymbolCode(normalizeSymbol(snapshot.value(QStringLiteral("symbol")).toString())) == plainCode) {
                    return buildResult(snapshot);
                }
            }
        }

        for (auto it = snapshotsBySymbol.constBegin(); it != snapshotsBySymbol.constEnd(); ++it) {
            const QVariantMap snapshot = it.value();
            if (snapshot.value(QStringLiteral("name")).toString().trimmed() == keyword) {
                return buildResult(snapshot);
            }
        }

        for (auto it = snapshotsBySymbol.constBegin(); it != snapshotsBySymbol.constEnd(); ++it) {
            const QVariantMap snapshot = it.value();
            const QString name = snapshot.value(QStringLiteral("name")).toString().trimmed();
            if (!keyword.isEmpty() && !name.isEmpty() && name.contains(keyword, Qt::CaseInsensitive)) {
                return buildResult(snapshot);
            }
        }

        const auto& defaultNames = defaultNameMap();
        for (auto it = defaultNames.constBegin(); it != defaultNames.constEnd(); ++it) {
            if (it.key() == normalizedQuery || it.key().section('.', 0, 0) == plainCode || it.value() == keyword || (!keyword.isEmpty() && it.value().contains(keyword, Qt::CaseInsensitive))) {
                QVariantMap snapshot = defaultSnapshotMap().value(it.key());
                if (snapshot.isEmpty()) {
                    snapshot.insert(QStringLiteral("symbol"), it.key());
                    snapshot.insert(QStringLiteral("name"), it.value());
                    snapshot.insert(QStringLiteral("price"), 0.0);
                    snapshot.insert(QStringLiteral("change"), 0.0);
                    snapshot.insert(QStringLiteral("color"), QStringLiteral("#3b82f6"));
                    snapshot.insert(QStringLiteral("source"), QStringLiteral("seed"));
                    snapshot.insert(QStringLiteral("updatedAt"), QStringLiteral("--"));
                }
                return buildResult(snapshot);
            }
        }

        const QString databaseSymbol = !normalizedQuery.isEmpty() ? normalizedQuery : keyword.toUpper();
        if (!databaseSymbol.isEmpty()) {
            QVariantMap databaseSnapshot = lookupLatestDailySnapshotFromDatabase(databaseSymbol);
            if (!databaseSnapshot.isEmpty()) {
                return buildResult(databaseSnapshot);
            }

            const QString databaseName = lookupDisplayNameFromDatabase(databaseSymbol);
            if (!databaseName.isEmpty()) {
                QVariantMap snapshot;
                snapshot.insert(QStringLiteral("symbol"), databaseSymbol);
                snapshot.insert(QStringLiteral("name"), databaseName);
                snapshot.insert(QStringLiteral("price"), 0.0);
                snapshot.insert(QStringLiteral("change"), 0.0);
                snapshot.insert(QStringLiteral("color"), QStringLiteral("#3b82f6"));
                snapshot.insert(QStringLiteral("source"), QStringLiteral("database_name"));
                snapshot.insert(QStringLiteral("updatedAt"), QStringLiteral("--"));
                snapshot.insert(QStringLiteral("live"), false);
                return buildResult(snapshot);
            }
        }

        return {};
    } catch (const std::exception& e) {
        qWarning() << "MarketDataService: resolveInstrument failed" << query << e.what();
    } catch (...) {
        qWarning() << "MarketDataService: resolveInstrument failed with unknown error" << query;
    }

    return {};
}

QString MarketDataService::normalizeSymbol(const QString& symbol) const
{
    const QString normalized = symbol.trimmed().toUpper();
    if (normalized.isEmpty()) {
        return {};
    }

    if (normalized.startsWith(QStringLiteral("SHSE."))) {
        return normalized.mid(5) + QStringLiteral(".SH");
    }
    if (normalized.startsWith(QStringLiteral("SZSE."))) {
        return normalized.mid(5) + QStringLiteral(".SZ");
    }
    if (normalized.startsWith(QStringLiteral("BSE."))) {
        return normalized.mid(4) + QStringLiteral(".BJ");
    }

    if (normalized.startsWith(QStringLiteral("CFFEX."))) {
        return normalized.mid(6) + QStringLiteral(".CFFEX");
    }
    if (normalized.startsWith(QStringLiteral("SHFE."))) {
        return normalized.mid(5) + QStringLiteral(".SHFE");
    }
    if (normalized.startsWith(QStringLiteral("DCE."))) {
        return normalized.mid(4) + QStringLiteral(".DCE");
    }
    if (normalized.startsWith(QStringLiteral("CZCE."))) {
        return normalized.mid(5) + QStringLiteral(".CZCE");
    }
    if (normalized.startsWith(QStringLiteral("INE."))) {
        return normalized.mid(4) + QStringLiteral(".INE");
    }
    if (normalized.startsWith(QStringLiteral("GFEX."))) {
        return normalized.mid(5) + QStringLiteral(".GFEX");
    }

    const int dotIndex = normalized.indexOf('.');
    if (dotIndex > 0) {
        const QString code = normalized.left(dotIndex);
        const QString exchange = normalized.mid(dotIndex + 1);
        if (exchange == QStringLiteral("SHSE")) {
            return code + QStringLiteral(".SH");
        }
        if (exchange == QStringLiteral("SZSE")) {
            return code + QStringLiteral(".SZ");
        }
        if (exchange == QStringLiteral("BSE")) {
            return code + QStringLiteral(".BJ");
        }
        if (exchange == QStringLiteral("CFFEX") || exchange == QStringLiteral("SHFE")
            || exchange == QStringLiteral("DCE") || exchange == QStringLiteral("CZCE")
            || exchange == QStringLiteral("INE") || exchange == QStringLiteral("GFEX")) {
            return code + QStringLiteral(".") + exchange;
        }
    }

    return normalized;
}

void MarketDataService::setWatchlist(const QStringList& symbols)
{
    QStringList normalizedSymbols;
    for (const QString& symbol : symbols) {
        const QString trimmed = normalizeSymbol(symbol);
        if (!trimmed.isEmpty() && !normalizedSymbols.contains(trimmed)) {
            normalizedSymbols.push_back(trimmed);
        }
    }

    bool primaryChanged = false;
    {
        QMutexLocker locker(&m_mutex);
        if (normalizedSymbols.isEmpty()) {
            primaryChanged = !m_primarySymbol.isEmpty();
            m_watchlist.clear();
            m_primarySymbol.clear();

            for (auto it = m_snapshotsBySymbol.begin(); it != m_snapshotsBySymbol.end();) {
                if (it.value().value(QStringLiteral("source")).toString() == QStringLiteral("watchlist")) {
                    it = m_snapshotsBySymbol.erase(it);
                } else {
                    ++it;
                }
            }
        } else {
            primaryChanged = m_primarySymbol != normalizedSymbols.front();
            m_watchlist = normalizedSymbols;
            if (!m_watchlist.isEmpty()) {
                m_primarySymbol = m_watchlist.front();
            }
            for (const QString& symbol : m_watchlist) {
                if (!m_snapshotsBySymbol.contains(symbol)) {
                    QVariantMap snapshot = defaultSnapshotMap().value(symbol);
                    if (snapshot.isEmpty()) {
                        snapshot.insert(QStringLiteral("symbol"), symbol);
                        snapshot.insert(QStringLiteral("name"), defaultNameMap().value(symbol, symbol));
                        snapshot.insert(QStringLiteral("price"), 0.0);
                        snapshot.insert(QStringLiteral("change"), 0.0);
                        snapshot.insert(QStringLiteral("color"), QStringLiteral("#3b82f6"));
                        snapshot.insert(QStringLiteral("source"), QStringLiteral("watchlist"));
                        snapshot.insert(QStringLiteral("updatedAt"), QStringLiteral("--"));
                    }
                    m_snapshotsBySymbol.insert(symbol, snapshot);
                }
            }
        }
    }

    if (primaryChanged) {
        emit primarySymbolChanged();
    }
    emit marketSnapshotsChanged();

    if (normalizedSymbols.isEmpty()) {
        return;
    }

    qInfo() << "MarketDataService: setWatchlist"
            << "size=" << normalizedSymbols.size()
            << "primary=" << normalizedSymbols.front();

    for (const QString& symbol : normalizedSymbols) {
        publishWatchRequest(symbol);
    }
}

void MarketDataService::ensureWatchSymbol(const QString& symbol)
{
    const QString normalizedSymbol = normalizeSymbol(symbol);
    if (normalizedSymbol.isEmpty()) {
        return;
    }

    bool primaryChanged = false;
    bool shouldPublishWatchRequest = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_watchlist.contains(normalizedSymbol)) {
            m_watchlist.push_front(normalizedSymbol);
            shouldPublishWatchRequest = true;
        } else {
            m_watchlist.removeAll(normalizedSymbol);
            m_watchlist.push_front(normalizedSymbol);
        }
        while (m_watchlist.size() > 8) {
            m_watchlist.removeLast();
        }
        if (m_primarySymbol != normalizedSymbol) {
            m_primarySymbol = normalizedSymbol;
            primaryChanged = true;
        }

        QVariantMap snapshot = m_snapshotsBySymbol.value(normalizedSymbol);
        if (snapshot.isEmpty()) {
            snapshot = defaultSnapshotMap().value(normalizedSymbol);
        }
        if (snapshot.isEmpty()) {
            snapshot.insert(QStringLiteral("symbol"), normalizedSymbol);
            snapshot.insert(QStringLiteral("name"), defaultNameMap().value(normalizedSymbol, normalizedSymbol));
            snapshot.insert(QStringLiteral("price"), 0.0);
            snapshot.insert(QStringLiteral("change"), 0.0);
            snapshot.insert(QStringLiteral("color"), QStringLiteral("#3b82f6"));
            snapshot.insert(QStringLiteral("source"), QStringLiteral("watchlist"));
            snapshot.insert(QStringLiteral("updatedAt"), QStringLiteral("--"));
        }
        snapshot = hydrateDisplaySnapshot(snapshot);
        m_snapshotsBySymbol.insert(normalizedSymbol, snapshot);
    }

    if (primaryChanged) {
        emit primarySymbolChanged();
    }
    emit marketSnapshotsChanged();
    if (shouldPublishWatchRequest) {
        publishWatchRequest(normalizedSymbol);
    }
}

void MarketDataService::publishWatchRequest(const QString& symbol) const
{
    const QString normalizedSymbol = normalizeSymbol(symbol);
    if (normalizedSymbol.isEmpty()) {
        return;
    }

    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        return;
    }

    logWatchRequestThrottled(normalizedSymbol, QStringLiteral("publishWatchRequest"));

    engine::EventFormat event = engine::EventFormat::create_from_strings(
        engine::EventTypes::MARKET_WATCH_ENSURE,
        "MARKET_DATA_SERVICE",
        0);
    event.set("symbol", normalizedSymbol.toStdString());
    event.metadata["symbol"] = normalizedSymbol.toStdString();
    const auto result = bus->publish(event, static_cast<int>(engine::EventPriority::HIGH));
    if (!result) {
        qWarning() << "MarketDataService: failed to publish watch request" << normalizedSymbol << QString::fromStdString(result.message);
    }
}

void MarketDataService::logWatchRequestThrottled(const QString& symbol, const QString& reason) const
{
    qint64 now = 0;
    QString primarySymbol;
    int watchlistSize = 0;
    int runtimeSubscriptionCount = 0;
    int runtimeSubscriptionLimit = 0;
    int suppressedCount = 0;
    bool shouldLog = false;

    {
        QMutexLocker locker(&m_mutex);
        now = QDateTime::currentMSecsSinceEpoch();
        if (m_lastWatchRequestLogSymbol == symbol && now - m_lastWatchRequestLogAtMs < 1200) {
            ++m_suppressedWatchRequestLogs;
            return;
        }

        suppressedCount = m_suppressedWatchRequestLogs;
        m_suppressedWatchRequestLogs = 0;
        m_lastWatchRequestLogSymbol = symbol;
        m_lastWatchRequestLogAtMs = now;
        primarySymbol = m_primarySymbol;
        watchlistSize = m_watchlist.size();
        runtimeSubscriptionCount = m_runtimeSubscriptionCount;
        runtimeSubscriptionLimit = m_runtimeSubscriptionLimit;
        shouldLog = true;
    }

    if (suppressedCount > 0) {
        qInfo() << "MarketDataService: suppressed duplicate watch request logs"
                << "count=" << suppressedCount;
    }

    if (shouldLog) {
        qInfo() << "MarketDataService: publish watch request"
                << "reason=" << reason
                << "symbol=" << symbol
                << "primary=" << primarySymbol
                << "watchlistSize=" << watchlistSize
                << "runtimeSubscriptions=" << QString::number(runtimeSubscriptionCount)
                                         + "/"
                                         + QString::number(runtimeSubscriptionLimit);
    }
}

void MarketDataService::initializeEventBusIntegration()
{
    if (m_eventBusIntegrated) {
        return;
    }

    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        qWarning() << "MarketDataService: EventBus not ready, skip event integration";
        return;
    }

    m_marketTickSubscription = bus->subscribe(engine::EventTypes::MARKET_TICK,
        [this](const engine::EventFormat& event) {
            handleMarketEvent(event, QStringLiteral("market.tick"));
        });

    m_marketBarSubscription = bus->subscribe(engine::EventTypes::MARKET_BAR,
        [this](const engine::EventFormat& event) {
            handleMarketEvent(event, QStringLiteral("market.bar"));
        });

    m_tradingMarketTickSubscription = bus->subscribe(engine::EventTypes::TRADING_MARKET_TICK,
        [this](const engine::EventFormat& event) {
            handleMarketEvent(event, QStringLiteral("trading.market.tick"));
        });

    m_tradingMarketBarSubscription = bus->subscribe(engine::EventTypes::TRADING_MARKET_BAR,
        [this](const engine::EventFormat& event) {
            handleMarketEvent(event, QStringLiteral("trading.market.bar"));
        });

    m_runtimeSubscriptionStatusSubscription = bus->subscribe(kRuntimeSubscriptionStatusEvent,
        [this](const engine::EventFormat& event) {
            const int subscriptionCount = static_cast<int>(eventNumericValue(event, "subscription_count", 0.0));
            const int subscriptionLimit = static_cast<int>(eventNumericValue(event, "subscription_limit", 0.0));

            bool changed = false;
            {
                QMutexLocker locker(&m_mutex);
                if (m_runtimeSubscriptionCount != subscriptionCount
                    || m_runtimeSubscriptionLimit != subscriptionLimit) {
                    m_runtimeSubscriptionCount = subscriptionCount;
                    m_runtimeSubscriptionLimit = subscriptionLimit;
                    changed = true;
                }
            }

            if (changed) {
                qInfo() << "MarketDataService: runtime subscription status changed"
                        << "count=" << subscriptionCount
                        << "limit=" << subscriptionLimit;
                emit runtimeSubscriptionStatusChanged();
            }
        });

    m_eventBusIntegrated = true;
    qDebug() << "MarketDataService: EventBus integration initialized";
}

void MarketDataService::queueSnapshotUpdate(const QVariantMap& snapshot, bool liveUpdate)
{
    if (snapshot.isEmpty()) {
        return;
    }

    bool shouldScheduleFlush = false;
    {
        QMutexLocker locker(&m_pendingMutex);
        m_pendingSnapshots.push_back(snapshot);
        m_pendingLiveUpdate = m_pendingLiveUpdate || liveUpdate;
        if (!m_flushScheduled) {
            m_flushScheduled = true;
            shouldScheduleFlush = true;
        }
    }

    if (!shouldScheduleFlush) {
        return;
    }

    invokeOnMainThread(this,
        [](MarketDataService* service) {
            service->flushPendingSnapshots();
        });
}

void MarketDataService::flushPendingSnapshots()
{
    QVector<QVariantMap> pendingSnapshots;
    bool liveUpdate = false;
    {
        QMutexLocker locker(&m_pendingMutex);
        if (m_pendingSnapshots.isEmpty()) {
            m_flushScheduled = false;
            m_pendingLiveUpdate = false;
            return;
        }

        pendingSnapshots.swap(m_pendingSnapshots);
        liveUpdate = m_pendingLiveUpdate;
        m_pendingLiveUpdate = false;
        m_flushScheduled = false;
    }

    bool primaryChanged = false;
    bool liveStateChanged = false;
    QVariantMap lastMergedSnapshot;
    {
        QMutexLocker locker(&m_mutex);
        for (const QVariantMap& snapshot : pendingSnapshots) {
            const QVariantMap mergedSnapshot = mergeSnapshotLocked(snapshot,
                                                                   liveUpdate,
                                                                   &primaryChanged,
                                                                   &liveStateChanged);
            if (!mergedSnapshot.isEmpty()) {
                lastMergedSnapshot = mergedSnapshot;
            }
        }
    }

    if (lastMergedSnapshot.isEmpty()) {
        return;
    }

    if (primaryChanged) {
        emit primarySymbolChanged();
    }
    if (liveStateChanged) {
        emit hasLiveDataChanged();
    }
    emit marketSnapshotsChanged();
    emit marketEventReceived(lastMergedSnapshot);
}

QVariantMap MarketDataService::mergeSnapshotLocked(const QVariantMap& snapshot,
                                                   bool liveUpdate,
                                                   bool* primaryChanged,
                                                   bool* liveStateChanged)
{
    const QString symbol = snapshot.value(QStringLiteral("symbol")).toString().trimmed();
    if (symbol.isEmpty()) {
        return {};
    }

    QVariantMap mergedSnapshot;
    const QVariantMap previousSnapshot = m_snapshotsBySymbol.value(symbol);
    mergedSnapshot = previousSnapshot;
    for (auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it) {
        mergedSnapshot.insert(it.key(), it.value());
    }

    const QVariantMap latestTick = mergedSnapshot.value(QStringLiteral("latestTick")).toMap();
    if (!latestTick.isEmpty()) {
        QVariantMap normalizedTick = latestTick;
        QString direction = normalizedTick.value(QStringLiteral("direction")).toString().trimmed().toLower();
        if (direction.isEmpty()) {
            direction = inferTickDirection(previousSnapshot, normalizedTick);
        }
        normalizedTick.insert(QStringLiteral("direction"), direction);

        QVariantList recentTicks = previousSnapshot.value(QStringLiteral("recentTicks")).toList();
        recentTicks.push_front(normalizedTick);
        while (recentTicks.size() > 20) {
            recentTicks.removeLast();
        }

        mergedSnapshot.insert(QStringLiteral("recentTicks"), recentTicks);
        mergedSnapshot.remove(QStringLiteral("latestTick"));
    }

    const QString currentName = mergedSnapshot.value(QStringLiteral("name")).toString().trimmed();
    if (currentName.isEmpty() || currentName == symbol) {
        mergedSnapshot.insert(QStringLiteral("name"), resolveDisplayName(symbol));
    }
    m_snapshotsBySymbol.insert(symbol, mergedSnapshot);

    if (m_watchlist.isEmpty()) {
        m_watchlist.push_back(symbol);
    } else if (!m_watchlist.contains(symbol)) {
        m_watchlist.push_back(symbol);
    }

    if (m_primarySymbol.isEmpty()) {
        m_primarySymbol = symbol;
        if (primaryChanged) {
            *primaryChanged = true;
        }
    }

    if (liveUpdate && !m_hasLiveData) {
        m_hasLiveData = true;
        if (liveStateChanged) {
            *liveStateChanged = true;
        }
    }

    return mergedSnapshot;
}

void MarketDataService::upsertSnapshot(const QVariantMap& snapshot, bool liveUpdate)
{
    bool primaryChanged = false;
    bool liveStateChanged = false;
    QVariantMap mergedSnapshot;
    {
        QMutexLocker locker(&m_mutex);
        mergedSnapshot = mergeSnapshotLocked(snapshot, liveUpdate, &primaryChanged, &liveStateChanged);
    }

    if (mergedSnapshot.isEmpty()) {
        return;
    }

    if (primaryChanged) {
        emit primarySymbolChanged();
    }
    if (liveStateChanged) {
        emit hasLiveDataChanged();
    }
    emit marketSnapshotsChanged();
    emit marketEventReceived(mergedSnapshot);
}

void MarketDataService::handleMarketEvent(const engine::EventFormat& event, const QString& eventType)
{
    const QVariantMap snapshot = buildSnapshotFromEvent(event, eventType);
    if (snapshot.isEmpty()) {
        return;
    }
    queueSnapshotUpdate(snapshot, true);
}

QVariantMap MarketDataService::buildSnapshotFromEvent(const engine::EventFormat& event, const QString& eventType) const
{
    const QString symbol = normalizeSymbol(eventStringValue(event, "symbol"));
    if (symbol.isEmpty()) {
        return {};
    }

    const double closePrice = eventNumericValue(event, "close", 0.0);
    const double tickPrice = eventNumericValue(event, "price", 0.0);
    const double latestPrice = closePrice > 0.0 ? closePrice : tickPrice;
    if (latestPrice <= 0.0) {
        return {};
    }

    double referencePrice = eventNumericValue(event, "pre_close", 0.0);
    if (referencePrice <= 0.0) {
        referencePrice = eventNumericValue(event, "open", 0.0);
    }

    double changePercent = 0.0;
    if (referencePrice > 0.0) {
        changePercent = (latestPrice - referencePrice) / referencePrice * 100.0;
    }

    const QString displayName = eventDisplayName(event);
    const QString resolvedDisplayName = !displayName.isEmpty() ? displayName : symbol;

    QVariantMap snapshot;
    const QString updatedAt = preferredEventTimestamp(event);
    snapshot.insert(QString(factor::bridge::CommonFieldKeys::SYMBOL), symbol);
    snapshot.insert(QStringLiteral("name"), resolvedDisplayName);
    snapshot.insert(QStringLiteral("price"), latestPrice);
    snapshot.insert(QString(factor::bridge::MarketBarFieldKeys::OPEN), eventNumericValue(event, "open", referencePrice));
    snapshot.insert(QString(factor::bridge::MarketBarFieldKeys::HIGH), eventNumericValue(event, "high", latestPrice));
    snapshot.insert(QString(factor::bridge::MarketBarFieldKeys::LOW), eventNumericValue(event, "low", latestPrice));
    snapshot.insert(QString(factor::bridge::MarketBarFieldKeys::CLOSE), closePrice > 0.0 ? closePrice : latestPrice);
    snapshot.insert(QStringLiteral("preClose"), referencePrice);
    snapshot.insert(QString(factor::bridge::MarketBarFieldKeys::VOLUME), eventNumericValue(event, "volume", eventNumericValue(event, "cum_volume", 0.0)));
    snapshot.insert(QStringLiteral("amount"), eventNumericValue(event, "amount", eventNumericValue(event, "cum_amount", 0.0)));
    snapshot.insert(QStringLiteral("change"), changePercent);
    snapshot.insert(QStringLiteral("color"), colorForChange(changePercent));
    snapshot.insert(QStringLiteral("source"), eventType);
    snapshot.insert(QStringLiteral("updatedAt"), updatedAt);

    if (eventType.endsWith(QStringLiteral(".tick"))) {
        std::vector<double> bidPrices = eventNumericVectorValue(event, "bid_prices");
        std::vector<double> bidVolumes = eventNumericVectorValue(event, "bid_volumes");
        std::vector<double> askPrices = eventNumericVectorValue(event, "ask_prices");
        std::vector<double> askVolumes = eventNumericVectorValue(event, "ask_volumes");

        if (bidPrices.empty()) {
            const double bidPrice = eventNumericValue(event, "bid_price", 0.0);
            const double bidVolume = eventNumericValue(event, "bid_volume", 0.0);
            if (bidPrice > 0.0 || bidVolume > 0.0) {
                bidPrices.push_back(bidPrice);
                bidVolumes.push_back(bidVolume);
            }
        }

        if (askPrices.empty()) {
            const double askPrice = eventNumericValue(event, "ask_price", 0.0);
            const double askVolume = eventNumericValue(event, "ask_volume", 0.0);
            if (askPrice > 0.0 || askVolume > 0.0) {
                askPrices.push_back(askPrice);
                askVolumes.push_back(askVolume);
            }
        }

        const QVariantMap depthSnapshot = buildDepthSnapshot(bidPrices, bidVolumes, askPrices, askVolumes, eventType);
        if (!depthSnapshot.isEmpty()) {
            snapshot.insert(QStringLiteral("depthSnapshot"), depthSnapshot);
        }

        QVariantMap latestTick;
        const double lastVolume = eventNumericValue(event, "last_volume", eventNumericValue(event, "volume", 0.0));
        latestTick.insert(QStringLiteral("time"), tickDisplayTime(updatedAt));
        latestTick.insert(QStringLiteral("fullTime"), updatedAt);
        latestTick.insert(QStringLiteral("price"), latestPrice);
        latestTick.insert(QStringLiteral("volume"), lastVolume > 0.0 ? lastVolume : eventNumericValue(event, "cum_volume", 0.0));
        latestTick.insert(QStringLiteral("direction"), QString());
        snapshot.insert(QStringLiteral("latestTick"), latestTick);
    }

    return snapshot;
}

QVariantList MarketDataService::orderedSnapshotsLocked() const
{
    QVariantList result;
    result.reserve(m_snapshotsBySymbol.size());

    for (const QString& symbol : m_watchlist) {
        if (m_snapshotsBySymbol.contains(symbol)) {
            result.push_back(hydrateDisplaySnapshot(m_snapshotsBySymbol.value(symbol)));
        }
    }

    for (auto it = m_snapshotsBySymbol.constBegin(); it != m_snapshotsBySymbol.constEnd(); ++it) {
        if (!m_watchlist.contains(it.key())) {
            result.push_back(hydrateDisplaySnapshot(it.value()));
        }
    }

    return result;
}

QString MarketDataService::resolveDisplayName(const QString& symbol) const
{
    const auto& names = defaultNameMap();
    if (names.contains(symbol)) {
        return names.value(symbol);
    }

    const QString databaseName = lookupDisplayNameFromDatabase(symbol);
    if (!databaseName.isEmpty()) {
        return databaseName;
    }

    return symbol;
}
