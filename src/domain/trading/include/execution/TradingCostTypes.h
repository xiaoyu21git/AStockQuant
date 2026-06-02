#pragma once

#include <cstdint>

namespace astock::domain::trading::trading_cost {

struct PriceTicks final {
    static constexpr int32_t kInvalidValue = 0;

    int32_t value{kInvalidValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value > kInvalidValue;
    }
};

struct QuantityLots final {
    static constexpr int32_t kInvalidValue = 0;

    int32_t value{kInvalidValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value > kInvalidValue;
    }
};

struct BpsRate final {
    static constexpr int32_t kMinValue = 0;
    static constexpr int32_t kMaxValue = 10000;

    int32_t value{kMinValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value >= kMinValue && value <= kMaxValue;
    }
};

struct CashMicros final {
    static constexpr int64_t kMinValue = 0;

    int64_t value{kMinValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value >= kMinValue;
    }
};

enum class OrderSide {
    Buy,
    Sell
};

struct TradeFill final {
    PriceTicks price{};
    QuantityLots quantity{};
    OrderSide side{OrderSide::Buy};

    [[nodiscard]] bool isValid() const noexcept
    {
        return price.isValid() && quantity.isValid();
    }
};

struct TradingCostSpec final {
    BpsRate commissionBps{};
    BpsRate slippageBps{};
    BpsRate sellTaxBps{};

    [[nodiscard]] bool isValid() const noexcept
    {
        return commissionBps.isValid() && slippageBps.isValid() && sellTaxBps.isValid();
    }
};

struct TradingCostBreakdown final {
    CashMicros commission{};
    CashMicros slippage{};
    CashMicros tax{};
    CashMicros total{};
};

} // namespace astock::domain::trading::trading_cost

