#include "../include/RuntimeStrategyFactory.h"

#include "MultiFactorSelectionStrategy.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kZeroCount = 0;
constexpr double kZeroValue = 0.0;
constexpr double kFullWeight = 1.0;
constexpr double kMagnitudeEpsilon = 1e-12;

struct CandidateSignal final {
    domain::strategy::InstrumentId instrumentId{};
    domain::strategy::RuntimeOrderSide side{domain::strategy::RuntimeOrderSide::Buy};
    double score{0.0};
};

[[nodiscard]] double resolveWeightCap(const domain::strategies::MultiFactorSelectionStrategy& strategy,
                                      const domain::strategy::RuntimeStrategyContext& context) noexcept
{
    return (std::max)(kZeroValue, (std::min)(strategy.maxWeightPerStock(), context.maxTargetWeight()));
}

[[nodiscard]] std::vector<CandidateSignal> buildCandidateSignals(
    const domain::strategies::MultiFactorSelectionStrategy& strategy,
    const std::vector<domain::strategies::MultiFactorScore>& scores)
{
    std::vector<CandidateSignal> candidates;
    const std::size_t maxPositions = static_cast<std::size_t>(strategy.maxPositions());
    const std::size_t selectionCount = maxPositions > kZeroCount
        ? (std::min)(maxPositions, scores.size())
        : kZeroCount;
    candidates.reserve(selectionCount);

    for (const domain::strategies::MultiFactorScore& score : scores) {
        if (candidates.size() >= selectionCount || score.symbolId == 0) {
            break;
        }

        const bool useSellSide = strategy.allowsShort() && score.score < kZeroValue;
        if (!useSellSide && !(score.score > kZeroValue)) {
            continue;
        }

        candidates.push_back(CandidateSignal{
            domain::strategy::InstrumentId(score.symbolId),
            useSellSide ? domain::strategy::RuntimeOrderSide::Sell
                        : domain::strategy::RuntimeOrderSide::Buy,
            score.score});
    }

    return candidates;
}

[[nodiscard]] std::vector<double> buildRawWeights(
    const domain::strategies::MultiFactorSelectionStrategy& strategy,
    const std::vector<CandidateSignal>& candidates)
{
    if (candidates.empty()) {
        return {};
    }

    std::vector<double> rawWeights(candidates.size(), kZeroValue);
    switch (strategy.weightScheme()) {
    case domain::strategies::WeightScheme::EQUAL: {
        const double equalWeight = kFullWeight / static_cast<double>(candidates.size());
        std::fill(rawWeights.begin(), rawWeights.end(), equalWeight);
        return rawWeights;
    }
    case domain::strategies::WeightScheme::SIGNAL_STRENGTH: {
        double magnitudeSum = kZeroValue;
        for (const CandidateSignal& candidate : candidates) {
            magnitudeSum += std::abs(candidate.score);
        }

        if (magnitudeSum <= kMagnitudeEpsilon) {
            const double equalWeight = kFullWeight / static_cast<double>(candidates.size());
            std::fill(rawWeights.begin(), rawWeights.end(), equalWeight);
            return rawWeights;
        }

        for (std::size_t index = 0; index < candidates.size(); ++index) {
            rawWeights[index] = std::abs(candidates[index].score) / magnitudeSum;
        }
        return rawWeights;
    }
    case domain::strategies::WeightScheme::MARKET_CAP:
    case domain::strategies::WeightScheme::RISK_PARITY:
        return {};
    }

    return {};
}

class MultiFactorSelectionRuntimeStrategy final : public domain::strategy::IRuntimeStrategy {
public:
    MultiFactorSelectionRuntimeStrategy(
        std::shared_ptr<const domain::strategies::MultiFactorSelectionStrategy> strategyDefinition,
        domain::strategy::StrategyInstanceId strategyInstanceId,
        domain::strategy::rules::RuleSetId ruleSetId)
        : strategyDefinition_(std::move(strategyDefinition))
        , strategyInstanceId_(strategyInstanceId)
        , ruleSetId_(ruleSetId)
    {
    }

    [[nodiscard]] domain::strategy::StrategyInstanceId instanceId() const noexcept override
    {
        return strategyInstanceId_;
    }

    [[nodiscard]] bool isEnabled() const noexcept override
    {
        return strategyDefinition_ && strategyDefinition_->isEnabled();
    }

    [[nodiscard]] domain::strategy::rules::RuleSetId ruleSetId() const noexcept override
    {
        return ruleSetId_;
    }

    void evaluate(const std::vector<domain::strategy::RuntimeFactorSnapshot>& factorSnapshots,
                  const domain::strategy::RuntimeStrategyContext& context,
                  std::vector<domain::strategy::StrategySignal>& outputSignals) override
    {
        if (!strategyDefinition_
            || !strategyDefinition_->isConfigured()
            || !context.isValid()
            || context.strategyInstanceId() != strategyInstanceId_
            || factorSnapshots.empty()) {
            return;
        }

        const double weightCap = resolveWeightCap(*strategyDefinition_, context);
        const double minWeight = strategyDefinition_->minWeightPerStock();
        if (!(weightCap > kZeroValue) || weightCap < minWeight) {
            return;
        }

        const std::vector<domain::strategies::MultiFactorScore> scores =
            strategyDefinition_->computeCompositeScores(factorSnapshots);
        const std::vector<CandidateSignal> candidates = buildCandidateSignals(*strategyDefinition_, scores);
        const std::vector<double> rawWeights = buildRawWeights(*strategyDefinition_, candidates);
        if (candidates.empty() || rawWeights.size() != candidates.size()) {
            return;
        }

        outputSignals.reserve(outputSignals.size() + candidates.size());
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            const double boundedWeight = (std::max)(minWeight, (std::min)(rawWeights[index], weightCap));
            outputSignals.push_back(domain::strategy::StrategySignal(
                strategyInstanceId_,
                candidates[index].instrumentId,
                candidates[index].side,
                candidates[index].score,
                boundedWeight));
        }
    }

private:
    std::shared_ptr<const domain::strategies::MultiFactorSelectionStrategy> strategyDefinition_;
    domain::strategy::StrategyInstanceId strategyInstanceId_{0};
    domain::strategy::rules::RuleSetId ruleSetId_{domain::strategy::rules::kRuleSetAllPass};
};

}

namespace domain::strategy {

std::shared_ptr<IRuntimeStrategy> createMultiFactorSelectionRuntimeStrategy(
    std::shared_ptr<const ::domain::strategies::MultiFactorSelectionStrategy> strategyDefinition,
    StrategyInstanceId strategyInstanceId,
    rules::RuleSetId ruleSetId)
{
    if (!strategyDefinition || strategyInstanceId == 0) {
        return {};
    }

    return std::make_shared<MultiFactorSelectionRuntimeStrategy>(
        std::move(strategyDefinition),
        strategyInstanceId,
        ruleSetId);
}

std::optional<StrategyEngine> createMultiFactorRuntimeEngine(
    MultiFactorRuntimeEngineSetup setup)
{
    if (!setup.strategyDefinition
        || setup.strategyInstanceId == 0
        || !setup.context.isValid()
        || setup.context.strategyInstanceId() != setup.strategyInstanceId) {
        return std::nullopt;
    }

    std::shared_ptr<IRuntimeStrategy> runtimeStrategy = createMultiFactorSelectionRuntimeStrategy(
        setup.strategyDefinition,
        setup.strategyInstanceId,
        setup.ruleSetId);
    if (!runtimeStrategy) {
        return std::nullopt;
    }

    StrategyEngine engine = StrategyEngine::builder()
        .withFactorCallbacks(std::move(setup.factorCallbacks))
        .build();
    const StrategyServiceFlowResult registerResult = engine.registerStrategy(
        std::move(runtimeStrategy),
        setup.context);
    if (!registerResult.isOk()) {
        return std::nullopt;
    }

    return engine;
}

} // namespace domain::strategy