#pragma once

#include <optional>
#include <vector>

#include "OrderRoutingTypes.h"

namespace astock::domain::backtest::order_routing {

enum class OrderRoutingError {
    None,
    InvalidInput,
    InvalidIntent,
    OrderDeltaExceeded
};

struct OrderRoutingResult final {
    OrderRoutingError error{OrderRoutingError::None};
    std::optional<RoutedOrderSet> value;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == OrderRoutingError::None && value.has_value();
    }
};

class IOrderRouter {
public:
    virtual ~IOrderRouter() = default;

    virtual OrderRoutingResult route(RoutingSpec spec,
                                     std::vector<ExecutionIntent> intents) const = 0;
};

class SimpleOrderRouter final : public IOrderRouter {
public:
    OrderRoutingResult route(RoutingSpec spec,
                             std::vector<ExecutionIntent> intents) const override;
};

} // namespace astock::domain::backtest::order_routing
