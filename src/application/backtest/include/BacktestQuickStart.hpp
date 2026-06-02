#pragma once

#include "BacktestRuntime.hpp"

namespace application::backtest {

struct QuickStartRunOutcome final {
    RunErrorCode buildCode{RunErrorCode::None};
    RunBacktestIngressResult runResult;
    std::optional<PersistedRunArtifact> artifact;

    [[nodiscard]] bool ok() const noexcept
    {
        return buildCode == RunErrorCode::None && runResult.ok();
    }
};

class BacktestQuickStart final {
public:
    [[nodiscard]] static QuickStartRunOutcome runAndFetchArtifact(
        const ExistingModuleSlots& slots,
        RunSpec spec);

    [[nodiscard]] static QuickStartRunOutcome runAndFetchArtifactWithPolicies(
        const ExistingModuleSlots& slots,
        RunSpec spec,
        std::unique_ptr<ISignalValueProjection> signalValueProjection,
        std::unique_ptr<IRiskLimitsPolicy> riskLimitsPolicy,
        std::unique_ptr<ITranslationSpecPolicy> translationSpecPolicy);

    [[nodiscard]] static QuickStartRunOutcome runAndFetchArtifactWithFillSideMode(
        const ExistingModuleSlots& slots,
        RunSpec spec,
        FillOrderSideMode fillSideMode);
};

} // namespace application::backtest