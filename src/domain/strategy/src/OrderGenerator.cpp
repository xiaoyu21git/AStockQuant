#include "../include/OrderGenerator.h"
#include "../include/StrategyServiceTypes.h"  // SignalIntent
#include "../../../engine/include/AccountEngine.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace domain::strategy {

using OrderSide = domain::trading::OrderSide;

std::vector<domain::trading::OrderRequest> OrderGenerator::generate(
    const std::vector<domain::trading::OrderRequest>& rawOrders,
    const IPositionProvider& posProvider,
    const engine::AccountInfo& account,
    double priceForWeight) const
{
    using OrderRequest = domain::trading::OrderRequest;
    using OrderSide = domain::trading::OrderSide;

    std::vector<OrderRequest> result;
    constexpr std::int64_t kMinLot = 100;
    std::unordered_set<std::string> seenKeys;  // 同标的+方向去重

    auto stripExchange = [](const std::string& sym) -> std::string {
        auto dot = sym.find('.');
        return (dot != std::string::npos) ? sym.substr(0, dot) : sym;
    };

    for (const auto& raw : rawOrders) {
        if (!raw.isValid()) continue;

        // 同标的+方向去重
        std::string dedupKey = stripExchange(raw.symbol())
            + (raw.side() == OrderSide::Buy ? "_B" : "_S");
        if (seenKeys.count(dedupKey)) continue;
        seenKeys.insert(dedupKey);

        double targetWeight = raw.extensionAs<double>(domain::trading::ExtKey::kTargetWeight, 0.0);
        double signalScore  = raw.extensionAs<double>(domain::trading::ExtKey::kSignalScore, 0.5);
        std::string code = stripExchange(raw.symbol());

        std::int64_t currentQty = posProvider.quantityOf(code);

        if (priceForWeight <= 0 || account.totalAsset <= 0) continue;

        double currentW = static_cast<double>(currentQty) * priceForWeight / account.totalAsset;
        std::int64_t targetQty = static_cast<std::int64_t>(
            targetWeight * account.totalAsset / priceForWeight / 100.0) * 100;

        std::int64_t deltaQty = 0;
        SignalIntent intent = SignalIntent::KEEP;
        OrderSide side = raw.side();

        if (side == OrderSide::Buy) {
            if (currentW < 0.001) {
                intent = SignalIntent::OPEN; deltaQty = targetQty;
            } else if (targetWeight > currentW) {
                intent = SignalIntent::ADD; deltaQty = targetQty - currentQty;
            } else {
                continue;  // Buy 但 target <= current → 矛盾，丢弃
            }
        } else { // Sell
            if (currentQty <= 0) continue;  // 无持仓，丢弃
            if (targetWeight >= currentW) continue;  // Sell 但 target >= current → 矛盾
            if (targetWeight > 0.0) {
                if (targetQty < kMinLot) {
                    intent = SignalIntent::CLOSE;
                    deltaQty = currentQty;
                } else {
                    intent = SignalIntent::REDUCE;
                    deltaQty = currentQty - targetQty;
                }
            } else {
                intent = SignalIntent::CLOSE; deltaQty = currentQty;
            }
        }

        if (deltaQty < kMinLot) continue;  // 不足一手

        OrderRequest order = m_orderBuilder->buildSignalOrder(
            raw.symbol(), side, 0, deltaQty, signalScore);
        order.setExtension(domain::trading::ExtKey::kSignalIntent, static_cast<std::uint64_t>(intent));
        order.setExtension(domain::trading::ExtKey::kTargetWeight, targetWeight);  // 保留原始权重用于上限压缩
        result.push_back(std::move(order));
    }

    // ── 总敞口上限: 超出100%时按比例压缩全部买单 ──
    {
        double totalWeight = 0.0;
        for (const auto& o : result) {
            if (o.side() != OrderSide::Buy) continue;
            totalWeight += o.extensionAs<double>(domain::trading::ExtKey::kTargetWeight, 0.0);
        }
        if (totalWeight > 1.0) {
            double scale = 1.0 / totalWeight;
            for (auto& o : result) {
                if (o.side() != OrderSide::Buy) continue;
                double w = o.extensionAs<double>(domain::trading::ExtKey::kTargetWeight, 0.0);
                std::int64_t newQty = static_cast<std::int64_t>(o.quantity() * scale / kMinLot) * kMinLot;
                if (newQty >= kMinLot) o.setQuantity(newQty);
                o.setExtension(domain::trading::ExtKey::kTargetWeight, w * scale);
            }
        }
    }

    return result;
}

} // namespace domain::strategy
