#pragma once

#include "BacktestInterfaces.hpp"

namespace application::backtest {

class DomainFactorMetricsEngineAdapter final : public IMetricsEngine {
public:
    [[nodiscard]] StageResult aggregateMetrics(RunContext& context) const override;

private:
    static constexpr std::uint32_t kMinimumSnapshotCount = 1U;
    static constexpr std::uint32_t kMinimumGroupCount = 1U;
    static constexpr std::uint32_t kDefaultGroupCount = 2U;
    static constexpr std::uint32_t kSummaryMetricCount = 8U;
    static constexpr double kFactorBucketWidth = 1.0;
};

} // namespace application::backtest