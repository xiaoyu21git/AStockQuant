#include "PerformanceMetricsAggregatorAbstraction.h"

#include <cmath>
#include <limits>
#include <utility>

namespace astock::domain::backtest::performance_metrics {

bool BasicPerformanceMetricsAggregator::toInt32Checked(long double value, int32_t* out)
{
    if (out == nullptr || !std::isfinite(value)) {
        return false;
    }

    const long double minValue = static_cast<long double>(std::numeric_limits<int32_t>::min());
    const long double maxValue = static_cast<long double>(std::numeric_limits<int32_t>::max());
    if (value < minValue || value > maxValue) {
        return false;
    }

    *out = static_cast<int32_t>(value);
    return true;
}

MetricsResult BasicPerformanceMetricsAggregator::aggregate(MetricsSpec spec,
                                                           std::vector<EquityPoint> series) const
{
    if (!spec.isValid()) {
        return MetricsResult{MetricsError::InvalidInput, std::nullopt};
    }
    if (series.size() < 2U) {
        return MetricsResult{MetricsError::InsufficientData, std::nullopt};
    }
    int32_t previousDayIndex = EquityPoint::kMinDayIndex;
    bool hasPrevious = false;
    for (const EquityPoint& point : series) {
        if (!point.isValid()) {
            return MetricsResult{MetricsError::InvalidPoint, std::nullopt};
        }
        if (hasPrevious && point.dayIndex <= previousDayIndex) {
            return MetricsResult{MetricsError::InvalidPoint, std::nullopt};
        }
        previousDayIndex = point.dayIndex;
        hasPrevious = true;
    }

    MetricsSummary out;

    const long double firstEquity = static_cast<long double>(series.front().equityMicros);
    const long double lastEquity = static_cast<long double>(series.back().equityMicros);
    const long double totalReturnBps = ((lastEquity - firstEquity) * static_cast<long double>(kBpsBase)) / firstEquity;
    if (!BasicPerformanceMetricsAggregator::toInt32Checked(totalReturnBps, &out.totalReturnBps)) {
        return MetricsResult{MetricsError::InvalidPoint, std::nullopt};
    }

    int64_t peak = series.front().equityMicros;
    long double maxDrawdown = 0.0L;

    long double sumDailyReturnsBps = 0.0L;
    std::vector<long double> dailyReturnsBps;
    dailyReturnsBps.reserve(series.size() - 1U);

    for (std::size_t index = 1; index < series.size(); ++index) {
        const int64_t equity = series[index].equityMicros;
        peak = (equity > peak) ? equity : peak;

        const long double drawdown =
            (static_cast<long double>(peak - equity) * static_cast<long double>(kBpsBase))
            / static_cast<long double>(peak);
        maxDrawdown = (drawdown > maxDrawdown) ? drawdown : maxDrawdown;

        const int64_t previous = series[index - 1U].equityMicros;
        const long double retBps =
            (static_cast<long double>(equity - previous) * static_cast<long double>(kBpsBase))
            / static_cast<long double>(previous);
        if (!std::isfinite(retBps)) {
            return MetricsResult{MetricsError::InvalidPoint, std::nullopt};
        }
        dailyReturnsBps.push_back(retBps);
        sumDailyReturnsBps += retBps;
    }

    const long double count = static_cast<long double>(dailyReturnsBps.size());
    const long double mean = (count > 0.0L) ? (sumDailyReturnsBps / count) : 0.0L;

    long double varianceScaled = 0.0L;
    for (const long double retBps : dailyReturnsBps) {
        const long double diff = retBps - mean;
        varianceScaled += diff * diff;
    }
    varianceScaled = (count > 0.0L) ? (varianceScaled / count) : 0.0L;
    if (!std::isfinite(varianceScaled)) {
        return MetricsResult{MetricsError::InvalidPoint, std::nullopt};
    }

    if (!BasicPerformanceMetricsAggregator::toInt32Checked(maxDrawdown, &out.maxDrawdownBps)) {
        return MetricsResult{MetricsError::InvalidPoint, std::nullopt};
    }
    if (!BasicPerformanceMetricsAggregator::toInt32Checked(mean, &out.averageDailyReturnBps)) {
        return MetricsResult{MetricsError::InvalidPoint, std::nullopt};
    }

    const long double volatilityBps = std::sqrt(varianceScaled);
    if (!BasicPerformanceMetricsAggregator::toInt32Checked(volatilityBps, &out.volatilityBps)) {
        return MetricsResult{MetricsError::InvalidPoint, std::nullopt};
    }

    return MetricsResult{MetricsError::None, std::move(out)};
}

} // namespace astock::domain::backtest::performance_metrics
