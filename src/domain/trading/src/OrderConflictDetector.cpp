#include "OrderConflictDetector.h"

#include <unordered_map>

namespace domain::trading {

ConflictResult OrderConflictDetector::detect(const std::string& symbol,
                                              OrderSide requestedSide,
                                              const std::vector<TradeOrder>& pendingOrders) const {
    if (pendingOrders.empty() || symbol.empty()) {
        return {false};
    }
    for (const auto& order : pendingOrders) {
        if (order.symbol == symbol && order.side != requestedSide) {
            return {
                true,
                order.symbol,
                order.side,
                "PENDING",
                "",
                "reverse_order_pending"
            };
        }
    }
    return {false};
}

} // namespace domain::trading