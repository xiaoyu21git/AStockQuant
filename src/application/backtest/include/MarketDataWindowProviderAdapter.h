#pragma once

#include "BacktestInterfaces.hpp"

namespace application::backtest {

class WindowedMarketDataProviderAdapter final : public IMarketDataWindowProvider {
public:
    [[nodiscard]] StageResult loadWindowData(RunContext& context) const override;

private:
    static constexpr int32_t kCrossSectionWarmupDays = 20;
    static constexpr int32_t kTimeSeriesWarmupDays = 60;
};

} // namespace application::backtest