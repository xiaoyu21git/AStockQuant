#pragma once

#include "TradingTypes.h"

namespace domain::trading {

class TradingLedger {
public:
    virtual ~TradingLedger() = default;

    virtual void applyOrderAccepted(const AcceptedOrder& order) = 0;
    virtual void applyFill(const FillEvent& fill) = 0;
    virtual void applyCancel(const CancelEvent& cancel) = 0;
    virtual void applyPriceMark(const MarketPriceMark& mark) = 0;

    [[nodiscard]] virtual TradingSnapshot snapshot() const = 0;
};

} // namespace domain::trading