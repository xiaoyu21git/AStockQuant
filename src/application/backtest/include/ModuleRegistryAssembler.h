#pragma once

#include "ExecutionStageFactory.hpp"
#include "DiagnosticsAdapter.h"
#include "ExportArtifactBuilderAdapter.h"
#include "MarketDataWindowProviderAdapter.h"
#include "MetricsAdapter.h"
#include "PortfolioConstructionAdapter.h"
#include "PositionStateAdapter.h"
#include "RunArtifactRepository.h"
#include "RunDefaults.h"
#include "SignalProducerFactory.hpp"

#include <memory>

namespace application::backtest {

struct OwnedModules final {
    std::unique_ptr<ISignalValueProjection> signalValueProjection;
    std::unique_ptr<IRiskLimitsPolicy> riskLimitsPolicy;
    std::unique_ptr<ITranslationSpecPolicy> translationSpecPolicy;
    std::unique_ptr<IRunSpecValidator> runSpecValidator;
    std::unique_ptr<IModeRouter> modeRouter;
    std::unique_ptr<IRuntimeGuard> runtimeGuard;
    std::unique_ptr<IMarketDataWindowProvider> marketDataWindowProvider;
    std::unique_ptr<ISignalProducer> factorSignalProducer;
    std::unique_ptr<ISignalProducer> strategySignalProducer;
    std::unique_ptr<IPortfolioConstructionEngine> portfolioConstructionEngine;
    std::unique_ptr<IRiskApprovalStageEngine> riskApprovalEngine;
    std::unique_ptr<IOrderGenerationEngine> orderGenerationEngine;
    std::unique_ptr<IFillEngine> backtestFillEngine;
    std::unique_ptr<IFillEngine> liveFillEngine;
    std::unique_ptr<IPositionStateEngine> positionStateEngine;
    std::unique_ptr<IMetricsEngine> metricsEngine;
    std::unique_ptr<IDiagnosticsEngine> diagnosticsEngine;
    std::unique_ptr<IResultRepository> resultRepository;
    std::unique_ptr<IExportArtifactBuilder> exportArtifactBuilder;

    [[nodiscard]] RunModuleRegistry registry() const noexcept;
};

class ModuleRegistryAssembler final {
public:
    [[nodiscard]] static OwnedModules assemble(const ExistingModuleSlots& slots);

    [[nodiscard]] static OwnedModules assemble(
        const ExistingModuleSlots& slots,
        std::unique_ptr<ISignalValueProjection> signalValueProjection,
        std::unique_ptr<IRiskLimitsPolicy> riskLimitsPolicy,
        std::unique_ptr<ITranslationSpecPolicy> translationSpecPolicy);
};

} // namespace application::backtest