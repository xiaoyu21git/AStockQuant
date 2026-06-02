#pragma once

#include "ExecutionStageAdapters.h"

#include <memory>

namespace application::backtest {

struct ExecutionStageFactoryResult final {
    std::unique_ptr<IRiskApprovalStageEngine> riskApprovalEngine;
    std::unique_ptr<IOrderGenerationEngine> orderGenerationEngine;
    std::unique_ptr<IFillEngine> backtestFillEngine;
    std::unique_ptr<IFillEngine> liveFillEngine;

    [[nodiscard]] bool hasRequiredStages() const noexcept
    {
        return riskApprovalEngine != nullptr
            && orderGenerationEngine != nullptr
            && backtestFillEngine != nullptr
            && liveFillEngine != nullptr;
    }
};

struct ExecutionStagePolicyRefs final {
    const ISignalValueProjection* signalValueProjection{nullptr};
    const IRiskLimitsPolicy* riskLimitsPolicy{nullptr};
    const ITranslationSpecPolicy* translationSpecPolicy{nullptr};
};

class ExecutionStageFactory final {
public:
    [[nodiscard]] static ExecutionStageFactoryResult create(
        const ExistingModuleSlots& slots,
        ExecutionStagePolicyRefs policyRefs);
};

} // namespace application::backtest