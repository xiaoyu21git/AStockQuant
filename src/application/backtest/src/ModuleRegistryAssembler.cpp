#include "ModuleRegistryAssembler.h"
#include "../../../domain/trading/include/execution/RiskApprovalEngine.h"
#include "../../../domain/trading/include/execution/SignalOrderTranslator.h"

namespace application::backtest {

RunModuleRegistry OwnedModules::registry() const noexcept
{
    RunModuleRegistry registry;
    registry.runSpecValidator = runSpecValidator.get();
    registry.modeRouter = modeRouter.get();
    registry.runtimeGuard = runtimeGuard.get();
    registry.marketDataWindowProvider = marketDataWindowProvider.get();
    registry.factorSignalProducer = factorSignalProducer.get();
    registry.strategySignalProducer = strategySignalProducer.get();
    registry.portfolioConstructionEngine = portfolioConstructionEngine.get();
    registry.riskApprovalEngine = riskApprovalEngine.get();
    registry.orderGenerationEngine = orderGenerationEngine.get();
    registry.backtestFillEngine = backtestFillEngine.get();
    registry.liveFillEngine = liveFillEngine.get();
    registry.positionStateEngine = positionStateEngine.get();
    registry.metricsEngine = metricsEngine.get();
    registry.diagnosticsEngine = diagnosticsEngine.get();
    registry.resultRepository = resultRepository.get();
    registry.exportArtifactBuilder = exportArtifactBuilder.get();
    return registry;
}

OwnedModules ModuleRegistryAssembler::assemble(const ExistingModuleSlots& slots)
{
    return assemble(slots, nullptr, nullptr, nullptr);
}

OwnedModules ModuleRegistryAssembler::assemble(
    const ExistingModuleSlots& slots,
    std::unique_ptr<ISignalValueProjection> signalValueProjection,
    std::unique_ptr<IRiskLimitsPolicy> riskLimitsPolicy,
    std::unique_ptr<ITranslationSpecPolicy> translationSpecPolicy)
{
    OwnedModules owned;

    owned.signalValueProjection = signalValueProjection
        ? std::move(signalValueProjection)
        : std::make_unique<FirstSliceSignalValueProjection>();
    owned.riskLimitsPolicy = riskLimitsPolicy
        ? std::move(riskLimitsPolicy)
        : std::make_unique<DefaultRiskLimitsPolicy>();
    owned.translationSpecPolicy = translationSpecPolicy
        ? std::move(translationSpecPolicy)
        : std::make_unique<DefaultTranslationSpecPolicy>();

    owned.runSpecValidator = std::make_unique<StrictRunSpecValidator>();

    StaticModeRouteConfig modeRouteConfig;
    modeRouteConfig.factorHasSignalProducer = slots.factorComputeEngine != nullptr;
    modeRouteConfig.strategyHasSignalProducer = true;
    modeRouteConfig.factorHasFillEngine = true;
    modeRouteConfig.strategyHasFillEngine = true;
    owned.modeRouter = std::make_unique<StaticModeRouter>(modeRouteConfig);

    owned.runtimeGuard = std::make_unique<StrictRuntimeGuard>();
    owned.marketDataWindowProvider = std::make_unique<WindowedMarketDataProviderAdapter>();

    SignalProducerFactoryResult signalProducers = SignalProducerFactory::create(slots);
    owned.factorSignalProducer = std::move(signalProducers.factorSignalProducer);
    owned.strategySignalProducer = std::move(signalProducers.strategySignalProducer);

    owned.portfolioConstructionEngine = std::make_unique<RequestedTargetPositionConstructionAdapter>();

    ExecutionStagePolicyRefs policyRefs;
    policyRefs.signalValueProjection = owned.signalValueProjection.get();
    policyRefs.riskLimitsPolicy = owned.riskLimitsPolicy.get();
    policyRefs.translationSpecPolicy = owned.translationSpecPolicy.get();

    // 使用 OwnedModules 成员持有默认引擎实例，避免 static 局部变量线程安全问题
    if (slots.riskApprovalEngine == nullptr) {
        owned.ownedRiskApprovalEngine =
            std::make_unique<astock::domain::trading::risk_approval::SequentialRiskApprovalEngine>();
    }
    if (slots.signalOrderTranslator == nullptr) {
        owned.ownedSignalOrderTranslator =
            std::make_unique<astock::domain::trading::signal_orders::LinearSignalOrderTranslator>();
    }

    ExistingModuleSlots patchedSlots = slots;
    if (patchedSlots.riskApprovalEngine == nullptr) {
        patchedSlots.riskApprovalEngine = owned.ownedRiskApprovalEngine.get();
    }
    if (patchedSlots.signalOrderTranslator == nullptr) {
        patchedSlots.signalOrderTranslator = owned.ownedSignalOrderTranslator.get();
    }

    ExecutionStageFactoryResult executionStages = ExecutionStageFactory::create(
        patchedSlots,
        policyRefs);
    owned.riskApprovalEngine = std::move(executionStages.riskApprovalEngine);
    owned.orderGenerationEngine = std::move(executionStages.orderGenerationEngine);
    owned.backtestFillEngine = std::move(executionStages.backtestFillEngine);
    owned.liveFillEngine = std::move(executionStages.liveFillEngine);

    owned.positionStateEngine = std::make_unique<NetPositionStateEngineAdapter>();
    owned.metricsEngine = std::make_unique<DomainFactorMetricsEngineAdapter>();
    owned.diagnosticsEngine = std::make_unique<DomainFactorDiagnosticsEngineAdapter>();
    owned.resultRepository = std::make_unique<InMemoryRunArtifactRepository>();
    owned.exportArtifactBuilder = std::make_unique<StrictExportArtifactBuilderAdapter>();

    return owned;
}

} // namespace application::backtest