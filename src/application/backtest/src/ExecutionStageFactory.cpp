#include "ExecutionStageFactory.hpp"

namespace application::backtest {

ExecutionStageFactoryResult ExecutionStageFactory::create(
    const ExistingModuleSlots& slots,
    ExecutionStagePolicyRefs policyRefs)
{
    ExecutionStageFactoryResult result;

    if (slots.riskApprovalEngine != nullptr) {
        if (policyRefs.signalValueProjection != nullptr && policyRefs.riskLimitsPolicy != nullptr) {
            result.riskApprovalEngine =
                std::make_unique<SignalDrivenRiskApprovalStageAdapter>(
                    *slots.riskApprovalEngine,
                    *policyRefs.signalValueProjection,
                    *policyRefs.riskLimitsPolicy);
        } else {
            result.riskApprovalEngine =
                std::make_unique<SignalDrivenRiskApprovalStageAdapter>(*slots.riskApprovalEngine);
        }
    }

    if (slots.signalOrderTranslator != nullptr) {
        if (policyRefs.signalValueProjection != nullptr && policyRefs.translationSpecPolicy != nullptr) {
            result.orderGenerationEngine =
                std::make_unique<SignalDrivenOrderGenerationAdapter>(
                    *slots.signalOrderTranslator,
                    *policyRefs.signalValueProjection,
                    *policyRefs.translationSpecPolicy);
        } else {
            result.orderGenerationEngine =
                std::make_unique<SignalDrivenOrderGenerationAdapter>(*slots.signalOrderTranslator);
        }
    }

    result.backtestFillEngine = std::make_unique<BacktestVenueFillEngineAdapter>();
    result.liveFillEngine = std::make_unique<LiveVenueFillEngineAdapter>();

    return result;
}

} // namespace application::backtest