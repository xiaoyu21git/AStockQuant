#include "factor_compute/FactorSignalAdapter.h"

#include <chrono>
#include <unordered_map>

namespace factor::compute {

FactorSignalAdapter::FactorSignalAdapter(
    const IFactorRegistry& registry,
    const IFactorComputeDispatcher& dispatcher,
    const IPostProcessingPipeline& postProcessingPipeline,
    const IFactorSignalSetAssembler& assembler,
    const IMarketDataView& marketDataView,
    ISignalCache& signalCache,
    const IAnalysisModule* analysisModule)
    : registry_(registry)
    , dispatcher_(dispatcher)
    , postProcessingPipeline_(postProcessingPipeline)
    , assembler_(assembler)
    , marketDataView_(marketDataView)
    , signalCache_(signalCache)
    , analysisModule_(analysisModule)
{
}

FactorResult<SignalSet>
FactorSignalAdapter::generate(const GenerateSpec& spec)
{
    resetAnalysisState();

    // --- 阶段 1: 参数校验 ---
    if (!spec.isValid()) {
        return FactorResult<SignalSet>::failure(FactorError::InvalidUniverse);
    }

    const SignalCacheKey cacheKey = [&spec]() {
        SignalCacheKey key;
        key.mode = spec.mode;
        key.dateRange = spec.dateRange;
        key.postProcessingConfig = spec.postProcessingConfig;
        key.instruments = spec.instrumentUniverse;
        key.factors = spec.requestedFactors;
        return key;
    }();

    if (!cacheKey.isValid()) {
        return FactorResult<SignalSet>::failure(FactorError::InvalidUniverse);
    }

    // --- 阶段 2: 缓存检查 ---
    const std::optional<SignalSet> cached = signalCache_.load(cacheKey);
    if (cached.has_value()) {
        return FactorResult<SignalSet>::success(cached.value());
    }

    // --- 阶段 3: Registry 构建计算计划 ---
    const FactorResult<ComputePlan> planResult = registry_.buildPlan(
        spec.requestedFactors,
        spec.dateRange,
        spec.instrumentUniverse);
    if (!planResult.hasValue()) {
        return FactorResult<SignalSet>::failure(planResult.error());
    }
    const ComputePlan& plan = planResult.value();

    if (plan.nodes.empty()) {
        return FactorResult<SignalSet>::failure(FactorError::InvalidFormula);
    }

    // --- 阶段 4: 验证行情视图可用性 ---
    const NumericConstMatrixView closeView = marketDataView_.close();
    if (!closeView.isValid()) {
        return FactorResult<SignalSet>::failure(FactorError::InsufficientData);
    }

    const auto& marketDates = marketDataView_.dates();
    const auto& marketInstruments = marketDataView_.instruments();

    if (static_cast<size_t>(closeView.rowCount) < plan.universe.size()
        || static_cast<size_t>(closeView.columnCount) < plan.universe.size()) {
        return FactorResult<SignalSet>::failure(FactorError::InsufficientData);
    }

    // --- 阶段 5: 计算 ---
    const auto startTime = std::chrono::steady_clock::now();
    uint32_t completedFactorCount = 0U;
    std::vector<FactorError> stageErrors;

    // 预分配结果缓冲区
    const int32_t timeCount = static_cast<int32_t>(marketDates.size());
    const int32_t instrumentCount = static_cast<int32_t>(spec.instrumentUniverse.size());
    const int32_t factorCount = static_cast<int32_t>(spec.requestedFactors.size());

    if (timeCount <= 0 || instrumentCount <= 0 || factorCount <= 0) {
        return FactorResult<SignalSet>::failure(FactorError::InvalidUniverse);
    }

    SignalTensorBuffer rawTensor;
    rawTensor.timeCount = timeCount;
    rawTensor.instrumentCount = instrumentCount;
    rawTensor.factorCount = factorCount;
    const size_t flatSize = static_cast<size_t>(timeCount)
        * static_cast<size_t>(instrumentCount)
        * static_cast<size_t>(factorCount);
    rawTensor.values = std::vector<double>(flatSize, 0.0);
    rawTensor.mask = std::vector<uint8_t>(flatSize, 1U);

    // 构建因子节点索引映射
    std::unordered_map<uint32_t, const ComputePlanNode*> nodeByFactorId;
    for (const auto& node : plan.nodes) {
        nodeByFactorId[node.factor.value] = &node;
    }

    for (int32_t factorIdx = 0; factorIdx < factorCount; ++factorIdx) {
        // 预算检查
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime);
        if (elapsed.count() > spec.runtimeBudget.timeoutMilliseconds) {
            stageErrors.push_back(FactorError::Timeout);
            break;
        }

        const FactorId factorId = spec.requestedFactors[static_cast<size_t>(factorIdx)];
        const auto it = nodeByFactorId.find(factorId.value);
        if (it == nodeByFactorId.end()) {
            return FactorResult<SignalSet>::failure(FactorError::InternalError);
        }

        const FactorResult<std::vector<double>> matrixResult =
            dispatcher_.evaluateOnClose(closeView, it->second->computeFunctionToken);
        if (!matrixResult.hasValue()) {
            return FactorResult<SignalSet>::failure(matrixResult.error());
        }

        const std::vector<double>& matrix = matrixResult.value();
        const size_t expectedSize = static_cast<size_t>(closeView.rowCount)
            * static_cast<size_t>(closeView.columnCount);
        if (matrix.size() < expectedSize) {
            return FactorResult<SignalSet>::failure(FactorError::InternalError);
        }

        // 填充张量切片
        for (int32_t t = 0; t < timeCount; ++t) {
            for (int32_t i = 0; i < instrumentCount; ++i) {
                const size_t flatIdx = static_cast<size_t>(t) * static_cast<size_t>(instrumentCount) * static_cast<size_t>(factorCount)
                    + static_cast<size_t>(i) * static_cast<size_t>(factorCount)
                    + static_cast<size_t>(factorIdx);
                const size_t matrixIdx = static_cast<size_t>(t) * static_cast<size_t>(closeView.columnCount)
                    + static_cast<size_t>(i);

                if (matrixIdx >= matrix.size()) {
                    return FactorResult<SignalSet>::failure(FactorError::InternalError);
                }

                const double value = matrix[matrixIdx];
                const bool missing = std::isnan(value);
                rawTensor.values[flatIdx] = missing ? 0.0 : value;
                rawTensor.mask[flatIdx] = missing ? 1U : 0U;
            }
        }

        ++completedFactorCount;
    }

    // --- 阶段 6: 后处理 ---
    const FactorResult<SignalTensorBuffer> processedResult =
        postProcessingPipeline_.run(std::move(rawTensor), spec);
    if (!processedResult.hasValue()) {
        return FactorResult<SignalSet>::failure(processedResult.error());
    }

    const SignalTensorBuffer& processed = processedResult.value();
    if (!processed.isValid()) {
        return FactorResult<SignalSet>::failure(FactorError::InternalError);
    }

    // --- 阶段 7: 装配 SignalSet ---
    AssembleContext assembleCtx;
    assembleCtx.dates = marketDates;
    assembleCtx.instruments = spec.instrumentUniverse;
    assembleCtx.factors = spec.requestedFactors;
    assembleCtx.progress.plannedFactorCount = static_cast<uint32_t>(factorCount);
    assembleCtx.progress.completedFactorCount = completedFactorCount;
    assembleCtx.isPartial = !stageErrors.empty();

    SignalSet signalSet = assembler_.assemble(processed.asView(), assembleCtx);
    signalSet.isPartial = !stageErrors.empty();

    // --- 阶段 8: 缓存（仅完整结果） ---
    if (stageErrors.empty()) {
        signalCache_.store(cacheKey, signalSet);
    }

    // --- 阶段 9: 分析（可选） ---
    if (analysisModule_ != nullptr && !signalSet.isPartial) {
        const FactorResult<AnalysisReport> analysisResult =
            analysisModule_->analyze(signalSet, spec, closeView);
        captureAnalysisResult(analysisResult);
    }

    return FactorResult<SignalSet>::success(signalSet);
}

FactorResult<SignalValue>
FactorSignalAdapter::query(const QuerySpec& spec) const
{
    if (!spec.isValid()) {
        return FactorResult<SignalValue>::failure(FactorError::InvalidUniverse);
    }

    const DateRange dateRange{spec.date, spec.date};
    const std::vector<InstrumentId> universe{spec.instrument};
    const std::vector<FactorId> factors{spec.factor};

    const FactorResult<ComputePlan> planResult = registry_.buildPlan(
        factors, dateRange, universe);
    if (!planResult.hasValue()) {
        return FactorResult<SignalValue>::failure(planResult.error());
    }

    const NumericConstMatrixView closeView = marketDataView_.close();
    if (!closeView.isValid()) {
        return FactorResult<SignalValue>::failure(FactorError::InsufficientData);
    }

    const auto& marketDates = marketDataView_.dates();
    const auto& marketInstruments = marketDataView_.instruments();

    // 查找日期索引
    int32_t dateIdx = -1;
    for (size_t i = 0; i < marketDates.size(); ++i) {
        if (marketDates[i].value == spec.date.value) {
            dateIdx = static_cast<int32_t>(i);
            break;
        }
    }

    // 查找标的索引
    int32_t instIdx = -1;
    for (size_t i = 0; i < marketInstruments.size(); ++i) {
        if (marketInstruments[i].value == spec.instrument.value) {
            instIdx = static_cast<int32_t>(i);
            break;
        }
    }

    if (dateIdx < 0 || instIdx < 0) {
        return FactorResult<SignalValue>::failure(FactorError::InsufficientData);
    }

    const ComputePlanNode& node = planResult.value().nodes.front();
    const FactorResult<std::vector<double>> matrixResult =
        dispatcher_.evaluateOnClose(closeView, node.computeFunctionToken);
    if (!matrixResult.hasValue()) {
        return FactorResult<SignalValue>::failure(matrixResult.error());
    }

    const std::vector<double>& matrix = matrixResult.value();
    const size_t flatIdx = static_cast<size_t>(dateIdx) * static_cast<size_t>(closeView.columnCount)
        + static_cast<size_t>(instIdx);
    if (flatIdx >= matrix.size()) {
        return FactorResult<SignalValue>::failure(FactorError::InternalError);
    }

    const double value = matrix[flatIdx];
    SignalValue sv;
    sv.value = std::isnan(value) ? 0.0 : value;
    sv.isMissing = std::isnan(value);
    return FactorResult<SignalValue>::success(sv);
}

void FactorSignalAdapter::resetAnalysisState() noexcept
{
    latestAnalysisReport_.reset();
    latestAnalysisError_.reset();
}

void FactorSignalAdapter::captureAnalysisResult(const FactorResult<AnalysisReport>& result) noexcept
{
    if (result.hasValue()) {
        latestAnalysisReport_ = result.value();
        latestAnalysisError_.reset();
    } else {
        latestAnalysisReport_.reset();
        latestAnalysisError_ = result.error();
    }
}

} // namespace factor::compute