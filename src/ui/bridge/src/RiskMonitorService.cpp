#include "RiskMonitorService.h"

#include "FactorService.h"
#include "StrategyService.h"

#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"

#include "../../domain/backtest/include/DatabaseStockDataProvider.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QSet>
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
    return {};
}

double eventDoubleValue(const engine::EventFormat& event, const std::string& key, double fallback = 0.0)
{
    auto numericValue = event.get<double>(key);
    if (numericValue.has_value()) {
        return *numericValue;
    }
    const QString textValue = eventStringValue(event, key);
    if (textValue.isEmpty()) {
        return fallback;
    }
    bool ok = false;
    const double value = textValue.toDouble(&ok);
    return ok ? value : fallback;
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
{
}

void RiskMonitorService::initialize()
{
    QMutexLocker locker(&m_mutex);
    if (m_initialized) {
        return;
    }

    initializeEventBusIntegration();

    m_initialized = true;
    emit initializedChanged();
}

bool RiskMonitorService::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized;
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

    m_eventBusIntegrated = true;
    qDebug() << "RiskMonitorService: EventBus integration initialized";
}

void RiskMonitorService::handleStrategySignal(const engine::EventFormat& event)
{
    const QString strategyId = eventStringValue(event, "strategy_id");
    const QString strategyName = eventStringValue(event, "strategy_name");
    const QString symbol = eventStringValue(event, "symbol");
    const QString action = eventStringValue(event, "action").toUpper();
    const double price = eventDoubleValue(event, "price", 0.0);
    const double strength = eventDoubleValue(event, "strength", 0.0);

    QVariantMap decision;
    decision.insert("strategyId", strategyId);
    decision.insert("strategyName", strategyName);
    decision.insert("symbol", symbol);
    decision.insert("action", action);
    decision.insert("price", price);
    decision.insert("strength", strength);

    QString decisionType = QString::fromUtf8(engine::EventTypes::RISK_APPROVAL);
    QString reason = QStringLiteral("基础风控校验通过");
    double riskScore = 0.15;

    if (strategyId.isEmpty() || symbol.isEmpty() || action.isEmpty()) {
        decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
        reason = QStringLiteral("策略信号缺少必要字段");
        riskScore = 1.0;
    } else if (price <= 0.0) {
        decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
        reason = QStringLiteral("价格无效，拒绝执行");
        riskScore = 0.95;
    } else if (strength <= 0.0) {
        decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
        reason = QStringLiteral("信号强度不足");
        riskScore = 0.75;
    } else {
        const QVariantMap strategy = StrategyService::instance()->getStrategyById(strategyId);
        const QString strategyStatus = strategy.value("status").toString().trimmed().toUpper();
        if (!strategy.isEmpty() && strategyStatus != QStringLiteral("ACTIVE") && strategyStatus != QStringLiteral("TESTING")) {
            decisionType = QString::fromUtf8(engine::EventTypes::RISK_REJECT);
            reason = QStringLiteral("策略未激活，拒绝执行");
            riskScore = 0.9;
        }
    }

    decision.insert("approved", decisionType == QString::fromUtf8(engine::EventTypes::RISK_APPROVAL));
    decision.insert("decisionType", decisionType);
    decision.insert("reason", reason);
    decision.insert("riskScore", riskScore);

    publishRiskDecision(decision, decisionType, QString::fromStdString(event.id));
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
    event.set("symbol", decision.value("symbol").toString().toStdString());
    event.set("action", decision.value("action").toString().toStdString());
    event.set("price", decision.value("price").toDouble());
    event.set("strength", decision.value("strength").toDouble());
    event.set("risk_score", decision.value("riskScore").toDouble());
    event.set("approved", decision.value("approved").toBool());
    event.set("reason", decision.value("reason").toString().toStdString());
    event.metadata["strategy_id"] = decision.value("strategyId").toString().toStdString();
    event.metadata["symbol"] = decision.value("symbol").toString().toStdString();
    event.metadata["action"] = decision.value("action").toString().toStdString();
    event.metadata["approved"] = decision.value("approved").toBool() ? "true" : "false";
    event.metadata["reason"] = decision.value("reason").toString().toStdString();
    event.metadata["risk_score"] = QString::number(decision.value("riskScore").toDouble(), 'f', 6).toStdString();

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