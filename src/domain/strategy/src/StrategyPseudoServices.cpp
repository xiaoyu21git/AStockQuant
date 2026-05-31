#include "../include/IStrategyService.h"

#include <algorithm>
#include <cmath>

namespace domain::strategy {

namespace {
constexpr std::uint32_t kDefaultOrderQuantity = 100;
const auto kZeroLatency = std::chrono::microseconds(0);
constexpr StrategyCount kSingleSignalCount = 1;
}

LocalRuleEvaluationService::LocalRuleEvaluationService()
{
    std::vector<rules::RuleId> defaultRules;
    defaultRules.push_back(kRuleScoreNonNegative);
    defaultRules.push_back(kRuleTargetWeightAbsLimit);
    const std::lock_guard<std::mutex> lock(ruleSetsMutex_);
    ruleSets_.push_back(rules::RuleSet(rules::kRuleSetAllPass, defaultRules));
}

RuleEvaluationResult LocalRuleEvaluationService::evaluate(
    const StrategySignal& signal,
    rules::RuleSetId ruleSetId,
    const rules::RuleEvaluationContext& context)
{
    const auto beginAt = std::chrono::steady_clock::now();
    if (context.phase() != rules::RuleEvaluationPhase::LowLatency
        || context.candidateSignalCount() != kSingleSignalCount
        || signal.strategyInstanceId() != context.strategyInstanceId()) {
        return RuleEvaluationResult(
            false,
            signal,
            RuleRejectReason::RuleTemplateBlocked,
            kZeroLatency);
    }

    std::vector<rules::RuleId> selectedRules;
    {
        const std::lock_guard<std::mutex> lock(ruleSetsMutex_);
        const auto found = std::find_if(
            ruleSets_.begin(),
            ruleSets_.end(),
            [ruleSetId](const rules::RuleSet& set) {
                return set.id() == ruleSetId;
            });
        if (found == ruleSets_.end()) {
            return RuleEvaluationResult(
                false,
                signal,
                RuleRejectReason::RuleTemplateBlocked,
                kZeroLatency);
        }
        selectedRules = found->rules();
    }

    if (!signal.isValid()) {
        return RuleEvaluationResult(
            false,
            signal,
            RuleRejectReason::InvalidSignal,
            kZeroLatency);
    }

    bool passed = true;
    RuleRejectReason rejectReason = RuleRejectReason::None;
    for (rules::RuleId ruleId : selectedRules) {
        if (ruleId == kRuleScoreNonNegative && signal.score() < kMinSignalScore) {
            passed = false;
            rejectReason = RuleRejectReason::RuleTemplateBlocked;
            break;
        }
        if (ruleId == kRuleTargetWeightAbsLimit
            && std::fabs(signal.targetWeight()) > kMaxAbsoluteTargetWeight) {
            passed = false;
            rejectReason = RuleRejectReason::RiskGuardBlocked;
            break;
        }
    }

    const auto endAt = std::chrono::steady_clock::now();
    const auto latency =
        std::chrono::duration_cast<std::chrono::microseconds>(endAt - beginAt);
    return RuleEvaluationResult(
        passed,
        signal,
        rejectReason,
        latency);
}

StrategyServiceFlowResult LocalRuleEvaluationService::evaluateBatch(
    const std::vector<StrategySignal>& candidateSignals,
    rules::RuleSetId ruleSetId,
    const rules::RuleEvaluationContext& context,
    std::vector<RuleEvaluationResult>& outputResults)
{
    outputResults.clear();
    outputResults.reserve(candidateSignals.size());

    if (context.phase() != rules::RuleEvaluationPhase::Batch) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidInput);
    }

    if (context.candidateSignalCount() != static_cast<StrategyCount>(candidateSignals.size())) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidInput);
    }

    std::vector<rules::RuleId> selectedRules;
    {
        const std::lock_guard<std::mutex> lock(ruleSetsMutex_);
        const auto found = std::find_if(
            ruleSets_.begin(),
            ruleSets_.end(),
            [ruleSetId](const rules::RuleSet& set) {
                return set.id() == ruleSetId;
            });
        if (found == ruleSets_.end()) {
            return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidInput);
        }
        selectedRules = found->rules();
    }

    for (const StrategySignal& signal : candidateSignals) {
        const auto beginAt = std::chrono::steady_clock::now();
        if (!signal.isValid()) {
            outputResults.push_back(RuleEvaluationResult(
                false,
                signal,
                RuleRejectReason::InvalidSignal,
                kZeroLatency));
            continue;
        }
        if (signal.strategyInstanceId() != context.strategyInstanceId()) {
            outputResults.push_back(RuleEvaluationResult(
                false,
                signal,
                RuleRejectReason::RuleTemplateBlocked,
                kZeroLatency));
            continue;
        }

        bool passed = true;
        RuleRejectReason rejectReason = RuleRejectReason::None;

        for (rules::RuleId ruleId : selectedRules) {
            if (ruleId == kRuleScoreNonNegative && signal.score() < kMinSignalScore) {
                passed = false;
                rejectReason = RuleRejectReason::RuleTemplateBlocked;
                break;
            }
            if (ruleId == kRuleTargetWeightAbsLimit
                && std::fabs(signal.targetWeight()) > kMaxAbsoluteTargetWeight) {
                passed = false;
                rejectReason = RuleRejectReason::RiskGuardBlocked;
                break;
            }
        }

        const auto endAt = std::chrono::steady_clock::now();
        const auto latency =
            std::chrono::duration_cast<std::chrono::microseconds>(endAt - beginAt);

        outputResults.push_back(RuleEvaluationResult(
            passed,
            signal,
            rejectReason,
            latency));
    }
    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

bool LocalRuleEvaluationService::isReady() const
{
    const std::lock_guard<std::mutex> lock(ruleSetsMutex_);
    const auto found = std::find_if(
        ruleSets_.begin(),
        ruleSets_.end(),
        [](const rules::RuleSet& set) {
            return set.id() == rules::kRuleSetAllPass;
        });
    return found != ruleSets_.end() && !found->rules().empty();
}

void LocalRuleEvaluationService::saveRuleSet(const rules::RuleSet& ruleSet)
{
    if (!ruleSet.isValid()) {
        return;
    }

    const std::lock_guard<std::mutex> lock(ruleSetsMutex_);
    const auto found = std::find_if(
        ruleSets_.begin(),
        ruleSets_.end(),
        [&ruleSet](const rules::RuleSet& item) {
            return item.id() == ruleSet.id();
        });
    if (found == ruleSets_.end()) {
        ruleSets_.push_back(ruleSet);
        return;
    }
    *found = ruleSet;
}

std::optional<rules::RuleSet> LocalRuleEvaluationService::ruleSet(rules::RuleSetId id) const
{
    const std::lock_guard<std::mutex> lock(ruleSetsMutex_);
    const auto found = std::find_if(
        ruleSets_.begin(),
        ruleSets_.end(),
        [id](const rules::RuleSet& item) {
            return item.id() == id;
        });
    if (found == ruleSets_.end()) {
        return std::nullopt;
    }
    return *found;
}

std::vector<rules::RuleId> LocalRuleEvaluationService::availableRules() const
{
    std::vector<rules::RuleId> output;
    {
        const std::lock_guard<std::mutex> lock(ruleSetsMutex_);
        for (const rules::RuleSet& set : ruleSets_) {
            const std::vector<rules::RuleId>& ids = set.rules();
            output.insert(output.end(), ids.begin(), ids.end());
        }
    }
    std::sort(output.begin(), output.end());
    output.erase(std::unique(output.begin(), output.end()), output.end());
    return output;
}

StrategyServiceFlowResult DefaultOrderBuilder::buildOrder(
    const StrategySignal& signal,
    const RuntimeStrategyContext& context,
    OrderRequest& outputOrder) const
{
    if (!signal.isValid() || !context.isValid()) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidInput);
    }
    if (!context.autoExecutionEnabled()) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidState);
    }
    if (std::fabs(signal.targetWeight()) > context.maxTargetWeight()) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::OrderBuildFailed);
    }

    // 下单数量始终受运行时上下文约束；仅在未配置上限时使用默认值。
    const std::uint32_t quantity = context.maxOrderQuantity() > 0
        ? context.maxOrderQuantity()
        : kDefaultOrderQuantity;
    outputOrder = OrderRequest(
        signal.strategyInstanceId(),
        signal.instrumentId(),
        signal.side(),
        quantity);
    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

} // namespace domain::strategy
