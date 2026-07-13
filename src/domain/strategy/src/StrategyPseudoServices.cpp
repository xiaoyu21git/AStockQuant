#include "../include/IStrategyService.h"
#include "foundation/log/logging.hpp"

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
    std::string failDetail;
    for (rules::RuleId ruleId : selectedRules) {
        if (ruleId == kRuleScoreNonNegative && signal.score() < kMinSignalScore) {
            passed = false;
            rejectReason = RuleRejectReason::RuleTemplateBlocked;
            failDetail = "score≥0 不通过(score=" + std::to_string(signal.score()) + ")";
            break;
        }
        if (ruleId == kRuleTargetWeightAbsLimit
            && std::fabs(signal.targetWeight()) > kMaxAbsoluteTargetWeight) {
            passed = false;
            rejectReason = RuleRejectReason::RiskGuardBlocked;
            failDetail = "|targetWeight|≤1.0 不通过(weight=" + std::to_string(signal.targetWeight()) + ")";
            break;
        }
    }

    if (!passed) {
        char symBuf[16];
        std::snprintf(symBuf, sizeof(symBuf), "%06u", signal.instrumentId().value);
        INTERNAL_WARN_STREAM << "[RuleEval] 规则拒绝: " << symBuf
                             << " " << failDetail
                             << " score=" << signal.score()
                             << " targetWeight=" << signal.targetWeight()
                             << " side=" << (signal.side() == RuntimeOrderSide::Buy ? 'B' : 'S');
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
        std::string failDetail;

        for (rules::RuleId ruleId : selectedRules) {
            if (ruleId == kRuleScoreNonNegative && signal.score() < kMinSignalScore) {
                passed = false;
                rejectReason = RuleRejectReason::RuleTemplateBlocked;
                failDetail = "score≥0 不通过(score=" + std::to_string(signal.score()) + ")";
                break;
            }
            if (ruleId == kRuleTargetWeightAbsLimit
                && std::fabs(signal.targetWeight()) > kMaxAbsoluteTargetWeight) {
                passed = false;
                rejectReason = RuleRejectReason::RiskGuardBlocked;
                failDetail = "|targetWeight|≤1.0 不通过(weight=" + std::to_string(signal.targetWeight()) + ")";
                break;
            }
        }

        if (!passed) {
            char symBuf[16];
            std::snprintf(symBuf, sizeof(symBuf), "%06u", signal.instrumentId().value);
            INTERNAL_WARN_STREAM << "[RuleEval] 规则拒绝: " << symBuf
                                 << " " << failDetail
                                 << " score=" << signal.score()
                                 << " targetWeight=" << signal.targetWeight()
                                 << " side=" << (signal.side() == RuntimeOrderSide::Buy ? 'B' : 'S');
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

    const double weight = std::fabs(signal.targetWeight());
    const std::uint32_t base = context.maxOrderQuantity() > 0
        ? context.maxOrderQuantity() : kDefaultOrderQuantity;
    std::uint32_t quantity = static_cast<std::uint32_t>(weight * base);
    if (quantity < 100) quantity = 100;
    quantity = quantity / 100 * 100;

    // 填充统一定单类型 — 优先使用 signal 携带的真实代码, 回退到 InstrumentId 格式化
    const std::string& realCode = signal.symbolCode();
    if (!realCode.empty()) {
        outputOrder.setSymbol(realCode);              // 真实6位代码 (如 "600000")
    } else {
        char codeBuf[16];
        std::snprintf(codeBuf, sizeof(codeBuf), "%06u", signal.instrumentId().value);
        outputOrder.setSymbol(codeBuf);               // 回退: InstrumentId 格式化 (回测/兼容)
    }
    outputOrder.setStrategyId(std::to_string(signal.strategyInstanceId()));
    outputOrder.setSide((signal.side() == RuntimeOrderSide::Buy)
                        ? OrderSide::Buy : OrderSide::Sell);
    outputOrder.setQuantity(static_cast<int64_t>(quantity));
    outputOrder.setExtension(domain::trading::ExtKey::kSignalScore, signal.score());
    outputOrder.setExtension(domain::trading::ExtKey::kTargetWeight, signal.targetWeight());
    outputOrder.setExtension(domain::trading::ExtKey::kSignalIntent,
        static_cast<uint64_t>(signal.intent()));
    outputOrder.setOrderType(OrderType::Market);
    outputOrder.setPrice(0);                         // drainQueue 用 tick 价补

    INTERNAL_DEBUG_STREAM << "[OrderBuild] signal->order: symbol=" << outputOrder.symbol()
                          << " side=" << (outputOrder.side() == OrderSide::Buy ? "B" : "S")
                          << " qty=" << outputOrder.quantity()
                          << " weight=" << weight << " score=" << signal.score();

    return StrategyServiceFlowResult(StrategyServiceFlowCode::Ok);
}

} // namespace domain::strategy
