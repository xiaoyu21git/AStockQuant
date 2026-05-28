#pragma once

#include "StrategyEngineModelTypes.h"

namespace domain::backtest::strategy_engine {

struct PositionSnapshot final {
    SymbolId symbolId;
    ShareQuantity quantity;
    CashAmount marketValue;
    Weight targetWeight;

    [[nodiscard]] bool isValid() const
    {
        return symbolId.isValid()
            && quantity.isPositive()
            && marketValue.isNonNegative()
            && targetWeight.isValid();
    }
};

using PositionSnapshotList = ObjectList<PositionSnapshot>;

struct PortfolioState final {
    CashAmount availableCash;
    CashAmount totalEquity;
    PositionSnapshotList positions;

    [[nodiscard]] bool isValid() const
    {
        if (!availableCash.isNonNegative() || !totalEquity.isNonNegative()) {
            return false;
        }

        for (const PositionSnapshot& position : positions) {
            if (!position.isValid()) {
                return false;
            }
        }

        return true;
    }
};

struct RuleCheckResult final {
    bool blocked{false};
    bool forceExit{false};
    RuleDecision decision;

    [[nodiscard]] bool isValid() const
    {
        return !blocked || decision.isValid();
    }
};

struct ExecutionOrder final {
    OrderId orderId;
    SymbolId symbolId;
    OrderSide side{OrderSide::Buy};
    OrderType orderType{OrderType::Market};
    ShareQuantity quantity;
    PriceValue referencePrice;
    TradingDayIndex tradingDay;
    LayerId layerId;

    [[nodiscard]] bool isValid() const
    {
        return orderId.isValid()
            && symbolId.isValid()
            && quantity.isPositive()
            && referencePrice.isPositive()
            && tradingDay.isValid()
            && layerId.isValid();
    }
};

using ExecutionOrderList = ObjectList<ExecutionOrder>;

struct ExecutionFill final {
    OrderId orderId;
    ShareQuantity filledQuantity;
    PriceValue fillPrice;
    CashAmount fee;
    TradingDayIndex tradingDay;

    [[nodiscard]] bool isValid() const
    {
        return orderId.isValid()
            && filledQuantity.isPositive()
            && fillPrice.isPositive()
            && fee.isNonNegative()
            && tradingDay.isValid();
    }
};

using ExecutionFillList = ObjectList<ExecutionFill>;

struct LayerExecutionState final {
    LayerId layerId;
    UniverseId inputUniverseId;
    UniverseId outputUniverseId;
    SymbolIdList selectedSymbols;
    CandidateScoreList candidateScores;
    TargetWeightList targetWeights;
    ExecutionOrderList executionOrders;
    ExecutionFillList executionFills;
    RuleDecisionList ruleDecisions;

    [[nodiscard]] bool isValid() const
    {
        if (!layerId.isValid() || !inputUniverseId.isValid() || !outputUniverseId.isValid()) {
            return false;
        }

        for (const SymbolId& symbolId : selectedSymbols) {
            if (!symbolId.isValid()) {
                return false;
            }
        }

        for (const CandidateScore& candidateScore : candidateScores) {
            if (!candidateScore.isValid()) {
                return false;
            }
        }

        for (const TargetWeight& targetWeight : targetWeights) {
            if (!targetWeight.isValid()) {
                return false;
            }
        }

        for (const ExecutionOrder& executionOrder : executionOrders) {
            if (!executionOrder.isValid()) {
                return false;
            }
        }

        for (const ExecutionFill& executionFill : executionFills) {
            if (!executionFill.isValid()) {
                return false;
            }
        }

        for (const RuleDecision& ruleDecision : ruleDecisions) {
            if (!ruleDecision.isValid()) {
                return false;
            }
        }

        return true;
    }
};

using LayerExecutionStateList = ObjectList<LayerExecutionState>;

struct StrategyContext final {
    StrategyIdentity identity;
    TradingDayIndex tradingDay;
    LayerId activeLayerId;
    UniverseId activeUniverseId;
    RiskSpec riskSpec;
    ExecutionSpec executionSpec;
    PortfolioState portfolioState;

    [[nodiscard]] bool isValid() const
    {
        return identity.isValid()
            && tradingDay.isValid()
            && activeLayerId.isValid()
            && activeUniverseId.isValid()
            && riskSpec.isValid()
            && executionSpec.isValid()
            && portfolioState.isValid();
    }
};

struct EquityCurvePoint final {
    TradingDayIndex tradingDay;
    CashAmount equity;
    ReturnValue periodReturn;

    [[nodiscard]] bool isValid() const
    {
        return tradingDay.isValid() && equity.isNonNegative() && periodReturn.isValid();
    }
};

using EquityCurve = ObjectList<EquityCurvePoint>;

struct BacktestRuntimeSessionState final {
    RunId runId;
    BacktestRunState runState{BacktestRunState::Created};
    TradingDayIndex currentTradingDay;
    CashAmount startingEquity;
    PortfolioState portfolioState;
    EquityCurve equityCurve;
    LayerExecutionStateList layerStates;

    [[nodiscard]] bool isValid() const
    {
        if (!runId.isValid() || !currentTradingDay.isValid() || !startingEquity.isNonNegative()
            || !portfolioState.isValid()) {
            return false;
        }

        for (const EquityCurvePoint& equityCurvePoint : equityCurve) {
            if (!equityCurvePoint.isValid()) {
                return false;
            }
        }

        for (const LayerExecutionState& layerState : layerStates) {
            if (!layerState.isValid()) {
                return false;
            }
        }

        return true;
    }
};

} // namespace domain::backtest::strategy_engine