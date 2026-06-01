#pragma once

#include <cstdint>
#include <optional>

#include "TradingCostTypes.h"

namespace astock::domain::backtest::trading_cost {

enum class TradingCostError {
    None,
    InvalidInput
};

struct TradingCostResult final {
    TradingCostError error{TradingCostError::None};
    std::optional<TradingCostBreakdown> value;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == TradingCostError::None && value.has_value();
    }
};

class ITradingCostModel {
public:
    virtual ~ITradingCostModel() = default;

    virtual TradingCostResult calculate(TradeFill fill, TradingCostSpec spec) const = 0;
};

class LinearBpsTradingCostModel final : public ITradingCostModel {
public:
    static constexpr int64_t kMicrosPerPriceTick = 1000000;
    static constexpr int64_t kBpsBase = 10000;

    TradingCostResult calculate(TradeFill fill, TradingCostSpec spec) const override;

private:
    static bool checkedMultiplyInt64(int64_t left, int64_t right, int64_t* out);
    static bool checkedAddInt64(int64_t left, int64_t right, int64_t* out);
};

} // namespace astock::domain::backtest::trading_cost
