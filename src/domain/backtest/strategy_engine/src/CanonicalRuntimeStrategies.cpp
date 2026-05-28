#include "CanonicalRuntimeStrategies.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace domain::backtest::strategy_engine {

namespace {

constexpr std::uint64_t kLotSize = 100U;

struct RankedCandidate final {
    SymbolId symbolId;
    ScoreValue score;
    ScoreValue tieBreaker;
};

struct MarketBarView final {
    SymbolId symbolId;
    PriceValue openPrice;
    PriceValue closePrice;
};

class OverlayFactorSelectionScope final {
public:
    explicit OverlayFactorSelectionScope(const FactorOverlayConfig* overlayConfig)
    {
        if (overlayConfig == nullptr || !overlayConfig->enabled) {
            return;
        }

        for (const FactorId factorId : overlayConfig->factorIds) {
            requestedFactorIds_.emplace(factorId.value());
        }
    }

    [[nodiscard]] bool accepts(const FactorId factorId) const
    {
        return requestedFactorIds_.empty()
            || requestedFactorIds_.find(factorId.value()) != requestedFactorIds_.end();
    }

private:
    std::unordered_set<std::uint64_t> requestedFactorIds_;
};

class BehaviorScoreContext final {
public:
    BehaviorScoreContext(const MarketDataSlice& marketData,
                         const OverlayFactorSelectionScope& overlayFactorScope)
    {
        double minClosePrice = std::numeric_limits<double>::max();

        for (const MarketBar& marketBar : marketData.bars) {
            maxClosePrice_ = std::max(maxClosePrice_, marketBar.closePrice.value());
            minClosePrice = std::min(minClosePrice, marketBar.closePrice.value());
            maxVolume_ = std::max(maxVolume_, static_cast<double>(marketBar.volume.value()));
            maxMomentumRatio_ = std::max(maxMomentumRatio_, closeToOpenRatio(marketBar));
            maxSpreadRatio_ = std::max(maxSpreadRatio_, intradaySpreadRatio(marketBar));
        }

        for (const FactorSnapshot& factorSnapshot : marketData.factorSnapshots) {
            if (!overlayFactorScope.accepts(factorSnapshot.factorId)) {
                continue;
            }

            factorValues_.emplace(buildFactorValueKey(factorSnapshot.symbolId, factorSnapshot.factorId),
                                  factorSnapshot.value.value());

            FactorRangeState& rangeState = factorRanges_[factorSnapshot.factorId.value()];
            rangeState.observe(factorSnapshot.value.value());
        }

        if (minClosePrice <= std::numeric_limits<double>::max()) {
            minClosePrice_ = minClosePrice;
        }
    }

    [[nodiscard]] double normalizedClose(const MarketBar& marketBar) const
    {
        if (!(maxClosePrice_ > 0.0)) {
            return 0.0;
        }

        return clampUnit(marketBar.closePrice.value() / maxClosePrice_);
    }

    [[nodiscard]] double meanReversionPotential(const MarketBar& marketBar) const
    {
        const double closeRange = maxClosePrice_ - minClosePrice_;
        if (!(closeRange > 0.0)) {
            return 0.0;
        }

        return clampUnit((maxClosePrice_ - marketBar.closePrice.value()) / closeRange);
    }

    [[nodiscard]] double momentumStrength(const MarketBar& marketBar) const
    {
        if (!(maxMomentumRatio_ > 0.0)) {
            return 0.0;
        }

        return clampUnit(closeToOpenRatio(marketBar) / maxMomentumRatio_);
    }

    [[nodiscard]] double normalizedVolume(const MarketBar& marketBar) const
    {
        if (!(maxVolume_ > 0.0)) {
            return 0.0;
        }

        return clampUnit(static_cast<double>(marketBar.volume.value()) / maxVolume_);
    }

    [[nodiscard]] double spreadTightness(const MarketBar& marketBar) const
    {
        if (!(maxSpreadRatio_ > 0.0)) {
            return 1.0;
        }

        return clampUnit(1.0 - (intradaySpreadRatio(marketBar) / maxSpreadRatio_));
    }

    [[nodiscard]] double requireOverlayCompositeScore(const SymbolId symbolId,
                                                      const FactorOverlayConfig& overlayConfig) const
    {
        if (!overlayConfig.enabled) {
            throw std::invalid_argument("overlay composite requested for disabled overlay");
        }

        double compositeScore = 0.0;
        double totalWeight = 0.0;

        for (const FactorWeight& factorWeight : overlayConfig.weights) {
            const double normalizedFactorValue = requireNormalizedFactorValue(symbolId, factorWeight.factorId);
            compositeScore += normalizedFactorValue * factorWeight.weight.value();
            totalWeight += factorWeight.weight.value();
        }

        if (!(totalWeight > 0.0)) {
            throw std::invalid_argument("overlay weights must contain positive total weight");
        }

        return clampUnit(compositeScore / totalWeight);
    }

private:
    struct FactorRangeState final {
        double minValue{std::numeric_limits<double>::max()};
        double maxValue{std::numeric_limits<double>::lowest()};
        bool initialized{false};

        void observe(const double value)
        {
            minValue = initialized ? std::min(minValue, value) : value;
            maxValue = initialized ? std::max(maxValue, value) : value;
            initialized = true;
        }
    };

    [[nodiscard]] static double clampUnit(const double value)
    {
        if (!std::isfinite(value)) {
            return 0.0;
        }

        return std::clamp(value, 0.0, 1.0);
    }

    [[nodiscard]] static double closeToOpenRatio(const MarketBar& marketBar)
    {
        const double openPrice = marketBar.openPrice.value();
        if (!(openPrice > 0.0)) {
            return 0.0;
        }

        return clampUnit(marketBar.closePrice.value() / openPrice);
    }

    [[nodiscard]] static double intradaySpreadRatio(const MarketBar& marketBar)
    {
        const double closePrice = marketBar.closePrice.value();
        if (!(closePrice > 0.0)) {
            return 0.0;
        }

        return clampUnit((marketBar.highPrice.value() - marketBar.lowPrice.value()) / closePrice);
    }

    [[nodiscard]] static std::uint64_t buildFactorValueKey(const SymbolId symbolId, const FactorId factorId)
    {
        return (static_cast<std::uint64_t>(symbolId.value()) << 32U)
            | static_cast<std::uint64_t>(factorId.value());
    }

    [[nodiscard]] double requireNormalizedFactorValue(const SymbolId symbolId, const FactorId factorId) const
    {
        const auto valueIterator = factorValues_.find(buildFactorValueKey(symbolId, factorId));
        if (valueIterator == factorValues_.end()) {
            throw std::invalid_argument("missing overlay factor snapshot");
        }

        const auto rangeIterator = factorRanges_.find(factorId.value());
        if (rangeIterator == factorRanges_.end() || !rangeIterator->second.initialized) {
            throw std::invalid_argument("missing overlay factor range");
        }

        const FactorRangeState& rangeState = rangeIterator->second;
        const double range = rangeState.maxValue - rangeState.minValue;
        if (!(range > 0.0)) {
            return 1.0;
        }

        return clampUnit((valueIterator->second - rangeState.minValue) / range);
    }

    double maxClosePrice_{0.0};
    double minClosePrice_{0.0};
    double maxVolume_{0.0};
    double maxMomentumRatio_{0.0};
    double maxSpreadRatio_{0.0};
    std::unordered_map<std::uint64_t, double> factorValues_;
    std::unordered_map<std::uint64_t, FactorRangeState> factorRanges_;
};

class IBehaviorScoreStrategy {
public:
    virtual ~IBehaviorScoreStrategy() = default;

    [[nodiscard]] virtual ScoreValue score(const MarketBar& marketBar,
                                           const BehaviorScoreContext& scoreContext) const = 0;
};

class TrendFollowingBehaviorScoreStrategy final : public IBehaviorScoreStrategy {
public:
    [[nodiscard]] ScoreValue score(const MarketBar& marketBar,
                                   const BehaviorScoreContext& scoreContext) const override
    {
        return ScoreValue(scoreContext.normalizedClose(marketBar));
    }
};

class MeanReversionBehaviorScoreStrategy final : public IBehaviorScoreStrategy {
public:
    [[nodiscard]] ScoreValue score(const MarketBar& marketBar,
                                   const BehaviorScoreContext& scoreContext) const override
    {
        return ScoreValue(scoreContext.meanReversionPotential(marketBar));
    }
};

class MomentumBehaviorScoreStrategy final : public IBehaviorScoreStrategy {
public:
    [[nodiscard]] ScoreValue score(const MarketBar& marketBar,
                                   const BehaviorScoreContext& scoreContext) const override
    {
        return ScoreValue((scoreContext.momentumStrength(marketBar) * 0.75)
                          + (scoreContext.normalizedClose(marketBar) * 0.25));
    }
};

class ArbitrageBehaviorScoreStrategy final : public IBehaviorScoreStrategy {
public:
    [[nodiscard]] ScoreValue score(const MarketBar& marketBar,
                                   const BehaviorScoreContext& scoreContext) const override
    {
        return ScoreValue((scoreContext.spreadTightness(marketBar) * 0.65)
                          + (scoreContext.normalizedVolume(marketBar) * 0.35));
    }
};

class MultiFactorBehaviorScoreStrategy final : public IBehaviorScoreStrategy {
public:
    [[nodiscard]] ScoreValue score(const MarketBar& marketBar,
                                   const BehaviorScoreContext& scoreContext) const override
    {
        return ScoreValue((scoreContext.normalizedClose(marketBar) * 0.45)
                          + (scoreContext.momentumStrength(marketBar) * 0.35)
                          + (scoreContext.normalizedVolume(marketBar) * 0.20));
    }
};

class MachineLearningBehaviorScoreStrategy final : public IBehaviorScoreStrategy {
public:
    [[nodiscard]] ScoreValue score(const MarketBar& marketBar,
                                   const BehaviorScoreContext& scoreContext) const override
    {
        return ScoreValue((scoreContext.normalizedClose(marketBar) * 0.35)
                          + (scoreContext.momentumStrength(marketBar) * 0.25)
                          + (scoreContext.normalizedVolume(marketBar) * 0.25)
                          + (scoreContext.spreadTightness(marketBar) * 0.15));
    }
};

class EventDrivenBehaviorScoreStrategy final : public IBehaviorScoreStrategy {
public:
    [[nodiscard]] ScoreValue score(const MarketBar& marketBar,
                                   const BehaviorScoreContext& scoreContext) const override
    {
        return ScoreValue((scoreContext.normalizedVolume(marketBar) * 0.60)
                          + (scoreContext.momentumStrength(marketBar) * 0.40));
    }
};

class HighFrequencyBehaviorScoreStrategy final : public IBehaviorScoreStrategy {
public:
    [[nodiscard]] ScoreValue score(const MarketBar& marketBar,
                                   const BehaviorScoreContext& scoreContext) const override
    {
        return ScoreValue((scoreContext.spreadTightness(marketBar) * 0.70)
                          + (scoreContext.normalizedVolume(marketBar) * 0.30));
    }
};

class CustomBehaviorScoreStrategy final : public IBehaviorScoreStrategy {
public:
    [[nodiscard]] ScoreValue score(const MarketBar& marketBar,
                                   const BehaviorScoreContext& scoreContext) const override
    {
        return ScoreValue((scoreContext.normalizedClose(marketBar) * 0.50)
                          + (scoreContext.momentumStrength(marketBar) * 0.30)
                          + (scoreContext.spreadTightness(marketBar) * 0.20));
    }
};

class BehaviorScoreStrategyCatalog final {
public:
    [[nodiscard]] const IBehaviorScoreStrategy& resolve(const StrategyBehaviorKind behaviorKind) const
    {
        switch (behaviorKind) {
        case StrategyBehaviorKind::TrendFollowing:
            return trendFollowing_;
        case StrategyBehaviorKind::MeanReversion:
            return meanReversion_;
        case StrategyBehaviorKind::Momentum:
            return momentum_;
        case StrategyBehaviorKind::Arbitrage:
            return arbitrage_;
        case StrategyBehaviorKind::MultiFactor:
            return multiFactor_;
        case StrategyBehaviorKind::MachineLearning:
            return machineLearning_;
        case StrategyBehaviorKind::EventDriven:
            return eventDriven_;
        case StrategyBehaviorKind::HighFrequency:
            return highFrequency_;
        case StrategyBehaviorKind::Custom:
            return custom_;
        }

        throw std::invalid_argument("invalid behavior kind");
    }

private:
    TrendFollowingBehaviorScoreStrategy trendFollowing_;
    MeanReversionBehaviorScoreStrategy meanReversion_;
    MomentumBehaviorScoreStrategy momentum_;
    ArbitrageBehaviorScoreStrategy arbitrage_;
    MultiFactorBehaviorScoreStrategy multiFactor_;
    MachineLearningBehaviorScoreStrategy machineLearning_;
    EventDrivenBehaviorScoreStrategy eventDriven_;
    HighFrequencyBehaviorScoreStrategy highFrequency_;
    CustomBehaviorScoreStrategy custom_;
};

class BehaviorAwareCandidateRanker final {
public:
    [[nodiscard]] std::vector<RankedCandidate> rank(const DecisionLayer& decisionLayer,
                                                    const StrategyContext& context,
                                                    const MarketDataSlice& marketData) const
    {
        std::vector<RankedCandidate> rankedCandidates;
        rankedCandidates.reserve(marketData.bars.size());

        const OverlayFactorSelectionScope overlayFactorScope(
            decisionLayer.overlay.enabled ? &decisionLayer.overlay : nullptr);
        const BehaviorScoreContext scoreContext(marketData, overlayFactorScope);
        const IBehaviorScoreStrategy& scoreStrategy = strategyCatalog_.resolve(context.identity.behaviorKind);

        for (const MarketBar& marketBar : marketData.bars) {
            const ScoreValue baseScore = scoreStrategy.score(marketBar, scoreContext);
            ScoreValue finalScore = baseScore;

            if (decisionLayer.overlay.enabled) {
                finalScore = ScoreValue(scoreContext.requireOverlayCompositeScore(marketBar.symbolId,
                                                                                  decisionLayer.overlay));
            }

            if (decisionLayer.overlay.enabled
                && finalScore.value() < decisionLayer.overlay.minimumCompositeScore.value()) {
                continue;
            }

            rankedCandidates.push_back({marketBar.symbolId, finalScore, baseScore});
        }

        std::sort(rankedCandidates.begin(),
                  rankedCandidates.end(),
                  [](const RankedCandidate& left, const RankedCandidate& right) {
                      if (left.score.value() == right.score.value()) {
                          if (left.tieBreaker.value() == right.tieBreaker.value()) {
                              return left.symbolId.value() < right.symbolId.value();
                          }
                          return left.tieBreaker.value() > right.tieBreaker.value();
                      }
                      return left.score.value() > right.score.value();
                  });
        return rankedCandidates;
    }

private:
    BehaviorScoreStrategyCatalog strategyCatalog_;
};

[[nodiscard]] bool holdsSymbol(const PortfolioState& portfolioState, const SymbolId symbolId)
{
    for (const PositionSnapshot& positionSnapshot : portfolioState.positions) {
        if (positionSnapshot.symbolId == symbolId) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] const PositionSnapshot* findPosition(const PortfolioState& portfolioState, const SymbolId symbolId)
{
    for (const PositionSnapshot& positionSnapshot : portfolioState.positions) {
        if (positionSnapshot.symbolId == symbolId) {
            return &positionSnapshot;
        }
    }

    return nullptr;
}

[[nodiscard]] const MarketBar* findMarketBar(const MarketDataSlice& marketData, const SymbolId symbolId)
{
    for (const MarketBar& marketBar : marketData.bars) {
        if (marketBar.symbolId == symbolId) {
            return &marketBar;
        }
    }

    return nullptr;
}

[[nodiscard]] PriceValue resolveReferencePrice(const MarketBar& marketBar, const OrderType orderType)
{
    switch (orderType) {
    case OrderType::Market:
        return marketBar.openPrice;
    case OrderType::Limit:
    case OrderType::MarketOnClose:
        return marketBar.closePrice;
    }

    throw std::invalid_argument("invalid order type");
}

[[nodiscard]] std::uint64_t buildOrderIdentity(const TradingDayIndex tradingDay,
                                               const SymbolId symbolId,
                                               const bool sellSide,
                                               const std::uint32_t ordinal)
{
    const std::uint64_t tradingDayPart = static_cast<std::uint64_t>(tradingDay.value() + 1) << 32U;
    const std::uint64_t symbolPart = static_cast<std::uint64_t>(symbolId.value()) << 8U;
    const std::uint64_t sidePart = sellSide ? 1U : 0U;
    const std::uint64_t ordinalPart = static_cast<std::uint64_t>(ordinal + 1U) << 1U;
    return tradingDayPart | symbolPart | ordinalPart | sidePart;
}

[[nodiscard]] ShareQuantity normalizeToLot(const double rawShares)
{
    if (!std::isfinite(rawShares) || rawShares <= 0.0) {
        return ShareQuantity{};
    }

    const auto shares = static_cast<std::uint64_t>(std::floor(rawShares));
    return ShareQuantity((shares / kLotSize) * kLotSize);
}

[[nodiscard]] double resolveBudget(const RiskSpec& riskSpec,
                                   const std::size_t symbolCount)
{
    if (symbolCount == 0U) {
        return 0.0;
    }

    const double totalBudget = riskSpec.maxPositionRatio.value();
    const double singleCapBudget = riskSpec.maxSinglePositionRatio.value() * static_cast<double>(symbolCount);
    return std::min(totalBudget, singleCapBudget);
}

[[nodiscard]] double resolveCandidateScore(const CandidateScoreList& candidateScores, const SymbolId symbolId)
{
    for (const CandidateScore& candidateScore : candidateScores) {
        if (candidateScore.symbolId == symbolId) {
            return std::max(0.0, candidateScore.score.value());
        }
    }

    return 0.0;
}

[[nodiscard]] bool containsTarget(const TargetWeightList& targetWeights, const SymbolId symbolId)
{
    for (const TargetWeight& targetWeight : targetWeights) {
        if (targetWeight.symbolId == symbolId) {
            return true;
        }
    }

    return false;
}

} // namespace

LayerSelectionResult CanonicalLayerSelectionStrategy::select(const DecisionLayer& decisionLayer,
                                                             const StrategyContext& context,
                                                             const MarketDataSlice& marketData) const
{
    if (!decisionLayer.isValid() || !marketData.isValid()) {
        throw std::invalid_argument("invalid layer selection input");
    }

    LayerSelectionResult result;
    result.outputUniverseId = decisionLayer.inputUniverseId;

    const BehaviorAwareCandidateRanker candidateRanker;
    const std::vector<RankedCandidate> rankedCandidates = candidateRanker.rank(decisionLayer, context, marketData);
    const std::uint32_t targetCount = decisionLayer.overlay.enabled
        ? decisionLayer.overlay.targetPositionCount.value()
        : decisionLayer.targetPositionCount.value();
    const std::size_t boundedCount = std::min<std::size_t>(rankedCandidates.size(), targetCount);

    for (std::size_t index = 0; index < boundedCount; ++index) {
        result.selectedSymbols.add(rankedCandidates[index].symbolId);
        result.candidateScores.add(CandidateScore{rankedCandidates[index].symbolId, rankedCandidates[index].score});
    }

    return result;
}

RuleCheckResult CanonicalRuleChecker::checkRules(const StrategyContext& context,
                                                 const SymbolId symbolId,
                                                 const DecisionType decisionType) const
{
    if (!context.isValid() || !symbolId.isValid()) {
        throw std::invalid_argument("invalid rule check input");
    }

    RuleCheckResult result;
    const PositionSnapshot* positionSnapshot = findPosition(context.portfolioState, symbolId);

    if (decisionType == DecisionType::Entry && positionSnapshot != nullptr) {
        result.blocked = true;
        result.decision = RuleDecision{RuleDecisionCode::EntryBlocked, context.activeLayerId, symbolId, {}};
        return result;
    }

    if (decisionType == DecisionType::Hold
        && positionSnapshot != nullptr
        && positionSnapshot->targetWeight.value() > context.riskSpec.maxSinglePositionRatio.value()) {
        result.forceExit = true;
        result.decision = RuleDecision{RuleDecisionCode::ForcedExit, context.activeLayerId, symbolId, {}};
    }

    return result;
}

ExecutionFill CanonicalExecutionSimulator::execute(const ExecutionOrder& order,
                                                   const MarketDataSlice& marketData) const
{
    if (!order.isValid() || !marketData.isValid()) {
        throw std::invalid_argument("invalid execution input");
    }

    const MarketBar* marketBar = findMarketBar(marketData, order.symbolId);
    const PriceValue fillPrice = marketBar != nullptr
        ? resolveReferencePrice(*marketBar, order.orderType)
        : order.referencePrice;

    return ExecutionFill{order.orderId,
                         order.quantity,
                         fillPrice,
                         CashAmount(0.0),
                         order.tradingDay};
}

TargetWeightList CanonicalPortfolioOptimizer::computeWeights(const SymbolIdList& candidateSymbols,
                                                             const CandidateScoreList& candidateScores,
                                                             const PortfolioState&,
                                                             const RiskSpec& riskSpec) const
{
    if (!riskSpec.isValid()) {
        throw std::invalid_argument("invalid optimizer input");
    }

    TargetWeightList targetWeights;
    if (candidateSymbols.empty()) {
        return targetWeights;
    }

    const double budget = resolveBudget(riskSpec, candidateSymbols.size());
    if (!(budget > 0.0)) {
        for (const SymbolId symbolId : candidateSymbols) {
            targetWeights.add(TargetWeight{symbolId, Weight(0.0)});
        }
        return targetWeights;
    }

    double scoreSum = 0.0;
    for (const SymbolId symbolId : candidateSymbols) {
        scoreSum += resolveCandidateScore(candidateScores, symbolId);
    }

    const double equalWeight = budget / static_cast<double>(candidateSymbols.size());
    for (const SymbolId symbolId : candidateSymbols) {
        double weightValue = equalWeight;
        if (scoreSum > 0.0) {
            weightValue = budget * (resolveCandidateScore(candidateScores, symbolId) / scoreSum);
        }

        weightValue = std::min(weightValue, riskSpec.maxSinglePositionRatio.value());
        targetWeights.add(TargetWeight{symbolId, Weight(weightValue)});
    }

    return targetWeights;
}

ExecutionPolicyDecision CanonicalExecutionPolicyStrategy::evaluate(const ExecutionSpec& executionSpec,
                                                                   const StrategyContext& context) const
{
    if (!executionSpec.isValid() || !context.isValid()) {
        throw std::invalid_argument("invalid execution policy input");
    }

    const std::uint32_t rebalanceEvery = executionSpec.rebalanceFrequencyDays.value();
    const bool rebalanceRequired = (context.tradingDay.value() % static_cast<std::int32_t>(rebalanceEvery)) == 0;

    return ExecutionPolicyDecision{rebalanceRequired,
                                   executionSpec.executionMode == ExecutionMode::Intraday,
                                   executionSpec.defaultOrderType,
                                   CandidateCount(1U),
                                   context.activeLayerId};
}

ExecutionOrderList CanonicalExecutionPolicyStrategy::buildOrders(const TargetWeightList& targetWeights,
                                                                 const PortfolioState& portfolioState,
                                                                 const MarketDataSlice& marketData,
                                                                 const ExecutionPolicyDecision& decision) const
{
    if (!portfolioState.isValid() || !marketData.isValid() || !decision.isValid()) {
        throw std::invalid_argument("invalid order build input");
    }

    ExecutionOrderList orders;
    std::uint32_t ordinal = 0U;

    for (const TargetWeight& targetWeight : targetWeights) {
        const MarketBar* marketBar = findMarketBar(marketData, targetWeight.symbolId);
        if (marketBar == nullptr) {
            continue;
        }

        const double targetNotional = portfolioState.totalEquity.value() * targetWeight.weight.value();
        const ShareQuantity targetQuantity = normalizeToLot(targetNotional / marketBar->closePrice.value());
        const PositionSnapshot* currentPosition = findPosition(portfolioState, targetWeight.symbolId);
        const std::uint64_t currentQuantity = currentPosition != nullptr ? currentPosition->quantity.value() : 0U;

        if (targetQuantity.value() == currentQuantity) {
            continue;
        }

        const bool sellSide = targetQuantity.value() < currentQuantity;
        const std::uint64_t deltaQuantity = sellSide
            ? (currentQuantity - targetQuantity.value())
            : (targetQuantity.value() - currentQuantity);
        const ShareQuantity normalizedQuantity(deltaQuantity);
        if (!normalizedQuantity.isPositive()) {
            continue;
        }

        orders.add(ExecutionOrder{OrderId(buildOrderIdentity(marketData.tradingDay,
                                                             targetWeight.symbolId,
                                                             sellSide,
                                                             ordinal++)),
                                  targetWeight.symbolId,
                                  sellSide ? OrderSide::Sell : OrderSide::Buy,
                                  decision.orderType,
                                  normalizedQuantity,
                                  resolveReferencePrice(*marketBar, decision.orderType),
                                  marketData.tradingDay,
                                  decision.layerId});
    }

    for (const PositionSnapshot& positionSnapshot : portfolioState.positions) {
        if (!containsTarget(targetWeights, positionSnapshot.symbolId)) {
            const MarketBar* marketBar = findMarketBar(marketData, positionSnapshot.symbolId);
            if (marketBar == nullptr || !positionSnapshot.quantity.isPositive()) {
                continue;
            }

            orders.add(ExecutionOrder{OrderId(buildOrderIdentity(marketData.tradingDay,
                                                                 positionSnapshot.symbolId,
                                                                 true,
                                                                 ordinal++)),
                                      positionSnapshot.symbolId,
                                      OrderSide::Sell,
                                      decision.orderType,
                                      positionSnapshot.quantity,
                                      resolveReferencePrice(*marketBar, decision.orderType),
                                      marketData.tradingDay,
                                      decision.layerId});
        }
    }

    return orders;
}

} // namespace domain::backtest::strategy_engine