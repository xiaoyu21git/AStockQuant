#include "PositionSizer.h"

#include <algorithm>
#include <cmath>

namespace domain::strategy {

// ═══════════════════════════════════════════════════════════════
// weightToQty
// ═══════════════════════════════════════════════════════════════

std::int64_t PositionSizer::weightToQty(double weight) const noexcept {
    if (m_baseQty == 0) return 0;
    std::int64_t qty = static_cast<std::int64_t>(weight * static_cast<double>(m_baseQty));
    qty = qty / kMinLot * kMinLot;            // 取整到整手
    if (qty < kMinLot) qty = kMinLot;         // 最低 1 手
    return qty;
}

// ═══════════════════════════════════════════════════════════════
// buyDelta
// ═══════════════════════════════════════════════════════════════

OrderDelta PositionSizer::buyDelta(
    std::int64_t currentQty, double targetWeight) const
{
    OrderDelta result;
    std::int64_t targetQty = weightToQty(targetWeight);

    if (currentQty < kMinLot) {
        result.intent   = SignalIntent::OPEN;
        result.deltaQty = targetQty;
    } else if (targetQty > currentQty) {
        result.intent   = SignalIntent::ADD;
        result.deltaQty = targetQty - currentQty;
    }
    // else: targetQty <= currentQty → deltaQty=0, caller discards
    return result;
}

// ═══════════════════════════════════════════════════════════════
// sellDelta
// ═══════════════════════════════════════════════════════════════

OrderDelta PositionSizer::sellDelta(
    std::int64_t currentQty, double targetWeight,
    std::int64_t requestedQty) const
{
    OrderDelta result;
    if (currentQty <= 0) return result;

    if (targetWeight > 0.0) {
        // 策略信号: targetWeight 表示期望的新持仓权重
        std::int64_t targetQty = weightToQty(targetWeight);
        if (targetQty >= currentQty) return result;
        if (targetQty < kMinLot) {
            result.intent   = SignalIntent::CLOSE;
            result.deltaQty = currentQty;
        } else {
            result.intent   = SignalIntent::REDUCE;
            result.deltaQty = currentQty - targetQty;
        }
    } else {
        // 规则出场 / 策略清仓
        if (requestedQty > 0 && requestedQty < currentQty) {
            result.intent   = SignalIntent::REDUCE;
            result.deltaQty = requestedQty;
        } else {
            result.intent   = SignalIntent::CLOSE;
            result.deltaQty = currentQty;
        }
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════
// compressBuys
// ═══════════════════════════════════════════════════════════════

void PositionSizer::compressBuys(
    std::vector<domain::trading::OrderRequest>& orders) const
{
    using OrderSide = domain::trading::OrderSide;

    double totalWeight = 0.0;
    for (const auto& o : orders) {
        if (o.side() != OrderSide::Buy) continue;
        totalWeight += o.extensionAs<double>(domain::trading::ExtKey::kTargetWeight, 0.0);
    }
    if (totalWeight <= 1.0) return;

    double scale = 1.0 / totalWeight;
    for (auto& o : orders) {
        if (o.side() != OrderSide::Buy) continue;
        double w = o.extensionAs<double>(domain::trading::ExtKey::kTargetWeight, 0.0);
        std::int64_t newQty = static_cast<std::int64_t>(o.quantity() * scale / kMinLot) * kMinLot;
        if (newQty >= kMinLot) o.setQuantity(newQty);
        o.setExtension(domain::trading::ExtKey::kTargetWeight, w * scale);
    }
}

} // namespace domain::strategy
