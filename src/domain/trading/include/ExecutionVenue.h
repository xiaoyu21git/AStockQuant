#pragma once

#include "TradingTypes.h"

#include <vector>

namespace domain::trading {

class ExecutionVenue {
public:
    virtual ~ExecutionVenue() = default;

    [[nodiscard]] virtual ExecutionVenueResult submit(const OrderPlan& plan,
                                                       const TradingExecutionContext& context) = 0;

    [[nodiscard]] virtual CancelResult cancel(const OrderRef& orderRef) = 0;

    [[nodiscard]] virtual std::vector<FillEvent> poll() = 0;
};

} // namespace domain::trading