#pragma once

#include <cstdint>
#include <optional>

namespace astock::domain::backtest::windowing {

struct TradingDay final {
    static constexpr int32_t kInvalidValue = 0;

    int32_t value{kInvalidValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value > kInvalidValue;
    }

    friend bool operator==(TradingDay left, TradingDay right) noexcept
    {
        return left.value == right.value;
    }

    friend bool operator!=(TradingDay left, TradingDay right) noexcept
    {
        return !(left == right);
    }

    friend bool operator<(TradingDay left, TradingDay right) noexcept
    {
        return left.value < right.value;
    }

    friend bool operator<=(TradingDay left, TradingDay right) noexcept
    {
        return left.value <= right.value;
    }
};

struct DayRange final {
    TradingDay start{};
    TradingDay end{};

    [[nodiscard]] bool isValid() const noexcept
    {
        return start.isValid() && end.isValid() && start <= end;
    }
};

struct DayCount final {
    static constexpr int32_t kInvalidValue = 0;

    int32_t value{kInvalidValue};

    [[nodiscard]] bool isValid() const noexcept
    {
        return value >= kInvalidValue;
    }
};

enum class WindowingMode {
    CrossSection,
    TimeSeries
};

struct WindowBuildSpec final {
    DayRange requested{};
    WindowingMode mode{WindowingMode::CrossSection};
};

struct EffectiveWindow final {
    DayRange requested{};
    DayRange effective{};
    DayCount warmupDays{};
};

enum class WindowBuildError {
    None,
    InvalidInput,
    InvalidPolicyOutput,
    NonTradingBoundary,
    MissingHistoricalTradingDay
};

struct WindowBuildResult final {
    WindowBuildError error{WindowBuildError::None};
    std::optional<EffectiveWindow> value;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == WindowBuildError::None && value.has_value();
    }
};

class ITradingCalendar {
public:
    virtual ~ITradingCalendar() = default;

    virtual bool isTradingDay(TradingDay day) const = 0;
    virtual std::optional<TradingDay> shiftTradingDays(TradingDay anchor, int32_t offset) const = 0;
};

class IWarmupDaysPolicy {
public:
    virtual ~IWarmupDaysPolicy() = default;

    virtual DayCount requiredWarmupDays(WindowingMode mode) const = 0;
};

class FixedWarmupDaysPolicy final : public IWarmupDaysPolicy {
public:
    explicit FixedWarmupDaysPolicy(DayCount crossSectionWarmup, DayCount timeSeriesWarmup);

    DayCount requiredWarmupDays(WindowingMode mode) const override;

private:
    DayCount crossSectionWarmup_{};
    DayCount timeSeriesWarmup_{};
};

class IBacktestWindowBuilder {
public:
    virtual ~IBacktestWindowBuilder() = default;

    virtual WindowBuildResult build(WindowBuildSpec spec) const = 0;
};

class BacktestWindowBuilder final : public IBacktestWindowBuilder {
public:
    static constexpr int32_t kZeroOffset = 0;
    static constexpr int32_t kBackwardDirection = -1;

    BacktestWindowBuilder(const ITradingCalendar& calendar, const IWarmupDaysPolicy& warmupPolicy);

    WindowBuildResult build(WindowBuildSpec spec) const override;

private:
    const ITradingCalendar& calendar_;
    const IWarmupDaysPolicy& warmupPolicy_;
};

} // namespace astock::domain::backtest::windowing
