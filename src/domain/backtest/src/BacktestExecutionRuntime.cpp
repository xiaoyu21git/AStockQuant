#include "BacktestExecutionRuntime.h"

#include <foundation.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace {

using namespace domain::backtest::runtime;

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
                   const std::string& note)
{
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

} // namespace

namespace domain::backtest::runtime {

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

void processBarImmediate(
    engine::BacktestResult& result,
    const domain::model::Bar& bar,
    const StrategyProfile& profile,
    const RuleTemplateRuntimeSupport& ruleTemplateSupport,
    double maxPositionRatio,
    double commissionRate,
    double slippageRate,
    double minVolume,
    BacktestRuntimeState& state)
{
    if (bar.close <= 0.0) {
        return;
    }

    foundation::Timestamp timestamp = foundation::Timestamp::from_seconds(bar.time / 1000);
    SymbolRuntimeState& symbolState = state.symbolStates[bar.symbol];
    symbolState.closes.push_back(bar.close);
    state.latestPrices[bar.symbol] = bar.close;
    state.latestTimestamps[bar.symbol] = timestamp;

    if (minVolume > 0.0 && bar.volume > 0.0 && bar.volume < minVolume) {
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
                          commissionRate,
                          slippageRate,
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

        const double tradePrice = slippageRate > 0.0 ? bar.close * (1.0 + slippageRate) : bar.close;
        const double currentEquity = calculatePortfolioEquity(state);
        const double allocationRatio = (std::min)((std::max)(profile.positionSizeRatio, 0.01), (std::max)(maxPositionRatio, 0.01));
        const double targetCash = currentEquity * allocationRatio;
        const double investCash = (std::min)(state.cash, targetCash);
        const double rawQuantity = tradePrice > 0.0 ? investCash / tradePrice : 0.0;
        const double quantity = std::floor(rawQuantity / 100.0) * 100.0;

        if (quantity > 0.0) {
            const double entryCommission = commissionRate > 0.0 ? tradePrice * quantity * commissionRate : 0.0;
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
                      commissionRate,
                      slippageRate,
                      symbolState,
                      state,
                      "signal exit");
    }

    result.update_equity_curve(timestamp, calculatePortfolioEquity(state));
}

void processBarWithFactorOverlay(
    engine::BacktestResult& result,
    const domain::model::Bar& bar,
    const StrategyProfile& profile,
    const RuleTemplateRuntimeSupport& ruleTemplateSupport,
    double commissionRate,
    double slippageRate,
    double minVolume,
    BacktestRuntimeState& state,
    std::vector<PendingBuyCandidate>& pendingCandidates)
{
    if (bar.close <= 0.0) {
        return;
    }

    foundation::Timestamp timestamp = foundation::Timestamp::from_seconds(bar.time / 1000);
    SymbolRuntimeState& symbolState = state.symbolStates[bar.symbol];
    symbolState.closes.push_back(bar.close);
    state.latestPrices[bar.symbol] = bar.close;
    state.latestTimestamps[bar.symbol] = timestamp;

    if (minVolume > 0.0 && bar.volume > 0.0 && bar.volume < minVolume) {
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
                          commissionRate,
                          slippageRate,
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
                      commissionRate,
                      slippageRate,
                      symbolState,
                      state,
                      "signal exit");
    }

    result.update_equity_curve(timestamp, calculatePortfolioEquity(state));
}

void executePendingBuys(
    engine::BacktestResult& result,
    const StrategyProfile& profile,
    double maxPositionRatio,
    double commissionRate,
    double slippageRate,
    BacktestRuntimeState& state,
    const FactorOverlayRuntimeSupport& factorOverlaySupport,
    std::vector<PendingBuyCandidate>& pendingCandidates)
{
    if (pendingCandidates.empty()) {
        return;
    }

    applyFactorOverlayScores(factorOverlaySupport, pendingCandidates);

    int availableSlots = resolveAvailablePendingBuySlots(
        factorOverlaySupport,
        state,
        static_cast<int>(pendingCandidates.size()));

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

        const double tradePrice = slippageRate > 0.0 ? candidate.bar.close * (1.0 + slippageRate) : candidate.bar.close;
        const double currentEquity = calculatePortfolioEquity(state);
        const double allocationRatio = (std::min)((std::max)(profile.positionSizeRatio, 0.01), (std::max)(maxPositionRatio, 0.01));
        const double targetCash = currentEquity * allocationRatio;
        const double investCash = (std::min)(state.cash, targetCash);
        const double rawQuantity = tradePrice > 0.0 ? investCash / tradePrice : 0.0;
        const double quantity = std::floor(rawQuantity / 100.0) * 100.0;

        if (quantity <= 0.0) {
            continue;
        }

        const double entryCommission = commissionRate > 0.0 ? tradePrice * quantity * commissionRate : 0.0;
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

void finalizeOpenPositions(
    engine::BacktestResult& result,
    double commissionRate,
    double slippageRate,
    BacktestRuntimeState& state)
{
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

} // namespace domain::backtest::runtime