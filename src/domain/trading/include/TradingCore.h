#pragma once

#include "TradingTypes.h"

namespace domain::trading {

class TradingCore {
public:
    virtual ~TradingCore() = default;

    [[nodiscard]] virtual ExecutionResult execute(const TradeIntentBatch& batch,
                                                  const TradingExecutionContext& context) = 0;

    virtual void applyFill(const FillEvent& fill) = 0;
    virtual void markToMarket(const MarketPriceMark& mark) = 0;

    [[nodiscard]] virtual TradingSnapshot snapshot() const = 0;
};

} // namespace domain::trading