#include "../include/DefaultTradingCore.h"

#include "../include/BacktestExecutionVenue.h"
#include "../include/DefaultRiskRuleChain.h"
#include "../include/DefaultTradingPlanner.h"
#include "../include/InMemoryTradingLedger.h"

#include <memory>

namespace domain::trading {

DefaultTradingCore::DefaultTradingCore(std::shared_ptr<TradingPlanner> planner,
                                       std::shared_ptr<TradingLedger> ledger,
                                       std::shared_ptr<RiskRuleChain> riskRuleChain,
                                       std::shared_ptr<ExecutionVenue> executionVenue)
    : planner_(std::move(planner))
    , ledger_(std::move(ledger))
    , riskRuleChain_(std::move(riskRuleChain))
    , executionVenue_(std::move(executionVenue))
{
    if (!planner_) {
        planner_ = std::make_shared<DefaultTradingPlanner>();
    }
    if (!ledger_) {
        ledger_ = std::make_shared<InMemoryTradingLedger>();
    }
    if (!riskRuleChain_) {
        riskRuleChain_ = std::make_shared<DefaultRiskRuleChain>();
    }
    if (!executionVenue_) {
        executionVenue_ = std::make_shared<BacktestExecutionVenue>();
    }
}

ExecutionResult DefaultTradingCore::execute(const TradeIntentBatch& batch,
                                            const TradingExecutionContext& context)
{
    ExecutionResult result;
    const TradingSnapshot startSnapshot = ledger_->snapshot();

    result.riskDecision = riskRuleChain_->evaluate(batch, startSnapshot, context.riskProfile);
    if (result.riskDecision.isBlocking()) {
        result.endingSnapshot = startSnapshot;
        result.diagnostics.insert(QStringLiteral("status"), QStringLiteral("blocked_by_risk"));
        return result;
    }

    result.orderPlan = planner_->buildOrderPlan(batch, startSnapshot, context);
    if (!result.orderPlan.isValid()) {
        result.endingSnapshot = startSnapshot;
        result.diagnostics.insert(QStringLiteral("status"), QStringLiteral("no_order_plan"));
        return result;
    }

    const ExecutionVenueResult venueResult = executionVenue_->submit(result.orderPlan, context);
    result.acceptedOrders = venueResult.acceptedOrders;
    result.fills = venueResult.fills;
    result.diagnostics = venueResult.diagnostics;

    for (const AcceptedOrder& order : result.acceptedOrders) {
        ledger_->applyOrderAccepted(order);
    }
    for (const FillEvent& fill : result.fills) {
        ledger_->applyFill(fill);
    }

    result.endingSnapshot = ledger_->snapshot();
    return result;
}

void DefaultTradingCore::applyFill(const FillEvent& fill)
{
    ledger_->applyFill(fill);
}

void DefaultTradingCore::markToMarket(const MarketPriceMark& mark)
{
    ledger_->applyPriceMark(mark);
}

TradingSnapshot DefaultTradingCore::snapshot() const
{
    return ledger_->snapshot();
}

} // namespace domain::trading