#pragma once

#include "TradingLedger.h"

namespace domain::trading {

class InMemoryTradingLedger final : public TradingLedger {
public:
    explicit InMemoryTradingLedger(const TradingSnapshot& initialSnapshot = TradingSnapshot{});

    void applyOrderAccepted(const AcceptedOrder& order) override;
    void applyFill(const FillEvent& fill) override;
    void applyCancel(const CancelEvent& cancel) override;
    void applyPriceMark(const MarketPriceMark& mark) override;

    [[nodiscard]] TradingSnapshot snapshot() const override;

private:
    TradingSnapshot snapshot_;
};

} // namespace domain::trading