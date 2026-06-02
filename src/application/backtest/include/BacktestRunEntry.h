#pragma once

#include "BacktestRuntime.hpp"

namespace application::backtest {

class BacktestRunEntry final {
public:
    [[nodiscard]] static RunBacktestIngressResult runBacktest(
        const ExistingModuleSlots& slots,
        RunSpec spec);

    [[nodiscard]] static RunBacktestIngressResult runBacktestWithFillSideMode(
        const ExistingModuleSlots& slots,
        RunSpec spec,
        FillOrderSideMode fillSideMode);
};

} // namespace application::backtest