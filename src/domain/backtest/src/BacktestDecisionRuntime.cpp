#include "BacktestDecisionRuntime.h"
#include "StockDataProvider.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

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

double clampPositive(double value, double fallback)
{
    return value > 0.0 ? value : fallback;
}

double getDoubleParam(const std::map<std::string, double>& params,
                      const std::string& primaryKey,
                      const std::string& secondaryKey,
                      double fallback)
{
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
                            const std::string& key)
{
    const auto it = options.find(key);
    return it == options.end() ? std::string() : it->second;
}

bool getBoolOption(const std::map<std::string, std::string>& options,
                   const std::string& key,
                   bool fallback)
{
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

double calculateMean(const std::vector<double>& values, std::size_t begin, std::size_t end)
{
    if (begin >= end || end > values.size()) {
        return 0.0;
    }

    double sum = 0.0;
    for (std::size_t index = begin; index < end; ++index) {
        sum += values[index];
    }
    return sum / static_cast<double>(end - begin);
}

double calculateStdDev(const std::vector<double>& values, std::size_t begin, std::size_t end, double mean)
{
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

std::map<std::string, double> computeFactorCompositeScores(
    const domain::backtest::runtime::FactorOverlayRuntimeSupport& support,
    const QString& tradeDate,
    const std::vector<domain::backtest::runtime::PendingBuyCandidate>& candidates)
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

} // namespace

namespace domain::backtest::runtime {

QString backtestDateKey(long long epochMs)
{
    return QString::fromStdString(
        foundation::Timestamp::from_milliseconds(epochMs).to_string("%Y-%m-%d"));
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

StrategyProfile buildStrategyProfile(
    const std::string& strategyName,
    double maxPositionRatio,
    const std::map<std::string, double>& strategyParams,
    const std::map<std::string, std::string>& strategyOptions)
{
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

TradingSignal evaluateSignal(
    const StrategyProfile& profile,
    const std::vector<double>& closes,
    const PositionState& position)
{
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

double calculatePortfolioEquity(const BacktestRuntimeState& state)
{
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

void applyFactorOverlayScores(
    const FactorOverlayRuntimeSupport& support,
    std::vector<PendingBuyCandidate>& candidates)
{
    if (candidates.empty()) {
        return;
    }

    const QString tradeDate = backtestDateKey(candidates.front().bar.time);
    const std::map<std::string, double> factorScores = computeFactorCompositeScores(
        support,
        tradeDate,
        candidates);

    for (auto& candidate : candidates) {
        const auto factorIt = factorScores.find(candidate.bar.symbol);
        candidate.factorCompositeScore = factorIt == factorScores.end() ? 0.0 : factorIt->second;
        candidate.combinedSelectionScore = candidate.ruleSelectionScore + candidate.factorCompositeScore;
        candidate.templateEntryResult.payload.insert(QStringLiteral("factorOverlayScore"), candidate.factorCompositeScore);
        candidate.templateEntryResult.payload.insert(QStringLiteral("selectionScore"), candidate.combinedSelectionScore);
    }

    std::stable_sort(candidates.begin(), candidates.end(), [](const PendingBuyCandidate& left, const PendingBuyCandidate& right) {
        if (left.combinedSelectionScore == right.combinedSelectionScore) {
            return left.orderIndex < right.orderIndex;
        }
        return left.combinedSelectionScore > right.combinedSelectionScore;
    });
}

int resolveAvailablePendingBuySlots(
    const FactorOverlayRuntimeSupport& support,
    const BacktestRuntimeState& state,
    int pendingCandidateCount)
{
    if (!support.active() || support.targetPositionCount <= 0) {
        return pendingCandidateCount;
    }
    return (std::max)(0, support.targetPositionCount - openPositionCount(state));
}

} // namespace domain::backtest::runtime