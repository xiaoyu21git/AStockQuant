#pragma once

#include "BacktestInterfaces.hpp"

namespace application::backtest {

class StrictExportArtifactBuilderAdapter final : public IExportArtifactBuilder {
public:
    [[nodiscard]] StageResult buildExportArtifacts(RunContext& context) const override;

private:
    static constexpr std::uint32_t kMinimumPersistedArtifactCount = 1U;
    static constexpr std::uint32_t kMinimumMetricCount = 1U;
    static constexpr std::uint32_t kMinimumDiagnosticsCount = 1U;
    static constexpr std::uint32_t kMinimumTargetPositionCount = 1U;
    static constexpr std::uint32_t kMinimumRebalancePointCount = 1U;
    static constexpr std::uint32_t kMinimumOrderFlowCount = 1U;
};

} // namespace application::backtest