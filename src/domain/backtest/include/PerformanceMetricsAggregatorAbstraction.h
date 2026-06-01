#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "PerformanceMetricsTypes.h"

namespace astock::domain::backtest::performance_metrics {

enum class MetricsError {
    None,
    InvalidInput,
    InvalidPoint,
    InsufficientData
};

struct MetricsResult final {
    MetricsError error{MetricsError::None};
    std::optional<MetricsSummary> value;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == MetricsError::None && value.has_value();
    }
};

class IPerformanceMetricsAggregator {
public:
    virtual ~IPerformanceMetricsAggregator() = default;

    virtual MetricsResult aggregate(MetricsSpec spec,
                                    std::vector<EquityPoint> series) const = 0;
};

class BasicPerformanceMetricsAggregator final : public IPerformanceMetricsAggregator {
public:
    static constexpr int64_t kBpsBase = 10000;

    MetricsResult aggregate(MetricsSpec spec,
                            std::vector<EquityPoint> series) const override;

private:
    static bool toInt32Checked(long double value, int32_t* out);
};

} // namespace astock::domain::backtest::performance_metrics
