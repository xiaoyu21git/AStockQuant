#include "MarketDataWindowProviderAdapter.h"

#include "../../../domain/backtest/include/BacktestRequest.h"
#include "../../../domain/backtest/include/BacktestWindowBuilder.h"
#include "../../../domain/backtest/include/RebalanceScheduleBuilder.h"

#include <cmath>
#include <ctime>

namespace application::backtest {
namespace {

[[nodiscard]] bool isLeapYear(int32_t year) noexcept
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

[[nodiscard]] bool isValidDate(int32_t yyyymmdd) noexcept
{
    if (yyyymmdd <= 0) {
        return false;
    }

    const int32_t year = yyyymmdd / 10000;
    const int32_t month = (yyyymmdd / 100) % 100;
    const int32_t day = yyyymmdd % 100;

    if (year < 1900 || year > 2100) {
        return false;
    }
    if (month < 1 || month > 12) {
        return false;
    }

    static constexpr int32_t kDaysInMonth[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    int32_t maxDay = kDaysInMonth[month - 1];
    if (month == 2 && isLeapYear(year)) {
        maxDay = 29;
    }

    return day >= 1 && day <= maxDay;
}

[[nodiscard]] std::optional<std::tm> toTm(int32_t yyyymmdd) noexcept
{
    if (!isValidDate(yyyymmdd)) {
        return std::nullopt;
    }

    std::tm tmValue{};
    tmValue.tm_year = (yyyymmdd / 10000) - 1900;
    tmValue.tm_mon = ((yyyymmdd / 100) % 100) - 1;
    tmValue.tm_mday = yyyymmdd % 100;
    tmValue.tm_hour = 0;
    tmValue.tm_min = 0;
    tmValue.tm_sec = 0;
    tmValue.tm_isdst = -1;
    return tmValue;
}

[[nodiscard]] std::optional<int32_t> tmToDate(const std::tm& tmValue) noexcept
{
    const int32_t year = tmValue.tm_year + 1900;
    const int32_t month = tmValue.tm_mon + 1;
    const int32_t day = tmValue.tm_mday;
    const int32_t yyyymmdd = year * 10000 + month * 100 + day;
    if (!isValidDate(yyyymmdd)) {
        return std::nullopt;
    }
    return yyyymmdd;
}

[[nodiscard]] std::optional<int32_t> shiftCalendarDays(int32_t yyyymmdd, int32_t offsetDays) noexcept
{
    const std::optional<std::tm> initialTm = toTm(yyyymmdd);
    if (!initialTm.has_value()) {
        return std::nullopt;
    }

    std::tm shifted = *initialTm;
    shifted.tm_mday += offsetDays;
    const std::time_t normalized = std::mktime(&shifted);
    if (normalized == static_cast<std::time_t>(-1)) {
        return std::nullopt;
    }

    std::tm* normalizedTm = std::localtime(&normalized);
    if (normalizedTm == nullptr) {
        return std::nullopt;
    }

    return tmToDate(*normalizedTm);
}

[[nodiscard]] bool isTradingDayDate(int32_t yyyymmdd) noexcept
{
    const std::optional<std::tm> tmValue = toTm(yyyymmdd);
    if (!tmValue.has_value()) {
        return false;
    }

    std::tm normalized = *tmValue;
    const std::time_t raw = std::mktime(&normalized);
    if (raw == static_cast<std::time_t>(-1)) {
        return false;
    }

    std::tm* day = std::localtime(&raw);
    if (day == nullptr) {
        return false;
    }

    static constexpr int kSunday = 0;
    static constexpr int kSaturday = 6;
    return day->tm_wday != kSunday && day->tm_wday != kSaturday;
}

class CompositeTradingCalendar final
    : public astock::domain::backtest::windowing::ITradingCalendar
    , public astock::domain::backtest::rebalancing::ITradingCalendar {
public:
    [[nodiscard]] bool isTradingDay(astock::domain::backtest::windowing::TradingDay day) const override
    {
        return isTradingDayDate(day.value);
    }

    [[nodiscard]] std::optional<astock::domain::backtest::windowing::TradingDay>
    shiftTradingDays(astock::domain::backtest::windowing::TradingDay anchor,
                     int32_t offset) const override
    {
        const std::optional<int32_t> shifted = shiftTradingDaysCore(anchor.value, offset);
        if (!shifted.has_value()) {
            return std::nullopt;
        }

        astock::domain::backtest::windowing::TradingDay out;
        out.value = *shifted;
        return out;
    }

    [[nodiscard]] std::optional<astock::domain::backtest::rebalancing::TradingDay>
    nextTradingDayOnOrAfter(astock::domain::backtest::rebalancing::TradingDay day) const override
    {
        int32_t cursor = day.value;
        for (int32_t steps = 0; steps < kMaxCalendarScanSteps; ++steps) {
            if (isTradingDayDate(cursor)) {
                astock::domain::backtest::rebalancing::TradingDay out;
                out.value = cursor;
                return out;
            }
            const std::optional<int32_t> next = shiftCalendarDays(cursor, kForwardDirection);
            if (!next.has_value()) {
                return std::nullopt;
            }
            cursor = *next;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<astock::domain::backtest::rebalancing::TradingDay>
    previousTradingDayOnOrBefore(astock::domain::backtest::rebalancing::TradingDay day) const override
    {
        int32_t cursor = day.value;
        for (int32_t steps = 0; steps < kMaxCalendarScanSteps; ++steps) {
            if (isTradingDayDate(cursor)) {
                astock::domain::backtest::rebalancing::TradingDay out;
                out.value = cursor;
                return out;
            }
            const std::optional<int32_t> prev = shiftCalendarDays(cursor, kBackwardDirection);
            if (!prev.has_value()) {
                return std::nullopt;
            }
            cursor = *prev;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<astock::domain::backtest::rebalancing::TradingDay>
    shiftTradingDays(astock::domain::backtest::rebalancing::TradingDay day,
                     int32_t offset) const override
    {
        const std::optional<int32_t> shifted = shiftTradingDaysCore(day.value, offset);
        if (!shifted.has_value()) {
            return std::nullopt;
        }

        astock::domain::backtest::rebalancing::TradingDay out;
        out.value = *shifted;
        return out;
    }

private:
    [[nodiscard]] static std::optional<int32_t> shiftTradingDaysCore(int32_t anchor, int32_t offset)
    {
        if (!isValidDate(anchor)) {
            return std::nullopt;
        }

        if (offset == kNoOffset) {
            return isTradingDayDate(anchor)
                ? std::optional<int32_t>(anchor)
                : std::nullopt;
        }

        const int32_t direction = offset > 0 ? kForwardDirection : kBackwardDirection;
        const int32_t remainingInitial = std::abs(offset);

        int32_t remaining = remainingInitial;
        int32_t cursor = anchor;
        for (int32_t steps = 0; steps < kMaxCalendarScanSteps && remaining > 0; ++steps) {
            const std::optional<int32_t> next = shiftCalendarDays(cursor, direction);
            if (!next.has_value()) {
                return std::nullopt;
            }

            cursor = *next;
            if (isTradingDayDate(cursor)) {
                --remaining;
            }
        }

        return remaining == 0 ? std::optional<int32_t>(cursor) : std::nullopt;
    }

private:
    static constexpr int32_t kNoOffset = 0;
    static constexpr int32_t kForwardDirection = 1;
    static constexpr int32_t kBackwardDirection = -1;
    static constexpr int32_t kMaxCalendarScanSteps = 40000;
};

} // namespace

StageResult WindowedMarketDataProviderAdapter::loadWindowData(RunContext& context) const
{
    StageResult result;
    result.stage = RunStage::LoadWindowData;
    result.code = RunErrorCode::None;

    if (!context.spec.request) {
        result.code = RunErrorCode::MissingWindowProvider;
        return result;
    }

    const domain::backtest::BacktestRequest& request = *context.spec.request;

    CompositeTradingCalendar calendar;
    astock::domain::backtest::windowing::DayCount crossSectionWarmup;
    crossSectionWarmup.value = kCrossSectionWarmupDays;
    astock::domain::backtest::windowing::DayCount timeSeriesWarmup;
    timeSeriesWarmup.value = kTimeSeriesWarmupDays;
    astock::domain::backtest::windowing::FixedWarmupDaysPolicy warmupPolicy(
        crossSectionWarmup,
        timeSeriesWarmup);
    astock::domain::backtest::windowing::BacktestWindowBuilder windowBuilder(calendar, warmupPolicy);

    astock::domain::backtest::windowing::WindowBuildSpec windowSpec;
    windowSpec.mode = astock::domain::backtest::windowing::WindowingMode::CrossSection;
    windowSpec.requested.start.value = request.window.startDate;
    windowSpec.requested.end.value = request.window.endDate;

    const auto windowResult = windowBuilder.build(windowSpec);
    if (!windowResult.ok()) {
        result.code = RunErrorCode::StageExecutionFailed;
        return result;
    }

    const auto& effectiveWindow = windowResult.value->effective;
    astock::domain::backtest::rebalancing::RebalancePlanSpec planSpec;
    planSpec.window.start.value = effectiveWindow.start.value;
    planSpec.window.end.value = effectiveWindow.end.value;
    planSpec.anchor = astock::domain::backtest::rebalancing::RebalanceAnchor::StartDay;
    planSpec.interval.value = request.executionSpec.rebalanceFrequencyDays;

    astock::domain::backtest::rebalancing::RebalanceScheduleBuilder scheduleBuilder(calendar);
    const auto planResult = scheduleBuilder.build(planSpec);
    if (!planResult.ok()) {
        result.code = RunErrorCode::StageExecutionFailed;
        return result;
    }

    context.workingSet.effectiveWindowStartDate = effectiveWindow.start.value;
    context.workingSet.effectiveWindowEndDate = effectiveWindow.end.value;
    context.workingSet.rebalancePointCount =
        static_cast<std::uint32_t>(planResult.value->schedule.size());

    if (context.workingSet.rebalancePointCount == 0U) {
        result.code = RunErrorCode::StageExecutionFailed;
    }

    return result;
}

} // namespace application::backtest