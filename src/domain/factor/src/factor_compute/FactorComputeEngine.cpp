#include "factor_compute/FactorComputeEngine.h"

#include "factor_compute/IAnalysisModule.h"
#include "factor_compute/PostProcessingPipeline.h"
#include "factor_compute/SignalCache.h"

#include <cmath>
#include <chrono>
#include <unordered_map>
#include <vector>

namespace factor::compute {

namespace {

constexpr double kDefaultSignalValue = 0.0;
constexpr uint8_t kMissingMaskValue = 1U;
constexpr uint8_t kPresentMaskValue = 0U;
constexpr uint64_t kSingleSignalValueBytes = sizeof(double) + sizeof(uint8_t);
constexpr double kTempBufferRatio = 0.5;
constexpr double kMemorySafetyRatio = 0.8;
constexpr uint64_t kOperatorWorkspaceBytesEstimate = 64ULL * 1024ULL * 1024ULL;

struct DateAxisSelection final {
    std::vector<DateKey> dates;
    std::vector<int32_t> sourceRowIndices;

    [[nodiscard]] bool isValid() const noexcept
    {
        return !dates.empty() && dates.size() == sourceRowIndices.size();
    }
};

std::optional<int32_t> toInt32Index(size_t index);

DateAxisSelection buildDateAxisSelection(const DateRange& dateRange, const std::vector<DateKey>& marketDates)
{
    DateAxisSelection selection;
    selection.dates.reserve(marketDates.size());
    selection.sourceRowIndices.reserve(marketDates.size());
    for (size_t index = 0; index < marketDates.size(); ++index) {
        const std::optional<int32_t> sourceRowIndex = toInt32Index(index);
        if (!sourceRowIndex.has_value()) {
            break;
        }

        const DateKey date = marketDates[index];
        if (date.value < dateRange.from.value || date.value > dateRange.to.value) {
            continue;
        }

        selection.dates.push_back(date);
        selection.sourceRowIndices.push_back(sourceRowIndex.value());
    }
    return selection;
}

DateRange buildSingleDateRange(DateKey date)
{
    return DateRange{date, date};
}

std::vector<InstrumentId> buildSingleInstrumentUniverse(InstrumentId instrument)
{
    return std::vector<InstrumentId>{instrument};
}

std::vector<FactorId> buildSingleFactorRequest(FactorId factor)
{
    return std::vector<FactorId>{factor};
}

std::optional<size_t> checkedMultiply(size_t lhs, size_t rhs)
{
    if (lhs == 0U || rhs == 0U) {
        return static_cast<size_t>(0U);
    }

    if (lhs > (std::numeric_limits<size_t>::max() / rhs)) {
        return std::nullopt;
    }

    return lhs * rhs;
}

std::optional<size_t> buildTensorFlatCount(int32_t timeCount, int32_t instrumentCount, int32_t factorCount)
{
    if (timeCount < 0 || instrumentCount < 0 || factorCount < 0) {
        return std::nullopt;
    }

    const std::optional<size_t> timeInstrumentCount = checkedMultiply(
        static_cast<size_t>(timeCount),
        static_cast<size_t>(instrumentCount));
    if (!timeInstrumentCount.has_value()) {
        return std::nullopt;
    }

    return checkedMultiply(timeInstrumentCount.value(), static_cast<size_t>(factorCount));
}

std::optional<int32_t> toInt32Index(size_t index)
{
    if (index > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        return std::nullopt;
    }

    return static_cast<int32_t>(index);
}

SignalCacheKey buildSignalCacheKey(const GenerateSpec& spec)
{
    SignalCacheKey key;
    key.mode = spec.mode;
    key.dateRange = spec.dateRange;
    key.postProcessingConfig = spec.postProcessingConfig;
    key.instruments = spec.instrumentUniverse;
    key.factors = spec.requestedFactors;
    return key;
}

bool isMissingValue(double value)
{
    return std::isnan(value);
}

/// @brief 设计文档 Section 6.2 内存预估公式
///
/// base_bytes = T * N * F * sizeof(double)
/// mask_bytes = T * N * F * sizeof(uint8_t)
/// temp_bytes = base_bytes * temp_buffer_ratio
/// estimated_total = base_bytes + mask_bytes + temp_bytes + operator_workspace_bytes
/// 若 estimated_total > available_memory * kMemorySafetyRatio，立即返回 true。
bool exceedsMemoryBudget(int32_t timeCount, int32_t instrumentCount, int32_t factorCount, const RuntimeBudget& budget)
{
    const uint64_t elementCount = static_cast<uint64_t>(timeCount)
        * static_cast<uint64_t>(instrumentCount)
        * static_cast<uint64_t>(factorCount);
    const uint64_t baseBytes = elementCount * sizeof(double);
    const uint64_t maskBytes = elementCount * sizeof(uint8_t);
    const uint64_t tempBytes = static_cast<uint64_t>(static_cast<double>(baseBytes) * kTempBufferRatio);

    // 防止加法溢出
    uint64_t estimatedTotal = baseBytes;
    if (estimatedTotal > std::numeric_limits<uint64_t>::max() - maskBytes) {
        return true;
    }
    estimatedTotal += maskBytes;
    if (estimatedTotal > std::numeric_limits<uint64_t>::max() - tempBytes) {
        return true;
    }
    estimatedTotal += tempBytes;
    if (estimatedTotal > std::numeric_limits<uint64_t>::max() - kOperatorWorkspaceBytesEstimate) {
        return true;
    }
    estimatedTotal += kOperatorWorkspaceBytesEstimate;

    const uint64_t safetyLimit = static_cast<uint64_t>(
        static_cast<double>(budget.memoryLimitBytes) * kMemorySafetyRatio);
    return estimatedTotal > safetyLimit;
}

int32_t findDateIndex(const std::vector<DateKey>& dates, DateKey date)
{
    for (size_t index = 0; index < dates.size(); ++index) {
        if (dates[index].value == date.value) {
            const std::optional<int32_t> dateIndex = toInt32Index(index);
            return dateIndex.value_or(-1);
        }
    }
    return -1;
}

int32_t findInstrumentIndex(const std::vector<InstrumentId>& instruments, InstrumentId instrument)
{
    for (size_t index = 0; index < instruments.size(); ++index) {
        if (instruments[index].value == instrument.value) {
            const std::optional<int32_t> instrumentIndex = toInt32Index(index);
            return instrumentIndex.value_or(-1);
        }
    }
    return -1;
}

std::optional<size_t> flattenIndex(
    int32_t timeIndex,
    int32_t instrumentIndex,
    int32_t factorIndex,
    int32_t instrumentCount,
    int32_t factorCount)
{
    if (timeIndex < 0
        || instrumentIndex < 0
        || factorIndex < 0
        || instrumentCount <= 0
        || factorCount <= 0) {
        return std::nullopt;
    }

    const std::optional<size_t> timeStride = checkedMultiply(
        static_cast<size_t>(instrumentCount),
        static_cast<size_t>(factorCount));
    if (!timeStride.has_value()) {
        return std::nullopt;
    }

    const std::optional<size_t> timeOffset = checkedMultiply(
        static_cast<size_t>(timeIndex),
        timeStride.value());
    if (!timeOffset.has_value()) {
        return std::nullopt;
    }

    const std::optional<size_t> instrumentOffset = checkedMultiply(
        static_cast<size_t>(instrumentIndex),
        static_cast<size_t>(factorCount));
    if (!instrumentOffset.has_value()) {
        return std::nullopt;
    }

    if (timeOffset.value() > (std::numeric_limits<size_t>::max() - instrumentOffset.value())) {
        return std::nullopt;
    }
    const size_t timeInstrumentOffset = timeOffset.value() + instrumentOffset.value();
    if (timeInstrumentOffset > (std::numeric_limits<size_t>::max() - static_cast<size_t>(factorIndex))) {
        return std::nullopt;
    }

    return timeInstrumentOffset + static_cast<size_t>(factorIndex);
}

std::optional<size_t> flattenMatrixIndex(int32_t rowIndex, int32_t columnIndex, int32_t columnCount)
{
    if (rowIndex < 0 || columnIndex < 0 || columnCount <= 0) {
        return std::nullopt;
    }

    const std::optional<size_t> rowOffset = checkedMultiply(
        static_cast<size_t>(rowIndex),
        static_cast<size_t>(columnCount));
    if (!rowOffset.has_value()) {
        return std::nullopt;
    }

    if (rowOffset.value() > (std::numeric_limits<size_t>::max() - static_cast<size_t>(columnIndex))) {
        return std::nullopt;
    }

    return rowOffset.value() + static_cast<size_t>(columnIndex);
}

[[nodiscard]] bool hasSufficientMatrixSize(
    const std::vector<double>& matrix,
    NumericConstMatrixView view) noexcept
{
    const std::optional<size_t> expectedMatrixSize = checkedMultiply(
        static_cast<size_t>(view.rowCount),
        static_cast<size_t>(view.columnCount));
    return expectedMatrixSize.has_value() && matrix.size() >= expectedMatrixSize.value();
}

class GenerateWorkflow final {
public:
    GenerateWorkflow(
        const GenerateSpec& spec,
        const SignalCacheKey& cacheKey,
        const IFactorRegistry& factorRegistry,
        const IFactorSignalSetAssembler& signalSetAssembler,
        const IFactorComputeDispatcher& factorComputeDispatcher,
        const IMarketDataView& marketDataView,
        ISignalCache& signalCache,
        const IPostProcessingPipeline& postProcessingPipeline)
        : spec_(spec)
        , cacheKey_(cacheKey)
        , factorRegistry_(factorRegistry)
        , signalSetAssembler_(signalSetAssembler)
        , factorComputeDispatcher_(factorComputeDispatcher)
        , marketDataView_(marketDataView)
        , signalCache_(signalCache)
        , postProcessingPipeline_(postProcessingPipeline)
        , startAt_(std::chrono::steady_clock::now())
    {
    }

    [[nodiscard]] FactorResult<SignalSet> run()
    {
        const FactorResult<ComputePlan> computePlanResult = factorRegistry_.buildPlan(
            spec_.requestedFactors,
            spec_.dateRange,
            spec_.instrumentUniverse);
        if (!computePlanResult.hasValue()) {
            return FactorResult<SignalSet>::failure(computePlanResult.error());
        }

        const FactorError prepareError = prepareExecutionContext(computePlanResult.value());
        if (prepareError != FactorError::None) {
            return FactorResult<SignalSet>::failure(prepareError);
        }

        const FactorError fillError = fillRawTensor();
        if (fillError != FactorError::None) {
            return FactorResult<SignalSet>::failure(fillError);
        }

        const FactorResult<SignalTensorBuffer> processedTensorResult =
            postProcessingPipeline_.run(std::move(rawTensor_), spec_);
        if (!processedTensorResult.hasValue()) {
            return FactorResult<SignalSet>::failure(processedTensorResult.error());
        }

        const SignalTensorBuffer& processedTensor = processedTensorResult.value();
        if (!processedTensor.isValid()) {
            return FactorResult<SignalSet>::failure(FactorError::InternalError);
        }

        AssembleContext assembleContext;
        assembleContext.dates = dateAxisSelection_.dates;
        assembleContext.instruments = spec_.instrumentUniverse;
        assembleContext.factors = spec_.requestedFactors;
        assembleContext.progress.plannedFactorCount = static_cast<uint32_t>(factorCount_);
        assembleContext.progress.completedFactorCount = completedFactorCount_;
        assembleContext.isPartial = hasPartialResult_;

        const SignalSet signalSet = signalSetAssembler_.assemble(processedTensor.asView(), assembleContext);
        if (!hasPartialResult_) {
            signalCache_.store(cacheKey_, signalSet);
        }

        return FactorResult<SignalSet>::success(signalSet);
    }

    [[nodiscard]] NumericConstMatrixView closeView() const noexcept
    {
        return closeView_;
    }

private:
    [[nodiscard]] bool hasTimedOut() const
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startAt_);
        return elapsed.count() > spec_.runtimeBudget.timeoutMilliseconds;
    }

    [[nodiscard]] FactorError prepareExecutionContext(const ComputePlan& computePlan)
    {
        nodeByFactorId_.reserve(computePlan.nodes.size());
        for (const ComputePlanNode& node : computePlan.nodes) {
            if (!node.isValid()) {
                return FactorError::InvalidFormula;
            }
            nodeByFactorId_.emplace(node.factor.value, &node);
        }

        dateAxisSelection_ = buildDateAxisSelection(spec_.dateRange, marketDataView_.dates());
        if (!dateAxisSelection_.isValid()) {
            return FactorError::InsufficientData;
        }

        timeCount_ = static_cast<int32_t>(dateAxisSelection_.dates.size());
        instrumentCount_ = static_cast<int32_t>(spec_.instrumentUniverse.size());
        factorCount_ = static_cast<int32_t>(spec_.requestedFactors.size());

        if (exceedsMemoryBudget(timeCount_, instrumentCount_, factorCount_, spec_.runtimeBudget)) {
            return FactorError::MemoryExceeded;
        }

        const std::optional<size_t> flatCount = buildTensorFlatCount(timeCount_, instrumentCount_, factorCount_);
        if (!flatCount.has_value()) {
            return FactorError::MemoryExceeded;
        }

        rawTensor_.values = std::vector<double>(flatCount.value(), kDefaultSignalValue);
        rawTensor_.mask = std::vector<uint8_t>(flatCount.value(), kMissingMaskValue);
        rawTensor_.timeCount = timeCount_;
        rawTensor_.instrumentCount = instrumentCount_;
        rawTensor_.factorCount = factorCount_;

        closeView_ = marketDataView_.close();
        if (!closeView_.isValid()) {
            return FactorError::InsufficientData;
        }
        if (closeView_.rowCount < timeCount_ || closeView_.columnCount < instrumentCount_) {
            return FactorError::InsufficientData;
        }

        return FactorError::None;
    }

    [[nodiscard]] FactorError fillRawTensor()
    {
        for (int32_t factorIndex = 0; factorIndex < factorCount_; ++factorIndex) {
            if (hasTimedOut()) {
                hasPartialResult_ = completedFactorCount_ < static_cast<uint32_t>(factorCount_);
                break;
            }

            const FactorId factorId = spec_.requestedFactors[static_cast<size_t>(factorIndex)];
            const auto nodeIt = nodeByFactorId_.find(factorId.value);
            if (nodeIt == nodeByFactorId_.end()) {
                return FactorError::InternalError;
            }

            const ComputePlanNode& node = *nodeIt->second;
            const FactorResult<std::vector<double>> factorMatrixResult = factorComputeDispatcher_.evaluateOnClose(
                closeView_,
                node.computeFunctionToken);
            if (!factorMatrixResult.hasValue()) {
                return factorMatrixResult.error();
            }

            const std::vector<double>& factorMatrix = factorMatrixResult.value();
            if (!hasSufficientMatrixSize(factorMatrix, closeView_)) {
                return FactorError::InternalError;
            }

            const FactorError fillFactorError = fillSingleFactorSlice(factorIndex, factorMatrix);
            if (fillFactorError != FactorError::None) {
                return fillFactorError;
            }

            ++completedFactorCount_;

            if (hasTimedOut()) {
                const bool hasRemainingFactors = (factorIndex + 1) < factorCount_;
                if (hasRemainingFactors) {
                    hasPartialResult_ = true;
                    break;
                }
            }
        }

        return FactorError::None;
    }

    [[nodiscard]] FactorError fillSingleFactorSlice(
        int32_t factorIndex,
        const std::vector<double>& factorMatrix)
    {
        for (int32_t timeIndex = 0; timeIndex < timeCount_; ++timeIndex) {
            for (int32_t instrumentIndex = 0; instrumentIndex < instrumentCount_; ++instrumentIndex) {
                const int32_t sourceRowIndex = dateAxisSelection_.sourceRowIndices[static_cast<size_t>(timeIndex)];
                if (sourceRowIndex < 0 || sourceRowIndex >= closeView_.rowCount) {
                    return FactorError::InsufficientData;
                }

                const std::optional<size_t> matrixFlat = flattenMatrixIndex(
                    sourceRowIndex,
                    instrumentIndex,
                    closeView_.columnCount);
                const std::optional<size_t> tensorFlat = flattenIndex(
                    timeIndex,
                    instrumentIndex,
                    factorIndex,
                    instrumentCount_,
                    factorCount_);
                if (!matrixFlat.has_value()
                    || !tensorFlat.has_value()
                    || matrixFlat.value() >= factorMatrix.size()
                    || tensorFlat.value() >= rawTensor_.values.size()
                    || tensorFlat.value() >= rawTensor_.mask.size()) {
                    return FactorError::InternalError;
                }

                const double computedValue = factorMatrix[matrixFlat.value()];
                rawTensor_.values[tensorFlat.value()] = isMissingValue(computedValue) ? kDefaultSignalValue : computedValue;
                rawTensor_.mask[tensorFlat.value()] = isMissingValue(computedValue) ? kMissingMaskValue : kPresentMaskValue;
            }
        }

        return FactorError::None;
    }

    const GenerateSpec& spec_;
    const SignalCacheKey cacheKey_;
    const IFactorRegistry& factorRegistry_;
    const IFactorSignalSetAssembler& signalSetAssembler_;
    const IFactorComputeDispatcher& factorComputeDispatcher_;
    const IMarketDataView& marketDataView_;
    ISignalCache& signalCache_;
    const IPostProcessingPipeline& postProcessingPipeline_;
    std::chrono::steady_clock::time_point startAt_;
    DateAxisSelection dateAxisSelection_;
    SignalTensorBuffer rawTensor_;
    NumericConstMatrixView closeView_;
    std::unordered_map<uint32_t, const ComputePlanNode*> nodeByFactorId_;
    int32_t timeCount_{0};
    int32_t instrumentCount_{0};
    int32_t factorCount_{0};
    uint32_t completedFactorCount_{0U};
    bool hasPartialResult_{false};
};

class QueryWorkflow final {
public:
    QueryWorkflow(
        const QuerySpec& spec,
        const IFactorRegistry& factorRegistry,
        const IFactorComputeDispatcher& factorComputeDispatcher,
        const IMarketDataView& marketDataView)
        : spec_(spec)
        , factorRegistry_(factorRegistry)
        , factorComputeDispatcher_(factorComputeDispatcher)
        , marketDataView_(marketDataView)
    {
    }

    [[nodiscard]] FactorResult<SignalValue> run() const
    {
        if (!spec_.isValid()) {
            return FactorResult<SignalValue>::failure(FactorError::InvalidUniverse);
        }

        const FactorResult<ComputePlan> computePlanResult = factorRegistry_.buildPlan(
            buildSingleFactorRequest(spec_.factor),
            buildSingleDateRange(spec_.date),
            buildSingleInstrumentUniverse(spec_.instrument));
        if (!computePlanResult.hasValue()) {
            return FactorResult<SignalValue>::failure(computePlanResult.error());
        }

        const FactorResult<QueryContext> contextResult = buildQueryContext(computePlanResult.value());
        if (!contextResult.hasValue()) {
            return FactorResult<SignalValue>::failure(contextResult.error());
        }

        return resolveSignalValue(contextResult.value());
    }

private:
    struct QueryContext final {
        ComputePlanNode node;
        NumericConstMatrixView closeView;
        int32_t dateIndex{0};
        int32_t instrumentIndex{0};
    };

    [[nodiscard]] FactorResult<QueryContext> buildQueryContext(const ComputePlan& computePlan) const
    {
        if (computePlan.nodes.size() != 1U || !computePlan.nodes.front().isValid()) {
            return FactorResult<QueryContext>::failure(FactorError::InternalError);
        }

        QueryContext context;
        context.node = computePlan.nodes.front();
        if (context.node.factor.value != spec_.factor.value) {
            return FactorResult<QueryContext>::failure(FactorError::InternalError);
        }

        context.closeView = marketDataView_.close();
        if (!context.closeView.isValid()) {
            return FactorResult<QueryContext>::failure(FactorError::InsufficientData);
        }

        context.dateIndex = findDateIndex(marketDataView_.dates(), spec_.date);
        context.instrumentIndex = findInstrumentIndex(marketDataView_.instruments(), spec_.instrument);
        if (context.dateIndex < 0 || context.instrumentIndex < 0) {
            return FactorResult<QueryContext>::failure(FactorError::InsufficientData);
        }
        if (context.dateIndex >= context.closeView.rowCount || context.instrumentIndex >= context.closeView.columnCount) {
            return FactorResult<QueryContext>::failure(FactorError::InsufficientData);
        }

        return FactorResult<QueryContext>::success(context);
    }

    [[nodiscard]] FactorResult<SignalValue> resolveSignalValue(const QueryContext& context) const
    {
        const FactorResult<std::vector<double>> factorMatrixResult = factorComputeDispatcher_.evaluateOnClose(
            context.closeView,
            context.node.computeFunctionToken);
        if (!factorMatrixResult.hasValue()) {
            return FactorResult<SignalValue>::failure(factorMatrixResult.error());
        }

        const std::vector<double>& factorMatrix = factorMatrixResult.value();
        const std::optional<size_t> matrixFlat =
            flattenMatrixIndex(context.dateIndex, context.instrumentIndex, context.closeView.columnCount);
        if (!hasSufficientMatrixSize(factorMatrix, context.closeView)
            || !matrixFlat.has_value()
            || matrixFlat.value() >= factorMatrix.size()) {
            return FactorResult<SignalValue>::failure(FactorError::InternalError);
        }

        const double value = factorMatrix[matrixFlat.value()];

        SignalValue signalValue;
        signalValue.value = isMissingValue(value) ? kDefaultSignalValue : value;
        signalValue.isMissing = isMissingValue(value);
        return FactorResult<SignalValue>::success(signalValue);
    }

    const QuerySpec& spec_;
    const IFactorRegistry& factorRegistry_;
    const IFactorComputeDispatcher& factorComputeDispatcher_;
    const IMarketDataView& marketDataView_;
};

} // namespace

FactorComputeEngine::FactorComputeEngine(
    const IFactorRegistry& factorRegistry,
    const IFactorSignalSetAssembler& signalSetAssembler,
    const IFactorComputeDispatcher& factorComputeDispatcher,
    const IMarketDataView& marketDataView) noexcept
    : ownedSignalCache_(std::make_unique<SignalCache>())
    , ownedPostProcessingPipeline_(std::make_unique<PostProcessingPipeline>())
    , ownedAnalysisModule_(std::make_unique<AnalysisModule>())
    , factorRegistry_(factorRegistry)
    , signalSetAssembler_(signalSetAssembler)
    , factorComputeDispatcher_(factorComputeDispatcher)
    , marketDataView_(marketDataView)
    , signalCache_(ownedSignalCache_.get())
    , postProcessingPipeline_(ownedPostProcessingPipeline_.get())
    , analysisModule_(ownedAnalysisModule_.get())
{
}

FactorComputeEngine::FactorComputeEngine(
    const IFactorRegistry& factorRegistry,
    const IFactorSignalSetAssembler& signalSetAssembler,
    const IFactorComputeDispatcher& factorComputeDispatcher,
    const IMarketDataView& marketDataView,
    ISignalCache& signalCache,
    const IPostProcessingPipeline& postProcessingPipeline) noexcept
    : ownedAnalysisModule_(std::make_unique<AnalysisModule>())
    , factorRegistry_(factorRegistry)
    , signalSetAssembler_(signalSetAssembler)
    , factorComputeDispatcher_(factorComputeDispatcher)
    , marketDataView_(marketDataView)
    , signalCache_(&signalCache)
    , postProcessingPipeline_(&postProcessingPipeline)
    , analysisModule_(ownedAnalysisModule_.get())
{
}

FactorComputeEngine::FactorComputeEngine(
    const IFactorRegistry& factorRegistry,
    const IFactorSignalSetAssembler& signalSetAssembler,
    const IFactorComputeDispatcher& factorComputeDispatcher,
    const IMarketDataView& marketDataView,
    ISignalCache& signalCache,
    const IPostProcessingPipeline& postProcessingPipeline,
    const IAnalysisModule& analysisModule) noexcept
    : factorRegistry_(factorRegistry)
    , signalSetAssembler_(signalSetAssembler)
    , factorComputeDispatcher_(factorComputeDispatcher)
    , marketDataView_(marketDataView)
    , signalCache_(&signalCache)
    , postProcessingPipeline_(&postProcessingPipeline)
    , analysisModule_(&analysisModule)
{
}

FactorResult<SignalSet>
FactorComputeEngine::generate(const GenerateSpec& spec)
{
    resetLatestAnalysisState();

    if (!spec.isValid()) {
        return FactorResult<SignalSet>::failure(FactorError::InvalidUniverse);
    }

    const SignalCacheKey cacheKey = buildSignalCacheKey(spec);
    if (!cacheKey.isValid()) {
        return FactorResult<SignalSet>::failure(FactorError::InvalidUniverse);
    }

    const std::optional<SignalSet> cachedSignalSet = signalCache_->load(cacheKey);
    if (cachedSignalSet.has_value()) {
        return FactorResult<SignalSet>::success(cachedSignalSet.value());
    }

    GenerateWorkflow workflow(
        spec,
        cacheKey,
        factorRegistry_,
        signalSetAssembler_,
        factorComputeDispatcher_,
        marketDataView_,
        *signalCache_,
        *postProcessingPipeline_);
    const FactorResult<SignalSet> generateResult = workflow.run();
    if (!generateResult.hasValue()) {
        return generateResult;
    }

    const SignalSet signalSet = generateResult.value();
    const FactorResult<AnalysisReport> analysisResult = analysisModule_->analyze(signalSet, spec, workflow.closeView());
    captureLatestAnalysisState(analysisResult);

    return FactorResult<SignalSet>::success(signalSet);
}

FactorResult<SignalValue>
FactorComputeEngine::query(const QuerySpec& spec) const
{
    const QueryWorkflow workflow(spec, factorRegistry_, factorComputeDispatcher_, marketDataView_);
    return workflow.run();
}

std::optional<FactorQualityMetrics16View>
FactorComputeEngine::latestFactorQualityMetrics16() const noexcept
{
    if (!latestAnalysisReport_.has_value()) {
        return std::nullopt;
    }

    return buildFactorQualityMetrics16View(latestAnalysisReport_.value());
}

std::optional<FactorQualityMetrics16DiagnosticsView>
FactorComputeEngine::latestFactorQualityDiagnostics16() const noexcept
{
    if (!latestAnalysisReport_.has_value()) {
        return std::nullopt;
    }

    return buildFactorQualityMetrics16DiagnosticsView(latestAnalysisReport_.value());
}

std::optional<FactorQualityMetrics16Snapshot>
FactorComputeEngine::latestFactorQualitySnapshot16() const noexcept
{
    if (!latestAnalysisReport_.has_value()) {
        return std::nullopt;
    }

    return buildFactorQualityMetrics16Snapshot(latestAnalysisReport_.value());
}

void
FactorComputeEngine::resetLatestAnalysisState() noexcept
{
    latestAnalysisReport_.reset();
    latestAnalysisError_.reset();
}

void
FactorComputeEngine::captureLatestAnalysisState(const FactorResult<AnalysisReport>& analysisResult) noexcept
{
    if (analysisResult.hasValue()) {
        latestAnalysisReport_ = analysisResult.value();
        latestAnalysisError_.reset();
        return;
    }

    latestAnalysisReport_.reset();
    latestAnalysisError_ = analysisResult.error();
}

} // namespace factor::compute


