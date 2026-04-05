#include "RiskMonitorService.h"

#include "FactorService.h"
#include "MarketDataService.h"
#include "PositionAccountService.h"
#include "RiskConfigService.h"
#include "StrategyService.h"
#include "TradeExecutionService.h"
#include "TradingConnectionConfigService.h"

#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"

#include "../../domain/backtest/include/DatabaseStockDataProvider.h"

#include <QDate>
#include <QDateTime>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QMutexLocker>
#include <QSet>
#include <QThread>
#include <QVariantList>

#include <algorithm>
#include <cmath>

namespace {

struct PortfolioFactorAllocation {
    QString factorId;
    QString factorName;
    double weight{0.0};
};

struct ScoreState {
    double score{0.0};
    int contributionCount{0};
};

struct PositionAccountState {
    QVariantMap accountSnapshot;
    QVariantList positions;
};

struct StrategyLookupState {
    bool serviceInitialized{false};
    QVariantMap strategy;
};

QVariant firstConfiguredValue(const QVariantMap& map, const QStringList& keys)
{
    for (const QString& key : keys) {
        if (!map.contains(key)) {
            continue;
        }

        const QVariant value = map.value(key);
        if (!value.isValid() || value.isNull()) {
            continue;
        }

        if (value.typeId() == QMetaType::QString && value.toString().trimmed().isEmpty()) {
            continue;
        }

        return value;
    }

    return {};
}

double normalizedRatio(double value, double fallback)
{
    if (!std::isfinite(value) || value <= 0.0) {
        return fallback;
    }
    return value > 1.0 ? value / 100.0 : value;
}

double numericRatioParam(const QVariantMap& map, const QStringList& keys, double fallback)
{
    const QVariant rawValue = firstConfiguredValue(map, keys);
    if (!rawValue.isValid()) {
        return fallback;
    }

    bool ok = false;
    const double numericValue = rawValue.toDouble(&ok);
    if (!ok) {
        return fallback;
    }

    return normalizedRatio(numericValue, fallback);
}

int integerParam(const QVariantMap& map, const QStringList& keys, int fallback)
{
    const QVariant rawValue = firstConfiguredValue(map, keys);
    if (!rawValue.isValid()) {
        return fallback;
    }

    bool ok = false;
    const int numericValue = rawValue.toInt(&ok);
    return ok && numericValue > 0 ? numericValue : fallback;
}

double numericParam(const QVariantMap& map, const QStringList& keys, double fallback)
{
    const QVariant rawValue = firstConfiguredValue(map, keys);
    if (!rawValue.isValid()) {
        return fallback;
    }

    bool ok = false;
    const double numericValue = rawValue.toDouble(&ok);
    return ok ? numericValue : fallback;
}

bool boolParam(const QVariantMap& map, const QStringList& keys, bool fallback)
{
    const QVariant rawValue = firstConfiguredValue(map, keys);
    if (!rawValue.isValid()) {
        return fallback;
    }

    if (rawValue.typeId() == QMetaType::Bool) {
        return rawValue.toBool();
    }

    if (rawValue.canConvert<double>()) {
        bool ok = false;
        const double numericValue = rawValue.toDouble(&ok);
        if (ok) {
            return numericValue != 0.0;
        }
    }

    const QString textValue = rawValue.toString().trimmed().toLower();
    if (textValue.isEmpty()) {
        return fallback;
    }
    if (textValue == QStringLiteral("true")
        || textValue == QStringLiteral("1")
        || textValue == QStringLiteral("yes")
        || textValue == QStringLiteral("on")) {
        return true;
    }
    if (textValue == QStringLiteral("false")
        || textValue == QStringLiteral("0")
        || textValue == QStringLiteral("no")
        || textValue == QStringLiteral("off")) {
        return false;
    }

    return fallback;
}

QVariantList variantListFromRaw(const QVariant& rawValue)
{
    if (!rawValue.isValid() || rawValue.isNull()) {
        return {};
    }

    if (rawValue.canConvert<QVariantList>()) {
        return rawValue.toList();
    }

    if (rawValue.typeId() == QMetaType::QString) {
        const QByteArray json = rawValue.toString().trimmed().toUtf8();
        if (json.isEmpty()) {
            return {};
        }

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(json, &error);
        if (error.error != QJsonParseError::NoError || !document.isArray()) {
            return {};
        }

        return document.array().toVariantList();
    }

    return {};
}

QVariantMap resolveStrategyParameters(const QVariantMap& strategy, const QVariantMap& latestBacktest)
{
    QVariantMap parameters;

    const QVariant strategyParameters = strategy.value("parameters");
    if (strategyParameters.canConvert<QVariantMap>()) {
        parameters = strategyParameters.toMap();
    }

    const QVariantMap runtimeParameters = latestBacktest.value("runtimeParameters").toMap();
    for (auto it = runtimeParameters.constBegin(); it != runtimeParameters.constEnd(); ++it) {
        const QVariant value = it.value();
        if (!value.isValid() || value.isNull()) {
            continue;
        }
        parameters.insert(it.key(), value);
    }

    return parameters;
}

std::vector<PortfolioFactorAllocation> parsePortfolioAllocations(const QVariantMap& strategy,
                                                                const QVariantMap& parameters)
{
    QVariant rawAllocations = parameters.value("portfolio_allocations_json");
    if (!rawAllocations.isValid() || rawAllocations.isNull()) {
        rawAllocations = parameters.value("factor_allocations");
    }
    if (!rawAllocations.isValid() || rawAllocations.isNull()) {
        rawAllocations = strategy.value("factor_allocations");
    }

    const QVariantList items = variantListFromRaw(rawAllocations);
    std::vector<PortfolioFactorAllocation> allocations;
    allocations.reserve(static_cast<std::size_t>(items.size()));

    for (const QVariant& item : items) {
        const QVariantMap map = item.toMap();
        const QString factorId = firstConfiguredValue(map, {"factorId", "factor_id"}).toString().trimmed();
        if (factorId.isEmpty()) {
            continue;
        }

        bool ok = false;
        const double rawWeight = firstConfiguredValue(map, {"weight", "ratio", "allocation", "value"}).toDouble(&ok);
        const double weight = ok ? normalizedRatio(rawWeight, 0.0) : 0.0;
        if (weight <= 0.0) {
            continue;
        }

        PortfolioFactorAllocation allocation;
        allocation.factorId = factorId;
        allocation.factorName = firstConfiguredValue(map, {"factorName", "factor_name", "instanceName", "instance_name", "label", "name"}).toString().trimmed();
        allocation.weight = weight;
        allocations.push_back(std::move(allocation));
    }

    double totalWeight = 0.0;
    for (const PortfolioFactorAllocation& allocation : allocations) {
        totalWeight += allocation.weight;
    }

    if (totalWeight > 0.0) {
        for (PortfolioFactorAllocation& allocation : allocations) {
            allocation.weight /= totalWeight;
        }
    }

    return allocations;
}

QSet<QString> resolveUniverseSymbols(domain::backtest::DatabaseStockDataProvider& stockProvider,
                                     const QVariantMap& latestBacktest,
                                     const QString& snapshotDate)
{
    const QString universeType = firstConfiguredValue(latestBacktest, {"universeType"}).toString().trimmed().toLower();
    const QString indexSymbol = firstConfiguredValue(latestBacktest, {"indexSymbol"}).toString().trimmed();
    const QString runtimeUniverseId = latestBacktest.value("runtimeParameters").toMap().value("universeId").toString().trimmed();

    if (universeType == "index") {
        const QString resolvedIndex = !indexSymbol.isEmpty() ? indexSymbol : runtimeUniverseId;
        if (resolvedIndex.isEmpty()) {
            return {};
        }

        const std::vector<std::string> symbols = stockProvider.getIndexConstituentSymbols(resolvedIndex, snapshotDate);
        QSet<QString> resolved;
        for (const std::string& symbol : symbols) {
            resolved.insert(QString::fromStdString(symbol));
        }
        return resolved;
    }

    if (universeType == "stock" && !runtimeUniverseId.isEmpty()) {
        return {runtimeUniverseId};
    }

    return {};
}

QVariantMap buildPositionRow(const QString& symbol,
                            double score,
                            int contributionCount,
                            double targetWeightRatio,
                            double singlePositionLimitRatio,
                            double lastClose,
                            const QString& snapshotDate)
{
    const double ratioValue = targetWeightRatio * 100.0;
    const double limitValue = singlePositionLimitRatio * 100.0;

    QString badgeType = QStringLiteral("normal");
    QString badgeText = QStringLiteral("正常");
    QString statusText = QStringLiteral("正常");
    QString statusType = QStringLiteral("green");
    QString recommendation = QStringLiteral("继续观察");

    if (limitValue > 0.0 && ratioValue >= limitValue) {
        badgeType = QStringLiteral("danger");
        badgeText = QStringLiteral("超出上限");
        statusText = QStringLiteral("高风险");
        statusType = QStringLiteral("red");
        recommendation = QStringLiteral("需要收缩配置");
    } else if (limitValue > 0.0 && ratioValue >= limitValue * 0.8) {
        badgeType = QStringLiteral("warning");
        badgeText = QStringLiteral("接近上限");
        statusText = QStringLiteral("预警");
        statusType = QStringLiteral("yellow");
        recommendation = QStringLiteral("接近上限");
    }

    if (lastClose <= 0.0 && badgeType == QStringLiteral("normal")) {
        badgeType = QStringLiteral("warning");
        badgeText = QStringLiteral("待校验");
        statusText = QStringLiteral("价格缺失");
        statusType = QStringLiteral("yellow");
        recommendation = QStringLiteral("复核最新行情");
    }

    QVariantMap row;
    row.insert("name", symbol);
    row.insert("symbol", symbol);
    row.insert("ratio", QString::number(ratioValue, 'f', 1) + "%");
    row.insert("ratioValue", ratioValue);
    row.insert("badgeText", badgeText);
    row.insert("badgeType", badgeType);
    row.insert("statusText", statusText);
    row.insert("statusType", statusType);
    row.insert("recommendation", recommendation);
    row.insert("score", score);
    row.insert("factorCoverage", contributionCount);
    row.insert("lastPrice", lastClose);
    row.insert("snapshotDate", snapshotDate);
    return row;
}

double resolveLatestClose(domain::backtest::DatabaseStockDataProvider& stockProvider,
                          const QString& symbol,
                          const QString& snapshotDate)
{
    const QDate endDate = QDate::fromString(snapshotDate, QStringLiteral("yyyy-MM-dd"));
    const QString startDate = endDate.isValid()
        ? endDate.addDays(-10).toString(QStringLiteral("yyyy-MM-dd"))
        : snapshotDate;

    const std::vector<domain::model::Bar> bars = stockProvider.getStockBars(
        symbol.toStdString(),
        startDate.toStdString(),
        snapshotDate.toStdString());

    for (auto it = bars.rbegin(); it != bars.rend(); ++it) {
        if (it->close > 0.0) {
            return it->close;
        }
    }

    return 0.0;
}

QString eventStringValue(const engine::EventFormat& event, const std::string& key)
{
    const auto metadataIt = event.metadata.find(key);
    if (metadataIt != event.metadata.end()) {
        return QString::fromStdString(metadataIt->second).trimmed();
    }

    auto value = event.get<std::string>(key);
    if (value.has_value()) {
        return QString::fromStdString(*value).trimmed();
    }
    auto numericValue = event.get<double>(key);
    if (numericValue.has_value()) {
        return QString::number(*numericValue, 'f', 6);
    }

    auto intValue = event.get<int64_t>(key);
    if (intValue.has_value()) {
        return QString::number(*intValue);
    }
    return {};
}

double eventDoubleValue(const engine::EventFormat& event, const std::string& key, double fallback = 0.0)
{
    auto numericValue = event.get<double>(key);
    if (numericValue.has_value()) {
        return *numericValue;
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

QString normalizedSideText(QString side)
{
    side = side.trimmed().toUpper();
    if (side == QStringLiteral("BUY") || side == QStringLiteral("LONG") || side == QStringLiteral("1")) {
        return QStringLiteral("BUY");
    }
    if (side == QStringLiteral("SELL") || side == QStringLiteral("SHORT") || side == QStringLiteral("2")) {
        return QStringLiteral("SELL");
    }
    return {};
}

QString normalizedActionText(QString action, const QString& side)
{
    action = action.trimmed();
    if (!action.isEmpty()) {
        return action;
    }
    return side;
}

QString normalizedPositionEffectText(QString positionEffect)
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

double normalizedPercentValue(double value, double fallback)
{
    if (!std::isfinite(value) || value <= 0.0) {
        return fallback;
    }
    return value <= 1.0 ? value * 100.0 : value;
}

bool increasesExposure(const QString& side, const QString& positionEffect)
{
    if (positionEffect == QStringLiteral("CLOSE")) {
        return false;
    }
    if (positionEffect == QStringLiteral("OPEN")) {
        return true;
    }
    return side == QStringLiteral("BUY");
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

double symbolMarketValue(const QVariantList& positions, const QString& symbol)
{
    const QString normalizedSymbol = symbol.trimmed().toUpper();
    for (const QVariant& rawPosition : positions) {
        const QVariantMap position = rawPosition.toMap();
        if (position.value(QStringLiteral("symbol")).toString().trimmed().toUpper() != normalizedSymbol) {
            continue;
        }
        return firstConfiguredValue(position, {QStringLiteral("marketValue"), QStringLiteral("market_value")}).toDouble();
    }

    return 0.0;
}

QVariantMap positionSnapshotForSymbol(const QVariantList& positions, const QString& symbol)
{
    const QString normalizedSymbol = symbol.trimmed().toUpper();
    for (const QVariant& rawPosition : positions) {
        const QVariantMap position = rawPosition.toMap();
        if (position.value(QStringLiteral("symbol")).toString().trimmed().toUpper() == normalizedSymbol) {
            return position;
        }
    }

    return {};
}

double positionReturnPercent(const QVariantMap& position)
{
    const double avgPrice = firstConfiguredValue(position, {QStringLiteral("avgPrice"), QStringLiteral("costBasis"), QStringLiteral("cost_basis")}).toDouble();
    const double lastPrice = firstConfiguredValue(position, {QStringLiteral("lastPrice"), QStringLiteral("last_price"), QStringLiteral("price")}).toDouble();
    if (!std::isfinite(avgPrice) || avgPrice <= 0.0 || !std::isfinite(lastPrice) || lastPrice <= 0.0) {
        return 0.0;
    }

    const QString positionSide = firstConfiguredValue(position, {QStringLiteral("positionSide"), QStringLiteral("position_side")}).toString().trimmed().toUpper();
    if (positionSide == QStringLiteral("SHORT")) {
        return ((avgPrice - lastPrice) / avgPrice) * 100.0;
    }

    return ((lastPrice - avgPrice) / avgPrice) * 100.0;
}

QVariantMap loadRiskConfigurationSnapshot()
{
    QVariantMap configuration;
    RiskConfigService* riskConfigService = RiskConfigService::instance();
    if (!riskConfigService) {
        return configuration;
    }

    configuration = riskConfigService->appliedConfiguration();
    if (configuration.isEmpty()) {
        configuration = riskConfigService->currentConfiguration();
    }
    return configuration;
}

QVariantMap loadTradingConfigurationSnapshot()
{
    TradingConnectionConfigService* configService = TradingConnectionConfigService::instance();
    if (!configService) {
        return {};
    }

    return configService->loadConfiguration();
}

StrategyLookupState loadStrategyLookupState(const QString& strategyId)
{
    StrategyLookupState result;
    StrategyService* strategyService = StrategyService::instance();
    if (!strategyService || !strategyService->isInitialized()) {
        return result;
    }

    result.serviceInitialized = true;
    if (strategyId.trimmed().isEmpty()) {
        return result;
    }

    result.strategy = strategyService->getStrategyById(strategyId);
    return result;
}

PositionAccountState loadPositionAccountState()
{
    PositionAccountState state;
    PositionAccountService* positionAccountService = PositionAccountService::instance();
    if (!positionAccountService) {
        return state;
    }

    positionAccountService->initialize();
    state.accountSnapshot = positionAccountService->accountSnapshot();
    state.positions = positionAccountService->positions();

    return state;
}

QVariantMap loadMarketSnapshotForSymbol(const QString& symbol)
{
    MarketDataService* marketDataService = MarketDataService::instance();
    if (!marketDataService) {
        return {};
    }

    marketDataService->initialize();
    marketDataService->ensureWatchSymbol(symbol);

    const QString normalizedSymbol = symbol.trimmed().toUpper();
    const QVariantList snapshots = marketDataService->marketSnapshots();
    for (const QVariant& rawSnapshot : snapshots) {
        const QVariantMap snapshot = rawSnapshot.toMap();
        if (snapshot.value(QStringLiteral("symbol")).toString().trimmed().toUpper() == normalizedSymbol) {
            return snapshot;
        }
    }

    return {};
}

double firstDepthLevelPrice(const QVariantMap& depthSnapshot, const QString& sideKey)
{
    const QVariantList levels = depthSnapshot.value(sideKey).toList();
    for (const QVariant& rawLevel : levels) {
        const double levelPrice = rawLevel.toMap().value(QStringLiteral("price")).toDouble();
        if (std::isfinite(levelPrice) && levelPrice > 0.0) {
            return levelPrice;
        }
    }

    return 0.0;
}

double slippageReferencePrice(const QVariantMap& marketSnapshot, const QString& side)
{
    const QVariantMap depthSnapshot = marketSnapshot.value(QStringLiteral("depthSnapshot")).toMap();
    if (side == QStringLiteral("BUY")) {
        const double askPrice = firstDepthLevelPrice(depthSnapshot, QStringLiteral("asks"));
        if (askPrice > 0.0) {
            return askPrice;
        }
    } else if (side == QStringLiteral("SELL")) {
        const double bidPrice = firstDepthLevelPrice(depthSnapshot, QStringLiteral("bids"));
        if (bidPrice > 0.0) {
            return bidPrice;
        }
    }

    return firstConfiguredValue(
        marketSnapshot,
        {QStringLiteral("price"), QStringLiteral("lastPrice"), QStringLiteral("last_price"), QStringLiteral("close")}).toDouble();
}

double adverseSlippagePercent(const QString& side, double orderPrice, double referencePrice)
{
    if (!std::isfinite(orderPrice) || orderPrice <= 0.0 || !std::isfinite(referencePrice) || referencePrice <= 0.0) {
        return 0.0;
    }

    if (side == QStringLiteral("BUY")) {
        if (orderPrice <= referencePrice) {
            return 0.0;
        }
        return ((orderPrice - referencePrice) / referencePrice) * 100.0;
    }

    if (side == QStringLiteral("SELL")) {
        if (orderPrice >= referencePrice) {
            return 0.0;
        }
        return ((referencePrice - orderPrice) / referencePrice) * 100.0;
    }

    return std::abs(orderPrice - referencePrice) / referencePrice * 100.0;
}

QString normalizedTradingDateText(const QString& rawValue)
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

QString breakerEventTradingDate(const engine::EventFormat& event)
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
            return normalizedTradingDateText(rawValue);
        }
    }

    return normalizedTradingDateText({});
}

bool isBoardLotPositionType(const QString& type)
{
    const QString normalizedType = type.trimmed().toLower();
    return normalizedType == QStringLiteral("stock")
        || normalizedType == QStringLiteral("margin_buy")
        || normalizedType == QStringLiteral("margin_sell");
}

qint64 breakerCloseQuantity(qint64 closeableQuantity, const QString& type, double ratio)
{
    if (closeableQuantity <= 0 || !std::isfinite(ratio) || ratio <= 0.0) {
        return 0;
    }

    qint64 quantity = ratio >= 1.0
        ? closeableQuantity
        : static_cast<qint64>(std::floor(static_cast<double>(closeableQuantity) * ratio));
    if (quantity <= 0) {
        return 0;
    }

    if (isBoardLotPositionType(type) && ratio < 1.0) {
        quantity = (quantity / 100) * 100;
    }

    if (quantity > closeableQuantity) {
        quantity = closeableQuantity;
    }
    return quantity;
}

double breakerReferencePrice(const QVariantMap& position, const QString& side)
{
    const double positionReferencePrice = firstConfiguredValue(
        position,
        {QStringLiteral("lastPrice"), QStringLiteral("last_price"), QStringLiteral("price"), QStringLiteral("costBasis"), QStringLiteral("cost_basis")}).toDouble();
    if (positionReferencePrice > 0.0) {
        return positionReferencePrice;
    }

    const QString symbol = firstConfiguredValue(position, {QStringLiteral("symbol")}).toString().trimmed().toUpper();
    const QVariantMap marketSnapshot = symbol.isEmpty() ? QVariantMap{} : loadMarketSnapshotForSymbol(symbol);
    const double marketReferencePrice = slippageReferencePrice(marketSnapshot, side);
    if (marketReferencePrice > 0.0) {
        return marketReferencePrice;
    }

    return 0.0;
}

int currentBreakerStage(const QVariantMap& riskConfiguration, double currentDrawdownPercent)
{
    if (!std::isfinite(currentDrawdownPercent) || currentDrawdownPercent <= 0.0) {
        return 0;
    }

    const double level3Breaker = normalizedPercentValue(
        numericParam(riskConfiguration, {QStringLiteral("level3Breaker")}, 0.0),
        0.0);
    if (level3Breaker > 0.0 && currentDrawdownPercent >= level3Breaker) {
        return 3;
    }

    const double level2Breaker = normalizedPercentValue(
        numericParam(riskConfiguration, {QStringLiteral("level2Breaker")}, 0.0),
        0.0);
    if (level2Breaker > 0.0 && currentDrawdownPercent >= level2Breaker) {
        return 2;
    }

    const double level1Breaker = normalizedPercentValue(
        numericParam(riskConfiguration, {QStringLiteral("level1Breaker")}, 0.0),
        0.0);
    if (level1Breaker > 0.0 && currentDrawdownPercent >= level1Breaker) {
        return 1;
    }

    return 0;
}

} // namespace

RiskMonitorService* RiskMonitorService::m_instance = nullptr;
QMutex RiskMonitorService::m_instanceMutex;

RiskMonitorService* RiskMonitorService::instance()
{
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        m_instance = new RiskMonitorService();
        m_instance->initialize();
    }
    return m_instance;
}

RiskMonitorService::RiskMonitorService(QObject* parent)
    : QObject(parent)
    , m_initialized(false)
    , m_eventBusIntegrated(false)
    , m_peakObservedTotalAsset(0.0)
    , m_currentDrawdownPercent(0.0)
    , m_varUsagePercent(0.0)
    , m_currentTotalExposurePercent(0.0)
    , m_varBudgetAmount(0.0)
    , m_estimatedVarAmount(0.0)
    , m_lastBreakerAutoActionStage(0)
    , m_level3TradingHaltActive(false)
{
}

void RiskMonitorService::initialize()
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_initialized) {
            return;
        }
    }

    QCoreApplication* app = QCoreApplication::instance();
    if (!app || QThread::currentThread() == app->thread()) {
        RiskConfigService* riskConfigService = RiskConfigService::instance();
        PositionAccountService* positionAccountService = PositionAccountService::instance();
        StrategyService::instance();

        if (positionAccountService) {
            positionAccountService->initialize();
            connect(positionAccountService,
                    &PositionAccountService::accountSnapshotChanged,
                    this,
                    [this, positionAccountService]() {
                        const QVariantMap snapshot = positionAccountService->accountSnapshot();
                        updateLiveMetricsFromAccountSnapshot(snapshot);
                        evaluateBreakerActions(snapshot, snapshot.value(QStringLiteral("dailyTurnoverDate")).toString());
                    });
        }

        if (riskConfigService) {
            connect(riskConfigService,
                    &RiskConfigService::currentConfigurationChanged,
                    this,
                    [this]() { refreshLiveMetrics(); });
            connect(riskConfigService,
                    &RiskConfigService::appliedConfigurationChanged,
                    this,
                    [this]() { refreshLiveMetrics(); });
        }
    }

    initializeEventBusIntegration();
    refreshLiveMetrics();

    {
        QMutexLocker locker(&m_mutex);
        if (m_initialized) {
            return;
        }
        m_initialized = true;
    }

    emit initializedChanged();
}

bool RiskMonitorService::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized;
}

double RiskMonitorService::currentDrawdownPercent() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentDrawdownPercent;
}

double RiskMonitorService::varUsagePercent() const
{
    QMutexLocker locker(&m_mutex);
    return m_varUsagePercent;
}

double RiskMonitorService::currentTotalExposurePercent() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentTotalExposurePercent;
}

double RiskMonitorService::varBudgetAmount() const
{
    QMutexLocker locker(&m_mutex);
    return m_varBudgetAmount;
}

double RiskMonitorService::estimatedVarAmount() const
{
    QMutexLocker locker(&m_mutex);
    return m_estimatedVarAmount;
}

QVariantMap RiskMonitorService::buildPortfolioSnapshot(const QVariantMap& strategy,
                                                      const QVariantMap& latestBacktest)
{
    QVariantMap result;
    result.insert("status", QStringLiteral("error"));
    result.insert("positions", QVariantList{});

    if (strategy.isEmpty()) {
        result.insert("error", QStringLiteral("缺少策略上下文"));
        return result;
    }

    FactorService* factorService = FactorService::instance();
    factorService->initialize();

    const QVariantMap parameters = resolveStrategyParameters(strategy, latestBacktest);
    const std::vector<PortfolioFactorAllocation> allocations = parsePortfolioAllocations(strategy, parameters);
    if (allocations.empty()) {
        result.insert("error", QStringLiteral("策略未配置可用的组合因子"));
        return result;
    }

    const QString snapshotDate = factorService->getLatestAvailableTradeDate().trimmed();
    if (snapshotDate.isEmpty()) {
        result.insert("error", QStringLiteral("未找到最新交易日"));
        return result;
    }

    domain::backtest::DatabaseStockDataProvider stockProvider(nullptr);
    const QSet<QString> universeSymbols = resolveUniverseSymbols(stockProvider, latestBacktest, snapshotDate);

    QHash<QString, ScoreState> scoreBySymbol;
    int totalFactorSnapshots = 0;

    for (const PortfolioFactorAllocation& allocation : allocations) {
        const QVariantMap factorValuesResult = factorService->getFactorValues(allocation.factorId, snapshotDate);
        if (factorValuesResult.value("status").toString() != QStringLiteral("success")) {
            continue;
        }

        const QVariantMap stockValues = factorValuesResult.value("stockValues").toMap();
        if (stockValues.isEmpty()) {
            continue;
        }

        struct RankedSymbol {
            QString symbol;
            double value{0.0};
        };

        std::vector<RankedSymbol> rankedSymbols;
        rankedSymbols.reserve(static_cast<std::size_t>(stockValues.size()));

        for (auto it = stockValues.constBegin(); it != stockValues.constEnd(); ++it) {
            bool ok = false;
            const double factorValue = it.value().toDouble(&ok);
            if (!ok || !std::isfinite(factorValue)) {
                continue;
            }

            if (!universeSymbols.isEmpty() && !universeSymbols.contains(it.key())) {
                continue;
            }

            rankedSymbols.push_back({it.key(), factorValue});
        }

        if (rankedSymbols.empty()) {
            continue;
        }

        totalFactorSnapshots += 1;
        std::sort(rankedSymbols.begin(), rankedSymbols.end(), [](const RankedSymbol& left, const RankedSymbol& right) {
            if (left.value == right.value) {
                return left.symbol < right.symbol;
            }
            return left.value < right.value;
        });

        const double denominator = rankedSymbols.size() > 1
            ? static_cast<double>(rankedSymbols.size() - 1)
            : 1.0;

        for (std::size_t index = 0; index < rankedSymbols.size(); ++index) {
            const double rankScore = rankedSymbols.size() > 1
                ? static_cast<double>(index) / denominator
                : 1.0;
            ScoreState& state = scoreBySymbol[rankedSymbols[index].symbol];
            state.score += allocation.weight * rankScore;
            state.contributionCount += 1;
        }
    }

    if (scoreBySymbol.isEmpty()) {
        result.insert("error", QStringLiteral("最新交易日未生成可用的候选持仓"));
        return result;
    }

    struct SelectedSymbol {
        QString symbol;
        double score{0.0};
        int contributionCount{0};
    };

    std::vector<SelectedSymbol> rankedResults;
    rankedResults.reserve(static_cast<std::size_t>(scoreBySymbol.size()));
    for (auto it = scoreBySymbol.constBegin(); it != scoreBySymbol.constEnd(); ++it) {
        if (!std::isfinite(it.value().score) || it.value().contributionCount <= 0) {
            continue;
        }

        rankedResults.push_back({it.key(), it.value().score, it.value().contributionCount});
    }

    std::sort(rankedResults.begin(), rankedResults.end(), [](const SelectedSymbol& left, const SelectedSymbol& right) {
        if (left.score == right.score) {
            return left.symbol < right.symbol;
        }
        return left.score > right.score;
    });

    const int topN = integerParam(parameters, {"top_n", "topN", "maxPositions"}, 10);
    if (topN > 0 && static_cast<std::size_t>(topN) < rankedResults.size()) {
        rankedResults.resize(static_cast<std::size_t>(topN));
    }

    const double portfolioExposure = numericRatioParam(
        parameters,
        {"maxTotalExposure", "maxPositionRatio"},
        0.67);
    const double singlePositionLimit = numericRatioParam(
        parameters,
        {"maxPositionPercent", "maxSinglePositionRatio", "positionPercent", "position_size", "positionSize"},
        0.15);
    const double equalWeightRatio = rankedResults.empty()
        ? 0.0
        : portfolioExposure / static_cast<double>(rankedResults.size());
    const double targetWeightRatio = equalWeightRatio < singlePositionLimit
        ? equalWeightRatio
        : singlePositionLimit;

    QVariantList positions;
    positions.reserve(static_cast<qsizetype>(rankedResults.size()));
    for (const SelectedSymbol& selected : rankedResults) {
        const double lastClose = resolveLatestClose(stockProvider, selected.symbol, snapshotDate);
        positions.push_back(buildPositionRow(
            selected.symbol,
            selected.score,
            selected.contributionCount,
            targetWeightRatio,
            singlePositionLimit,
            lastClose,
            snapshotDate));
    }

    QVariantMap diagnostics;
    diagnostics.insert("snapshotDate", snapshotDate);
    diagnostics.insert("candidateCount", scoreBySymbol.size());
    diagnostics.insert("selectedCount", positions.size());
    diagnostics.insert("allocationCount", static_cast<int>(allocations.size()));
    diagnostics.insert("factorSnapshotCount", totalFactorSnapshots);
    diagnostics.insert("targetWeightPercent", targetWeightRatio * 100.0);
    diagnostics.insert("portfolioExposurePercent", portfolioExposure * 100.0);
    diagnostics.insert("singlePositionLimitPercent", singlePositionLimit * 100.0);
    diagnostics.insert("universeType", firstConfiguredValue(latestBacktest, {"universeType"}).toString());
    diagnostics.insert("indexSymbol", firstConfiguredValue(latestBacktest, {"indexSymbol"}).toString());

    result.insert("status", QStringLiteral("success"));
    result.insert("snapshotDate", snapshotDate);
    result.insert("positions", positions);
    result.insert("diagnostics", diagnostics);
    result.insert("recordedAt", QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    return result;
}

void RiskMonitorService::initializeEventBusIntegration()
{
    if (m_eventBusIntegrated) {
        return;
    }

    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        qWarning() << "RiskMonitorService: EventBus not ready, skip event integration";
        return;
    }

    m_strategySignalSubscription = bus->subscribe(engine::EventTypes::STRATEGY_SIGNAL,
        [this](const engine::EventFormat& event) {
            handleStrategySignal(event);
        });
    m_accountUpdateSubscription = bus->subscribe(engine::EventTypes::TRADING_ACCOUNT_UPDATED,
        [this](const engine::EventFormat& event) {
            handleTradingAccountUpdate(event);
        });

    m_eventBusIntegrated = true;
    qDebug() << "RiskMonitorService: EventBus integration initialized";
}

void RiskMonitorService::handleStrategySignal(const engine::EventFormat& event)
{
    QVariantMap signalData;
    signalData.insert(QStringLiteral("correlationId"), !event.correlation_id.empty()
        ? QString::fromStdString(event.correlation_id)
        : QString::fromStdString(event.id));
    signalData.insert(QStringLiteral("strategyId"), eventStringValue(event, "strategy_id"));
    signalData.insert(QStringLiteral("businessStrategyId"), eventStringValue(event, "business_strategy_id"));
    signalData.insert(QStringLiteral("strategyName"), eventStringValue(event, "strategy_name"));
    signalData.insert(QStringLiteral("runtimeStrategyId"), eventStringValue(event, "runtime_strategy_id"));
    signalData.insert(QStringLiteral("orderId"), eventStringValue(event, "order_id"));
    signalData.insert(QStringLiteral("clientOrderId"), eventStringValue(event, "client_order_id"));
    signalData.insert(QStringLiteral("symbol"), eventStringValue(event, "symbol"));
    signalData.insert(QStringLiteral("side"), eventStringValue(event, "side"));
    signalData.insert(QStringLiteral("action"), eventStringValue(event, "action"));
    signalData.insert(QStringLiteral("price"), eventDoubleValue(event, "price", 0.0));
    signalData.insert(QStringLiteral("strength"), eventDoubleValue(event, "strength", 0.0));
    signalData.insert(QStringLiteral("quantity"), static_cast<qint64>(eventDoubleValue(event, "quantity", eventDoubleValue(event, "total_quantity", 0.0))));
    signalData.insert(QStringLiteral("orderType"), eventStringValue(event, "order_type"));
    signalData.insert(QStringLiteral("type"), eventStringValue(event, "type"));
    signalData.insert(QStringLiteral("positionEffect"), eventStringValue(event, "position_effect_text").trimmed().isEmpty()
        ? eventStringValue(event, "position_effect")
        : eventStringValue(event, "position_effect_text"));
    signalData.insert(QStringLiteral("underlying"), eventStringValue(event, "underlying"));
    signalData.insert(QStringLiteral("optionType"), eventStringValue(event, "option_type"));
    signalData.insert(QStringLiteral("expiry"), eventStringValue(event, "expiry"));
    reviewTradeSignal(signalData, true);
}

void RiskMonitorService::handleTradingAccountUpdate(const engine::EventFormat& event)
{
    const double totalAsset = eventDoubleValue(
        event,
        "total_asset",
        eventDoubleValue(event, "totalAsset", 0.0));
    syncObservedTotalAssetPeak(totalAsset);

    QVariantMap accountSnapshot;
    accountSnapshot.insert(QStringLiteral("totalAsset"), totalAsset);
    accountSnapshot.insert(QStringLiteral("marketValue"), eventDoubleValue(event, "market_value", eventDoubleValue(event, "marketValue", 0.0)));
    updateLiveMetricsFromAccountSnapshot(accountSnapshot);
}

void RiskMonitorService::syncObservedTotalAssetPeak(double totalAsset)
{
    if (!std::isfinite(totalAsset) || totalAsset <= 0.0) {
        return;
    }

    QMutexLocker locker(&m_mutex);
    if (totalAsset > m_peakObservedTotalAsset) {
        m_peakObservedTotalAsset = totalAsset;
    }
}

void RiskMonitorService::refreshLiveMetrics()
{
    const PositionAccountState accountState = loadPositionAccountState();
    updateLiveMetricsFromAccountSnapshot(accountState.accountSnapshot);
}

void RiskMonitorService::updateLiveMetricsFromAccountSnapshot(const QVariantMap& accountSnapshot)
{
    const double totalAsset = numericParam(
        accountSnapshot,
        {QStringLiteral("totalAsset"), QStringLiteral("total_asset"), QStringLiteral("nav")},
        numericParam(accountSnapshot,
                     {QStringLiteral("availableCash"), QStringLiteral("available_cash")},
                     0.0)
            + numericParam(accountSnapshot,
                           {QStringLiteral("marketValue"), QStringLiteral("market_value")},
                           0.0));
    const double marketValue = numericParam(
        accountSnapshot,
        {QStringLiteral("marketValue"), QStringLiteral("market_value")},
        0.0);
    const QVariantMap riskConfiguration = loadRiskConfigurationSnapshot();
    const double maxTotalExposure = normalizedPercentValue(
        numericParam(riskConfiguration,
                     {QStringLiteral("maxTotalExposure"), QStringLiteral("maxPositionRatio")},
                     67.0),
        67.0);

    double nextCurrentDrawdownPercent = 0.0;
    if (totalAsset > 0.0) {
        syncObservedTotalAssetPeak(totalAsset);
        QMutexLocker locker(&m_mutex);
        if (m_peakObservedTotalAsset > 0.0) {
            nextCurrentDrawdownPercent = -((m_peakObservedTotalAsset - totalAsset) / m_peakObservedTotalAsset) * 100.0;
        }
    }

    const double nextCurrentTotalExposurePercent = totalAsset > 0.0
        ? (marketValue / totalAsset) * 100.0
        : 0.0;
    const double nextVarBudgetAmount = totalAsset > 0.0 && maxTotalExposure > 0.0
        ? totalAsset * (maxTotalExposure / 100.0)
        : 0.0;
    const double nextEstimatedVarAmount = marketValue > 0.0 ? marketValue : 0.0;
    const double nextVarUsagePercent = nextVarBudgetAmount > 0.0
        ? (nextEstimatedVarAmount / nextVarBudgetAmount) * 100.0
        : 0.0;

    bool drawdownChanged = false;
    bool varUsageChanged = false;
    bool exposureChanged = false;
    {
        QMutexLocker locker(&m_mutex);
        drawdownChanged = std::abs(m_currentDrawdownPercent - nextCurrentDrawdownPercent) > 0.001;
        varUsageChanged = std::abs(m_varUsagePercent - nextVarUsagePercent) > 0.001
            || std::abs(m_varBudgetAmount - nextVarBudgetAmount) > 0.01
            || std::abs(m_estimatedVarAmount - nextEstimatedVarAmount) > 0.01;
        exposureChanged = std::abs(m_currentTotalExposurePercent - nextCurrentTotalExposurePercent) > 0.001;

        m_currentDrawdownPercent = nextCurrentDrawdownPercent;
        m_varUsagePercent = nextVarUsagePercent;
        m_currentTotalExposurePercent = nextCurrentTotalExposurePercent;
        m_varBudgetAmount = nextVarBudgetAmount;
        m_estimatedVarAmount = nextEstimatedVarAmount;
    }

    if (drawdownChanged) {
        emit currentDrawdownPercentChanged();
    }
    if (varUsageChanged) {
        emit varUsagePercentChanged();
    }
    if (exposureChanged) {
        emit currentTotalExposurePercentChanged();
    }
}

void RiskMonitorService::resetBreakerStateIfNeeded(const QString& tradingDate)
{
    const QString normalizedTradingDate = normalizedTradingDateText(tradingDate);

    QMutexLocker locker(&m_mutex);
    if (m_breakerTradingDate == normalizedTradingDate) {
        return;
    }

    m_breakerTradingDate = normalizedTradingDate;
    m_lastBreakerAutoActionStage = 0;
    m_level3TradingHaltActive = false;
}

void RiskMonitorService::evaluateBreakerActions(const QVariantMap& accountSnapshot, const QString& tradingDate)
{
    const double totalAsset = numericParam(
        accountSnapshot,
        {QStringLiteral("totalAsset"), QStringLiteral("total_asset"), QStringLiteral("nav")},
        0.0);
    if (!std::isfinite(totalAsset) || totalAsset <= 0.0) {
        return;
    }

    resetBreakerStateIfNeeded(tradingDate);

    double currentDrawdownPercent = 0.0;
    {
        QMutexLocker locker(&m_mutex);
        if (m_peakObservedTotalAsset > 0.0) {
            currentDrawdownPercent = ((m_peakObservedTotalAsset - totalAsset) / m_peakObservedTotalAsset) * 100.0;
        }
    }

    const QVariantMap riskConfiguration = loadRiskConfigurationSnapshot();
    const int breakerStage = currentBreakerStage(riskConfiguration, currentDrawdownPercent);
    if (breakerStage < 2) {
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        if (breakerStage <= m_lastBreakerAutoActionStage) {
            return;
        }

        m_lastBreakerAutoActionStage = breakerStage;
        if (breakerStage >= 3) {
            m_level3TradingHaltActive = true;
        }
    }

    const PositionAccountState accountState = loadPositionAccountState();
    dispatchBreakerOrders(breakerStage, accountState.positions, tradingDate);
}

void RiskMonitorService::dispatchBreakerOrders(int breakerStage, const QVariantList& positions, const QString& tradingDate)
{
    if (breakerStage < 2 || positions.isEmpty()) {
        return;
    }

    const double reduceRatio = breakerStage >= 3 ? 1.0 : 0.5;
    for (const QVariant& rawPosition : positions) {
        const QVariantMap position = rawPosition.toMap();
        const QString symbol = firstConfiguredValue(position, {QStringLiteral("symbol")}).toString().trimmed().toUpper();
        if (symbol.isEmpty()) {
            continue;
        }

        const QString type = firstConfiguredValue(position, {QStringLiteral("type")}).toString().trimmed().toLower();
        const QString positionSide = firstConfiguredValue(position, {QStringLiteral("positionSide"), QStringLiteral("position_side")}).toString().trimmed().toUpper();
        const qint64 closeableQuantity = static_cast<qint64>(numericParam(
            position,
            {QStringLiteral("closeableQuantity"), QStringLiteral("closeable_quantity"), QStringLiteral("availableQuantity"), QStringLiteral("available_quantity"), QStringLiteral("quantity")},
            0.0));
        const qint64 quantity = breakerCloseQuantity(closeableQuantity, type, reduceRatio);
        if (quantity <= 0) {
            continue;
        }

        const QString side = positionSide == QStringLiteral("SHORT")
            ? QStringLiteral("BUY")
            : QStringLiteral("SELL");
        const double price = breakerReferencePrice(position, side);
        if (!std::isfinite(price) || price <= 0.0) {
            qWarning() << "RiskMonitorService: skip breaker order without valid reference price" << symbol << breakerStage;
            continue;
        }

        QVariantMap request;
        request.insert(QStringLiteral("symbol"), symbol);
        request.insert(QStringLiteral("side"), side);
        request.insert(QStringLiteral("price"), price);
        request.insert(QStringLiteral("quantity"), quantity);
        request.insert(QStringLiteral("orderType"), QStringLiteral("LIMIT"));
        request.insert(QStringLiteral("mode"), type.isEmpty() ? QStringLiteral("stock") : type);
        request.insert(QStringLiteral("positionEffect"), QStringLiteral("CLOSE"));
        request.insert(QStringLiteral("strength"), 1.0);
        request.insert(QStringLiteral("tradingDate"), normalizedTradingDateText(tradingDate));
        request.insert(QStringLiteral("riskBypassTradingHalt"), true);
        request.insert(QStringLiteral("riskActionSource"), breakerStage >= 3
            ? QStringLiteral("level3_breaker")
            : QStringLiteral("level2_breaker"));
        request.insert(QStringLiteral("riskBreakerStage"), breakerStage);
        submitBreakerOrder(request);
    }
}

void RiskMonitorService::submitBreakerOrder(const QVariantMap& request)
{
    QCoreApplication* app = QCoreApplication::instance();
    if (app && QThread::currentThread() != app->thread()) {
        QMetaObject::invokeMethod(app, [request]() {
            TradeExecutionService* tradeExecutionService = TradeExecutionService::instance();
            if (!tradeExecutionService) {
                return;
            }

            if (!tradeExecutionService->submitBridgeOrder(request)) {
                qWarning() << "RiskMonitorService: breaker order submission failed" << tradeExecutionService->lastErrorMessage();
            }
        }, Qt::QueuedConnection);
        return;
    }

    TradeExecutionService* tradeExecutionService = TradeExecutionService::instance();
    if (!tradeExecutionService) {
        return;
    }

    if (!tradeExecutionService->submitBridgeOrder(request)) {
        qWarning() << "RiskMonitorService: breaker order submission failed" << tradeExecutionService->lastErrorMessage();
    }
}

QVariantMap RiskMonitorService::reviewTradeSignal(const QVariantMap& signalData, bool publishDecisionEvent)
{
    const QVariantMap tradingConfiguration = loadTradingConfigurationSnapshot();
    const bool strictStrategyValidation = tradingConfiguration.value(QStringLiteral("enabled")).toBool()
        && !tradingConfiguration.value(QStringLiteral("readOnly"), true).toBool();
    const QString configuredBoundStrategyId = tradingConfiguration.value(QStringLiteral("boundStrategyId")).toString().trimmed();
    const QString businessStrategyId = signalData.value(QStringLiteral("businessStrategyId")).toString().trimmed();
    const QString strategyId = businessStrategyId.isEmpty()
        ? signalData.value(QStringLiteral("strategyId")).toString().trimmed()
        : businessStrategyId;
    const QString strategyName = signalData.value(QStringLiteral("strategyName")).toString().trimmed();
    const QString symbol = signalData.value(QStringLiteral("symbol")).toString().trimmed().toUpper();
    const QString side = normalizedSideText(signalData.value(QStringLiteral("side")).toString().trimmed().isEmpty()
        ? signalData.value(QStringLiteral("action")).toString()
        : signalData.value(QStringLiteral("side")).toString());
    const QString action = normalizedActionText(signalData.value(QStringLiteral("action")).toString(), side);
    const QString positionEffect = normalizedPositionEffectText(signalData.value(QStringLiteral("positionEffect")).toString());
    const double price = signalData.value(QStringLiteral("price")).toDouble();
    const double cashAmount = signalData.value(QStringLiteral("cashAmount")).toDouble();
    const double strength = signalData.value(QStringLiteral("strength")).toDouble();
    const qint64 quantity = signalData.value(QStringLiteral("quantity")).toLongLong();
    const QString orderType = signalData.value(QStringLiteral("orderType")).toString().trimmed().toUpper();
    const QString gmStrategyId = signalData.value(QStringLiteral("runtimeStrategyId")).toString().trimmed();
    const QString orderId = signalData.value(QStringLiteral("orderId")).toString().trimmed();
    const QString clientOrderId = signalData.value(QStringLiteral("clientOrderId")).toString().trimmed();
    const QString mode = signalData.value(QStringLiteral("type")).toString().trimmed().toLower();
    const QString underlying = signalData.value(QStringLiteral("underlying")).toString().trimmed().toUpper();
    const QString optionType = signalData.value(QStringLiteral("optionType")).toString().trimmed().toLower();
    const QString expiry = signalData.value(QStringLiteral("expiry")).toString().trimmed();
    const bool riskBypassTradingHalt = boolParam(
        signalData,
        {QStringLiteral("riskBypassTradingHalt")},
        false);
    const QString tradingDate = normalizedTradingDateText(signalData.value(QStringLiteral("tradingDate")).toString());

    resetBreakerStateIfNeeded(tradingDate);

    QVariantMap decision;
    decision.insert("strategyId", strategyId);
    decision.insert("strategyName", strategyName);
    decision.insert("symbol", symbol);
    decision.insert("action", action);
    decision.insert("side", side);
    decision.insert("price", price);
    if (cashAmount > 0.0) {
        decision.insert("cashAmount", cashAmount);
    }
    decision.insert("strength", strength);
    decision.insert("quantity", quantity);
    decision.insert("orderType", orderType);
    decision.insert("businessStrategyId", strategyId);
    decision.insert("runtimeStrategyId", gmStrategyId);
    decision.insert("orderId", orderId);
    decision.insert("clientOrderId", clientOrderId);
    decision.insert("type", mode);
    decision.insert("positionEffect", positionEffect);
    decision.insert("underlying", underlying);
    decision.insert("optionType", optionType);
    decision.insert("expiry", expiry);

    QString decisionType = QString::fromUtf8(engine::EventTypes::RISK_APPROVAL);
    QString reason = QStringLiteral("基础风控校验通过");
    double riskScore = 0.15;
    const StrategyLookupState strategyLookup = loadStrategyLookupState(strategyId);
    const bool priceOptionalAction = isCashRepayAction(action) || isShareReturnAction(action);

    if (strategyId.isEmpty() || symbol.isEmpty() || side.isEmpty()) {
        decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
        reason = QStringLiteral("策略信号缺少必要字段");
        riskScore = 1.0;
    } else if (strictStrategyValidation && configuredBoundStrategyId.isEmpty()) {
        decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
        reason = QStringLiteral("当前交易连接未绑定系统策略，拒绝实盘委托");
        riskScore = 0.98;
    } else if (strictStrategyValidation && strategyId != configuredBoundStrategyId) {
        decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
        reason = QStringLiteral("委托策略与当前绑定策略不一致，拒绝实盘委托");
        riskScore = 0.97;
    } else if (strictStrategyValidation && !strategyLookup.serviceInitialized) {
        decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
        reason = QStringLiteral("策略服务未初始化，拒绝实盘委托");
        riskScore = 0.96;
    } else if (strictStrategyValidation && strategyLookup.strategy.isEmpty()) {
        decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
        reason = QStringLiteral("未找到绑定策略，拒绝实盘委托");
        riskScore = 0.94;
    } else if (!priceOptionalAction && price <= 0.0) {
        decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
        reason = QStringLiteral("价格无效，拒绝执行");
        riskScore = 0.95;
    } else if (strength <= 0.0) {
        decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
        reason = QStringLiteral("信号强度不足");
        riskScore = 0.75;
    } else {
        const QVariantMap strategy = strategyLookup.strategy;
        const QString strategyStatus = strategy.value("status").toString().trimmed().toUpper();
        if (!strategy.isEmpty() && strategyStatus != QStringLiteral("ACTIVE") && strategyStatus != QStringLiteral("TESTING")) {
            decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
            reason = QStringLiteral("策略未激活，拒绝执行");
            riskScore = 0.9;
        }
    }

    if (decisionType == QString::fromUtf8(engine::EventTypes::RISK_APPROVAL) && !riskBypassTradingHalt) {
        bool level3TradingHaltActive = false;
        {
            QMutexLocker locker(&m_mutex);
            level3TradingHaltActive = m_level3TradingHaltActive;
        }

        if (level3TradingHaltActive) {
            decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
            reason = QStringLiteral("三级熔断已触发，当日停止交易");
            riskScore = 0.99;
        }
    }

    if (decisionType == QString::fromUtf8(engine::EventTypes::RISK_APPROVAL)) {
        const QVariantMap riskConfiguration = loadRiskConfigurationSnapshot();
        const bool autoStopEnabled = boolParam(
            riskConfiguration,
            {QStringLiteral("autoStopEnabled"), QStringLiteral("auto_stop_enabled")},
            true);
        const double stopLossPercent = autoStopEnabled
            ? normalizedPercentValue(
                numericParam(riskConfiguration,
                             {QStringLiteral("stopLossPercent"), QStringLiteral("stop_loss"), QStringLiteral("stopLoss")},
                             10.0),
                10.0)
            : 0.0;
        const double takeProfitPercent = normalizedPercentValue(
            numericParam(riskConfiguration,
                         {QStringLiteral("takeProfitPercent"), QStringLiteral("take_profit"), QStringLiteral("takeProfit")},
                         20.0),
            20.0);
        const double maxDrawdownLimit = normalizedPercentValue(
            numericParam(riskConfiguration,
                         {QStringLiteral("maxDrawdownLimit"), QStringLiteral("max_drawdown_limit")},
                         12.0),
            12.0);
        const double level1Breaker = normalizedPercentValue(
            numericParam(riskConfiguration,
                         {QStringLiteral("level1Breaker")},
                         0.0),
            0.0);
        const double level2Breaker = normalizedPercentValue(
            numericParam(riskConfiguration,
                         {QStringLiteral("level2Breaker")},
                         0.0),
            0.0);
        const double level3Breaker = normalizedPercentValue(
            numericParam(riskConfiguration,
                         {QStringLiteral("level3Breaker")},
                         0.0),
            0.0);

        const double orderSizeLimitWan = numericParam(
            riskConfiguration,
            {QStringLiteral("orderSizeLimit"), QStringLiteral("maxOrderSize")},
            100.0);
        const double turnoverLimitWan = numericParam(
            riskConfiguration,
            {QStringLiteral("turnoverLimit")},
            0.0);
        double slippageLimitPercent = 0.0;
        const QVariant rawSlippageLimit = firstConfiguredValue(riskConfiguration, {QStringLiteral("slippageLimit")});
        if (rawSlippageLimit.isValid()) {
            bool ok = false;
            const double numericValue = rawSlippageLimit.toDouble(&ok);
            slippageLimitPercent = ok && numericValue > 0.0 ? numericValue : 0.0;
        } else {
            slippageLimitPercent = normalizedPercentValue(
                numericParam(riskConfiguration,
                             {QStringLiteral("slippageRate"), QStringLiteral("slippage"), QStringLiteral("slippageCost")},
                             0.0),
                0.0);
        }
        double requestedNotional = signalData.value(QStringLiteral("requestedNotional")).toDouble();
        if (requestedNotional <= 0.0) {
            requestedNotional = cashAmount > 0.0
                ? cashAmount
                : (price > 0.0 && quantity > 0 ? price * static_cast<double>(quantity) : 0.0);
        }

        if (requestedNotional > 0.0 && orderSizeLimitWan > 0.0 && requestedNotional > (orderSizeLimitWan * 10000.0)) {
            decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
            reason = QStringLiteral("单笔委托金额 %1 万，超过风控上限 %2 万")
                .arg(requestedNotional / 10000.0, 0, 'f', 2)
                .arg(orderSizeLimitWan, 0, 'f', 2);
            riskScore = 0.92;
        } else if (!priceOptionalAction && price > 0.0 && slippageLimitPercent > 0.0) {
            const QVariantMap marketSnapshot = loadMarketSnapshotForSymbol(symbol);
            const double referencePrice = slippageReferencePrice(marketSnapshot, side);
            const double slippagePercent = adverseSlippagePercent(side, price, referencePrice);
            if (referencePrice > 0.0 && slippagePercent > slippageLimitPercent) {
                decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
                reason = QStringLiteral("委托价相对参考价偏离 %1%%，超过滑点容忍度 %2%%")
                    .arg(slippagePercent, 0, 'f', 2)
                    .arg(slippageLimitPercent, 0, 'f', 2);
                riskScore = 0.81;
            }
        }

        if (decisionType == QString::fromUtf8(engine::EventTypes::RISK_APPROVAL) && requestedNotional > 0.0) {
            const PositionAccountState accountState = loadPositionAccountState();
            const QVariantMap& accountSnapshot = accountState.accountSnapshot;
            const QVariantList& positions = accountState.positions;
            const double currentDailyTurnoverNotional = numericParam(
                accountSnapshot,
                {QStringLiteral("dailyTurnoverNotional"), QStringLiteral("daily_turnover_notional"), QStringLiteral("dailyTradedNotional")},
                0.0);

            if (turnoverLimitWan > 0.0 && (currentDailyTurnoverNotional + requestedNotional) > (turnoverLimitWan * 10000.0)) {
                decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
                reason = QStringLiteral("日累计成交金额 %1 万，叠加本笔后将超过上限 %2 万")
                    .arg((currentDailyTurnoverNotional + requestedNotional) / 10000.0, 0, 'f', 2)
                    .arg(turnoverLimitWan, 0, 'f', 2);
                riskScore = 0.83;
            } else if (increasesExposure(side, positionEffect)) {
                const QVariantMap symbolPosition = positionSnapshotForSymbol(positions, symbol);
                const double currentTotalAsset = numericParam(
                    accountSnapshot,
                    {QStringLiteral("totalAsset"), QStringLiteral("total_asset"), QStringLiteral("nav")},
                    numericParam(accountSnapshot,
                                 {QStringLiteral("availableCash"), QStringLiteral("available_cash")},
                                 0.0)
                        + numericParam(accountSnapshot,
                                       {QStringLiteral("marketValue"), QStringLiteral("market_value")},
                                       0.0));
                const double currentMarketValue = numericParam(
                    accountSnapshot,
                    {QStringLiteral("marketValue"), QStringLiteral("market_value")},
                    0.0);
                const double currentSymbolMarketValue = symbolMarketValue(positions, symbol);
                const double currentPositionReturnPercent = positionReturnPercent(symbolPosition);
                const double maxPositionPercent = normalizedPercentValue(
                    numericParam(riskConfiguration,
                                 {QStringLiteral("maxPositionPercent"), QStringLiteral("maxSinglePositionRatio"), QStringLiteral("positionPercent"), QStringLiteral("position_size"), QStringLiteral("positionSize")},
                                 15.0),
                    15.0);
                const double maxTotalExposure = normalizedPercentValue(
                    numericParam(riskConfiguration,
                                 {QStringLiteral("maxTotalExposure"), QStringLiteral("maxPositionRatio")},
                                 67.0),
                    67.0);

                double currentDrawdownPercent = 0.0;
                if (currentTotalAsset > 0.0) {
                    syncObservedTotalAssetPeak(currentTotalAsset);
                    QMutexLocker locker(&m_mutex);
                    if (m_peakObservedTotalAsset > 0.0) {
                        currentDrawdownPercent = ((m_peakObservedTotalAsset - currentTotalAsset) / m_peakObservedTotalAsset) * 100.0;
                    }
                }

                if (!symbolPosition.isEmpty() && stopLossPercent > 0.0 && currentPositionReturnPercent <= -stopLossPercent) {
                    decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
                    reason = QStringLiteral("标的当前收益 %1%% 已触发止损线 %2%%，禁止继续加仓")
                        .arg(currentPositionReturnPercent, 0, 'f', 2)
                        .arg(stopLossPercent, 0, 'f', 2);
                    riskScore = 0.91;
                } else if (!symbolPosition.isEmpty() && takeProfitPercent > 0.0 && currentPositionReturnPercent >= takeProfitPercent) {
                    decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
                    reason = QStringLiteral("标的当前收益 %1%% 已触发止盈线 %2%%，禁止继续加仓")
                        .arg(currentPositionReturnPercent, 0, 'f', 2)
                        .arg(takeProfitPercent, 0, 'f', 2);
                    riskScore = 0.72;
                } else if (level3Breaker > 0.0 && currentDrawdownPercent >= level3Breaker) {
                    decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
                    reason = QStringLiteral("账户当前回撤 %1%%，已触发三级熔断线 %2%%，禁止继续加仓")
                        .arg(currentDrawdownPercent, 0, 'f', 2)
                        .arg(level3Breaker, 0, 'f', 2);
                    riskScore = 0.95;
                } else if (level2Breaker > 0.0 && currentDrawdownPercent >= level2Breaker) {
                    decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
                    reason = QStringLiteral("账户当前回撤 %1%%，已触发二级熔断线 %2%%，禁止继续加仓")
                        .arg(currentDrawdownPercent, 0, 'f', 2)
                        .arg(level2Breaker, 0, 'f', 2);
                    riskScore = 0.9;
                } else if (level1Breaker > 0.0 && currentDrawdownPercent >= level1Breaker) {
                    decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
                    reason = QStringLiteral("账户当前回撤 %1%%，已触发一级熔断线 %2%%，禁止继续加仓")
                        .arg(currentDrawdownPercent, 0, 'f', 2)
                        .arg(level1Breaker, 0, 'f', 2);
                    riskScore = 0.86;
                } else if (maxDrawdownLimit > 0.0 && currentDrawdownPercent >= maxDrawdownLimit) {
                    decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
                    reason = QStringLiteral("账户当前回撤 %1%%，超过上限 %2%%，禁止继续加仓")
                        .arg(currentDrawdownPercent, 0, 'f', 2)
                        .arg(maxDrawdownLimit, 0, 'f', 2);
                    riskScore = 0.93;
                }

                if (decisionType == QString::fromUtf8(engine::EventTypes::RISK_APPROVAL) && currentTotalAsset > 0.0) {
                    const double projectedPositionPercent = ((currentSymbolMarketValue + requestedNotional) / currentTotalAsset) * 100.0;
                    const double projectedTotalExposure = ((currentMarketValue + requestedNotional) / currentTotalAsset) * 100.0;

                    if (projectedPositionPercent > maxPositionPercent) {
                        decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
                        reason = QStringLiteral("单票集中度 %1%%，超过上限 %2%%")
                            .arg(projectedPositionPercent, 0, 'f', 2)
                            .arg(maxPositionPercent, 0, 'f', 2);
                        riskScore = 0.88;
                    } else if (projectedTotalExposure > maxTotalExposure) {
                        decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
                        reason = QStringLiteral("组合总仓位 %1%%，超过上限 %2%%")
                            .arg(projectedTotalExposure, 0, 'f', 2)
                            .arg(maxTotalExposure, 0, 'f', 2);
                        riskScore = 0.84;
                    }
                }
            }
        }
    }

    decision.insert("approved", decisionType == QString::fromUtf8(engine::EventTypes::RISK_APPROVAL));
    decision.insert("decisionType", decisionType);
    decision.insert("reason", reason);
    decision.insert("riskScore", riskScore);

    if (publishDecisionEvent) {
        QString correlationId = signalData.value(QStringLiteral("correlationId")).toString().trimmed();
        if (correlationId.isEmpty()) {
            correlationId = !orderId.isEmpty() ? orderId : clientOrderId;
        }
        publishRiskDecision(decision, decisionType, correlationId);
    }

    return decision;
}

void RiskMonitorService::publishRiskDecision(const QVariantMap& decision,
                                            const QString& eventType,
                                            const QString& correlationId)
{
    engine::EventBus* bus = engine::get_engine_event_bus();
    if (!bus || !bus->is_running()) {
        return;
    }

    engine::EventFormat event = engine::EventFormat::create_from_strings(
        eventType.toStdString(),
        "RISK_MONITOR_SERVICE",
        0);
    event.correlation_id = correlationId.toStdString();
    event.set("strategy_id", decision.value("strategyId").toString().toStdString());
    event.set("strategy_name", decision.value("strategyName").toString().toStdString());
    if (!decision.value("businessStrategyId").toString().trimmed().isEmpty()) {
        event.set("business_strategy_id", decision.value("businessStrategyId").toString().toStdString());
    }
    if (!decision.value("runtimeStrategyId").toString().trimmed().isEmpty()) {
        event.set("runtime_strategy_id", decision.value("runtimeStrategyId").toString().toStdString());
    }
    if (!decision.value("orderId").toString().trimmed().isEmpty()) {
        event.set("order_id", decision.value("orderId").toString().toStdString());
    }
    if (!decision.value("clientOrderId").toString().trimmed().isEmpty()) {
        event.set("client_order_id", decision.value("clientOrderId").toString().toStdString());
    }
    event.set("symbol", decision.value("symbol").toString().toStdString());
    event.set("action", decision.value("action").toString().toStdString());
    event.set("side", decision.value("side").toString().toStdString());
    event.set("price", decision.value("price").toDouble());
    event.set("quantity", static_cast<int64_t>(decision.value("quantity").toLongLong()));
    if (decision.value("cashAmount").toDouble() > 0.0) {
        event.set("cash_amount", decision.value("cashAmount").toDouble());
    }
    if (!decision.value("orderType").toString().trimmed().isEmpty()) {
        event.set("order_type", decision.value("orderType").toString().toStdString());
    }
    if (!decision.value("type").toString().trimmed().isEmpty()) {
        event.set("type", decision.value("type").toString().toStdString());
    }
    if (!decision.value("positionEffect").toString().trimmed().isEmpty()) {
        event.set("position_effect_text", decision.value("positionEffect").toString().toStdString());
        event.set("position_effect", decision.value("positionEffect").toString().toStdString());
    }
    if (!decision.value("underlying").toString().trimmed().isEmpty()) {
        event.set("underlying", decision.value("underlying").toString().toStdString());
    }
    if (!decision.value("optionType").toString().trimmed().isEmpty()) {
        event.set("option_type", decision.value("optionType").toString().toStdString());
    }
    if (!decision.value("expiry").toString().trimmed().isEmpty()) {
        event.set("expiry", decision.value("expiry").toString().toStdString());
    }
    event.set("strength", decision.value("strength").toDouble());
    event.set("risk_score", decision.value("riskScore").toDouble());
    event.set("approved", decision.value("approved").toBool());
    event.set("reason", decision.value("reason").toString().toStdString());
    event.metadata["strategy_id"] = decision.value("strategyId").toString().toStdString();
    if (!decision.value("businessStrategyId").toString().trimmed().isEmpty()) {
        event.metadata["business_strategy_id"] = decision.value("businessStrategyId").toString().toStdString();
    }
    if (!decision.value("runtimeStrategyId").toString().trimmed().isEmpty()) {
        event.metadata["runtime_strategy_id"] = decision.value("runtimeStrategyId").toString().toStdString();
    }
    if (!decision.value("orderId").toString().trimmed().isEmpty()) {
        event.metadata["order_id"] = decision.value("orderId").toString().toStdString();
    }
    if (!decision.value("clientOrderId").toString().trimmed().isEmpty()) {
        event.metadata["client_order_id"] = decision.value("clientOrderId").toString().toStdString();
    }
    event.metadata["symbol"] = decision.value("symbol").toString().toStdString();
    event.metadata["action"] = decision.value("action").toString().toStdString();
    event.metadata["side"] = decision.value("side").toString().toStdString();
    if (decision.value("cashAmount").toDouble() > 0.0) {
        event.metadata["cash_amount"] = QString::number(decision.value("cashAmount").toDouble(), 'f', 6).toStdString();
    }
    if (!decision.value("orderType").toString().trimmed().isEmpty()) {
        event.metadata["order_type"] = decision.value("orderType").toString().toStdString();
    }
    if (!decision.value("type").toString().trimmed().isEmpty()) {
        event.metadata["type"] = decision.value("type").toString().toStdString();
    }
    if (!decision.value("positionEffect").toString().trimmed().isEmpty()) {
        event.metadata["position_effect"] = decision.value("positionEffect").toString().toStdString();
        event.metadata["position_effect_text"] = decision.value("positionEffect").toString().toStdString();
    }
    if (!decision.value("underlying").toString().trimmed().isEmpty()) {
        event.metadata["underlying"] = decision.value("underlying").toString().toStdString();
    }
    if (!decision.value("optionType").toString().trimmed().isEmpty()) {
        event.metadata["option_type"] = decision.value("optionType").toString().toStdString();
    }
    if (!decision.value("expiry").toString().trimmed().isEmpty()) {
        event.metadata["expiry"] = decision.value("expiry").toString().toStdString();
    }
    event.metadata["approved"] = decision.value("approved").toBool() ? "true" : "false";
    event.metadata["reason"] = decision.value("reason").toString().toStdString();
    event.metadata["risk_score"] = QString::number(decision.value("riskScore").toDouble(), 'f', 6).toStdString();
    event.metadata["event_contract"] = "canonical";

    const int priority = eventType == QString::fromUtf8(engine::EventTypes::RISK_REJECT)
        ? static_cast<int>(engine::EventPriority::HIGH)
        : static_cast<int>(engine::EventPriority::NORMAL);
    const auto result = bus->publish(event, priority);
    if (!result) {
        qWarning() << "RiskMonitorService: failed to publish risk decision" << QString::fromStdString(result.message);
        return;
    }

    emit riskDecisionPublished(decision);
}