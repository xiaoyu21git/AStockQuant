#include "../include/StrategyBridge.h"

#include "../include/StrategyLifecycleStatus.h"
#include "../include/StrategyListModel.h"

#include "database/StrategyRepository.h"

#include "../../domain/backtest/include/ResolvedStrategyBehavior.h"
#include "../../domain/strategies/include/MultiFactorSelectionStrategy.h"
#include "../../domain/strategies/include/StrategyDefinitionTypes.h"
#include "../../domain/strategy/include/RuntimeStrategyFactory.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSet>
#include <QTimer>

#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

using astock::database::PersistedStrategyData;
using astock::database::StrategyRepository;

namespace {

constexpr const char* kRuleProfileKey = "rule_profile";
constexpr const char* kRuleComposerStateKey = "rule_composer_state";
constexpr const char* kFactorOverlayKey = "factor_overlay";
constexpr std::uint32_t kDefaultRuntimeMaxOrderQuantity = 100;
constexpr std::uint64_t kDefaultRuntimeSnapshotVersion = 1;
constexpr std::int32_t kUnknownIndustryBucket = 0;

QString normalizedRuntimeDateFromTradingDay(const std::int32_t tradingDay)
{
    const QString rawText = QString::number(tradingDay);
    if (rawText.size() != 8) {
        return {};
    }

    const QDate date = QDate::fromString(rawText, QStringLiteral("yyyyMMdd"));
    return date.isValid() ? date.toString(QStringLiteral("yyyy-MM-dd")) : QString{};
}

std::uint64_t runtimeStrategyInstanceIdFromUuid(const std::string& strategyId)
{
    const std::uint64_t hashedValue = static_cast<std::uint64_t>(std::hash<std::string>{}(strategyId));
    return hashedValue == 0 ? kDefaultRuntimeSnapshotVersion : hashedValue;
}

std::optional<domain::strategies::WeightScheme> readWeightScheme(const QVariantMap& parameters)
{
    bool ok = false;
    const int index = parameters.value(QStringLiteral("weightScheme")).toInt(&ok);
    if (!ok) {
        return domain::strategies::WeightScheme::EQUAL;
    }

    switch (static_cast<domain::strategies::WeightScheme>(index)) {
    case domain::strategies::WeightScheme::EQUAL:
    case domain::strategies::WeightScheme::MARKET_CAP:
    case domain::strategies::WeightScheme::SIGNAL_STRENGTH:
    case domain::strategies::WeightScheme::RISK_PARITY:
        return static_cast<domain::strategies::WeightScheme>(index);
    }

    return std::nullopt;
}

std::optional<domain::strategies::RebalanceFrequency> readRebalanceFrequency(const QVariantMap& parameters)
{
    bool ok = false;
    const int index = parameters.value(QStringLiteral("rebalanceFrequency")).toInt(&ok);
    if (!ok) {
        return domain::strategies::RebalanceFrequency::DAILY;
    }

    const auto frequency = static_cast<domain::strategies::RebalanceFrequency>(index);
    if (!domain::strategies::isValidRebalanceFrequency(frequency)) {
        return std::nullopt;
    }
    return frequency;
}

std::optional<std::vector<domain::strategies::FactorWeight>> readFactorWeights(const QVariantMap& parameters)
{
    const QVariantList rawWeights = parameters.value(QStringLiteral("factorWeights")).toList();
    if (rawWeights.isEmpty()) {
        return std::nullopt;
    }

    std::vector<domain::strategies::FactorWeight> weights;
    weights.reserve(static_cast<std::size_t>(rawWeights.size()));
    for (const QVariant& rawWeight : rawWeights) {
        const QVariantMap item = rawWeight.toMap();
        bool factorOk = false;
        bool weightOk = false;
        const qulonglong factorId = item.value(QStringLiteral("factorId")).toULongLong(&factorOk);
        const double weight = item.value(QStringLiteral("weight")).toDouble(&weightOk);
        if (!factorOk || factorId == 0 || !weightOk) {
            return std::nullopt;
        }
        weights.push_back(domain::strategies::FactorWeight{
            static_cast<domain::strategies::FactorId>(factorId),
            weight});
    }

    return weights;
}

std::vector<domain::strategies::FactorId> factorIdsFromWeights(
    const std::vector<domain::strategies::FactorWeight>& weights)
{
    std::vector<domain::strategies::FactorId> factorIds;
    factorIds.reserve(weights.size());
    for (const domain::strategies::FactorWeight& item : weights) {
        factorIds.push_back(item.factorId);
    }
    return factorIds;
}

domain::strategy::CallbackRuntimeFactorServiceAdapter::Callbacks buildFactorCallbacks(
    std::vector<domain::strategies::FactorId> factorIds)
{
    struct RuntimeFactorCallbackState final {
        std::mutex mutex;
        QString latestTradeDate;
        QStringList latestSymbols;
        std::vector<domain::strategies::FactorId> factorIds;
    };

    auto state = std::make_shared<RuntimeFactorCallbackState>();
    state->factorIds = std::move(factorIds);

    domain::strategy::CallbackRuntimeFactorServiceAdapter::Callbacks callbacks;
    callbacks.updateIncremental = [state](const domain::strategy::MarketDataPoint& point) {
        if (!point.isValid()) {
            return domain::strategy::StrategyServiceFlowResult(domain::strategy::StrategyServiceFlowCode::InvalidInput);
        }

        const QString runtimeDate = normalizedRuntimeDateFromTradingDay(point.tradingDay());
        if (runtimeDate.isEmpty()) {
            return domain::strategy::StrategyServiceFlowResult(domain::strategy::StrategyServiceFlowCode::InvalidInput);
        }

        const std::lock_guard<std::mutex> lock(state->mutex);
        state->latestTradeDate = runtimeDate;
        state->latestSymbols = QStringList{QString::number(point.instrumentId().value())};
        return domain::strategy::StrategyServiceFlowResult(domain::strategy::StrategyServiceFlowCode::Ok);
    };

    callbacks.updateBatch = [state](const std::vector<domain::strategy::MarketDataPoint>& batch) {
        if (batch.empty()) {
            return domain::strategy::StrategyServiceFlowResult(domain::strategy::StrategyServiceFlowCode::InvalidInput);
        }

        QString latestDate;
        QStringList symbols;
        QSet<QString> seenSymbols;
        for (const domain::strategy::MarketDataPoint& point : batch) {
            if (!point.isValid()) {
                continue;
            }
            const QString runtimeDate = normalizedRuntimeDateFromTradingDay(point.tradingDay());
            if (!runtimeDate.isEmpty()) {
                latestDate = runtimeDate;
            }

            const QString symbol = QString::number(point.instrumentId().value());
            if (!symbol.isEmpty() && !seenSymbols.contains(symbol)) {
                seenSymbols.insert(symbol);
                symbols.append(symbol);
            }
        }

        if (latestDate.isEmpty() || symbols.isEmpty()) {
            return domain::strategy::StrategyServiceFlowResult(domain::strategy::StrategyServiceFlowCode::InvalidInput);
        }

        const std::lock_guard<std::mutex> lock(state->mutex);
        state->latestTradeDate = latestDate;
        state->latestSymbols = symbols;
        return domain::strategy::StrategyServiceFlowResult(domain::strategy::StrategyServiceFlowCode::Ok);
    };

    callbacks.copySnapshots = [state](std::vector<domain::strategy::RuntimeFactorSnapshot>& outputSnapshots) {
        outputSnapshots.clear();

        QString tradeDate;
        QStringList symbols;
        std::vector<domain::strategies::FactorId> factors;
        {
            const std::lock_guard<std::mutex> lock(state->mutex);
            tradeDate = state->latestTradeDate;
            symbols = state->latestSymbols;
            factors = state->factorIds;
        }

        if (tradeDate.isEmpty() || symbols.isEmpty() || factors.empty()) {
            return;
        }
        Q_UNUSED(kUnknownIndustryBucket)
    };

    return callbacks;
}

const QStringList& frozenStrategyUpsertPayloadKeys()
{
    static const QStringList keys = {
        QStringLiteral("strategyId"),
        QStringLiteral("strategyName"),
        QStringLiteral("strategyTypeIndex"),
        QStringLiteral("strategyBehaviorKind"),
        QStringLiteral("description"),
        QStringLiteral("assetTypeIndex"),
        QStringLiteral("timeFrameIndex"),
        QStringLiteral("riskLevelIndex"),
        QStringLiteral("optimization_method"),
        QStringLiteral("parameters"),
        QStringLiteral("tags"),
        QStringLiteral("status"),
        QStringLiteral("factorIds"),
        QStringLiteral("ruleIds")
    };
    return keys;
}

bool hasUnexpectedPayloadKeys(const QVariantMap& payload,
                              const QStringList& allowedKeys,
                              QString* firstUnexpectedKey)
{
    for (auto it = payload.constBegin(); it != payload.constEnd(); ++it) {
        if (allowedKeys.contains(it.key())) {
            continue;
        }
        if (firstUnexpectedKey) {
            *firstUnexpectedKey = it.key();
        }
        return true;
    }
    return false;
}

bool isVariantMapObject(const QVariantMap& payload, const QString& key)
{
    if (!payload.contains(key)) {
        return false;
    }
    const QVariant value = payload.value(key);
    return value.isValid() && !value.isNull() && value.canConvert<QVariantMap>();
}

bool isRequiredStrategyParametersShapeValid(const QVariantMap& parameters)
{
    return isVariantMapObject(parameters, QString::fromLatin1(kRuleProfileKey))
        && isVariantMapObject(parameters, QString::fromLatin1(kRuleComposerStateKey));
}

bool hasLegacyParameterKeys(const QVariantMap& parameters)
{
    return parameters.contains(QStringLiteral("commonConfig"))
        || parameters.contains(QStringLiteral("strategySpec"))
        || parameters.contains(QStringLiteral("rule_template_bindings"))
        || parameters.contains(QStringLiteral("advanced_options"))
        || parameters.contains(QStringLiteral("factorOverlaySnapshot"));
}

template <typename TValue, typename TConverter>
TValue readScalarByKeys(const QVariantMap& payload,
                        std::initializer_list<const char*> keys,
                        const TValue& fallback,
                        TConverter&& converter)
{
    for (const char* key : keys) {
        const QVariant rawValue = payload.value(QString::fromLatin1(key));
        if (!rawValue.isValid() || rawValue.isNull()) {
            continue;
        }

        if constexpr (std::is_same_v<TValue, bool>) {
            if (rawValue.metaType().id() != QMetaType::Bool) {
                continue;
            }
        }

        bool ok = false;
        const TValue parsed = converter(rawValue, ok);
        if (ok) {
            return parsed;
        }
    }

    return fallback;
}

template <typename TValue>
TValue readScalarByKeys(const QVariantMap& payload,
                        std::initializer_list<const char*> keys,
                        const TValue& fallback)
{
    if constexpr (std::is_same_v<TValue, bool>) {
        return readScalarByKeys<TValue>(
            payload,
            keys,
            fallback,
            [](const QVariant& rawValue, bool& ok) {
                ok = true;
                return rawValue.toBool();
            });
    } else if constexpr (std::is_same_v<TValue, int>) {
        return readScalarByKeys<TValue>(
            payload,
            keys,
            fallback,
            [](const QVariant& rawValue, bool& ok) {
                return rawValue.toInt(&ok);
            });
    } else if constexpr (std::is_same_v<TValue, double>) {
        return readScalarByKeys<TValue>(
            payload,
            keys,
            fallback,
            [](const QVariant& rawValue, bool& ok) {
                return rawValue.toDouble(&ok);
            });
    } else {
        static_assert(std::is_same_v<TValue, void>, "unsupported scalar read type");
    }
}

} // namespace

QString StrategyBridge::readText(const QVariantMap& payload,
                                 std::initializer_list<const char*> keys) const
{
    for (const char* key : keys) {
        const QVariant rawValue = payload.value(QString::fromLatin1(key));
        if (!rawValue.isValid() || rawValue.isNull()) {
            continue;
        }
        const QString value = rawValue.toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

bool StrategyBridge::isTypeIdxValid(const int index) const
{
    return domain::strategies::isValidStrategyTypeIndex(index);
}

StrategyBridge::StrategyTypeSpec StrategyBridge::readTypeSpec(const QVariantMap& payload) const
{
    StrategyTypeSpec spec;
    const int typeIndex = readScalarByKeys<int>(payload, {"strategyTypeIndex"}, -1);
    if (!isTypeIdxValid(typeIndex)) {
        return spec;
    }

    spec.value = static_cast<domain::strategies::StrategyType>(typeIndex);
    spec.valid = true;
    return spec;
}

StrategyBridge::StrategyBehaviorKindSpec StrategyBridge::readBehaviorKindSpec(const QVariantMap& payload) const
{
    StrategyBehaviorKindSpec spec;
    const int behaviorIndex = readScalarByKeys<int>(payload, {"strategyBehaviorKind"}, -1);
    if (!domain::strategies::isValidStrategyBehaviorKindIndex(behaviorIndex)) {
        return spec;
    }

    spec.value = static_cast<domain::strategies::StrategyBehaviorKind>(behaviorIndex);
    spec.valid = true;
    return spec;
}

StrategyBridge::FactorIdListSpec StrategyBridge::readFactorIds(const QVariantMap& payload) const
{
    FactorIdListSpec spec;
    const QVariant rawValue = payload.value(QStringLiteral("factorIds"));
    if (!rawValue.isValid() || rawValue.isNull()) {
        return spec;
    }
    spec.provided = true;
    const QVariantList rawList = rawValue.toList();
    spec.values.reserve(static_cast<size_t>(rawList.size()));
    for (const QVariant& item : rawList) {
        bool ok = false;
        const qulonglong value = item.toULongLong(&ok);
        if (!ok || value == 0) {
            spec.valid = false;
            return spec;
        }
        spec.values.push_back(static_cast<domain::strategies::FactorId>(value));
    }
    return spec;
}

StrategyBridge::RuleIdListSpec StrategyBridge::readRuleIds(const QVariantMap& payload) const
{
    RuleIdListSpec spec;
    const QVariant rawValue = payload.value(QStringLiteral("ruleIds"));
    if (!rawValue.isValid() || rawValue.isNull()) {
        return spec;
    }
    spec.provided = true;
    const QVariantList rawList = rawValue.toList();
    spec.values.reserve(static_cast<size_t>(rawList.size()));
    for (const QVariant& item : rawList) {
        bool ok = false;
        const qulonglong value = item.toULongLong(&ok);
        if (!ok || value == 0) {
            spec.valid = false;
            return spec;
        }
        spec.values.push_back(static_cast<domain::strategies::RuleId>(value));
    }
    return spec;
}

bool StrategyBridge::hasForbiddenFields(const QVariantMap& payload) const
{
    const QStringList forbiddenKeys = {
        QStringLiteral("strategyCode"),
        QStringLiteral("version"),
        QStringLiteral("author"),
        QStringLiteral("performanceMetrics"),
        QStringLiteral("engineStrategyId")
    };
    for (const QString& key : forbiddenKeys) {
        if (payload.contains(key) && payload.value(key).isValid() && !payload.value(key).isNull()) {
            return true;
        }
    }
    return false;
}

QVariant StrategyBridge::readValue(const QVariantMap& payload,
                                   std::initializer_list<const char*> keys) const
{
    for (const char* key : keys) {
        const QVariant value = payload.value(QString::fromLatin1(key));
        if (value.isValid() && !value.isNull()) {
            return value;
        }
    }
    return {};
}

QVariantMap StrategyBridge::readMap(const QVariantMap& payload,
                                    std::initializer_list<const char*> keys) const
{
    const QVariant value = readValue(payload, keys);
    return value.isValid() && value.canConvert<QVariantMap>() ? value.toMap() : QVariantMap{};
}

std::optional<domain::strategies::StrategyUuid> StrategyBridge::readId(const QVariantMap& payload) const
{
    const std::string rawValue = payload.value(QString::fromLatin1(kStrategyIdKey)).toString().trimmed().toStdString();
    if (!foundation::utils::Uuid::is_valid_uuid(rawValue)) {
        return std::nullopt;
    }
    return domain::strategies::StrategyUuid::from_string(rawValue);
}

StrategyBridge::BridgeUpsertRequest StrategyBridge::parseReq(const QVariantMap& payload) const
{
    BridgeUpsertRequest request;
    const QVariantMap parameters = readMap(payload, {"parameters"});
    const std::optional<domain::strategies::StrategyUuid> strategyId = readId(payload);
    if (strategyId.has_value()) {
        request.setStrategyId(*strategyId);
    }

    request.setStrategyName(readText(payload, {"strategyName"}).toStdString());
    request.setDescription(readText(payload, {"description"}).toStdString());
    request.setStrategyType(readTypeSpec(payload));
    request.setBehaviorKind(readBehaviorKindSpec(payload));
    request.setFactorIds(readFactorIds(payload));
    request.setRuleIds(readRuleIds(payload));
    request.setStatus(readScalarByKeys<bool>(payload, {"status"}, request.status()));
    request.setParameters(parameters);
    return request;
}

StrategyBridge::BridgeUpsertRequest::CommonConfigPayload StrategyBridge::readCommonConfig(//通用参数
    const QVariantMap& parameters) const
{
    BridgeUpsertRequest::CommonConfigPayload common;
    common.setAllowShort(readScalarByKeys<bool>(parameters, {"allowShort"}, common.allowShort()));
    common.setMaxPositions(readScalarByKeys<int>(parameters, {"maxPositions"}, common.maxPositions()));
    common.setMaxWeightPerStock(readScalarByKeys<double>(parameters, {"maxWeightPerStock"}, common.maxWeightPerStock()));
    common.setMinWeightPerStock(readScalarByKeys<double>(parameters, {"minWeightPerStock"}, common.minWeightPerStock()));
    common.setWeightScheme(readScalarByKeys<int>(parameters, {"weightScheme"}, common.weightScheme()));
    common.setRebalanceFrequency(readScalarByKeys<int>(parameters, {"rebalanceFrequency"}, common.rebalanceFrequency()));
    return common;
}

StrategyBridge::BridgeUpsertRequest::StrategySpecPayload StrategyBridge::readStrategySpecPayload(
    const StrategyTypeSpec& strategyType,
    const QVariantMap& parameters) const
{
    BridgeUpsertRequest::StrategySpecPayload spec;
    if (!strategyType.valid) {
        return spec;
    }

    const auto pickValue = [this, &parameters](std::initializer_list<const char*> keys) {
        return readValue(parameters, keys);
    };

    switch (strategyType.value) {
    case domain::strategies::StrategyType::DOUBLE_MOVING_AVERAGE:
        spec.setValue(QStringLiteral("fastPeriod"), pickValue({"fastPeriod"}));
        spec.setValue(QStringLiteral("slowPeriod"), pickValue({"slowPeriod"}));
        spec.setValue(QStringLiteral("priceField"), pickValue({"priceField"}));
        break;
    case domain::strategies::StrategyType::TURTLE_BREAKOUT:
        spec.setValue(QStringLiteral("channelPeriod"), pickValue({"channelPeriod"}));
        spec.setValue(QStringLiteral("breakoutMultiplier"), pickValue({"breakoutMultiplier"}));
        spec.setValue(QStringLiteral("atrPeriod"), pickValue({"atrPeriod"}));
        break;
    case domain::strategies::StrategyType::BOLLINGER_BAND_MEAN_REVERSION:
        spec.setValue(QStringLiteral("period"), pickValue({"period"}));
        spec.setValue(QStringLiteral("standardDeviationMultiplier"), pickValue({"standardDeviationMultiplier"}));
        spec.setValue(QStringLiteral("entryThreshold"), pickValue({"entryThreshold"}));
        spec.setValue(QStringLiteral("exitThreshold"), pickValue({"exitThreshold"}));
        break;
    case domain::strategies::StrategyType::RSI_MEAN_REVERSION:
        spec.setValue(QStringLiteral("period"), pickValue({"period"}));
        spec.setValue(QStringLiteral("oversoldLevel"), pickValue({"oversoldLevel"}));
        spec.setValue(QStringLiteral("overboughtLevel"), pickValue({"overboughtLevel"}));
        break;
    case domain::strategies::StrategyType::MULTI_FACTOR_SELECTION:
        spec.setValue(QStringLiteral("factorWeights"), pickValue({"factorWeights"}));
        spec.setValue(QStringLiteral("topN"), pickValue({"topN"}));
        spec.setValue(QStringLiteral("industryNeutral"), pickValue({"industryNeutral"}));
        break;
    case domain::strategies::StrategyType::EARNINGS_SURPRISE:
        spec.setValue(QStringLiteral("surpriseThreshold"), pickValue({"surpriseThreshold"}));
        spec.setValue(QStringLiteral("holdDays"), pickValue({"holdDays"}));
        spec.setValue(QStringLiteral("eventSources"), pickValue({"eventSources"}));
        break;
    case domain::strategies::StrategyType::STATISTICAL_PAIR_TRADING:
        spec.setValue(QStringLiteral("tradingPair"), pickValue({"tradingPair"}));
        spec.setValue(QStringLiteral("hedgeRatio"), pickValue({"hedgeRatio"}));
        spec.setValue(QStringLiteral("lookback"), pickValue({"lookback"}));
        spec.setValue(QStringLiteral("entryZScore"), pickValue({"entryZScore"}));
        spec.setValue(QStringLiteral("exitZScore"), pickValue({"exitZScore"}));
        break;
    case domain::strategies::StrategyType::RISK_PARITY_ALLOCATION:
        spec.setValue(QStringLiteral("assets"), pickValue({"assets"}));
        spec.setValue(QStringLiteral("volatilityLookback"), pickValue({"volatilityLookback"}));
        spec.setValue(QStringLiteral("targetVolatility"), pickValue({"targetVolatility"}));
        break;
    case domain::strategies::StrategyType::MACHINE_LEARNING_SELECTION:
        spec.setValue(QStringLiteral("modelId"), pickValue({"modelId"}));
        spec.setValue(QStringLiteral("featureIds"), pickValue({"featureIds"}));
        spec.setValue(QStringLiteral("topN"), pickValue({"topN"}));
        break;
    case domain::strategies::StrategyType::ORDER_FLOW_IMBALANCE:
        spec.setValue(QStringLiteral("depthLevels"), pickValue({"depthLevels"}));
        spec.setValue(QStringLiteral("imbalanceThreshold"), pickValue({"imbalanceThreshold"}));
        spec.setValue(QStringLiteral("maxHoldSeconds"), pickValue({"maxHoldSeconds"}));
        break;
    case domain::strategies::StrategyType::VOLATILITY_SPREAD:
        spec.setValue(QStringLiteral("underlying"), pickValue({"underlying"}));
        spec.setValue(QStringLiteral("optionChainFilter"), pickValue({"optionChainFilter"}));
        spec.setValue(QStringLiteral("historicalVolatilityWindow"), pickValue({"historicalVolatilityWindow"}));
        spec.setValue(QStringLiteral("entrySpreadUpper"), pickValue({"entrySpreadUpper"}));
        spec.setValue(QStringLiteral("entrySpreadLower"), pickValue({"entrySpreadLower"}));
        spec.setValue(QStringLiteral("deltaNeutral"), pickValue({"deltaNeutral"}));
        break;
    }

    return spec;
}



void StrategyBridge::applyReq(const BridgeUpsertRequest& request,
                              PersistedStrategyData& target) const
{
    if (request.hasStrategyId()) {
        target.strategyId = request.strategyId().to_string();
    }

    if (!request.strategyName().empty()) {
        target.metadata.name = request.strategyName();
    }
    if (!request.description().empty()) {
        target.metadata.description = request.description();
    }
    if (request.behaviorKind().valid) {
        target.metadata.behaviorKind = request.behaviorKind().value;
    }

    if (request.status()) {
        target.status = strategy_view::StrategyLifecycleStatus::Active;
    } else {
        target.status = strategy_view::StrategyLifecycleStatus::Inactive;
    }
    target.metadata.enabled = request.status();

    if (request.factorIds().provided) {
        target.metadata.factorIds = request.factorIds().values;
    }
    if (request.ruleIds().provided) {
        target.metadata.ruleIds = request.ruleIds().values;
    }

    target.parameters = request.parameters();

    if (request.strategyType().valid) {
        const int typeIndex = static_cast<int>(request.strategyType().value);
        if (!isTypeIdxValid(typeIndex)) {
            std::abort();
        }
        const auto storedType = static_cast<domain::backtest::StrategyStoredType>(typeIndex);
        const auto behaviorKind = static_cast<domain::backtest::StrategyBehaviorKind>(
            static_cast<int>(target.metadata.behaviorKind));

        target.strategyIdentity = domain::backtest::ResolvedStrategyIdentity{
            storedType,
            domain::backtest::ResolvedStrategyBehavior{behaviorKind, true},
            true
        };
    }
}

std::optional<domain::strategies::StrategyUuid> StrategyBridge::parseId(const QString& input) const
{
    const std::string rawValue = input.trimmed().toStdString();
    if (!foundation::utils::Uuid::is_valid_uuid(rawValue)) {
        return std::nullopt;
    }
    return domain::strategies::StrategyUuid::from_string(rawValue);
}

QString StrategyBridge::clearedMsg() const
{
    return QStringLiteral("StrategyBridge bridge typed persistence is not implemented");
}

StrategyBridge::StrategyBridge(QObject* parent)
    : QObject(parent)
    , m_repo(std::make_unique<StrategyRepository>())
    , m_listModel(new StrategyListModel(this))
{
}

StrategyBridge::~StrategyBridge() = default;

StrategyBridge* StrategyBridge::instance()
{
    static StrategyBridge* s_instance = []() -> StrategyBridge* {
        auto* bridge = new StrategyBridge();
        bridge->init();  // 同步初始化，确保 instance() 返回后 inited() == true
        return bridge;
    }();
    return s_instance;
}

void StrategyBridge::init()
{
    if (m_inited) {
        return;
    }

    if (!m_repo || !m_repo->initialize()) {
        setErr(QStringLiteral("initialize strategy repository failed"));
        emit operationFailed(kRepositoryErrorCode, m_err);
        return;
    }

    m_inited = true;
    emit initedChanged();
    refreshModel();
}

void StrategyBridge::initAsync()
{
    QTimer::singleShot(0, this, [this]() { init(); });
}

bool StrategyBridge::inited() const
{
    return m_inited;
}

bool StrategyBridge::cacheOk() const
{
    return m_cacheOk;
}

QString StrategyBridge::add(const QVariantMap& payload)
{
    QString unexpectedKey;
    if (hasUnexpectedPayloadKeys(payload, frozenStrategyUpsertPayloadKeys(), &unexpectedKey)) {
        setErr(QStringLiteral("add payload contains non-frozen field: %1").arg(unexpectedKey));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }

    if (hasForbiddenFields(payload)) {
        setErr(QStringLiteral("add payload contains forbidden fields"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }

    init();
    if (!m_inited) {
        return {};
    }

    const QVariantMap parameters = readMap(payload, {"parameters"});
    if (parameters.isEmpty()) {
        setErr(QStringLiteral("add parameters is required"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    if (hasLegacyParameterKeys(parameters)) {
        setErr(QStringLiteral("add parameters contains legacy fields"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    if (!isRequiredStrategyParametersShapeValid(parameters)) {
        setErr(QStringLiteral("add parameters must include rule_profile and rule_composer_state objects"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    if (payload.contains(QStringLiteral("strategyId"))) {
        const QString payloadStrategyId = payload.value(QStringLiteral("strategyId")).toString().trimmed();
        if (!payloadStrategyId.isEmpty() && !parseId(payloadStrategyId).has_value()) {
            setErr(QStringLiteral("add strategyId is not a valid UUID"));
            emit operationFailed(kInvalidArgumentCode, m_err);
            return {};
        }
    }

    const BridgeUpsertRequest request = parseReq(payload);
    if (!request.factorIds().valid) {
        setErr(QStringLiteral("add factorIds contains invalid value"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    if (!request.ruleIds().valid) {
        setErr(QStringLiteral("add ruleIds contains invalid value"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    if (request.strategyName().empty()) {
        setErr(QStringLiteral("add strategyName is required"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    if (!request.strategyType().valid) {
        setErr(QStringLiteral("add strategyTypeIndex is required"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    if (!request.behaviorKind().valid) {
        setErr(QStringLiteral("add strategyBehaviorKind is required"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }

    PersistedStrategyData strategy;
    applyReq(request, strategy);
    const QString strategyId = m_repo->save(strategy);
    if (strategyId.trimmed().isEmpty()) {
        setErr(QStringLiteral("add repository save failed"));
        emit operationFailed(kRepositoryErrorCode, m_err);
        return {};
    }
    if (!parseId(strategyId).has_value()) {
        m_repo->remove(strategyId);
        setErr(QStringLiteral("add repository returned non-UUID strategyId"));
        emit operationFailed(kRepositoryErrorCode, m_err);
        return {};
    }

    refreshModel();
    emit created(strategyId, get(strategyId));
    return strategyId;
}

bool StrategyBridge::update(const QVariantMap& payload)
{
    QString unexpectedKey;
    if (hasUnexpectedPayloadKeys(payload, frozenStrategyUpsertPayloadKeys(), &unexpectedKey)) {
        setErr(QStringLiteral("update payload contains non-frozen field: %1").arg(unexpectedKey));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }

    if (hasForbiddenFields(payload)) {
        setErr(QStringLiteral("update payload contains forbidden fields"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }

    init();
    if (!m_inited) {
        return false;
    }

    const QString payloadStrategyId = payload.value(QStringLiteral("strategyId")).toString().trimmed();
    if (payloadStrategyId.isEmpty()) {
        setErr(QStringLiteral("update strategyId is required"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    if (!parseId(payloadStrategyId).has_value()) {
        setErr(QStringLiteral("update strategyId is not a valid UUID"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }

    const QVariantMap parameters = readMap(payload, {"parameters"});
    if (parameters.isEmpty()) {
        setErr(QStringLiteral("update parameters is required"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    if (hasLegacyParameterKeys(parameters)) {
        setErr(QStringLiteral("update parameters contains legacy fields"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    if (!isRequiredStrategyParametersShapeValid(parameters)) {
        setErr(QStringLiteral("update parameters must include rule_profile and rule_composer_state objects"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }

    const BridgeUpsertRequest request = parseReq(payload);
    if (!request.factorIds().valid) {
        setErr(QStringLiteral("update factorIds contains invalid value"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    if (!request.ruleIds().valid) {
        setErr(QStringLiteral("update ruleIds contains invalid value"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    if (!request.behaviorKind().valid) {
        setErr(QStringLiteral("update strategyBehaviorKind is required"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }

    const QString strategyId = payloadStrategyId;
    const auto existing = m_repo->findById(strategyId);
    if (!existing.has_value()) {
        setErr(QStringLiteral("update strategyId not found"));
        emit operationFailed(kRepositoryErrorCode, m_err);
        return false;
    }

    PersistedStrategyData strategy = *existing;
    applyReq(request, strategy);

    const bool ok = m_repo->update(strategyId, strategy);
    if (!ok) {
        setErr(QStringLiteral("update repository update failed"));
        emit operationFailed(kRepositoryErrorCode, m_err);
        return false;
    }

    refreshModel();
    emit updated(strategyId);
    return true;
}

bool StrategyBridge::remove(const QString& strategyId)
{
    const QString repositoryId = strategyId.trimmed();
    if (repositoryId.isEmpty() || !parseId(repositoryId).has_value()) {
        setErr(QStringLiteral("deleteStrategy strategyId is invalid"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }

    init();
    if (!m_inited) {
        return false;
    }

    setBusy(true);
    const bool ok = m_repo->remove(repositoryId);
    setBusy(false);
    if (!ok) {
        setErr(QStringLiteral("deleteStrategy repository remove failed"));
        emit operationFailed(kRepositoryErrorCode, m_err);
        return false;
    }

    refreshModel();
    emit deleted(repositoryId);
    return true;
}

QVariantMap StrategyBridge::get(const QString& strategyId)
{
    const QString repositoryId = strategyId.trimmed();
    if (repositoryId.isEmpty() || !parseId(repositoryId).has_value()) {
        return {};
    }

    init();
    if (!m_inited) {
        return {};
    }

    const auto strategy = m_repo->findById(repositoryId);
    if (!strategy.has_value()) {
        return {};
    }
    return strategy->toVariantMap();
}

QVariantList StrategyBridge::list()
{
    init();
    if (!m_inited) {
        return {};
    }

    QVariantList list;
    const std::vector<PersistedStrategyData> all = m_repo->findAll();
    list.reserve(static_cast<int>(all.size()));
    qInfo() << "[StrategyBridge] repository findAll count=" << static_cast<int>(all.size());
    for (const PersistedStrategyData& data : all) {
        const QVariantMap row = data.toVariantMap();
        list.push_back(row);

        const QString strategyId = row.value(QStringLiteral("strategyId")).toString();
        const QString strategyName = row.value(QStringLiteral("strategyName")).toString();

        qInfo().noquote() << QStringLiteral("[StrategyBridge] row strategyId=%1 name=%2 keys=%3")
                                 .arg(strategyId,
                                      strategyName,
                                      QString::fromUtf8(
                                          QJsonDocument(QJsonObject::fromVariantMap(row)).toJson(
                                              QJsonDocument::Compact)));
    }
    return list;
}

bool StrategyBridge::start(const QString& strategyId)
{
    // Single-signature lifecycle: start moves strategy to Active directly.
    // This bridge intentionally has no intermediate runtime state.
    const QString repositoryId = strategyId.trimmed();
    if (repositoryId.isEmpty() || !parseId(repositoryId).has_value()) {
        setErr(QStringLiteral("startStrategy strategyId is invalid"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }

    init();
    if (!m_inited) {
        return false;
    }

    std::unique_ptr<domain::strategy::StrategyEngine> runtimeEngine;
    const std::optional<PersistedStrategyData> persistedStrategy = m_repo->findById(repositoryId);
    if (!persistedStrategy.has_value()) {
        setErr(QStringLiteral("startStrategy strategyId not found"));
        emit operationFailed(kRepositoryErrorCode, m_err);
        return false;
    }
    if (isRuntimeManagedStrategy(*persistedStrategy)) {
        QString runtimeError;
        runtimeEngine = buildRuntimeEngineSession(*persistedStrategy, &runtimeError);
        if (!runtimeEngine) {
            setErr(runtimeError.isEmpty()
                       ? QStringLiteral("startStrategy runtime session wiring failed")
                       : runtimeError);
            emit operationFailed(kRepositoryErrorCode, m_err);
            return false;
        }
    }

    const bool ok = m_repo->updateStatus(repositoryId, strategy_view::StrategyLifecycleStatus::Active);
    if (!ok) {
        setErr(QStringLiteral("startStrategy updateStatus failed"));
        emit operationFailed(kRepositoryErrorCode, m_err);
        return false;
    }

    if (runtimeEngine) {
        cacheRuntimeEngineSession(repositoryId, std::move(runtimeEngine));
    }

    refreshModel();
    emit started(repositoryId);
    return true;
}

bool StrategyBridge::stop(const QString& strategyId)
{
    // Single-signature lifecycle: stop moves strategy to Inactive directly
    // and removes the active runtime session.
    const QString repositoryId = strategyId.trimmed();
    if (repositoryId.isEmpty() || !parseId(repositoryId).has_value()) {
        setErr(QStringLiteral("stopStrategy strategyId is invalid"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }

    init();
    if (!m_inited) {
        return false;
    }

    const bool ok = m_repo->updateStatus(repositoryId, strategy_view::StrategyLifecycleStatus::Inactive);
    if (!ok) {
        setErr(QStringLiteral("stopStrategy updateStatus failed"));
        emit operationFailed(kRepositoryErrorCode, m_err);
        return false;
    }

    removeRuntimeEngineSession(repositoryId);

    refreshModel();
    emit stopped(repositoryId);
    return true;
}

bool StrategyBridge::isRuntimeManagedStrategy(const PersistedStrategyData& strategy) const
{
    return strategy.strategyIdentity.validStoredType
        && strategy.strategyIdentity.storedType == domain::backtest::StrategyStoredType::MULTI_FACTOR_SELECTION;
}

std::unique_ptr<domain::strategy::StrategyEngine> StrategyBridge::buildRuntimeEngineSession(
    const PersistedStrategyData& strategy,
    QString* errorMessage) const
{
    const QVariantMap& parameters = strategy.parameters;

    const std::optional<std::vector<domain::strategies::FactorWeight>> factorWeights = readFactorWeights(parameters);
    if (!factorWeights.has_value() || factorWeights->empty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("startStrategy factorWeights is required for runtime wiring");
        }
        return nullptr;
    }

    bool topNOk = false;
    const int topN = parameters.value(QStringLiteral("topN")).toInt(&topNOk);
    if (!topNOk || topN <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("startStrategy topN is required and must be positive");
        }
        return nullptr;
    }

    const std::optional<domain::strategies::WeightScheme> weightScheme = readWeightScheme(parameters);
    if (!weightScheme.has_value()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("startStrategy weightScheme is invalid");
        }
        return nullptr;
    }

    const std::optional<domain::strategies::RebalanceFrequency> rebalanceFrequency =
        readRebalanceFrequency(parameters);
    if (!rebalanceFrequency.has_value()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("startStrategy rebalanceFrequency is invalid");
        }
        return nullptr;
    }

    domain::strategies::StrategyCommonConfig commonConfig;
    commonConfig.allowShort = parameters.value(QStringLiteral("allowShort"), false).toBool();
    commonConfig.maxPositions = parameters.value(QStringLiteral("maxPositions"), topN).toInt();
    commonConfig.maxWeightPerStock = parameters.value(QStringLiteral("maxWeightPerStock"), 0.1).toDouble();
    commonConfig.minWeightPerStock = parameters.value(QStringLiteral("minWeightPerStock"), 0.0).toDouble();
    commonConfig.weightScheme = *weightScheme;
    commonConfig.rebalanceFrequency = *rebalanceFrequency;

    domain::strategies::StrategyMetadata metadata = strategy.metadata;
    if (metadata.factorIds.empty()) {
        metadata.factorIds = factorIdsFromWeights(*factorWeights);
    }

    domain::strategies::MultiFactorSelectionStrategySpec spec;
    spec.factorWeights = *factorWeights;
    spec.topN = topN;
    spec.industryNeutral = parameters.value(QStringLiteral("industryNeutral"), false).toBool();

    auto strategyDefinition = std::make_shared<domain::strategies::MultiFactorSelectionStrategy>(
        commonConfig,
        metadata,
        spec);

    const std::uint64_t strategyInstanceId = runtimeStrategyInstanceIdFromUuid(strategy.strategyId);
    const std::uint32_t maxOrderQuantity = static_cast<std::uint32_t>(
        parameters.value(QStringLiteral("maxOrderQuantity"), static_cast<int>(kDefaultRuntimeMaxOrderQuantity)).toUInt());
    const std::uint64_t snapshotVersion = strategy.updatedAt.isValid()
        ? static_cast<std::uint64_t>(strategy.updatedAt.toSecsSinceEpoch())
        : kDefaultRuntimeSnapshotVersion;

    const domain::strategy::RuntimeStrategyContext runtimeContext(
        strategyInstanceId,
        snapshotVersion > 0 ? snapshotVersion : kDefaultRuntimeSnapshotVersion,
        maxOrderQuantity > 0 ? maxOrderQuantity : kDefaultRuntimeMaxOrderQuantity,
        commonConfig.maxWeightPerStock,
        true);
    if (!runtimeContext.isValid()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("startStrategy runtime context is invalid");
        }
        return nullptr;
    }

    domain::strategy::MultiFactorRuntimeEngineSetup setup;
    setup.strategyDefinition = strategyDefinition;
    setup.strategyInstanceId = strategyInstanceId;
    setup.context = runtimeContext;
    setup.factorCallbacks = buildFactorCallbacks(metadata.factorIds);
    setup.ruleSetId = domain::strategy::rules::kRuleSetAllPass;

    std::optional<domain::strategy::StrategyEngine> engine =
        domain::strategy::createMultiFactorRuntimeEngine(std::move(setup));
    if (!engine.has_value()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("startStrategy createMultiFactorRuntimeEngine failed");
        }
        return nullptr;
    }

    return std::make_unique<domain::strategy::StrategyEngine>(std::move(*engine));
}

void StrategyBridge::cacheRuntimeEngineSession(
    const QString& strategyId,
    std::unique_ptr<domain::strategy::StrategyEngine> engine)
{
    if (strategyId.trimmed().isEmpty() || !engine) {
        return;
    }

    const std::lock_guard<std::mutex> lock(m_runtimeEngineMutex);
    m_runtimeEngines[strategyId.trimmed().toStdString()] = std::move(engine);
}

void StrategyBridge::removeRuntimeEngineSession(const QString& strategyId)
{
    if (strategyId.trimmed().isEmpty()) {
        return;
    }

    const std::lock_guard<std::mutex> lock(m_runtimeEngineMutex);
    m_runtimeEngines.erase(strategyId.trimmed().toStdString());
}

bool StrategyBridge::saveViewCfg(const QString& strategyId, const QVariantMap& visualConfig)
{
    Q_UNUSED(strategyId);
    Q_UNUSED(visualConfig);
    setErr(clearedMsg());
    emit operationFailed(kInvalidArgumentCode, m_err);
    return false;
}

domain::strategy::StrategyEngine* StrategyBridge::backtestEngineProvider(const QString& strategyId)
{
    const std::string key = strategyId.trimmed().toStdString();
    const std::lock_guard<std::mutex> lock(m_runtimeEngineMutex);
    const auto iter = m_runtimeEngines.find(key);
    if (iter == m_runtimeEngines.end()) {
        return nullptr;
    }
    return iter->second.get();
}

bool StrategyBridge::busy() const
{
    return m_busy;
}

QString StrategyBridge::errMsg() const
{
    return m_err;
}

QString StrategyBridge::selId() const
{
    return m_selId;
}

QAbstractListModel* StrategyBridge::listModel() const
{
    return m_listModel;
}

void StrategyBridge::setSelId(const QString& strategyId)
{
    const QString normalized = strategyId.trimmed();
    if (m_selId == normalized) {
        return;
    }

    m_selId = normalized;
    emit selIdChanged();
}

void StrategyBridge::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void StrategyBridge::setErr(const QString& message)
{
    if (m_err == message) {
        return;
    }

    m_err = message;
    emit errMsgChanged();
}

void StrategyBridge::refreshModel()
{
    const QVariantList strategies = list();
    qInfo() << "[StrategyBridge] refreshModel incoming rows=" << strategies.size();
    if (m_listModel != nullptr) {
        m_listModel->replaceAll(strategies);
    }

    if (!m_cacheOk) {
        m_cacheOk = true;
        emit cacheOkChanged();
    }
    emit strategiesChanged();
}
