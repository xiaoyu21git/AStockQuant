#pragma once

#include "IFactorRegistry.h"
#include "IFactorComputeDispatcher.h"
#include "IFactorComputeEngine.h"
#include "IPostProcessingPipeline.h"
#include "IFactorSignalSetAssembler.h"
#include "IMarketDataView.h"
#include "ISignalCache.h"
#include "IAnalysisModule.h"

#include <memory>
#include <optional>

namespace factor::compute {

struct DeltaMarketData;  // defined in ISignalEngine.h

class FactorComputeEngine final : public IFactorComputeEngine {
public:
    FactorComputeEngine(
        const IFactorRegistry& factorRegistry,
        const IFactorSignalSetAssembler& signalSetAssembler,
        const IFactorComputeDispatcher& factorComputeDispatcher,
        const IMarketDataView& marketDataView) noexcept;

    FactorComputeEngine(
        const IFactorRegistry& factorRegistry,
        const IFactorSignalSetAssembler& signalSetAssembler,
        const IFactorComputeDispatcher& factorComputeDispatcher,
        const IMarketDataView& marketDataView,
        ISignalCache& signalCache,
        const IPostProcessingPipeline& postProcessingPipeline) noexcept;

    FactorComputeEngine(
        const IFactorRegistry& factorRegistry,
        const IFactorSignalSetAssembler& signalSetAssembler,
        const IFactorComputeDispatcher& factorComputeDispatcher,
        const IMarketDataView& marketDataView,
        ISignalCache& signalCache,
        const IPostProcessingPipeline& postProcessingPipeline,
        const IAnalysisModule& analysisModule) noexcept;

    void setComputeMode(ComputeMode mode) noexcept override { computeMode_ = mode; }

    [[nodiscard]] FactorResult<SignalSet>
    generate(const GenerateSpec& spec) override;

    [[nodiscard]] FactorResult<SignalSet>
    incrementalUpdate(
        const SignalSet& baseResult,
        const DeltaMarketData& deltaData) override;

    [[nodiscard]] FactorResult<SignalValue>
    query(const QuerySpec& spec) const override;

    [[nodiscard]] const std::optional<AnalysisReport>&
    latestAnalysisReport() const noexcept { return latestAnalysisReport_; }

    [[nodiscard]] const std::optional<FactorError>&
    latestAnalysisError() const noexcept { return latestAnalysisError_; }

    [[nodiscard]] std::optional<FactorQualityMetrics16View>
    latestFactorQualityMetrics16() const noexcept;

    [[nodiscard]] std::optional<FactorQualityMetrics16DiagnosticsView>
    latestFactorQualityDiagnostics16() const noexcept;

    [[nodiscard]] std::optional<FactorQualityMetrics16Snapshot>
    latestFactorQualitySnapshot16() const noexcept;

private:
    void resetLatestAnalysisState() noexcept;

    void captureLatestAnalysisState(const FactorResult<AnalysisReport>& analysisResult) noexcept;

    std::unique_ptr<ISignalCache> ownedSignalCache_;
    std::unique_ptr<IPostProcessingPipeline> ownedPostProcessingPipeline_;
    std::unique_ptr<IAnalysisModule> ownedAnalysisModule_;
    const IFactorRegistry& factorRegistry_;
    const IFactorSignalSetAssembler& signalSetAssembler_;
    const IFactorComputeDispatcher& factorComputeDispatcher_;
    const IMarketDataView& marketDataView_;
    ISignalCache* signalCache_{nullptr};
    const IPostProcessingPipeline* postProcessingPipeline_{nullptr};
    const IAnalysisModule* analysisModule_{nullptr};
    std::optional<AnalysisReport> latestAnalysisReport_;
    std::optional<FactorError> latestAnalysisError_;
    ComputeMode computeMode_{ComputeMode::Batch};
};

} // namespace factor::compute

