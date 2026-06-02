#include "SignalProducers.h"

#include "../../../domain/backtest/include/BacktestRequest.h"

#include <QString>

namespace application::backtest {

namespace {

constexpr std::uint32_t kFNV1aOffsetBasis = 2166136261U;
constexpr std::uint32_t kFNV1aPrime = 16777619U;

[[nodiscard]] std::optional<factor::compute::InstrumentId> toInstrumentId(const QString& symbolText)
{
    const QString trimmedSymbolText = symbolText.trimmed();
    if (trimmedSymbolText.isEmpty()) {
        return std::nullopt;
    }

    const QByteArray utf8 = trimmedSymbolText.toUtf8();
    std::uint32_t hash = kFNV1aOffsetBasis;
    for (char ch : utf8) {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= kFNV1aPrime;
    }

    factor::compute::InstrumentId instrumentId;
    instrumentId.value = hash;
    if (!instrumentId.isValid()) {
        return std::nullopt;
    }
    return instrumentId;
}

[[nodiscard]] std::optional<factor::compute::FactorId> toFactorId(const QString& factorText)
{
    const QString trimmedFactorText = factorText.trimmed();
    if (trimmedFactorText.isEmpty()) {
        return std::nullopt;
    }

    const QByteArray utf8 = trimmedFactorText.toUtf8();
    std::uint32_t hash = kFNV1aOffsetBasis;
    for (char ch : utf8) {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= kFNV1aPrime;
    }

    factor::compute::FactorId mapped;
    mapped.value = hash;
    if (!mapped.isValid()) {
        return std::nullopt;
    }
    return mapped;
}

} // namespace

FactorSignalProducerAdapter::FactorSignalProducerAdapter(
    factor::compute::IFactorComputeEngine& factorComputeEngine)
    : factorComputeEngine_(factorComputeEngine)
{
}

StageResult FactorSignalProducerAdapter::generateSignal(RunContext& context) const
{
    StageResult stageResult;
    stageResult.stage = RunStage::GenerateSignal;
    stageResult.code = RunErrorCode::None;

    const std::optional<factor::compute::GenerateSpec> generateSpec = buildGenerateSpec(context);
    if (!generateSpec.has_value()) {
        stageResult.code = RunErrorCode::StageExecutionFailed;
        return stageResult;
    }

    factor::compute::FactorResult<factor::compute::SignalSet> factorResult =
        factorComputeEngine_.generate(*generateSpec);
    if (!factorResult.hasValue() || !factorResult.value().isValid()) {
        stageResult.code = RunErrorCode::StageExecutionFailed;
        return stageResult;
    }

    context.workingSet.signalBatch.factorSignalSet =
        std::make_shared<const factor::compute::SignalSet>(std::move(factorResult.value()));
    context.workingSet.signalBatch.strategySignalCount = 0U;
    return stageResult;
}

std::optional<factor::compute::GenerateSpec>
FactorSignalProducerAdapter::buildGenerateSpec(const RunContext& context) const
{
    if (!context.spec.request) {
        return std::nullopt;
    }

    const domain::backtest::BacktestRequest& request = *context.spec.request;
    const StageBudgetSpec& stageBudget = context.spec.runtimeBudget.forStage(RunStage::GenerateSignal);

    factor::compute::GenerateSpec spec;
    spec.mode = factor::compute::SignalEngineMode::FullPipeline;
    spec.dateRange.from.value = request.window.startDate;
    spec.dateRange.to.value = request.window.endDate;
    spec.runtimeBudget.timeoutMilliseconds = stageBudget.timeoutMilliseconds;
    spec.runtimeBudget.memoryLimitBytes = stageBudget.memoryLimitBytes;
    spec.chunkPolicy.dateChunkSize = kDateChunkSize;
    spec.chunkPolicy.instrumentChunkSize = kInstrumentChunkSize;
    spec.postProcessingConfig.winsorizeStdBand = kWinsorizeStdBand;
    spec.postProcessingConfig.stdEpsilon = kStdEpsilon;
    spec.postProcessingConfig.minimumValidSampleCount = kMinimumValidSampleCount;

    const auto& resolvedSymbols = request.universeSpec.resolvedSymbols;
    if (resolvedSymbols.size() < kMinimumInstrumentUniverseSize) {
        return std::nullopt;
    }

    spec.instrumentUniverse.reserve(static_cast<std::size_t>(resolvedSymbols.size()));
    for (const auto& symbolCode : resolvedSymbols) {
        const std::optional<factor::compute::InstrumentId> instrumentId = toInstrumentId(symbolCode.text());
        if (!instrumentId.has_value()) {
            return std::nullopt;
        }
        spec.instrumentUniverse.push_back(*instrumentId);
    }

    const auto& selectedFactors = request.factorOverlaySpec.selectedFactors;
    if (selectedFactors.size() < kMinimumRequestedFactorCount) {
        return std::nullopt;
    }

    spec.requestedFactors.reserve(static_cast<std::size_t>(selectedFactors.size()));
    for (const auto& factorId : selectedFactors) {
        const std::optional<factor::compute::FactorId> mappedFactorId = toFactorId(factorId.text());
        if (!mappedFactorId.has_value()) {
            return std::nullopt;
        }
        spec.requestedFactors.push_back(*mappedFactorId);
    }

    if (!spec.isValid()) {
        return std::nullopt;
    }

    return spec;
}

StageResult MissingStrategySignalProducerAdapter::generateSignal(RunContext& context) const
{
    StageResult stageResult;
    stageResult.stage = RunStage::GenerateSignal;
    stageResult.code = RunErrorCode::StageExecutionFailed;
    context.workingSet.signalBatch.factorSignalSet.reset();
    context.workingSet.signalBatch.strategySignalCount = 0U;
    return stageResult;
}

} // namespace application::backtest