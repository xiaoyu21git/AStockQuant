#include "../include/IStrategyService.h"

#include <algorithm>
#include <cstddef>

namespace domain::strategy {

namespace {
constexpr rules::RuleId kRuleScoreNonNegative = 1;
constexpr rules::RuleId kRuleTargetWeightAbsLimit = 2;
const auto kZeroLatency = std::chrono::microseconds(0);
constexpr StrategyCount kSingleSignalCount = 1;
}

PythonRuleEvaluationService::PythonRuleEvaluationService(IPythonRuleAdapter& adapter)
    : adapter_(adapter)
{
    const std::lock_guard<std::mutex> lock(ruleSetsMutex_);
    ruleSets_.push_back(rules::RuleSet(rules::kRuleSetAllPass, {}));
}

RuleEvaluationResult PythonRuleEvaluationService::evaluate(
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

    if (!signal.isValid()) {
        return RuleEvaluationResult(
            false,
            signal,
            RuleRejectReason::InvalidSignal,
            kZeroLatency);
    }

    std::vector<StrategySignal> oneSignal;
    oneSignal.push_back(signal);

    std::vector<PythonRuleResult> pythonResults;
    const StrategyServiceFlowResult flowResult =
        evaluateBatchInternal(oneSignal, ruleSetId, pythonResults);
    if (!flowResult.isOk() || pythonResults.size() != 1) {
        return RuleEvaluationResult(
            false,
            signal,
            RuleRejectReason::RuleTemplateBlocked,
            kZeroLatency);
    }

    const auto endAt = std::chrono::steady_clock::now();
    const auto latency =
        std::chrono::duration_cast<std::chrono::microseconds>(endAt - beginAt);
    return RuleEvaluationResult(
        pythonResults[0].passed(),
        signal,
        pythonResults[0].rejectReason(),
        latency);
}

StrategyServiceFlowResult PythonRuleEvaluationService::evaluateBatch(
    const std::vector<StrategySignal>& candidateSignals,
    rules::RuleSetId ruleSetId,
    const rules::RuleEvaluationContext& context,
    std::vector<RuleEvaluationResult>& outputResults)
{
    const auto beginAt = std::chrono::steady_clock::now();
    outputResults.clear();
    outputResults.reserve(candidateSignals.size());

    if (context.phase() != rules::RuleEvaluationPhase::Batch) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidInput);
    }

    if (candidateSignals.empty()) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
    }
    if (context.candidateSignalCount() != static_cast<StrategyCount>(candidateSignals.size())) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidInput);
    }

    std::vector<StrategySignal> validSignals;
    validSignals.reserve(candidateSignals.size());
    std::vector<std::size_t> validIndexes;
    validIndexes.reserve(candidateSignals.size());

    for (std::size_t i = 0; i < candidateSignals.size(); ++i) {
        if (candidateSignals[i].isValid()) {
            if (candidateSignals[i].strategyInstanceId() != context.strategyInstanceId()) {
                return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidInput);
            }
            validIndexes.push_back(i);
            validSignals.push_back(candidateSignals[i]);
        }
    }

    std::vector<PythonRuleResult> pythonResults;
    const StrategyServiceFlowResult flowResult = evaluateBatchInternal(
        validSignals,
        ruleSetId,
        pythonResults);
    if (!flowResult.isOk()) {
        return flowResult;
    }
    if (pythonResults.size() != validSignals.size()) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::RuleCheckFailed);
    }

    outputResults.resize(candidateSignals.size());
    for (std::size_t i = 0; i < candidateSignals.size(); ++i) {
        if (!candidateSignals[i].isValid()) {
            outputResults[i] = RuleEvaluationResult(
                false,
                candidateSignals[i],
                RuleRejectReason::InvalidSignal,
                kZeroLatency);
        }
    }

    const auto endAt = std::chrono::steady_clock::now();
    const auto batchLatency =
        std::chrono::duration_cast<std::chrono::microseconds>(endAt - beginAt);

    for (std::size_t i = 0; i < validIndexes.size(); ++i) {
        const std::size_t index = validIndexes[i];
        outputResults[index] = RuleEvaluationResult(
            pythonResults[i].passed(),
            candidateSignals[index],
            pythonResults[i].rejectReason(),
            batchLatency);
    }

    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

bool PythonRuleEvaluationService::isReady() const
{
    const std::lock_guard<std::mutex> lock(ruleSetsMutex_);
    const auto found = std::find_if(
        ruleSets_.begin(),
        ruleSets_.end(),
        [](const rules::RuleSet& set) {
            return set.id() == rules::kRuleSetAllPass;
        });
    return found != ruleSets_.end();
}

void PythonRuleEvaluationService::saveRuleSet(const rules::RuleSet& ruleSet)
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

std::optional<rules::RuleSet> PythonRuleEvaluationService::ruleSet(rules::RuleSetId id) const
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

std::vector<rules::RuleId> PythonRuleEvaluationService::availableRules() const
{
    std::vector<rules::RuleId> output;
    const std::lock_guard<std::mutex> lock(ruleSetsMutex_);
    for (const rules::RuleSet& set : ruleSets_) {
        const std::vector<rules::RuleId>& ids = set.rules();
        output.insert(output.end(), ids.begin(), ids.end());
    }
    std::sort(output.begin(), output.end());
    output.erase(std::unique(output.begin(), output.end()), output.end());
    return output;
}

std::vector<PythonRuleDescriptor> PythonRuleEvaluationService::buildDescriptorsForRuleSet(
    rules::RuleSetId ruleSetId) const
{
    std::vector<PythonRuleDescriptor> descriptors;
    const auto maybeSet = ruleSet(ruleSetId);
    if (!maybeSet.has_value()) {
        return descriptors;
    }

    for (rules::RuleId ruleId : maybeSet->rules()) {
        if (ruleId == kRuleScoreNonNegative) {
            descriptors.push_back(PythonRuleDescriptor(
                PythonRuleKind::ScoreNonNegative,
                true,
                0.0,
                0.0,
                0));
            continue;
        }
        if (ruleId == kRuleTargetWeightAbsLimit) {
            descriptors.push_back(PythonRuleDescriptor(
                PythonRuleKind::TargetWeightAbsoluteLimit,
                true,
                1.0,
                0.0,
                0));
            continue;
        }
        descriptors.push_back(PythonRuleDescriptor(
            PythonRuleKind::Custom,
            true,
            static_cast<double>(ruleId),
            0.0,
            0));
    }
    return descriptors;
}

StrategyServiceFlowResult PythonRuleEvaluationService::evaluateBatchInternal(
    const std::vector<StrategySignal>& candidateSignals,
    rules::RuleSetId ruleSetId,
    std::vector<PythonRuleResult>& outputResults) const
{
    outputResults.clear();
    if (candidateSignals.empty()) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
    }

    PythonRuleBatchRequest request;
    request.candidateSignals = candidateSignals;
    request.descriptors = buildDescriptorsForRuleSet(ruleSetId);
    if (request.descriptors.empty() && ruleSetId != rules::kRuleSetAllPass) {
        return StrategyServiceFlowResult(StrategyServiceFlowCode::InvalidInput);
    }
    try {
        return adapter_.checkBatch(request, outputResults);
    } catch (...) {
        outputResults.clear();
        return StrategyServiceFlowResult(StrategyServiceFlowCode::RuleCheckFailed);
    }
}

} // namespace domain::strategy
