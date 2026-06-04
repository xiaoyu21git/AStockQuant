#pragma once

#include "../../strategy/include/StrategySnapshotTypes.h"
#include "../../types/DomainDate.h"
#include "foundation/Utils/Uuid.h"

#include <cstdint>
#include <string>
#include <vector>

namespace domain::trading {

enum class TradingMode : int {
    Backtest = 0,
    Live = 1,
};

enum class IntentSource : int {
    StrategyExecution = 0,
    FactorBacktest = 1,
    LiveStrategy = 2,
    ManualTrader = 3,
};

enum class OrderSide : int {
    Buy = 0,
    Sell = 1,
    SellShort = 2,
    BuyToCover = 3,
};

enum class OrderType : int {
    Market = 0,
    Limit = 1,
    MarketOnClose = 2,
    NextSessionOpen = 3,
};

enum class ExecutionPriceModel : int {
    MarketOnClose = 0,
    NextSessionOpen = 1,
    Custom = 2,
};

enum class RiskDecisionType : int {
    Pass = 0,
    Warn = 1,
    Block = 2,
    ForceReduce = 3,
    TradingHalt = 4,
};

struct TradeIntent final {
    foundation::utils::Uuid intentId;
    IntentSource source{IntentSource::StrategyExecution};
    strategy::StrategyIdentity strategyIdentity;
    factor::MarketEnvironmentProfile marketProfile{factor::MarketEnvironmentProfile::GENERIC_EQUITY};
    OrderSide side{OrderSide::Buy};
    OrderType orderType{OrderType::Limit};
    strategy::SymbolCode symbol;
    strategy::Quantity quantity;
    strategy::Money referencePrice;
    DomainDate signalDate;
    DomainDate effectiveDate;

    [[nodiscard]] bool isValid() const
    {
        return strategyIdentity.isValid()
            && symbol.isValid()
            && quantity.isPositive()
            && referencePrice.isFinite()
            && effectiveDate.isValid();
    }
};

struct TargetPosition final {
    strategy::SymbolCode symbol;
    strategy::Ratio targetWeight;
    strategy::Quantity targetQuantity;
    strategy::Money referencePrice;

    [[nodiscard]] bool isValid() const
    {
        return symbol.isValid()
            && (targetWeight.isValid() || targetQuantity.isPositive())
            && referencePrice.isFinite();
    }
};

struct TargetPortfolio final {
    foundation::utils::Uuid portfolioId;
    IntentSource source{IntentSource::FactorBacktest};
    std::vector<TargetPosition> positions;
    DomainDate effectiveDate;

    [[nodiscard]] bool isValid() const
    {
        return !positions.empty() && effectiveDate.isValid();
    }
};

struct TradeIntentBatch final {
    foundation::utils::Uuid batchId;
    TradingMode mode{TradingMode::Backtest};
    IntentSource source{IntentSource::StrategyExecution};
    std::vector<TradeIntent> intents;
    std::vector<TargetPosition> targetPositions;

    [[nodiscard]] bool isValid() const
    {
        return !intents.empty() || !targetPositions.empty();
    }
};

struct TradingCostProfile final {
    strategy::Money initialCapital;
    strategy::Ratio commissionRate;
    strategy::Ratio slippageRate;
    strategy::Ratio taxRate;

    [[nodiscard]] bool isValid() const
    {
        return initialCapital.isPositive()
            && commissionRate.isValid()
            && slippageRate.isValid()
            && taxRate.isValid();
    }
};

struct TradingRiskProfile final {
    strategy::Ratio maxPositionRatio;
    strategy::Ratio maxSinglePositionRatio;
    strategy::Ratio maxDrawdownLimit;
    strategy::Ratio stopLossRate;
    int maxBatchOrders{0};
    strategy::Money maxBatchNotional;
    bool enableTradingHalt{false};

    [[nodiscard]] bool isValid() const
    {
        return maxPositionRatio.isValid()
            && maxSinglePositionRatio.isValid()
            && maxDrawdownLimit.isValid()
            && stopLossRate.isValid();
    }
};

struct TradingExecutionProfile final {
    strategy::StrategyExecutionKind executionKind{strategy::StrategyExecutionKind::Standard};
    strategy::PositionSizingMethod positionSizingMethod{strategy::PositionSizingMethod::FixedFraction};
    ExecutionPriceModel priceModel{ExecutionPriceModel::MarketOnClose};
    strategy::ShortSellingMode shortSellingMode{strategy::ShortSellingMode::Disabled};
    strategy::RebalanceFrequencyDays rebalanceFrequencyDays;

    [[nodiscard]] bool isValid() const
    {
        return rebalanceFrequencyDays.isPositive();
    }
};

struct TradingDateWindow final {
    DomainDate startDate;
    DomainDate endDate;

    [[nodiscard]] bool isValid() const
    {
        return startDate.isValid() && endDate.isValid() && startDate <= endDate;
    }
};

struct TradingRuntimeOptions final {
    int maxThreads{1};
    bool enableCache{false};
    int cacheTtlSeconds{0};

    [[nodiscard]] bool isValid() const
    {
        return maxThreads > 0 && cacheTtlSeconds >= 0;
    }
};

struct TradingExecutionContext final {
    TradingMode mode{TradingMode::Backtest};
    factor::MarketEnvironmentProfile marketProfile{factor::MarketEnvironmentProfile::GENERIC_EQUITY};
    TradingDateWindow window;
    TradingCostProfile costProfile;
    TradingRiskProfile riskProfile;
    TradingExecutionProfile executionProfile;
    TradingRuntimeOptions runtimeOptions;
    DiagnosticMap metadata;

    [[nodiscard]] bool isValid() const
    {
        return window.isValid()
            && costProfile.isValid()
            && riskProfile.isValid()
            && executionProfile.isValid()
            && runtimeOptions.isValid();
    }
};

struct TradingPositionSnapshot final {
    strategy::SymbolCode symbol;
    strategy::Quantity quantity;
    strategy::Money lastPrice;
    strategy::Money marketValue;
    strategy::Ratio exposureRatio;

    [[nodiscard]] bool isValid() const
    {
        return symbol.isValid() && lastPrice.isFinite() && marketValue.isFinite();
    }
};

struct TradingAccountSnapshot final {
    strategy::Money availableCash;
    strategy::Money marketValue;
    strategy::Money totalAsset;
    strategy::Money realizedPnl;
    strategy::Money unrealizedPnl;
    DomainDate tradingDate;

    [[nodiscard]] bool isValid() const
    {
        return availableCash.isFinite()
            && marketValue.isFinite()
            && totalAsset.isFinite();
    }
};

struct TradingSnapshot final {
    std::vector<TradingPositionSnapshot> positions;
    TradingAccountSnapshot account;
    std::vector<DiagnosticRecord> openOrders;
    DiagnosticMap diagnostics;

    [[nodiscard]] bool isValid() const
    {
        return account.isValid();
    }
};

struct RiskDecision final {
    RiskDecisionType type{RiskDecisionType::Pass};
    strategy::ReasonCode reasonCode;
    std::string message;
    DiagnosticMap attributes;

    [[nodiscard]] bool isBlocking() const
    {
        return type == RiskDecisionType::Block || type == RiskDecisionType::TradingHalt;
    }
};

struct OrderRef final {
    strategy::OrderId orderId;
    strategy::BatchId batchId;
    strategy::ExecutionScopeId executionScopeId;

    [[nodiscard]] bool isValid() const
    {
        return orderId.isValid() || batchId.isValid() || executionScopeId.isValid();
    }
};

struct OrderPlanItem final {
    strategy::OrderId plannedOrderId;
    strategy::SymbolCode symbol;
    OrderSide side{OrderSide::Buy};
    OrderType orderType{OrderType::Limit};
    strategy::Quantity quantity;
    strategy::Money limitPrice;
    strategy::BatchId batchId;
    strategy::ExecutionScopeId executionScopeId;
    DiagnosticMap metadata;

    [[nodiscard]] bool isValid() const
    {
        return symbol.isValid() && quantity.isPositive() && limitPrice.isFinite();
    }
};

struct OrderPlan final {
    std::vector<OrderPlanItem> items;
    DiagnosticMap diagnostics;

    [[nodiscard]] bool isValid() const
    {
        return !items.empty();
    }
};

struct AcceptedOrder final {
    OrderRef orderRef;
    strategy::SymbolCode symbol;
    OrderSide side{OrderSide::Buy};
    strategy::Quantity quantity;
    strategy::Money acceptedPrice;
    DiagnosticMap metadata;
};

struct FillEvent final {
    OrderRef orderRef;
    strategy::SymbolCode symbol;
    OrderSide side{OrderSide::Buy};
    strategy::Quantity fillQuantity;
    strategy::Money fillPrice;
    DomainDate fillDate;
    DiagnosticMap metadata;

    [[nodiscard]] bool isValid() const
    {
        return orderRef.isValid()
            && symbol.isValid()
            && fillQuantity.isPositive()
            && fillPrice.isFinite();
    }
};

struct CancelEvent final {
    OrderRef orderRef;
    std::string reason;
    DiagnosticMap metadata;
};

struct MarketPriceMark final {
    strategy::SymbolCode symbol;
    strategy::Money price;
    DomainDate tradingDate;

    [[nodiscard]] bool isValid() const
    {
        return symbol.isValid() && price.isFinite() && price.value > 0.0;
    }
};

struct ExecutionVenueResult final {
    std::vector<AcceptedOrder> acceptedOrders;
    std::vector<FillEvent> fills;
    DiagnosticMap diagnostics;
};

struct CancelResult final {
    bool accepted{false};
    std::string message;
    DiagnosticMap diagnostics;
};

struct ExecutionResult final {
    RiskDecision riskDecision;
    OrderPlan orderPlan;
    std::vector<AcceptedOrder> acceptedOrders;
    std::vector<FillEvent> fills;
    TradingSnapshot endingSnapshot;
    DiagnosticMap diagnostics;

    [[nodiscard]] bool isBlocked() const
    {
        return riskDecision.isBlocking();
    }
};

} // namespace domain::trading