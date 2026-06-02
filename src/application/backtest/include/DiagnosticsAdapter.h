#pragma once

#include "BacktestInterfaces.hpp"

namespace application::backtest {

class DomainFactorDiagnosticsEngineAdapter final : public IDiagnosticsEngine {
public:
    [[nodiscard]] StageResult buildDiagnostics(RunContext& context) const override;

private:
    static constexpr std::uint32_t kMinimumMetricCount = 1U;
    static constexpr std::uint32_t kMinimumSampleCount = 1U;
    static constexpr std::uint32_t kDefaultWindowAttemptCount = 1U;
    static constexpr std::uint32_t kDefaultNumGroups = 1U;
    static constexpr double kFullCoverageRatio = 1.0;
    static constexpr double kZeroValue = 0.0;
    static constexpr double kHalfRatio = 0.5;
    static constexpr double kTurnoverRatio = 0.5;
    static constexpr std::int32_t kMinimumHalfLife = 1;
};

} // namespace application::backtest