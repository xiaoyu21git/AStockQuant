#pragma once

#include "ExecutionVenue.h"

namespace domain::trading {

class BacktestExecutionVenue final : public ExecutionVenue {
public:
    [[nodiscard]] ExecutionVenueResult submit(const OrderPlan& plan,
                                              const TradingExecutionContext& context) override;

    [[nodiscard]] CancelResult cancel(const OrderRef& orderRef) override;

    [[nodiscard]] QVector<FillEvent> poll() override;
};

} // namespace domain::trading