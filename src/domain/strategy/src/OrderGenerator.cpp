#include "../include/OrderGenerator.h"
#include "../include/StrategyServiceTypes.h"
#include "foundation/market/AStockSymbol.h"
#include "foundation/log/logging.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <unordered_set>

namespace domain::strategy {

using OrderSide = domain::trading::OrderSide;
using OrderRequest = domain::trading::OrderRequest;

// ════════════════════════════════════════════════════════
// 手数/权重计算已收敛到 PositionSizer，本文件仅做编排
// ════════════════════════════════════════════════════════

std::vector<OrderRequest> OrderGenerator::generate(
    const std::vector<OrderRequest>& rawOrders,
    const IPositionProvider& posProvider,
    const std::string& strategyId,
    const std::string& accountId) const
{
    std::vector<OrderRequest> result;
    std::unordered_set<std::string> seenKeys;

    for (const auto& raw : rawOrders) {
        if (!raw.isValid()) continue;

        std::string dedupKey = foundation::market::AStockSymbol::codeOnly(raw.symbol())
            + (raw.side() == OrderSide::Buy ? "_B" : "_S");
        if (seenKeys.count(dedupKey)) continue;
        seenKeys.insert(dedupKey);

        double targetWeight = raw.extensionAs<double>(domain::trading::ExtKey::kTargetWeight, 0.0);
        double signalScore  = raw.extensionAs<double>(domain::trading::ExtKey::kSignalScore, 0.5);
        std::string code = foundation::market::AStockSymbol::codeOnly(raw.symbol());
        std::int64_t currentQty = posProvider.quantityOf(code);

        if (m_sizer->baseQty() == 0) {
            static std::atomic<int> skipDiag{0};
            if (skipDiag.fetch_add(1, std::memory_order_relaxed) < 3)
                INTERNAL_INFO_STREAM << "[OrdGen] SKIP: maxOrderQuantity=0 sym=" << raw.symbol();
            continue;
        }

        OrderDelta delta;

        if (raw.side() == OrderSide::Buy) {
            delta = m_sizer->buyDelta(currentQty, targetWeight);
        } else {
            delta = m_sizer->sellDelta(currentQty, targetWeight,
                                       static_cast<std::int64_t>(raw.quantity()));
        }

        if (delta.deltaQty < kMinLot) continue;

        static std::atomic<int> genDiag{0};
        if (genDiag.fetch_add(1, std::memory_order_relaxed) < 5)
            INTERNAL_INFO_STREAM << "[OrdGen] " << raw.symbol()
                                 << " tw=" << targetWeight
                                 << " deltaQty=" << delta.deltaQty
                                 << " maxOrderQty=" << m_sizer->baseQty()
                                 << " currentQty=" << currentQty
                                 << " targetQty=" << m_sizer->weightToQty(targetWeight);

        OrderRequest order = m_orderBuilder->buildSignalOrder(
            raw.symbol(), raw.side(), 0, delta.deltaQty, signalScore, strategyId, accountId);
        order.setExtension(domain::trading::ExtKey::kSignalIntent,
                           static_cast<std::uint64_t>(delta.intent));
        order.setExtension(domain::trading::ExtKey::kTargetWeight, targetWeight);
        order.setTraceId(raw.traceId());
        result.push_back(std::move(order));
    }

    m_sizer->compressBuys(result);
    return result;
}

} // namespace domain::strategy
