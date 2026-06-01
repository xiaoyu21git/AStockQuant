#include "RebalanceScheduleAbstraction.h"

#include <algorithm>
#include <utility>

namespace astock::domain::backtest::rebalancing {

RebalanceScheduleBuilder::RebalanceScheduleBuilder(const ITradingCalendar& calendar)
    : calendar_(calendar)
{
}

RebalancePlanResult RebalanceScheduleBuilder::build(RebalancePlanSpec spec) const
{
    if (!spec.window.isValid() || !spec.interval.isValid()) {
        return RebalancePlanResult{RebalancePlanError::InvalidInput, std::nullopt};
    }

    if (spec.anchor == RebalanceAnchor::StartDay) {
        return buildFromStartAnchor(spec);
    }
    return buildFromEndAnchor(spec);
}

RebalancePlanResult RebalanceScheduleBuilder::buildFromStartAnchor(RebalancePlanSpec spec) const
{
    const std::optional<TradingDay> first = calendar_.nextTradingDayOnOrAfter(spec.window.start);
    if (!first.has_value() || !first.value().isValid() || first.value() > spec.window.end) {
        return RebalancePlanResult{RebalancePlanError::MissingTradingDay, std::nullopt};
    }

    RebalancePlan plan;
    TradingDay current = first.value();
    while (current <= spec.window.end) {
        plan.schedule.push_back(current);

        const int32_t offset = kForwardDirection * spec.interval.value;
        const std::optional<TradingDay> next = calendar_.shiftTradingDays(current, offset);
        if (!next.has_value()) {
            break;
        }
        if (!next.value().isValid()) {
            return RebalancePlanResult{RebalancePlanError::InvalidCalendarProgress, std::nullopt};
        }
        if (next.value() <= current) {
            return RebalancePlanResult{RebalancePlanError::InvalidCalendarProgress, std::nullopt};
        }
        current = next.value();
    }

    return RebalancePlanResult{RebalancePlanError::None, std::move(plan)};
}

RebalancePlanResult RebalanceScheduleBuilder::buildFromEndAnchor(RebalancePlanSpec spec) const
{
    const std::optional<TradingDay> first = calendar_.previousTradingDayOnOrBefore(spec.window.end);
    if (!first.has_value() || !first.value().isValid() || first.value() < spec.window.start) {
        return RebalancePlanResult{RebalancePlanError::MissingTradingDay, std::nullopt};
    }

    RebalancePlan reversedPlan;
    TradingDay current = first.value();
    while (current >= spec.window.start) {
        reversedPlan.schedule.push_back(current);

        const int32_t offset = kBackwardDirection * spec.interval.value;
        const std::optional<TradingDay> previous = calendar_.shiftTradingDays(current, offset);
        if (!previous.has_value()) {
            break;
        }
        if (!previous.value().isValid()) {
            return RebalancePlanResult{RebalancePlanError::InvalidCalendarProgress, std::nullopt};
        }
        if (previous.value() >= current) {
            return RebalancePlanResult{RebalancePlanError::InvalidCalendarProgress, std::nullopt};
        }
        current = previous.value();
    }

    std::reverse(reversedPlan.schedule.begin(), reversedPlan.schedule.end());
    return RebalancePlanResult{RebalancePlanError::None, std::move(reversedPlan)};
}

} // namespace astock::domain::backtest::rebalancing
