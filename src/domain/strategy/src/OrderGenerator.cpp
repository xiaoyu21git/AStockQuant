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

namespace {
constexpr std::int64_t kMinLot = 100;

/// @brief 权重 → 目标股数（按权重建仓基数，取整到整手）
inline std::int64_t weightToTargetQty(double weight, std::uint32_t baseQty) noexcept {
    if (baseQty == 0) return 0;
    std::int64_t qty = static_cast<std::int64_t>(weight * static_cast<double>(baseQty));
    qty = qty / kMinLot * kMinLot;
    if (qty < kMinLot) qty = kMinLot;
    return qty;
}
} // namespace

OrderGenerator::OrderDelta OrderGenerator::computeBuyDelta(
    std::int64_t currentQty, double targetWeight,
    std::uint32_t maxOrderQuantity) const
{
    OrderDelta result;
    std::int64_t targetQty = weightToTargetQty(targetWeight, maxOrderQuantity);

    if (currentQty < kMinLot) {
        // 无现有持仓 → 新开仓
        result.intent = SignalIntent::OPEN;
        result.deltaQty = targetQty;
    } else if (targetQty > currentQty) {
        // 已有持仓, 但目标数量更大 → 加仓
        result.intent = SignalIntent::ADD;
        result.deltaQty = targetQty - currentQty;
    }
    return result;  // targetQty <= currentQty → deltaQty=0, caller discards
}

OrderGenerator::OrderDelta OrderGenerator::computeSellDelta(
    std::int64_t currentQty, double targetWeight,
    std::uint32_t maxOrderQuantity,
    std::int64_t requestedQty) const
{
    OrderDelta result;
    if (currentQty <= 0) return result;

    if (targetWeight > 0.0) {
        // 策略卖出: targetWeight 表示期望的新持仓权重
        std::int64_t targetQty = weightToTargetQty(targetWeight, maxOrderQuantity);
        if (targetQty >= currentQty) return result;  // 目标不低于当前 → 不卖
        if (targetQty < kMinLot) {
            result.intent = SignalIntent::CLOSE;
            result.deltaQty = currentQty;
        } else {
            result.intent = SignalIntent::REDUCE;
            result.deltaQty = currentQty - targetQty;
        }
    } else {
        // 规则出场: 尊重显式数量; 策略清仓: 全卖
        if (requestedQty > 0 && requestedQty < currentQty) {
            result.intent = SignalIntent::REDUCE;
            result.deltaQty = requestedQty;
        } else {
            result.intent = SignalIntent::CLOSE;
            result.deltaQty = currentQty;
        }
    }
    return result;
}

void OrderGenerator::compressBuyTotalWeight(std::vector<OrderRequest>& orders) const {
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

std::vector<OrderRequest> OrderGenerator::generate(
    const std::vector<OrderRequest>& rawOrders,
    const IPositionProvider& posProvider,
    std::uint32_t maxOrderQuantity,
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

        if (maxOrderQuantity == 0) {
            static std::atomic<int> skipDiag{0};
            if (skipDiag.fetch_add(1, std::memory_order_relaxed) < 3)
                INTERNAL_INFO_STREAM << "[OrdGen] SKIP: maxOrderQuantity=0 sym=" << raw.symbol();
            continue;
        }

        OrderDelta delta;

        if (raw.side() == OrderSide::Buy) {
            delta = computeBuyDelta(currentQty, targetWeight, maxOrderQuantity);
        } else {
            delta = computeSellDelta(currentQty, targetWeight, maxOrderQuantity,
                                     static_cast<std::int64_t>(raw.quantity()));
        }

        if (delta.deltaQty < kMinLot) continue;

        static std::atomic<int> genDiag{0};
        if (genDiag.fetch_add(1, std::memory_order_relaxed) < 5)
            INTERNAL_INFO_STREAM << "[OrdGen] " << raw.symbol()
                                 << " tw=" << targetWeight
                                 << " deltaQty=" << delta.deltaQty
                                 << " maxOrderQty=" << maxOrderQuantity
                                 << " currentQty=" << currentQty
                                 << " targetQty=" << weightToTargetQty(targetWeight, maxOrderQuantity);

        OrderRequest order = m_orderBuilder->buildSignalOrder(
            raw.symbol(), raw.side(), 0, delta.deltaQty, signalScore, strategyId, accountId);
        order.setExtension(domain::trading::ExtKey::kSignalIntent,
                           static_cast<std::uint64_t>(delta.intent));
        order.setExtension(domain::trading::ExtKey::kTargetWeight, targetWeight);
        result.push_back(std::move(order));
    }

    compressBuyTotalWeight(result);
    return result;
}

} // namespace domain::strategy
