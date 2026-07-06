#include "../include/RuntimeStrategyFactory.h"

#include "MultiFactorSelectionStrategy.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

namespace {

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
    candidates.reserve(std::min(static_cast<std::size_t>(strategy.maxPositions()), scores.size()));
    for (const auto& score : scores) {
        if (score.symbolId == 0) break;
        const bool useSellSide = strategy.allowsShort() && score.score < kZeroValue;
        if (!useSellSide && !(score.score > kZeroValue)) continue;
        candidates.push_back({domain::strategy::InstrumentId(score.symbolId),
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
    if (candidates.empty()) return {};

    std::vector<double> raw(candidates.size(), kZeroValue);
    switch (strategy.weightScheme()) {
    case domain::strategies::WeightScheme::EQUAL:
        std::fill(raw.begin(), raw.end(), kFullWeight / static_cast<double>(candidates.size()));
        return raw;
    case domain::strategies::WeightScheme::SIGNAL_STRENGTH: {
        double sum = 0.0;
        for (auto& c : candidates) sum += std::abs(c.score);
        if (sum <= kMagnitudeEpsilon) {
            std::fill(raw.begin(), raw.end(), kFullWeight / static_cast<double>(candidates.size()));
            return raw;
        }
        for (std::size_t i = 0; i < candidates.size(); ++i) raw[i] = std::abs(candidates[i].score) / sum;
        return raw;
    }
    default: return {};
    }
}

/// @brief 多因子选择运行时策略
///
/// 因子快照由 StrategyService 通过 IRuntimeFactorService::copySnapshots 注入，
/// 不再持有任何因子适配器。
class MultiFactorSelectionRuntimeStrategy final : public domain::strategy::IRuntimeStrategy {
public:
    MultiFactorSelectionRuntimeStrategy(
        std::shared_ptr<const domain::strategies::MultiFactorSelectionStrategy> strategyDefinition,
        domain::strategy::StrategyInstanceId strategyInstanceId)
        : strategyDefinition_(std::move(strategyDefinition))
        , strategyInstanceId_(strategyInstanceId)
    {
    }

    [[nodiscard]] domain::strategy::StrategyInstanceId instanceId() const noexcept override { return strategyInstanceId_; }
    [[nodiscard]] bool isEnabled() const noexcept override { return strategyDefinition_ && strategyDefinition_->isEnabled(); }
    [[nodiscard]] domain::strategy::rules::RuleSetId ruleSetId() const noexcept override {
        return domain::strategy::rules::kRuleSetAllPass;
    }
    [[nodiscard]] bool usesFactors() const noexcept override { return true; }

    void evaluate(const std::vector<domain::strategy::RuntimeFactorSnapshot>& factorSnapshots,
                  const domain::strategy::RuntimeStrategyContext& context,
                  std::vector<domain::strategy::StrategySignal>& outputSignals) override
    {
        if (!strategyDefinition_ || !strategyDefinition_->isConfigured() || !context.isValid()
            || context.strategyInstanceId() != strategyInstanceId_ || factorSnapshots.empty()) return;

        const double wCap = resolveWeightCap(*strategyDefinition_, context);
        const double wMin = strategyDefinition_->minWeightPerStock();
        if (!(wCap > kZeroValue) || wCap < wMin) return;

        auto scores = strategyDefinition_->computeCompositeScores(factorSnapshots);
        auto candidates = buildCandidateSignals(*strategyDefinition_, scores);
        auto rawWeights = buildRawWeights(*strategyDefinition_, candidates);
        if (candidates.empty() || rawWeights.size() != candidates.size()) return;

        outputSignals.reserve(outputSignals.size() + candidates.size());
        const auto& currentWeights = context.currentWeights();
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const double bw = std::max(wMin, std::min(rawWeights[i], wCap));
            // 查当前权重 → 对比目标权重 → 设置意图
            double currentW = 0.0;
            {
                char codeBuf[16];
                std::snprintf(codeBuf, sizeof(codeBuf), "%06u",
                    candidates[i].instrumentId.value);
                auto cwIt = currentWeights.find(codeBuf);
                if (cwIt != currentWeights.end()) currentW = cwIt->second;
            }
            domain::strategy::SignalIntent intent = domain::strategy::SignalIntent::KEEP;
            if (candidates[i].side == domain::strategy::RuntimeOrderSide::Sell) {
                intent = (currentW > 0.001) ? domain::strategy::SignalIntent::CLOSE
                                            : domain::strategy::SignalIntent::KEEP;
            } else if (bw > 0.0) {
                if (currentW < 0.001)       intent = domain::strategy::SignalIntent::OPEN;
                else if (bw > currentW)     intent = domain::strategy::SignalIntent::ADD;
                else if (bw < currentW)     intent = domain::strategy::SignalIntent::REDUCE;
                else                        intent = domain::strategy::SignalIntent::KEEP;
            }

            auto sig = domain::strategy::StrategySignal(
                strategyInstanceId_, candidates[i].instrumentId,
                candidates[i].side, candidates[i].score, bw);
            sig.setCurrentWeight(currentW);
            sig.setIntent(intent);
            outputSignals.push_back(std::move(sig));
        }
    }

private:
    std::shared_ptr<const domain::strategies::MultiFactorSelectionStrategy> strategyDefinition_;
    domain::strategy::StrategyInstanceId strategyInstanceId_{0};
};

} // anonymous namespace

namespace domain::strategy {

std::shared_ptr<IRuntimeStrategy> createMultiFactorSelectionRuntimeStrategy(
    std::shared_ptr<const ::domain::strategies::MultiFactorSelectionStrategy> strategyDefinition,
    StrategyInstanceId strategyInstanceId)
{
    if (!strategyDefinition || strategyInstanceId == 0) return {};
    return std::make_shared<MultiFactorSelectionRuntimeStrategy>(
        std::move(strategyDefinition), strategyInstanceId);
}

std::unique_ptr<StrategyEngine> createMultiFactorRuntimeEngine(MultiFactorRuntimeEngineSetup setup)
{
    if (!setup.strategyDefinition || setup.strategyInstanceId == 0
        || !setup.context.isValid() || setup.context.strategyInstanceId() != setup.strategyInstanceId)
        return nullptr;

    auto engine = StrategyEngine::builder()
        .build();
    const auto result = engine->registerStrategy(
        createMultiFactorSelectionRuntimeStrategy(setup.strategyDefinition, setup.strategyInstanceId),
        setup.context);
    if (!result.isOk()) return nullptr;
    return engine;
}

} // namespace domain::strategy
