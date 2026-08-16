#include "EngineListenerAssembler.h"

#include "PositionBook.h"
#include "MarketDataService.h"
#include "foundation/log/logging.hpp"

#include <cstdint>
#include <vector>

namespace domain::strategy {

void BookKeepingListener::onOrders(const std::vector<OrderRequest>& orders) {
    if (m_positionBook) {
        const std::int64_t today = domain::market::MarketDataService::instance().activeTradingDay();
        for (const auto& o : orders) {
            if (!o.isValid()) continue;
            const std::int64_t signedQty = o.side() == OrderSide::Buy
                ? static_cast<std::int64_t>(o.quantity())
                : -static_cast<std::int64_t>(o.quantity());
            m_positionBook->applyOrder(o.symbol(), signedQty, today);
        }
    }
    if (m_raw) {
        m_raw->onOrders(orders);
    }
}

IOrderListener* EngineListenerAssembler::install(IOrderListener* raw) {
    m_wrapper.rebind(raw);
    return &m_wrapper;
}

} // namespace domain::strategy
