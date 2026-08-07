#include "../include/OrderGenerator.h"
#include "../include/StrategyServiceTypes.h"
#include "../../../engine/include/AccountEngine.h"
#include "foundation/market/AStockSymbol.h"
#include "foundation/log/logging.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace domain::strategy {

using OrderSide = domain::trading::OrderSide;
using OrderRequest = domain::trading::OrderRequest;

namespace {
constexpr std::int64_t kMinLot = 100;

std::int64_t weightToQty(double weight, double totalAsset, double priceForWeight) {
    if (priceForWeight <= 0 || totalAsset <= 0) return 0;
    return static_cast<std::int64_t>(weight * totalAsset / priceForWeight / kMinLot) * kMinLot;
}

double qtyToWeight(std::int64_t qty, double totalAsset, double priceForWeight) {
    if (priceForWeight <= 0 || totalAsset <= 0) return 0.0;
    return static_cast<double>(qty) * priceForWeight / totalAsset;
}
} // namespace

OrderGenerator::OrderDelta OrderGenerator::computeBuyDelta(
    std::int64_t currentQty, double currentWeight,
    double targetWeight, double priceForWeight, double totalAsset) const
{
    OrderDelta result;
    if (currentWeight < 0.001) {
        result.intent = SignalIntent::OPEN;
        result.deltaQty = weightToQty(targetWeight, totalAsset, priceForWeight);
    } else if (targetWeight > currentWeight) {
        result.intent = SignalIntent::ADD;
        std::int64_t targetQty = weightToQty(targetWeight, totalAsset, priceForWeight);
        result.deltaQty = targetQty - currentQty;
    }
    return result;  // targetWeight <= currentWeight → deltaQty=0, caller discards
}

OrderGenerator::OrderDelta OrderGenerator::computeSellDelta(
    std::int64_t currentQty, double currentWeight,
    double targetWeight, double priceForWeight, double totalAsset,
    std::int64_t requestedQty) const
{
    OrderDelta result;
    if (currentQty <= 0) return result;
    if (targetWeight >= currentWeight) return result;  // 矛盾

    if (targetWeight > 0.0) {
        // 策略卖出: targetWeight 表示期望的新持仓权重
        std::int64_t targetQty = weightToQty(targetWeight, totalAsset, priceForWeight);
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
    const engine::AccountInfo& account,
    double priceForWeight,
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

        if (priceForWeight <= 0 || account.totalAsset <= 0) {
            static int skipDiag = 0;
            if (++skipDiag <= 3)
                INTERNAL_INFO_STREAM << "[OrdGen] SKIP: priceForWeight=" << priceForWeight
                                     << " totalAsset=" << account.totalAsset
                                     << " sym=" << raw.symbol();
            continue;
        }

        double currentWeight = qtyToWeight(currentQty, account.totalAsset, priceForWeight);
        OrderDelta delta;

        if (raw.side() == OrderSide::Buy) {
            delta = computeBuyDelta(currentQty, currentWeight, targetWeight,
                                    priceForWeight, account.totalAsset);
        } else {
            delta = computeSellDelta(currentQty, currentWeight, targetWeight,
                                     priceForWeight, account.totalAsset,
                                     static_cast<std::int64_t>(raw.quantity()));
        }

        if (delta.deltaQty < kMinLot) continue;

        static int genDiag = 0;
        if (++genDiag <= 5)
            INTERNAL_INFO_STREAM << "[OrdGen] " << raw.symbol()
                                 << " tw=" << targetWeight
                                 << " deltaQty=" << delta.deltaQty
                                 << " priceForWeight=" << priceForWeight
                                 << " totalAsset=" << account.totalAsset
                                 << " w2q=" << weightToQty(targetWeight, account.totalAsset, priceForWeight);

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
