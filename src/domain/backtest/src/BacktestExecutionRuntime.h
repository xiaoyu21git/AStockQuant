#pragma once

#include "BacktestDecisionRuntime.h"
#include "BacktestRuleTemplateEvaluator.h"

#include "../../../engine/include/BacktestResult.h"

namespace domain::backtest::runtime {

struct RuleTemplateRuntimeSupport {
    QVariantList compiledTemplates;
    QVariantMap baseFacts;
    QVariantMap strategyScope;

    bool active() const {
        return !compiledTemplates.isEmpty();
    }
};

void initializeRuleTemplateSummary(
    engine::BacktestResult& result,
    const RuleTemplateRuntimeSupport& support);

void processBarImmediate(
    engine::BacktestResult& result,
    const domain::model::Bar& bar,
    const StrategyProfile& profile,
    const RuleTemplateRuntimeSupport& ruleTemplateSupport,
    double maxPositionRatio,
    double commissionRate,
    double slippageRate,
    double minVolume,
    BacktestRuntimeState& state);

void processBarWithFactorOverlay(
    engine::BacktestResult& result,
    const domain::model::Bar& bar,
    const StrategyProfile& profile,
    const RuleTemplateRuntimeSupport& ruleTemplateSupport,
    double commissionRate,
    double slippageRate,
    double minVolume,
    BacktestRuntimeState& state,
    std::vector<PendingBuyCandidate>& pendingCandidates);

void executePendingBuys(
    engine::BacktestResult& result,
    const StrategyProfile& profile,
    double maxPositionRatio,
    double commissionRate,
    double slippageRate,
    BacktestRuntimeState& state,
    const FactorOverlayRuntimeSupport& factorOverlaySupport,
    std::vector<PendingBuyCandidate>& pendingCandidates);

void finalizeOpenPositions(
    engine::BacktestResult& result,
    double commissionRate,
    double slippageRate,
    BacktestRuntimeState& state);

} // namespace domain::backtest::runtime