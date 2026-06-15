#pragma once

#include <cstdint>
#include <optional>
#include <vector>
#include "../../types/DomainDate.h"

namespace astock::domain::backtest::rebalancing {

using ::domain::TradingDay;
using ::domain::DayRange;

struct DayCount final {
    static constexpr int32_t kInvalidValue = 0;
    int32_t value{kInvalidValue};
    [[nodiscard]] bool isValid() const noexcept { return value > kInvalidValue; }
};

struct RebalanceIntervalDays final {
    static constexpr int32_t kInvalidValue = 0;

    int32_t value{kInvalidValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value > kInvalidValue;
    }
};

enum class RebalanceAnchor {
    StartDay,
    EndDay
};

struct RebalancePlanSpec final {
    DayRange window{};
    RebalanceIntervalDays interval{};
    RebalanceAnchor anchor{RebalanceAnchor::StartDay};
};

struct RebalancePlan final {
    std::vector<TradingDay> schedule;
};

enum class RebalancePlanError {
    None,
    InvalidInput,
    MissingTradingDay,
    InvalidCalendarProgress
};

struct RebalancePlanResult final {
    RebalancePlanError error{RebalancePlanError::None};
    std::optional<RebalancePlan> value;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == RebalancePlanError::None && value.has_value();
    }
};

class ITradingCalendar {
public:
    virtual ~ITradingCalendar() = default;

    virtual std::optional<TradingDay> nextTradingDayOnOrAfter(TradingDay day) const = 0;
    virtual std::optional<TradingDay> previousTradingDayOnOrBefore(TradingDay day) const = 0;
    virtual std::optional<TradingDay> shiftTradingDays(TradingDay day, int32_t offset) const = 0;
};

class IRebalanceScheduleBuilder {
public:
    virtual ~IRebalanceScheduleBuilder() = default;

    virtual RebalancePlanResult build(RebalancePlanSpec spec) const = 0;
};

class RebalanceScheduleBuilder final : public IRebalanceScheduleBuilder {
public:
    static constexpr int32_t kForwardDirection = 1;
    static constexpr int32_t kBackwardDirection = -1;

    explicit RebalanceScheduleBuilder(const ITradingCalendar& calendar);

    RebalancePlanResult build(RebalancePlanSpec spec) const override;

private:
    RebalancePlanResult buildFromStartAnchor(RebalancePlanSpec spec) const;

    RebalancePlanResult buildFromEndAnchor(RebalancePlanSpec spec) const;

private:
    const ITradingCalendar& calendar_;
};

} // namespace astock::domain::backtest::rebalancing
