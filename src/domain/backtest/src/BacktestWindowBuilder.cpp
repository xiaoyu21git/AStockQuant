#include "BacktestWindowBuilder.h"
#include <foundation/log/logging.hpp>

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
    INTERNAL_INFO_STREAM << "[BacktestWindowBuilder] 正在构建窗口: start="
                        << spec.requested.start.value << " end=" << spec.requested.end.value
                        << " mode=" << static_cast<int>(spec.mode);

    if (!spec.requested.isValid()) {
        INTERNAL_ERROR_STREAM << "[BacktestWindowBuilder] 构建规格中日期范围无效";
        return WindowBuildResult{WindowBuildError::InvalidInput, std::nullopt};
    }

    if (!calendar_.isTradingDay(spec.requested.start) || !calendar_.isTradingDay(spec.requested.end)) {
        INTERNAL_ERROR_STREAM << "[BacktestWindowBuilder] 非交易日边界: "
                              << spec.requested.start.value << "~" << spec.requested.end.value;
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


