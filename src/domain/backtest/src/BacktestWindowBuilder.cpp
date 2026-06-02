#include "BacktestWindowBuilder.h"

namespace astock::domain::backtest::windowing {

FixedWarmupDaysPolicy::FixedWarmupDaysPolicy(DayCount crossSectionWarmup, DayCount timeSeriesWarmup)
    : crossSectionWarmup_(crossSectionWarmup)
    , timeSeriesWarmup_(timeSeriesWarmup)
{
}

DayCount FixedWarmupDaysPolicy::requiredWarmupDays(WindowingMode mode) const
{
    if (mode == WindowingMode::CrossSection) {
        return crossSectionWarmup_;
    }
    return timeSeriesWarmup_;
}

BacktestWindowBuilder::BacktestWindowBuilder(const ITradingCalendar& calendar,
                                             const IWarmupDaysPolicy& warmupPolicy)
    : calendar_(calendar)
    , warmupPolicy_(warmupPolicy)
{
}

WindowBuildResult BacktestWindowBuilder::build(WindowBuildSpec spec) const
{
    if (!spec.requested.isValid()) {
        return WindowBuildResult{WindowBuildError::InvalidInput, std::nullopt};
    }

    if (!calendar_.isTradingDay(spec.requested.start) || !calendar_.isTradingDay(spec.requested.end)) {
        return WindowBuildResult{WindowBuildError::NonTradingBoundary, std::nullopt};
    }

    const DayCount warmupDays = warmupPolicy_.requiredWarmupDays(spec.mode);
    if (!warmupDays.isValid()) {
        return WindowBuildResult{WindowBuildError::InvalidPolicyOutput, std::nullopt};
    }

    if (warmupDays.value == 0) {
        EffectiveWindow window{spec.requested, spec.requested, warmupDays};
        return WindowBuildResult{WindowBuildError::None, window};
    }

    const int32_t offset = kBackwardDirection * warmupDays.value;
    const std::optional<TradingDay> expandedStart =
        calendar_.shiftTradingDays(spec.requested.start, offset);
    if (!expandedStart.has_value()) {
        return WindowBuildResult{WindowBuildError::MissingHistoricalTradingDay, std::nullopt};
    }
    if (!expandedStart.value().isValid() || spec.requested.start < expandedStart.value()) {
        return WindowBuildResult{WindowBuildError::MissingHistoricalTradingDay, std::nullopt};
    }
    if (!calendar_.isTradingDay(expandedStart.value())) {
        return WindowBuildResult{WindowBuildError::NonTradingBoundary, std::nullopt};
    }

    DayRange effective{expandedStart.value(), spec.requested.end};
    EffectiveWindow window{spec.requested, effective, warmupDays};
    return WindowBuildResult{WindowBuildError::None, window};
}

} // namespace astock::domain::backtest::windowing


