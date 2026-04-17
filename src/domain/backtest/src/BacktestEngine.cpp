#include "BacktestEngine.h"
#include "BacktestRuleTemplateEvaluator.h"
#include "StockDataProvider.h"

#include <foundation.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

enum class TradingSignal {
    Hold,
    Buy,
    Sell
};

struct PositionState {
    double quantity{0.0};
    double entryPrice{0.0};
    foundation::Timestamp entryTime;

    bool hasPosition() const {
        return quantity > 0.0 && entryPrice > 0.0;
    }
};

struct SymbolRuntimeState {
    std::vector<double> closes;
    PositionState position;
};

struct BacktestRuntimeState {
    double cash{0.0};
    std::unordered_map<std::string, SymbolRuntimeState> symbolStates;
    std::unordered_map<std::string, double> latestPrices;
    std::unordered_map<std::string, foundation::Timestamp> latestTimestamps;
};

struct RuleTemplateRuntimeSupport {
    QVariantList compiledTemplates;
    QVariantMap baseFacts;
    QVariantMap strategyScope;

    bool active() const {
        return !compiledTemplates.isEmpty();
    }
};

struct FactorOverlayAllocation {
    std::string factorId;
    double weight{0.0};
};

struct FactorOverlayRuntimeSupport {
    bool enabled{false};
    int targetPositionCount{0};
    double minimumCompositeScore{0.0};
    std::vector<FactorOverlayAllocation> allocations;
    std::map<std::string, std::map<std::string, std::map<std::string, double>>> factorSeriesByFactor;

    bool active() const {
        return enabled && !allocations.empty() && !factorSeriesByFactor.empty();
    }
};

struct PendingBuyCandidate {
    domain::model::Bar bar;
    foundation::Timestamp timestamp;
    domain::backtest::rules::RuntimeRuleTemplateEvaluationResult templateEntryResult;
    double ruleSelectionScore{0.0};
    double factorCompositeScore{0.0};
    double combinedSelectionScore{0.0};
    std::size_t orderIndex{0};
};

QVariantMap primaryCompiledTemplate(const QVariantList& compiledTemplates)
{
    for (const QVariant& compiledTemplateValue : compiledTemplates) {
        const QVariantMap compiledTemplate = compiledTemplateValue.toMap();
        if (!compiledTemplate.isEmpty()) {
            return compiledTemplate;
        }
    }
    return {};
}

QString backtestDateKey(long long epochMs)
{
    return QString::fromStdString(
        foundation::Timestamp::from_milliseconds(epochMs).to_string("%Y-%m-%d"));
}

QVariantMap parseJsonObjectOption(const std::map<std::string, std::string>& options,
                                  const std::string& key)
{
    const auto it = options.find(key);
    if (it == options.end() || it->second.empty()) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(QString::fromStdString(it->second).toUtf8());
    if (document.isNull() || !document.isObject()) {
        return {};
    }
    return document.object().toVariantMap();
}

double firstNumericValue(const QVariantMap& map,
                         std::initializer_list<const char*> keys,
                         double fallback)
{
    for (const char* key : keys) {
        bool ok = false;
        const double value = map.value(QString::fromUtf8(key)).toDouble(&ok);
        if (ok && std::isfinite(value)) {
            return value;
        }
    }
    return fallback;
}

int firstPositiveIntValue(const QVariantMap& map,
                          std::initializer_list<const char*> keys,
                          int fallback)
{
    for (const char* key : keys) {
        bool ok = false;
        const int value = map.value(QString::fromUtf8(key)).toInt(&ok);
        if (ok && value > 0) {
            return value;
        }
    }
    return fallback;
}

double normalizedOverlayWeight(double rawWeight)
{
    if (!std::isfinite(rawWeight) || rawWeight <= 0.0) {
        return 0.0;
    }
    return rawWeight > 1.0 ? rawWeight / 100.0 : rawWeight;
}

struct StrategyProfile {
    std::string subtype;
    double positionSizeRatio{1.0};
    int fastPeriod{10};
    int slowPeriod{30};
    int bollPeriod{20};
    double bollStd{2.0};
    double reversionThreshold{0.5};
    int momentumPeriod{60};
    double spreadThreshold{0.02};
    double entryZScore{2.0};
    double exitZScore{0.5};
    bool autoStopEnabled{true};
    double stopLossRate{0.05};
    double takeProfitRate{0.15};
};

double clampPositive(double value, double fallback) {
    return value > 0.0 ? value : fallback;
}

double getDoubleParam(const std::map<std::string, double>& params,
                     const std::string& primaryKey,
                     const std::string& secondaryKey,
                     double fallback) {
    const auto primary = params.find(primaryKey);
    if (primary != params.end()) {
        return primary->second;
    }
    const auto secondary = params.find(secondaryKey);
    if (secondary != params.end()) {
        return secondary->second;
    }
    return fallback;
}

std::string getStringOption(const std::map<std::string, std::string>& options,
                            const std::string& key) {
    const auto it = options.find(key);
    return it == options.end() ? std::string() : it->second;
}

bool getBoolOption(const std::map<std::string, std::string>& options,
                   const std::string& key,
                   bool fallback) {
    const std::string value = getStringOption(options, key);
    if (value.empty()) {
        return fallback;
    }

    std::string normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "false" || normalized == "0" || normalized == "no" || normalized == "off") {
        return false;
    }
    return fallback;
}

double resultSelectionScore(const domain::backtest::rules::RuntimeRuleTemplateEvaluationResult& result)
{
    bool ok = false;
    const double selectionScore = result.payload.value(QStringLiteral("selectionScore")).toDouble(&ok);
    if (ok && std::isfinite(selectionScore)) {
        return selectionScore;
    }

    const double ruleSelectionScore = result.payload.value(QStringLiteral("ruleSelectionScore")).toDouble(&ok);
    if (ok && std::isfinite(ruleSelectionScore)) {
        return ruleSelectionScore;
    }

    const double bonusScore = result.payload.value(QStringLiteral("score")).toDouble(&ok);
    return ok && std::isfinite(bonusScore) ? bonusScore : 0.0;
}

int openPositionCount(const BacktestRuntimeState& state)
{
    int count = 0;
    for (const auto& entry : state.symbolStates) {
        if (entry.second.position.hasPosition()) {
            ++count;
        }
    }
    return count;
}

FactorOverlayRuntimeSupport buildFactorOverlayRuntimeSupport(
    const std::map<std::string, std::string>& strategyOptions,
    const std::vector<domain::model::Bar>& bars,
    const std::shared_ptr<domain::backtest::FactorDataProvider>& factorDataProvider)
{
    FactorOverlayRuntimeSupport support;
    if (!factorDataProvider || bars.empty()) {
        return support;
    }

    const QVariantMap overlay = parseJsonObjectOption(strategyOptions, "factor_overlay_json");
    if (overlay.isEmpty() || !overlay.value(QStringLiteral("enabled")).toBool()) {
        return support;
    }

    const QVariantList allocations = overlay.value(QStringLiteral("allocations")).toList();
    double totalWeight = 0.0;
    for (const QVariant& allocationValue : allocations) {
        const QVariantMap allocation = allocationValue.toMap();
        const std::string factorId = allocation.value(QStringLiteral("factor_id"), allocation.value(QStringLiteral("factorId"))).toString().trimmed().toStdString();
        const double weight = normalizedOverlayWeight(firstNumericValue(allocation, {"weight_percent", "weightPercent", "weight"}, 0.0));
        if (factorId.empty() || weight <= 0.0) {
            continue;
        }
        support.allocations.push_back({factorId, weight});
        totalWeight += weight;
    }

    if (support.allocations.empty() || totalWeight <= 0.0) {
        return FactorOverlayRuntimeSupport{};
    }

    for (auto& allocation : support.allocations) {
        allocation.weight /= totalWeight;
    }

    support.enabled = true;
    support.targetPositionCount = firstPositiveIntValue(overlay, {"targetPositionCount", "target_position_count"}, 10);
    support.minimumCompositeScore = firstNumericValue(overlay, {"minimumCompositeScore", "minimum_composite_score"}, 0.0);

    const QString startDate = backtestDateKey(bars.front().time);
    const QString endDate = backtestDateKey(bars.back().time);
    for (const auto& allocation : support.allocations) {
        support.factorSeriesByFactor.emplace(
            allocation.factorId,
            factorDataProvider->getFactorValuesRange(allocation.factorId, startDate.toStdString(), endDate.toStdString()));
    }

    return support;
}

std::map<std::string, double> computeFactorCompositeScores(
    const FactorOverlayRuntimeSupport& support,
    const QString& tradeDate,
    const std::vector<PendingBuyCandidate>& candidates)
{
    std::map<std::string, double> scores;
    if (!support.active() || candidates.empty()) {
        return scores;
    }

    for (const auto& allocation : support.allocations) {
        const auto seriesIt = support.factorSeriesByFactor.find(allocation.factorId);
        if (seriesIt == support.factorSeriesByFactor.end()) {
            continue;
        }

        const auto dateIt = seriesIt->second.find(tradeDate.toStdString());
        if (dateIt == seriesIt->second.end() || dateIt->second.empty()) {
            continue;
        }

        std::vector<std::pair<std::string, double>> rawValues;
        rawValues.reserve(candidates.size());
        for (const auto& candidate : candidates) {
            const auto valueIt = dateIt->second.find(candidate.bar.symbol);
            if (valueIt == dateIt->second.end() || !std::isfinite(valueIt->second)) {
                continue;
            }
            rawValues.push_back({candidate.bar.symbol, valueIt->second});
        }

        if (rawValues.empty()) {
            continue;
        }

        double mean = 0.0;
        for (const auto& item : rawValues) {
            mean += item.second;
        }
        mean /= static_cast<double>(rawValues.size());

        double variance = 0.0;
        for (const auto& item : rawValues) {
            const double diff = item.second - mean;
            variance += diff * diff;
        }
        variance = rawValues.size() > 1 ? variance / static_cast<double>(rawValues.size() - 1) : 0.0;
        const double stdDev = std::sqrt((std::max)(variance, 0.0));

        for (const auto& item : rawValues) {
            const double zScore = stdDev > std::numeric_limits<double>::epsilon()
                ? (item.second - mean) / stdDev
                : 0.0;
            scores[item.first] += allocation.weight * zScore;
        }
    }

    return scores;
}

double calculateMean(const std::vector<double>& values, std::size_t begin, std::size_t end) {
    if (begin >= end || end > values.size()) {
        return 0.0;
    }

    double sum = 0.0;
    for (std::size_t index = begin; index < end; ++index) {
        sum += values[index];
    }
    return sum / static_cast<double>(end - begin);
}

double calculateStdDev(const std::vector<double>& values, std::size_t begin, std::size_t end, double mean) {
    if (begin >= end || end - begin < 2 || end > values.size()) {
        return 0.0;
    }

    double variance = 0.0;
    for (std::size_t index = begin; index < end; ++index) {
        const double diff = values[index] - mean;
        variance += diff * diff;
    }
    variance /= static_cast<double>(end - begin - 1);
    return std::sqrt((std::max)(variance, 0.0));
}

StrategyProfile buildStrategyProfile(const std::string& strategyName,
                                     double maxPositionRatio,
                                     const std::map<std::string, double>& strategyParams,
                                     const std::map<std::string, std::string>& strategyOptions) {
    StrategyProfile profile;
    profile.fastPeriod = (std::max)(2, static_cast<int>(std::round(getDoubleParam(strategyParams, "fast_period", "fastPeriod", 10.0))));
    profile.slowPeriod = (std::max)(profile.fastPeriod + 1, static_cast<int>(std::round(getDoubleParam(strategyParams, "slow_period", "slowPeriod", 30.0))));
    profile.bollPeriod = (std::max)(5, static_cast<int>(std::round(getDoubleParam(strategyParams, "boll_period", "bollPeriod", 20.0))));
    profile.bollStd = clampPositive(getDoubleParam(strategyParams, "boll_std", "bollStd", 2.0), 2.0);
    profile.reversionThreshold = clampPositive(getDoubleParam(strategyParams, "reversion_threshold", "reversionThreshold", 0.5), 0.5);
    profile.momentumPeriod = (std::max)(5, static_cast<int>(std::round(getDoubleParam(strategyParams, "momentum_period", "momentumPeriod", 60.0))));
    profile.spreadThreshold = clampPositive(getDoubleParam(strategyParams, "spread_threshold", "spreadThreshold", 0.02), 0.02);
    profile.entryZScore = clampPositive(getDoubleParam(strategyParams, "entry_z_score", "entryZScore", 2.0), 2.0);
    profile.exitZScore = clampPositive(getDoubleParam(strategyParams, "exit_z_score", "exitZScore", 0.5), 0.5);
    profile.autoStopEnabled = getBoolOption(strategyOptions, "autoStopEnabled", true);
    profile.stopLossRate = profile.autoStopEnabled
        ? clampPositive(getDoubleParam(strategyParams, "stop_loss", "stopLossPercent", 0.05), 0.05)
        : 0.0;
    profile.takeProfitRate = clampPositive(getDoubleParam(strategyParams, "take_profit", "takeProfitPercent", 0.15), 0.15);

    const double configuredPosition = getDoubleParam(strategyParams, "position_size", "positionSize", maxPositionRatio);
    profile.positionSizeRatio = configuredPosition > 1.0 ? configuredPosition / 100.0 : configuredPosition;
    if (profile.positionSizeRatio <= 0.0) {
        profile.positionSizeRatio = maxPositionRatio;
    }
    profile.positionSizeRatio = (std::min)((std::max)(profile.positionSizeRatio, 0.01), (std::max)(maxPositionRatio, 0.01));

    profile.subtype = getStringOption(strategyOptions, "strategy_subtype");
    if (profile.subtype.empty()) {
        profile.subtype = getStringOption(strategyOptions, "sub_type");
    }
    if (profile.subtype.empty()) {
        profile.subtype = getStringOption(strategyOptions, "strategy_type");
    }
    if (profile.subtype.empty()) {
        profile.subtype = strategyName;
    }

    std::transform(profile.subtype.begin(), profile.subtype.end(), profile.subtype.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return profile;
}

TradingSignal evaluateSignal(const StrategyProfile& profile,
                             const std::vector<double>& closes,
                             const PositionState& position) {
    if (closes.empty()) {
        return TradingSignal::Hold;
    }

    const double currentPrice = closes.back();
    if (position.hasPosition()) {
        const double pnlRatio = currentPrice / position.entryPrice - 1.0;
        if ((profile.autoStopEnabled && profile.stopLossRate > 0.0 && pnlRatio <= -profile.stopLossRate)
            || pnlRatio >= profile.takeProfitRate) {
            return TradingSignal::Sell;
        }
    }

    const bool isMeanReversion = profile.subtype.find("mean") != std::string::npos || profile.subtype.find("reversion") != std::string::npos;
    const bool isMomentum = profile.subtype.find("alpha") != std::string::npos || profile.subtype.find("momentum") != std::string::npos;
    const bool isArbitrage = profile.subtype.find("arbitrage") != std::string::npos;

    if (isMeanReversion) {
        if (closes.size() < static_cast<std::size_t>(profile.bollPeriod)) {
            return TradingSignal::Hold;
        }

        const std::size_t begin = closes.size() - static_cast<std::size_t>(profile.bollPeriod);
        const double mean = calculateMean(closes, begin, closes.size());
        const double stdDev = calculateStdDev(closes, begin, closes.size(), mean);
        if (stdDev <= std::numeric_limits<double>::epsilon()) {
            return TradingSignal::Hold;
        }

        const double lowerBand = mean - profile.bollStd * stdDev;
        const double exitLevel = mean - profile.reversionThreshold * stdDev;
        if (!position.hasPosition() && currentPrice <= lowerBand) {
            return TradingSignal::Buy;
        }
        if (position.hasPosition() && currentPrice >= exitLevel) {
            return TradingSignal::Sell;
        }
        return TradingSignal::Hold;
    }

    if (isArbitrage) {
        if (closes.size() < static_cast<std::size_t>(profile.bollPeriod)) {
            return TradingSignal::Hold;
        }

        const std::size_t begin = closes.size() - static_cast<std::size_t>(profile.bollPeriod);
        const double mean = calculateMean(closes, begin, closes.size());
        const double stdDev = calculateStdDev(closes, begin, closes.size(), mean);
        if (stdDev <= std::numeric_limits<double>::epsilon()) {
            return TradingSignal::Hold;
        }

        const double zScore = (currentPrice - mean) / stdDev;
        if (!position.hasPosition() && zScore <= -profile.entryZScore) {
            return TradingSignal::Buy;
        }
        if (position.hasPosition() && zScore >= -profile.exitZScore) {
            return TradingSignal::Sell;
        }
        return TradingSignal::Hold;
    }

    if (isMomentum) {
        if (closes.size() <= static_cast<std::size_t>(profile.momentumPeriod)) {
            return TradingSignal::Hold;
        }

        const double basePrice = closes[closes.size() - static_cast<std::size_t>(profile.momentumPeriod) - 1];
        if (basePrice <= 0.0) {
            return TradingSignal::Hold;
        }

        const double momentum = currentPrice / basePrice - 1.0;
        if (!position.hasPosition() && momentum >= profile.spreadThreshold) {
            return TradingSignal::Buy;
        }
        if (position.hasPosition() && momentum <= 0.0) {
            return TradingSignal::Sell;
        }
        return TradingSignal::Hold;
    }

    if (closes.size() < static_cast<std::size_t>(profile.slowPeriod + 1)) {
        return TradingSignal::Hold;
    }

    const std::size_t size = closes.size();
    const double currentFast = calculateMean(closes, size - static_cast<std::size_t>(profile.fastPeriod), size);
    const double currentSlow = calculateMean(closes, size - static_cast<std::size_t>(profile.slowPeriod), size);
    const double previousFast = calculateMean(closes, size - static_cast<std::size_t>(profile.fastPeriod) - 1, size - 1);
    const double previousSlow = calculateMean(closes, size - static_cast<std::size_t>(profile.slowPeriod) - 1, size - 1);

    if (!position.hasPosition() && previousFast <= previousSlow && currentFast > currentSlow) {
        return TradingSignal::Buy;
    }
    if (position.hasPosition() && previousFast >= previousSlow && currentFast < currentSlow) {
        return TradingSignal::Sell;
    }
    return TradingSignal::Hold;
}

double calculatePortfolioEquity(const BacktestRuntimeState& state) {
    double equity = state.cash;
    for (const auto& entry : state.symbolStates) {
        if (!entry.second.position.hasPosition()) {
            continue;
        }

        const auto priceIt = state.latestPrices.find(entry.first);
        if (priceIt == state.latestPrices.end()) {
            continue;
        }
        equity += entry.second.position.quantity * priceIt->second;
    }
    return equity;
}

RuleTemplateRuntimeSupport buildRuleTemplateRuntimeSupport(
    const std::string& strategyName,
    const std::map<std::string, double>& strategyParams,
    const std::map<std::string, std::string>& strategyOptions)
{
    RuleTemplateRuntimeSupport support;

    const QVariantList bindings = domain::backtest::rules::bindingListFromStrategyOptions(strategyOptions);
    if (bindings.isEmpty()) {
        return support;
    }

    QString templateError;
    support.compiledTemplates = domain::backtest::rules::loadCompiledRuleTemplates(bindings, &templateError);
    if (support.compiledTemplates.isEmpty()) {
        throw std::runtime_error(templateError.toStdString());
    }

    support.baseFacts = domain::backtest::rules::flatFactsFromStrategyConfig(strategyOptions, strategyParams);
    support.strategyScope = domain::backtest::rules::strategyScopeFromBacktestConfig(
        strategyName,
        strategyOptions,
        strategyParams);
    return support;
}

QVariantMap buildRuleRuntimeSessionSnapshot(const PositionState& position,
                                           foundation::Timestamp timestamp,
                                           const BacktestRuntimeState& runtimeState)
{
    QVariantMap runtimeSession;
    runtimeSession.insert(QStringLiteral("cash"), runtimeState.cash);
    runtimeSession.insert(QStringLiteral("hasPosition"), position.hasPosition());
    runtimeSession.insert(QStringLiteral("positionQuantity"), position.quantity);
    runtimeSession.insert(QStringLiteral("entryPrice"), position.entryPrice);
    if (position.hasPosition()) {
        const long long elapsedMs = timestamp.to_milliseconds() - position.entryTime.to_milliseconds();
        runtimeSession.insert(QStringLiteral("holdingDays"), static_cast<double>(elapsedMs) / (24.0 * 60.0 * 60.0 * 1000.0));
    }
    return runtimeSession;
}

domain::backtest::rules::RuntimeRuleTemplateEvaluationResult evaluateBacktestRuleTemplate(
    const RuleTemplateRuntimeSupport& support,
    const std::string& symbol,
    double latestPrice,
    double referencePrice,
    const QString& candidateAction,
    const PositionState& position,
    foundation::Timestamp timestamp,
    const BacktestRuntimeState& runtimeState)
{
    domain::backtest::rules::RuntimeRuleTemplateEvaluationResult result;
    if (!support.active()) {
        return result;
    }

    QVariantMap flatFacts = support.baseFacts;
    flatFacts.insert(QStringLiteral("candidate.has_position"), position.hasPosition());
    flatFacts.insert(QStringLiteral("candidate.position_quantity"), position.quantity);
    flatFacts.insert(QStringLiteral("candidate.entry_price"), position.entryPrice);
    flatFacts.insert(QStringLiteral("candidate.price_change_ratio"),
        referencePrice > 0.0 ? latestPrice / referencePrice - 1.0 : 0.0);
    flatFacts.insert(QStringLiteral("candidate.pnl_ratio"),
        position.hasPosition() && position.entryPrice > 0.0 ? latestPrice / position.entryPrice - 1.0 : 0.0);

    domain::backtest::rules::RuntimeRuleTemplateEvaluationContext context;
    context.symbol = QString::fromStdString(symbol);
    context.latestPrice = latestPrice;
    context.referencePrice = referencePrice;
    context.marketEventType = QStringLiteral("backtest_bar");
    context.candidateAction = candidateAction;
    context.candidateStrength = referencePrice > 0.0 ? std::fabs(latestPrice / referencePrice - 1.0) : 0.0;
    context.strategy = support.strategyScope;
    context.flatEventFacts = flatFacts;
    context.runtimeSessionSnapshot = buildRuleRuntimeSessionSnapshot(position, timestamp, runtimeState);
    return domain::backtest::rules::evaluateRuleTemplates(support.compiledTemplates, context);
}

std::string ruleTemplateStringValue(const QVariantMap& templateMap, const QString& key)
{
    return templateMap.value(key).toString().trimmed().toStdString();
}

engine::BacktestResult::RuleTemplateGroupDecision buildRuleTemplateGroupDecision(
    const QVariantMap& decisionMap)
{
    engine::BacktestResult::RuleTemplateGroupDecision decision;
    decision.stage = decisionMap.value(QStringLiteral("stage")).toString().trimmed().toStdString();
    decision.group_id = decisionMap.value(QStringLiteral("groupId")).toString().trimmed().toStdString();
    decision.group_title = decisionMap.value(QStringLiteral("groupTitle")).toString().trimmed().toStdString();
    decision.group_role = decisionMap.value(QStringLiteral("groupRole")).toString().trimmed().toStdString();
    decision.group_operator = decisionMap.value(QStringLiteral("groupOperator")).toString().trimmed().toStdString();
    decision.disposition = decisionMap.value(QStringLiteral("disposition")).toString().trimmed().toStdString();
    decision.outcome = decisionMap.value(QStringLiteral("outcome")).toString().trimmed().toStdString();
    decision.skip_reason = decisionMap.value(QStringLiteral("skipReason")).toString().trimmed().toStdString();
    decision.matched_rule_id = decisionMap.value(QStringLiteral("matchedRuleId")).toString().trimmed().toStdString();
    decision.matched_result_type = decisionMap.value(QStringLiteral("matchedResultType")).toString().trimmed().toStdString();
    decision.matched_reason_code = decisionMap.value(QStringLiteral("matchedReasonCode")).toString().trimmed().toStdString();
    decision.member_count = decisionMap.value(QStringLiteral("memberCount")).toInt();
    decision.applicable_count = decisionMap.value(QStringLiteral("applicableCount")).toInt();
    decision.matched_count = decisionMap.value(QStringLiteral("matchedCount")).toInt();
    decision.filtered_count = decisionMap.value(QStringLiteral("filteredCount")).toInt();
    return decision;
}

std::vector<engine::BacktestResult::RuleTemplateGroupDecision> buildRuleTemplateGroupDecisions(
    const QVariantList& decisions)
{
    std::vector<engine::BacktestResult::RuleTemplateGroupDecision> results;
    results.reserve(static_cast<std::size_t>(decisions.size()));
    for (const QVariant& decisionValue : decisions) {
        const QVariantMap decisionMap = decisionValue.toMap();
        if (decisionMap.isEmpty()) {
            continue;
        }
        results.push_back(buildRuleTemplateGroupDecision(decisionMap));
    }
    return results;
}

void initializeRuleTemplateSummary(
    engine::BacktestResult& result,
    const RuleTemplateRuntimeSupport& support)
{
    if (!support.active()) {
        return;
    }

    const QVariantMap compiledTemplate = primaryCompiledTemplate(support.compiledTemplates);
    if (compiledTemplate.isEmpty()) {
        return;
    }

    result.set_rule_template_binding(
        ruleTemplateStringValue(compiledTemplate, QStringLiteral("_filePath")),
        ruleTemplateStringValue(compiledTemplate, QStringLiteral("namespace")),
        ruleTemplateStringValue(
            compiledTemplate.value(QStringLiteral("_binding")).toMap(),
            QStringLiteral("file_name")),
        ruleTemplateStringValue(
            compiledTemplate.value(QStringLiteral("_binding")).toMap(),
            QStringLiteral("group_id")),
        ruleTemplateStringValue(
            compiledTemplate.value(QStringLiteral("_binding")).toMap(),
            QStringLiteral("group_title")),
        ruleTemplateStringValue(
            compiledTemplate.value(QStringLiteral("_binding")).toMap(),
            QStringLiteral("group_role")),
        ruleTemplateStringValue(
            compiledTemplate.value(QStringLiteral("_binding")).toMap(),
            QStringLiteral("group_operator")));
}

engine::BacktestResult::RuleTemplateEvent buildRuleTemplateEvent(
    const domain::backtest::rules::RuntimeRuleTemplateEvaluationResult& evaluation,
    const std::string& symbol,
    foundation::Timestamp timestamp,
    const char* action,
    const char* eventType)
{
    engine::BacktestResult::RuleTemplateEvent event;
    event.timestamp = timestamp.to_string();
    event.symbol = symbol;
    event.action = action;
    event.event_type = eventType;
    event.rule_id = evaluation.ruleId.toStdString();
    event.reason_code = evaluation.reasonCode.toStdString();
    event.message = evaluation.message.toStdString();
    event.result_type = evaluation.resultType.toStdString();
    event.group_id = evaluation.binding.value(QStringLiteral("group_id")).toString().trimmed().toStdString();
    event.group_title = evaluation.binding.value(QStringLiteral("group_title")).toString().trimmed().toStdString();
    event.group_role = evaluation.binding.value(QStringLiteral("group_role")).toString().trimmed().toStdString();
    event.group_operator = evaluation.binding.value(QStringLiteral("group_operator")).toString().trimmed().toStdString();
    return event;
}

std::string ruleTemplateExitNote(const domain::backtest::rules::RuntimeRuleTemplateEvaluationResult& evaluation)
{
    if (!evaluation.reasonCode.trimmed().isEmpty()) {
        return std::string("rule template exit: ") + evaluation.reasonCode.toStdString();
    }
    if (!evaluation.message.trimmed().isEmpty()) {
        return std::string("rule template exit: ") + evaluation.message.toStdString();
    }
    return "rule template exit";
}

void closePosition(engine::BacktestResult& result,
                   const std::string& symbol,
                   double price,
                   foundation::Timestamp timestamp,
                   double commissionRate,
                   double slippageRate,
                   SymbolRuntimeState& symbolState,
                   BacktestRuntimeState& runtimeState,
                   const std::string& note) {
    PositionState& position = symbolState.position;
    if (!position.hasPosition() || price <= 0.0) {
        return;
    }

    const double exitPrice = slippageRate > 0.0 ? price * (1.0 - slippageRate) : price;
    const double entryCommission = commissionRate > 0.0 ? position.entryPrice * position.quantity * commissionRate : 0.0;
    const double exitCommission = commissionRate > 0.0 ? exitPrice * position.quantity * commissionRate : 0.0;
    const double totalCommission = entryCommission + exitCommission;
    const double grossProfit = (exitPrice - position.entryPrice) * position.quantity;
    const double profit = grossProfit - totalCommission;

    engine::BacktestResult::TradeRecord record{};
    record.trade_id = foundation::Uuid{};
    record.entry_time = position.entryTime;
    record.exit_time = timestamp;
    record.symbol = symbol;
    record.direction = "SELL";
    record.entry_price = position.entryPrice;
    record.exit_price = exitPrice;
    record.quantity = position.quantity;
    record.commission = totalCommission;
    record.profit = profit;
    record.profit_pct = position.entryPrice > 0.0 ? profit / (position.entryPrice * position.quantity) : 0.0;
    record.notes = note;
    result.add_trade_record(record);

    runtimeState.cash += position.quantity * exitPrice - exitCommission;
    position = PositionState{};
}

void processBarImmediate(engine::BacktestResult& result,
                         const domain::model::Bar& bar,
                         const StrategyProfile& profile,
                         const RuleTemplateRuntimeSupport& ruleTemplateSupport,
                         double max_position_ratio,
                         double commission_rate,
                         double slippage_rate,
                         double min_volume,
                         BacktestRuntimeState& state) {
    if (bar.close <= 0.0) {
        return;
    }

    foundation::Timestamp timestamp = foundation::Timestamp::from_seconds(bar.time / 1000);
    SymbolRuntimeState& symbolState = state.symbolStates[bar.symbol];
    symbolState.closes.push_back(bar.close);
    state.latestPrices[bar.symbol] = bar.close;
    state.latestTimestamps[bar.symbol] = timestamp;

    if (min_volume > 0.0 && bar.volume > 0.0 && bar.volume < min_volume) {
        result.update_equity_curve(timestamp, calculatePortfolioEquity(state));
        return;
    }

    const TradingSignal signal = evaluateSignal(profile, symbolState.closes, symbolState.position);
    if (symbolState.position.hasPosition()) {
        const double referencePrice = symbolState.position.entryPrice > 0.0
            ? symbolState.position.entryPrice
            : bar.close;
        const auto templateExitResult = evaluateBacktestRuleTemplate(
            ruleTemplateSupport,
            bar.symbol,
            bar.close,
            referencePrice,
            QStringLiteral("sell"),
            symbolState.position,
            timestamp,
            state);
        result.set_rule_template_group_decisions(
            buildRuleTemplateGroupDecisions(templateExitResult.groupDecisions));
        if (domain::backtest::rules::shouldForceExit(templateExitResult)) {
            result.record_rule_template_event(buildRuleTemplateEvent(
                templateExitResult,
                bar.symbol,
                timestamp,
                "sell",
                "forced_exit"));
            closePosition(result,
                          bar.symbol,
                          bar.close,
                          timestamp,
                          commission_rate,
                          slippage_rate,
                          symbolState,
                          state,
                          ruleTemplateExitNote(templateExitResult));
            result.update_equity_curve(timestamp, calculatePortfolioEquity(state));
            return;
        }
    }

    if (signal == TradingSignal::Buy && !symbolState.position.hasPosition()) {
        const double referencePrice = symbolState.closes.size() >= 2 ? symbolState.closes[symbolState.closes.size() - 2] : bar.close;
        const auto templateEntryResult = evaluateBacktestRuleTemplate(
            ruleTemplateSupport,
            bar.symbol,
            bar.close,
            referencePrice,
            QStringLiteral("buy"),
            symbolState.position,
            timestamp,
            state);
        result.set_rule_template_group_decisions(
            buildRuleTemplateGroupDecisions(templateEntryResult.groupDecisions));
        if (domain::backtest::rules::shouldBlockEntry(templateEntryResult)) {
            result.record_rule_template_event(buildRuleTemplateEvent(
                templateEntryResult,
                bar.symbol,
                timestamp,
                "buy",
                "entry_block"));
            result.update_equity_curve(timestamp, calculatePortfolioEquity(state));
            return;
        }

        const double tradePrice = slippage_rate > 0.0 ? bar.close * (1.0 + slippage_rate) : bar.close;
        const double currentEquity = calculatePortfolioEquity(state);
        const double allocationRatio = (std::min)((std::max)(profile.positionSizeRatio, 0.01), (std::max)(max_position_ratio, 0.01));
        const double targetCash = currentEquity * allocationRatio;
        const double investCash = (std::min)(state.cash, targetCash);
        const double rawQuantity = tradePrice > 0.0 ? investCash / tradePrice : 0.0;
        const double quantity = std::floor(rawQuantity / 100.0) * 100.0;

        if (quantity > 0.0) {
            const double entryCommission = commission_rate > 0.0 ? tradePrice * quantity * commission_rate : 0.0;
            const double totalCost = tradePrice * quantity + entryCommission;
            if (totalCost <= state.cash) {
                state.cash -= totalCost;
                symbolState.position.quantity = quantity;
                symbolState.position.entryPrice = tradePrice;
                symbolState.position.entryTime = timestamp;
            }
        }
    } else if (signal == TradingSignal::Sell && symbolState.position.hasPosition()) {
        closePosition(result,
                      bar.symbol,
                      bar.close,
                      timestamp,
                      commission_rate,
                      slippage_rate,
                      symbolState,
                      state,
                      "signal exit");
    }

    result.update_equity_curve(timestamp, calculatePortfolioEquity(state));
}

void processBarWithFactorOverlay(engine::BacktestResult& result,
                                 const domain::model::Bar& bar,
                                 const StrategyProfile& profile,
                                 const RuleTemplateRuntimeSupport& ruleTemplateSupport,
                                 double commission_rate,
                                 double slippage_rate,
                                 double min_volume,
                                 BacktestRuntimeState& state,
                                 std::vector<PendingBuyCandidate>& pendingCandidates) {
    if (bar.close <= 0.0) {
        return;
    }

    foundation::Timestamp timestamp = foundation::Timestamp::from_seconds(bar.time / 1000);
    SymbolRuntimeState& symbolState = state.symbolStates[bar.symbol];
    symbolState.closes.push_back(bar.close);
    state.latestPrices[bar.symbol] = bar.close;
    state.latestTimestamps[bar.symbol] = timestamp;

    if (min_volume > 0.0 && bar.volume > 0.0 && bar.volume < min_volume) {
        result.update_equity_curve(timestamp, calculatePortfolioEquity(state));
        return;
    }

    const TradingSignal signal = evaluateSignal(profile, symbolState.closes, symbolState.position);
    if (symbolState.position.hasPosition()) {
        const double referencePrice = symbolState.position.entryPrice > 0.0
            ? symbolState.position.entryPrice
            : bar.close;
        const auto templateExitResult = evaluateBacktestRuleTemplate(
            ruleTemplateSupport,
            bar.symbol,
            bar.close,
            referencePrice,
            QStringLiteral("sell"),
            symbolState.position,
            timestamp,
            state);
        result.set_rule_template_group_decisions(
            buildRuleTemplateGroupDecisions(templateExitResult.groupDecisions));
        if (domain::backtest::rules::shouldForceExit(templateExitResult)) {
            result.record_rule_template_event(buildRuleTemplateEvent(
                templateExitResult,
                bar.symbol,
                timestamp,
                "sell",
                "forced_exit"));
            closePosition(result,
                          bar.symbol,
                          bar.close,
                          timestamp,
                          commission_rate,
                          slippage_rate,
                          symbolState,
                          state,
                          ruleTemplateExitNote(templateExitResult));
            result.update_equity_curve(timestamp, calculatePortfolioEquity(state));
            return;
        }
    }

    if (signal == TradingSignal::Buy && !symbolState.position.hasPosition()) {
        const double referencePrice = symbolState.closes.size() >= 2 ? symbolState.closes[symbolState.closes.size() - 2] : bar.close;
        const auto templateEntryResult = evaluateBacktestRuleTemplate(
            ruleTemplateSupport,
            bar.symbol,
            bar.close,
            referencePrice,
            QStringLiteral("buy"),
            symbolState.position,
            timestamp,
            state);
        result.set_rule_template_group_decisions(
            buildRuleTemplateGroupDecisions(templateEntryResult.groupDecisions));
        if (domain::backtest::rules::shouldBlockEntry(templateEntryResult)) {
            result.record_rule_template_event(buildRuleTemplateEvent(
                templateEntryResult,
                bar.symbol,
                timestamp,
                "buy",
                "entry_block"));
            result.update_equity_curve(timestamp, calculatePortfolioEquity(state));
            return;
        }

        PendingBuyCandidate candidate;
        candidate.bar = bar;
        candidate.timestamp = timestamp;
        candidate.templateEntryResult = templateEntryResult;
        candidate.ruleSelectionScore = resultSelectionScore(templateEntryResult);
        candidate.combinedSelectionScore = candidate.ruleSelectionScore;
        candidate.orderIndex = pendingCandidates.size();
        pendingCandidates.push_back(std::move(candidate));
    } else if (signal == TradingSignal::Sell && symbolState.position.hasPosition()) {
        closePosition(result,
                      bar.symbol,
                      bar.close,
                      timestamp,
                      commission_rate,
                      slippage_rate,
                      symbolState,
                      state,
                      "signal exit");
    }

    result.update_equity_curve(timestamp, calculatePortfolioEquity(state));
}

void executePendingBuys(engine::BacktestResult& result,
                        const StrategyProfile& profile,
                        double max_position_ratio,
                        double commission_rate,
                        double slippage_rate,
                        BacktestRuntimeState& state,
                        const FactorOverlayRuntimeSupport& factorOverlaySupport,
                        std::vector<PendingBuyCandidate>& pendingCandidates) {
    if (pendingCandidates.empty()) {
        return;
    }

    const QString tradeDate = backtestDateKey(pendingCandidates.front().bar.time);
    const std::map<std::string, double> factorScores = computeFactorCompositeScores(
        factorOverlaySupport,
        tradeDate,
        pendingCandidates);

    for (auto& candidate : pendingCandidates) {
        const auto factorIt = factorScores.find(candidate.bar.symbol);
        candidate.factorCompositeScore = factorIt == factorScores.end() ? 0.0 : factorIt->second;
        candidate.combinedSelectionScore = candidate.ruleSelectionScore + candidate.factorCompositeScore;
        candidate.templateEntryResult.payload.insert(QStringLiteral("factorOverlayScore"), candidate.factorCompositeScore);
        candidate.templateEntryResult.payload.insert(QStringLiteral("selectionScore"), candidate.combinedSelectionScore);
    }

    std::stable_sort(pendingCandidates.begin(), pendingCandidates.end(), [](const PendingBuyCandidate& left, const PendingBuyCandidate& right) {
        if (left.combinedSelectionScore == right.combinedSelectionScore) {
            return left.orderIndex < right.orderIndex;
        }
        return left.combinedSelectionScore > right.combinedSelectionScore;
    });

    int availableSlots = static_cast<int>(pendingCandidates.size());
    if (factorOverlaySupport.active() && factorOverlaySupport.targetPositionCount > 0) {
        availableSlots = (std::max)(0, factorOverlaySupport.targetPositionCount - openPositionCount(state));
    }

    for (const auto& candidate : pendingCandidates) {
        if (availableSlots <= 0) {
            break;
        }

        if (factorOverlaySupport.active() && candidate.factorCompositeScore < factorOverlaySupport.minimumCompositeScore) {
            continue;
        }

        SymbolRuntimeState& symbolState = state.symbolStates[candidate.bar.symbol];
        if (symbolState.position.hasPosition()) {
            continue;
        }

        const double tradePrice = slippage_rate > 0.0 ? candidate.bar.close * (1.0 + slippage_rate) : candidate.bar.close;
        const double currentEquity = calculatePortfolioEquity(state);
        const double allocationRatio = (std::min)((std::max)(profile.positionSizeRatio, 0.01), (std::max)(max_position_ratio, 0.01));
        const double targetCash = currentEquity * allocationRatio;
        const double investCash = (std::min)(state.cash, targetCash);
        const double rawQuantity = tradePrice > 0.0 ? investCash / tradePrice : 0.0;
        const double quantity = std::floor(rawQuantity / 100.0) * 100.0;

        if (quantity <= 0.0) {
            continue;
        }

        const double entryCommission = commission_rate > 0.0 ? tradePrice * quantity * commission_rate : 0.0;
        const double totalCost = tradePrice * quantity + entryCommission;
        if (totalCost > state.cash) {
            continue;
        }

        state.cash -= totalCost;
        symbolState.position.quantity = quantity;
        symbolState.position.entryPrice = tradePrice;
        symbolState.position.entryTime = candidate.timestamp;
        --availableSlots;
    }

    result.update_equity_curve(pendingCandidates.front().timestamp, calculatePortfolioEquity(state));
    pendingCandidates.clear();
}

void finalizeOpenPositions(engine::BacktestResult& result,
                           double commissionRate,
                           double slippageRate,
                           BacktestRuntimeState& state) {
    for (auto& entry : state.symbolStates) {
        const auto priceIt = state.latestPrices.find(entry.first);
        const auto timeIt = state.latestTimestamps.find(entry.first);
        if (priceIt == state.latestPrices.end() || timeIt == state.latestTimestamps.end()) {
            continue;
        }

        closePosition(result,
                      entry.first,
                      priceIt->second,
                      timeIt->second,
                      commissionRate,
                      slippageRate,
                      entry.second,
                      state,
                      "final close");
    }

    if (!state.latestTimestamps.empty()) {
        auto latest = state.latestTimestamps.begin()->second;
        for (const auto& item : state.latestTimestamps) {
            if (item.second.to_milliseconds() > latest.to_milliseconds()) {
                latest = item.second;
            }
        }
        result.update_equity_curve(latest, calculatePortfolioEquity(state));
    }
}

} // namespace

namespace engine {

void BacktestEngine::setFactorProvider(std::shared_ptr<domain::backtest::FactorDataProvider> provider)
{
    factorDataProvider_ = std::move(provider);
}

BacktestResult BacktestEngine::run(
    const std::vector<domain::model::Bar>& bars,
    double initial_capital,
    const std::string& strategy_name,
    double max_position_ratio,
    double commission_rate,
    double slippage_rate,
    double min_volume)
{
    return run(bars,
               initial_capital,
               strategy_name,
               max_position_ratio,
               commission_rate,
               slippage_rate,
               min_volume,
               {},
               {});
}

BacktestResult BacktestEngine::run(
    const std::vector<domain::model::Bar>& bars,
    double initial_capital,
    const std::string& strategy_name,
    double max_position_ratio,
    double commission_rate,
    double slippage_rate,
    double min_volume,
    const std::map<std::string, double>& strategy_params,
    const std::map<std::string, std::string>& strategy_options)
{
    BacktestResult result;
    if (bars.empty()) {
        return result;
    }

    BacktestRuntimeState state;
    state.cash = initial_capital;
    const StrategyProfile profile = buildStrategyProfile(strategy_name, max_position_ratio, strategy_params, strategy_options);
    const RuleTemplateRuntimeSupport ruleTemplateSupport = buildRuleTemplateRuntimeSupport(
        strategy_name,
        strategy_params,
        strategy_options);
    const FactorOverlayRuntimeSupport factorOverlaySupport = buildFactorOverlayRuntimeSupport(
        strategy_options,
        bars,
        factorDataProvider_);
    initializeRuleTemplateSummary(result, ruleTemplateSupport);

    if (!factorOverlaySupport.active()) {
        for (std::size_t i = 0; i < bars.size(); ++i) {
            processBarImmediate(result,
                               bars[i],
                               profile,
                               ruleTemplateSupport,
                               max_position_ratio,
                               commission_rate,
                               slippage_rate,
                               min_volume,
                               state);
        }
    } else {
        std::vector<PendingBuyCandidate> pendingCandidates;
        long long currentBatchTime = bars.front().time;
        for (std::size_t i = 0; i < bars.size(); ++i) {
            if (bars[i].time != currentBatchTime) {
                executePendingBuys(result,
                                   profile,
                                   max_position_ratio,
                                   commission_rate,
                                   slippage_rate,
                                   state,
                                   factorOverlaySupport,
                                   pendingCandidates);
                currentBatchTime = bars[i].time;
            }

            processBarWithFactorOverlay(result,
                                        bars[i],
                                        profile,
                                        ruleTemplateSupport,
                                        commission_rate,
                                        slippage_rate,
                                        min_volume,
                                        state,
                                        pendingCandidates);
        }
        executePendingBuys(result,
                           profile,
                           max_position_ratio,
                           commission_rate,
                           slippage_rate,
                           state,
                           factorOverlaySupport,
                           pendingCandidates);
    }

    finalizeOpenPositions(result, commission_rate, slippage_rate, state);
    result.calculate_all_metrics();
    return result;
}

BacktestResult BacktestEngine::run(
    const std::vector<std::vector<domain::model::Bar>>& barSeries,
    double initial_capital,
    const std::string& strategy_name,
    double max_position_ratio,
    double commission_rate,
    double slippage_rate,
    double min_volume)
{
    return run(barSeries,
               initial_capital,
               strategy_name,
               max_position_ratio,
               commission_rate,
               slippage_rate,
               min_volume,
               {},
               {});
}

BacktestResult BacktestEngine::run(
    const std::vector<std::vector<domain::model::Bar>>& barSeries,
    double initial_capital,
    const std::string& strategy_name,
    double max_position_ratio,
    double commission_rate,
    double slippage_rate,
    double min_volume,
    const std::map<std::string, double>& strategy_params,
    const std::map<std::string, std::string>& strategy_options)
{
    BacktestResult result;

    struct Cursor {
        std::size_t seriesIndex;
        std::size_t barIndex;
    };

    struct CursorCompare {
        const std::vector<std::vector<domain::model::Bar>>* series;

        bool operator()(const Cursor& left, const Cursor& right) const {
            const auto& leftBar = (*series)[left.seriesIndex][left.barIndex];
            const auto& rightBar = (*series)[right.seriesIndex][right.barIndex];
            if (leftBar.time == rightBar.time) {
                return leftBar.symbol > rightBar.symbol;
            }
            return leftBar.time > rightBar.time;
        }
    };

    std::priority_queue<Cursor, std::vector<Cursor>, CursorCompare> heap{CursorCompare{&barSeries}};
    for (std::size_t seriesIndex = 0; seriesIndex < barSeries.size(); ++seriesIndex) {
        if (!barSeries[seriesIndex].empty()) {
            heap.push(Cursor{seriesIndex, 0});
        }
    }

    if (heap.empty()) {
        return result;
    }

    BacktestRuntimeState state;
    state.cash = initial_capital;
    const StrategyProfile profile = buildStrategyProfile(strategy_name, max_position_ratio, strategy_params, strategy_options);
    const RuleTemplateRuntimeSupport ruleTemplateSupport = buildRuleTemplateRuntimeSupport(
        strategy_name,
        strategy_params,
        strategy_options);
    std::vector<domain::model::Bar> flattenedBars;
    for (const auto& series : barSeries) {
        flattenedBars.insert(flattenedBars.end(), series.begin(), series.end());
    }
    std::sort(flattenedBars.begin(), flattenedBars.end(), [](const auto& left, const auto& right) {
        if (left.time == right.time) {
            return left.symbol < right.symbol;
        }
        return left.time < right.time;
    });
    const FactorOverlayRuntimeSupport factorOverlaySupport = buildFactorOverlayRuntimeSupport(
        strategy_options,
        flattenedBars,
        factorDataProvider_);
    initializeRuleTemplateSummary(result, ruleTemplateSupport);

    while (!heap.empty()) {
        if (!factorOverlaySupport.active()) {
            Cursor current = heap.top();
            heap.pop();

            const auto& bar = barSeries[current.seriesIndex][current.barIndex];
            processBarImmediate(result,
                               bar,
                               profile,
                               ruleTemplateSupport,
                               max_position_ratio,
                               commission_rate,
                               slippage_rate,
                               min_volume,
                               state);

            const std::size_t nextBarIndex = current.barIndex + 1;
            if (nextBarIndex < barSeries[current.seriesIndex].size()) {
                heap.push(Cursor{current.seriesIndex, nextBarIndex});
            }
            continue;
        }

        std::vector<PendingBuyCandidate> pendingCandidates;
        const long long batchTime = barSeries[heap.top().seriesIndex][heap.top().barIndex].time;
        while (!heap.empty()) {
            Cursor current = heap.top();
            const auto& bar = barSeries[current.seriesIndex][current.barIndex];
            if (bar.time != batchTime) {
                break;
            }
            heap.pop();

            processBarWithFactorOverlay(result,
                                        bar,
                                        profile,
                                        ruleTemplateSupport,
                                        commission_rate,
                                        slippage_rate,
                                        min_volume,
                                        state,
                                        pendingCandidates);

            const std::size_t nextBarIndex = current.barIndex + 1;
            if (nextBarIndex < barSeries[current.seriesIndex].size()) {
                heap.push(Cursor{current.seriesIndex, nextBarIndex});
            }
        }

        executePendingBuys(result,
                           profile,
                           max_position_ratio,
                           commission_rate,
                           slippage_rate,
                           state,
                           factorOverlaySupport,
                           pendingCandidates);
    }

    finalizeOpenPositions(result, commission_rate, slippage_rate, state);
    result.calculate_all_metrics();
    return result;
}

} // namespace engine
