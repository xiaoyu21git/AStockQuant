#include "TradingCostModel.h"

#include <limits>
#include <utility>

namespace astock::domain::trading::trading_cost {

bool LinearBpsTradingCostModel::checkedMultiplyInt64(int64_t left, int64_t right, int64_t* out)
{
    if (out == nullptr) {
        return false;
    }

    if (left == 0 || right == 0) {
        *out = 0;
        return true;
    }

    if (left > std::numeric_limits<int64_t>::max() / right) {
        return false;
    }

    *out = left * right;
    return true;
}

bool LinearBpsTradingCostModel::checkedAddInt64(int64_t left, int64_t right, int64_t* out)
{
    if (out == nullptr) {
        return false;
    }

    if (right > 0 && left > std::numeric_limits<int64_t>::max() - right) {
        return false;
    }

    *out = left + right;
    return true;
}

TradingCostResult LinearBpsTradingCostModel::calculate(TradeFill fill, TradingCostSpec spec) const
{
    if (!fill.isValid() || !spec.isValid()) {
        return TradingCostResult{TradingCostError::InvalidInput, std::nullopt};
    }

    int64_t notionalTicks = 0;
    if (!LinearBpsTradingCostModel::checkedMultiplyInt64(static_cast<int64_t>(fill.price.value),
                                                         static_cast<int64_t>(fill.quantity.value),
                                                         &notionalTicks)) {
        return TradingCostResult{TradingCostError::InvalidInput, std::nullopt};
    }

    int64_t notionalMicros = 0;
    if (!LinearBpsTradingCostModel::checkedMultiplyInt64(notionalTicks, kMicrosPerPriceTick, &notionalMicros)) {
        return TradingCostResult{TradingCostError::InvalidInput, std::nullopt};
    }

    const int64_t commissionMicros = (notionalMicros * static_cast<int64_t>(spec.commissionBps.value)) / kBpsBase;
    const int64_t slippageMicros = (notionalMicros * static_cast<int64_t>(spec.slippageBps.value)) / kBpsBase;

    int64_t taxMicros = 0;
    if (fill.side == OrderSide::Sell) {
        taxMicros = (notionalMicros * static_cast<int64_t>(spec.sellTaxBps.value)) / kBpsBase;
    }

    int64_t totalMicros = 0;
    if (!LinearBpsTradingCostModel::checkedAddInt64(commissionMicros, slippageMicros, &totalMicros)) {
        return TradingCostResult{TradingCostError::InvalidInput, std::nullopt};
    }
    if (!LinearBpsTradingCostModel::checkedAddInt64(totalMicros, taxMicros, &totalMicros)) {
        return TradingCostResult{TradingCostError::InvalidInput, std::nullopt};
    }

    TradingCostBreakdown breakdown;
    breakdown.commission = CashMicros{commissionMicros};
    breakdown.slippage = CashMicros{slippageMicros};
    breakdown.tax = CashMicros{taxMicros};
    breakdown.total = CashMicros{totalMicros};

    return TradingCostResult{TradingCostError::None, std::move(breakdown)};
}

} // namespace astock::domain::trading::trading_cost



