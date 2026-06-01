#pragma once

#include <cstdint>
#include <vector>

namespace astock::domain::backtest::performance_metrics {

struct EquityPoint final {
    static constexpr int32_t kMinDayIndex = 0;
    static constexpr int64_t kMinEquityMicros = 1;

    int32_t dayIndex{kMinDayIndex};
    int64_t equityMicros{kMinEquityMicros};

    [[nodiscard]] bool isValid() const noexcept
    {
        return dayIndex >= kMinDayIndex && equityMicros >= kMinEquityMicros;
    }
};

struct MetricsSpec final {
    static constexpr int32_t kMinAnnualizationDays = 1;

    int32_t annualizationDays{252};

    [[nodiscard]] bool isValid() const noexcept
    {
        return annualizationDays >= kMinAnnualizationDays;
    }
};

struct MetricsSummary final {
    int32_t totalReturnBps{0};
    int32_t maxDrawdownBps{0};
    int32_t averageDailyReturnBps{0};
    int32_t volatilityBps{0};
};

} // namespace astock::domain::backtest::performance_metrics
