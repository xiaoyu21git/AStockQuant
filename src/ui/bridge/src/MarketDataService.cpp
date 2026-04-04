#include "MarketDataService.h"

#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"
#include "DatabaseConnectionManager.h"

#include <QDateTime>
#include <QMetaObject>
#include <QPointer>
#include <QMutexLocker>
#include <QDebug>

#include <algorithm>

namespace {

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
        {QStringLiteral("600000.SH"), QStringLiteral("浦发银行")},
        {QStringLiteral("000001.SZ"), QStringLiteral("平安银行")},
        {QStringLiteral("600519.SH"), QStringLiteral("贵州茅台")},
        {QStringLiteral("300750.SZ"), QStringLiteral("宁德时代")},
        {QStringLiteral("601318.SH"), QStringLiteral("中国平安")},
        {QStringLiteral("688981.SH"), QStringLiteral("中芯国际")},
        {QStringLiteral("000858.SZ"), QStringLiteral("五粮液")}
    };
}

QHash<QString, QVariantMap> createDefaultSnapshots()
{
    return {
        {QStringLiteral("600000.SH"), QVariantMap{{QStringLiteral("symbol"), QStringLiteral("600000.SH")}, {QStringLiteral("name"), QStringLiteral("浦发银行")}, {QStringLiteral("price"), 0.0}, {QStringLiteral("change"), 0.0}, {QStringLiteral("color"), QStringLiteral("#3b82f6")}, {QStringLiteral("source"), QStringLiteral("seed")}, {QStringLiteral("updatedAt"), QStringLiteral("--")}}},
        {QStringLiteral("000001.SZ"), QVariantMap{{QStringLiteral("symbol"), QStringLiteral("000001.SZ")}, {QStringLiteral("name"), QStringLiteral("平安银行")}, {QStringLiteral("price"), 12.48}, {QStringLiteral("change"), 1.36}, {QStringLiteral("color"), QStringLiteral("#10b981")}, {QStringLiteral("source"), QStringLiteral("seed")}, {QStringLiteral("updatedAt"), QStringLiteral("--")}}},
        {QStringLiteral("600519.SH"), QVariantMap{{QStringLiteral("symbol"), QStringLiteral("600519.SH")}, {QStringLiteral("name"), QStringLiteral("贵州茅台")}, {QStringLiteral("price"), 1688.00}, {QStringLiteral("change"), 0.82}, {QStringLiteral("color"), QStringLiteral("#10b981")}, {QStringLiteral("source"), QStringLiteral("seed")}, {QStringLiteral("updatedAt"), QStringLiteral("--")}}},
        {QStringLiteral("300750.SZ"), QVariantMap{{QStringLiteral("symbol"), QStringLiteral("300750.SZ")}, {QStringLiteral("name"), QStringLiteral("宁德时代")}, {QStringLiteral("price"), 196.35}, {QStringLiteral("change"), -1.15}, {QStringLiteral("color"), QStringLiteral("#ef4444")}, {QStringLiteral("source"), QStringLiteral("seed")}, {QStringLiteral("updatedAt"), QStringLiteral("--")}}},
        {QStringLiteral("601318.SH"), QVariantMap{{QStringLiteral("symbol"), QStringLiteral("601318.SH")}, {QStringLiteral("name"), QStringLiteral("中国平安")}, {QStringLiteral("price"), 42.16}, {QStringLiteral("change"), 0.58}, {QStringLiteral("color"), QStringLiteral("#10b981")}, {QStringLiteral("source"), QStringLiteral("seed")}, {QStringLiteral("updatedAt"), QStringLiteral("--")}}},
        {QStringLiteral("688981.SH"), QVariantMap{{QStringLiteral("symbol"), QStringLiteral("688981.SH")}, {QStringLiteral("name"), QStringLiteral("中芯国际")}, {QStringLiteral("price"), 48.22}, {QStringLiteral("change"), 2.41}, {QStringLiteral("color"), QStringLiteral("#10b981")}, {QStringLiteral("source"), QStringLiteral("seed")}, {QStringLiteral("updatedAt"), QStringLiteral("--")}}},
        {QStringLiteral("000858.SZ"), QVariantMap{{QStringLiteral("symbol"), QStringLiteral("000858.SZ")}, {QStringLiteral("name"), QStringLiteral("五粮液")}, {QStringLiteral("price"), 136.70}, {QStringLiteral("change"), -0.43}, {QStringLiteral("color"), QStringLiteral("#ef4444")}, {QStringLiteral("source"), QStringLiteral("seed")}, {QStringLiteral("updatedAt"), QStringLiteral("--")}}}
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

    const QString normalizedSymbol = symbol.trimmed().toUpper();
    if (normalizedSymbol.isEmpty()) {
        return {};
    }

    {
        QMutexLocker locker(&cacheMutex);
        if (cachedNames.contains(normalizedSymbol)) {
            return cachedNames.value(normalizedSymbol);
        }
    }

    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        return {};
    }

    const auto result = database->executeQuery(
        QStringLiteral("SELECT name FROM symbol_info WHERE symbol = :symbol LIMIT 1"),
        {{QStringLiteral(":symbol"), normalizedSymbol}});
    if (result.isEmpty()) {
        return {};
    }

    const QString resolvedName = result.getRow(0).getString(QStringLiteral("name")).trimmed();
    if (resolvedName.isEmpty()) {
        return {};
    }

    {
        QMutexLocker locker(&cacheMutex);
        cachedNames.insert(normalizedSymbol, resolvedName);
    }
    return resolvedName;
}

QVariantMap lookupLatestDailySnapshotFromDatabase(const QString& symbol)
{
    static QMutex cacheMutex;
    static QHash<QString, QVariantMap> cachedSnapshots;

    const QString normalizedSymbol = symbol.trimmed().toUpper();
    if (normalizedSymbol.isEmpty()) {
        return {};
    }

    {
        QMutexLocker locker(&cacheMutex);
        if (cachedSnapshots.contains(normalizedSymbol)) {
            return cachedSnapshots.value(normalizedSymbol);
        }
    }

    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        return {};
    }

    const auto result = database->executeQuery(
        QStringLiteral(
            "SELECT d.symbol AS symbol, COALESCE(si.name, d.symbol) AS name, "
            "DATE_FORMAT(d.trade_date, '%Y-%m-%d') AS trade_date, d.close AS close, d.pre_close AS pre_close "
            "FROM daily_bar d "
            "LEFT JOIN symbol_info si ON d.symbol = si.symbol "
            "WHERE d.symbol = :symbol "
            "ORDER BY d.trade_date DESC LIMIT 1"),
        {{QStringLiteral(":symbol"), normalizedSymbol}});
    if (result.isEmpty()) {
        return {};
    }

    const auto& row = result.getRow(0);
    const double closePrice = row.getDouble(QStringLiteral("close"));
    if (closePrice <= 0.0) {
        return {};
    }

    double preClosePrice = row.getDouble(QStringLiteral("pre_close"));
    if (preClosePrice <= 0.0) {
        preClosePrice = closePrice;
    }

    const double changePercent = preClosePrice > 0.0
        ? (closePrice - preClosePrice) / preClosePrice * 100.0
        : 0.0;

    QVariantMap snapshot;
    snapshot.insert(QStringLiteral("symbol"), normalizedSymbol);
    snapshot.insert(QStringLiteral("name"), row.getString(QStringLiteral("name")).trimmed());
    snapshot.insert(QStringLiteral("price"), closePrice);
    snapshot.insert(QStringLiteral("close"), closePrice);
    snapshot.insert(QStringLiteral("preClose"), preClosePrice);
    snapshot.insert(QStringLiteral("pre_close"), preClosePrice);
    snapshot.insert(QStringLiteral("change"), changePercent);
    snapshot.insert(QStringLiteral("color"), colorForChange(changePercent));
    snapshot.insert(QStringLiteral("source"), QStringLiteral("daily_snapshot"));
    snapshot.insert(QStringLiteral("updatedAt"), row.getString(QStringLiteral("trade_date")).trimmed());
    snapshot.insert(QStringLiteral("live"), false);

    {
        QMutexLocker locker(&cacheMutex);
        cachedSnapshots.insert(normalizedSymbol, snapshot);
    }

    return snapshot;
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
    const QString symbol = snapshot.value(QStringLiteral("symbol")).toString().trimmed().toUpper();
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
    , m_primarySymbol(QStringLiteral("000001.SZ"))
{
}

void MarketDataService::initialize()
{
    QMutexLocker locker(&m_mutex);
    if (m_initialized) {
        return;
    }

    seedDefaultWatchlist();
    initializeEventBusIntegration();
    m_initialized = true;
    emit initializedChanged();
    emit marketSnapshotsChanged();
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

QVariantMap MarketDataService::resolveInstrument(const QString& query) const
{
    const QString normalizedQuery = normalizeSymbol(query);
    const QString keyword = query.trimmed();
    if (normalizedQuery.isEmpty() && keyword.isEmpty()) {
        return {};
    }

    QMutexLocker locker(&m_mutex);

    auto buildResult = [](const QVariantMap& snapshot) {
        QVariantMap result = hydrateDisplaySnapshot(snapshot);
        result.insert(QStringLiteral("matched"), true);
        return result;
    };

    for (auto it = m_snapshotsBySymbol.constBegin(); it != m_snapshotsBySymbol.constEnd(); ++it) {
        const QVariantMap snapshot = it.value();
        if (normalizeSymbol(snapshot.value(QStringLiteral("symbol")).toString()) == normalizedQuery) {
            return buildResult(snapshot);
        }
    }

    const QString plainCode = canonicalSymbolCode(normalizedQuery);
    if (!plainCode.isEmpty()) {
        for (auto it = m_snapshotsBySymbol.constBegin(); it != m_snapshotsBySymbol.constEnd(); ++it) {
            const QVariantMap snapshot = it.value();
            if (canonicalSymbolCode(normalizeSymbol(snapshot.value(QStringLiteral("symbol")).toString())) == plainCode) {
                return buildResult(snapshot);
            }
        }
    }

    for (auto it = m_snapshotsBySymbol.constBegin(); it != m_snapshotsBySymbol.constEnd(); ++it) {
        const QVariantMap snapshot = it.value();
        if (snapshot.value(QStringLiteral("name")).toString().trimmed() == keyword) {
            return buildResult(snapshot);
        }
    }

    for (auto it = m_snapshotsBySymbol.constBegin(); it != m_snapshotsBySymbol.constEnd(); ++it) {
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

    if (normalizedSymbols.isEmpty()) {
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        m_watchlist = normalizedSymbols;
        if (!m_watchlist.isEmpty()) {
            m_primarySymbol = m_watchlist.front();
        }
        for (const QString& symbol : m_watchlist) {
            if (!m_snapshotsBySymbol.contains(symbol)) {
                QVariantMap snapshot = defaultSnapshotMap().value(symbol);
                if (snapshot.isEmpty()) {
                    snapshot.insert(QStringLiteral("symbol"), symbol);
                    snapshot.insert(QStringLiteral("name"), resolveDisplayName(symbol));
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

    emit primarySymbolChanged();
    emit marketSnapshotsChanged();

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
    {
        QMutexLocker locker(&m_mutex);
        if (!m_watchlist.contains(normalizedSymbol)) {
            m_watchlist.push_front(normalizedSymbol);
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
            snapshot.insert(QStringLiteral("name"), resolveDisplayName(normalizedSymbol));
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
    publishWatchRequest(normalizedSymbol);
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
            const engine::EventFormat queuedEvent = event;
            invokeOnMainThread(this,
                [queuedEvent](MarketDataService* service) {
                    service->handleMarketEvent(queuedEvent, QStringLiteral("market.tick"));
                });
        });

    m_marketBarSubscription = bus->subscribe(engine::EventTypes::MARKET_BAR,
        [this](const engine::EventFormat& event) {
            const engine::EventFormat queuedEvent = event;
            invokeOnMainThread(this,
                [queuedEvent](MarketDataService* service) {
                    service->handleMarketEvent(queuedEvent, QStringLiteral("market.bar"));
                });
        });

    m_tradingMarketTickSubscription = bus->subscribe(engine::EventTypes::TRADING_MARKET_TICK,
        [this](const engine::EventFormat& event) {
            const engine::EventFormat queuedEvent = event;
            invokeOnMainThread(this,
                [queuedEvent](MarketDataService* service) {
                    service->handleMarketEvent(queuedEvent, QStringLiteral("trading.market.tick"));
                });
        });

    m_tradingMarketBarSubscription = bus->subscribe(engine::EventTypes::TRADING_MARKET_BAR,
        [this](const engine::EventFormat& event) {
            const engine::EventFormat queuedEvent = event;
            invokeOnMainThread(this,
                [queuedEvent](MarketDataService* service) {
                    service->handleMarketEvent(queuedEvent, QStringLiteral("trading.market.bar"));
                });
        });

    m_eventBusIntegrated = true;
    qDebug() << "MarketDataService: EventBus integration initialized";
}

void MarketDataService::seedDefaultWatchlist()
{
    if (!m_watchlist.isEmpty()) {
        return;
    }

    m_watchlist = QStringList{
        QStringLiteral("000001.SZ"),
        QStringLiteral("600519.SH"),
        QStringLiteral("300750.SZ"),
        QStringLiteral("601318.SH"),
        QStringLiteral("688981.SH"),
        QStringLiteral("000858.SZ")
    };

    for (const QString& symbol : m_watchlist) {
        QVariantMap snapshot = defaultSnapshotMap().value(symbol);
        if (snapshot.isEmpty()) {
            snapshot.insert(QStringLiteral("symbol"), symbol);
            snapshot.insert(QStringLiteral("name"), resolveDisplayName(symbol));
            snapshot.insert(QStringLiteral("price"), 0.0);
            snapshot.insert(QStringLiteral("change"), 0.0);
            snapshot.insert(QStringLiteral("color"), QStringLiteral("#3b82f6"));
            snapshot.insert(QStringLiteral("source"), QStringLiteral("watchlist"));
            snapshot.insert(QStringLiteral("updatedAt"), QStringLiteral("--"));
        }
        snapshot = hydrateDisplaySnapshot(snapshot);
        m_snapshotsBySymbol.insert(symbol, snapshot);
    }
}

void MarketDataService::upsertSnapshot(const QVariantMap& snapshot, bool liveUpdate)
{
    const QString symbol = snapshot.value(QStringLiteral("symbol")).toString().trimmed();
    if (symbol.isEmpty()) {
        return;
    }

    bool primaryChanged = false;
    bool liveStateChanged = false;
    QVariantMap mergedSnapshot;
    {
        QMutexLocker locker(&m_mutex);
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

        if (!mergedSnapshot.contains(QStringLiteral("name")) || mergedSnapshot.value(QStringLiteral("name")).toString().isEmpty()) {
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
            primaryChanged = true;
        }

        if (liveUpdate && !m_hasLiveData) {
            m_hasLiveData = true;
            liveStateChanged = true;
        }
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
    upsertSnapshot(snapshot, true);
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
    const QString canonicalDisplayName = resolveDisplayName(symbol);
    const QString resolvedDisplayName = !canonicalDisplayName.isEmpty()
        ? canonicalDisplayName
        : (!displayName.isEmpty() ? displayName : symbol);

    QVariantMap snapshot;
    const QString updatedAt = preferredEventTimestamp(event);
    snapshot.insert(QStringLiteral("symbol"), symbol);
    snapshot.insert(QStringLiteral("name"), resolvedDisplayName);
    snapshot.insert(QStringLiteral("price"), latestPrice);
    snapshot.insert(QStringLiteral("open"), eventNumericValue(event, "open", referencePrice));
    snapshot.insert(QStringLiteral("high"), eventNumericValue(event, "high", latestPrice));
    snapshot.insert(QStringLiteral("low"), eventNumericValue(event, "low", latestPrice));
    snapshot.insert(QStringLiteral("close"), closePrice > 0.0 ? closePrice : latestPrice);
    snapshot.insert(QStringLiteral("preClose"), referencePrice);
    snapshot.insert(QStringLiteral("volume"), eventNumericValue(event, "volume", eventNumericValue(event, "cum_volume", 0.0)));
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
            result.push_back(m_snapshotsBySymbol.value(symbol));
        }
    }

    for (auto it = m_snapshotsBySymbol.constBegin(); it != m_snapshotsBySymbol.constEnd(); ++it) {
        if (!m_watchlist.contains(it.key())) {
            result.push_back(it.value());
        }
    }

    return result;
}

QString MarketDataService::resolveDisplayName(const QString& symbol) const
{
    const QString databaseName = lookupDisplayNameFromDatabase(symbol);
    if (!databaseName.isEmpty()) {
        return databaseName;
    }

    const auto& names = defaultNameMap();
    if (names.contains(symbol)) {
        return names.value(symbol);
    }
    return symbol;
}
