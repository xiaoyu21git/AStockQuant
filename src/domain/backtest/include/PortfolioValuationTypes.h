#pragma once

#include <cstdint>
#include <vector>
#include "../../types/InstrumentId.h"

namespace astock::domain::backtest::portfolio_valuation {

using ::domain::InstrumentId;

struct QuantityLots final {
    static constexpr int32_t kMinValue = 0;

    int32_t value{kMinValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value >= kMinValue;
    }
};

struct PriceTicks final {
    static constexpr int32_t kMinValue = 1;

    int32_t value{kMinValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value >= kMinValue;
    }
};

struct HoldingState final {
    InstrumentId instrument{};
    QuantityLots longLots{};
    QuantityLots shortLots{};

    [[nodiscard]] bool isValid() const noexcept
    {
        return instrument.isValid() && longLots.isValid() && shortLots.isValid();
    }
};

struct MarketQuote final {
    InstrumentId instrument{};
    PriceTicks lastPrice{};

    [[nodiscard]] bool isValid() const noexcept
    {
        return instrument.isValid() && lastPrice.isValid();
    }
};

struct ValuationSpec final {
    static constexpr int64_t kDefaultMicrosPerTick = 1000000;

    int64_t microsPerTick{kDefaultMicrosPerTick};

    [[nodiscard]] bool isValid() const noexcept
    {
        return microsPerTick > 0;
    }
};

struct PortfolioValuationSummary final {
    int64_t grossLongMicros{0};
    int64_t grossShortMicros{0};
    int64_t netMicros{0};
};

} // namespace astock::domain::backtest::portfolio_valuation
