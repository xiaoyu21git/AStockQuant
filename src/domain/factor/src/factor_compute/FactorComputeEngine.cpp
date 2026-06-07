#include "factor_compute/FactorComputeEngine.h"

#include "factor_compute/IAnalysisModule.h"
#include "factor_compute/ISignalEngine.h"
#include "factor_compute/PostProcessingPipeline.h"
#include "factor_compute/SignalCache.h"

#include <chrono>
#include <cmath>
#include <future>
#include <thread>
#include <unordered_map>
#include <vector>

namespace factor::compute {

namespace {

constexpr signal_value_t kDefaultSignalValue = 0.0f;
constexpr uint8_t kMissingMaskValue = 1U;
constexpr uint8_t kPresentMaskValue = 0U;
constexpr uint64_t kSingleSignalValueBytes = sizeof(signal_value_t) + sizeof(uint8_t);
constexpr double kTempBufferRatio = 0.5;
constexpr double kMemorySafetyRatio = 0.8;
constexpr uint64_t kOperatorWorkspaceBytesEstimate = 64ULL * 1024ULL * 1024ULL;
constexpr int32_t kMaxParallelFactorCount = 8;  // 并行计算上限（防止过度并发）

struct DateAxisSelection final {
    std::vector<DateKey> dates;
    std::vector<int32_t> sourceRowIndices;

    [[nodiscard]] bool isValid() const noexcept
    {
        return !dates.empty() && dates.size() == sourceRowIndices.size();
    }
};

// ... 其余函数保持不变（toInt32Index, buildDateAxisSelection 等）
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
        static_cast<size_t>(timeCount), static_cast<size_t>(instrumentCount));
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

bool isMissingValue(signal_value_t value)
{
    return std::isnan(value);
}

/// @brief 内存预估公式
bool exceedsMemoryBudget(int32_t timeCount, int32_t instrumentCount, int32_t factorCount, const RuntimeBudget& budget)
{
    const uint64_t elementCount = static_cast<uint64_t>(timeCount)
        * static_cast<uint64_t>(instrumentCount)
        * static_cast<uint64_t>(factorCount);
    const uint64_t baseBytes = elementCount * sizeof(signal_value_t);
    const uint64_t maskBytes = elementCount * sizeof(uint8_t);
    const uint64_t tempBytes = static_cast<uint64_t>(static_cast<double>(baseBytes) * kTempBufferRatio);

    uint64_t estimatedTotal = baseBytes;
    if (estimatedTotal > std::numeric_limits<uint64_t>::max() - maskBytes) return true;
    estimatedTotal += maskBytes;
    if (estimatedTotal > std::numeric_limits<uint64_t>::max() - tempBytes) return true;
    estimatedTotal += tempBytes;
    if (estimatedTotal > std::numeric_limits<uint64_t>::max() - kOperatorWorkspaceBytesEstimate) return true;
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
    int32_t timeIndex, int32_t instrumentIndex, int32_t factorIndex,
    int32_t instrumentCount, int32_t factorCount)
{
    if (timeIndex < 0 || instrumentIndex < 0 || factorIndex < 0
        || instrumentCount <= 0 || factorCount <= 0) {
        return std::nullopt;
    }
    const std::optional<size_t> timeStride = checkedMultiply(
        static_cast<size_t>(instrumentCount), static_cast<size_t>(factorCount));
    if (!timeStride.has_value()) return std::nullopt;

    const std::optional<size_t> timeOffset = checkedMultiply(
        static_cast<size_t>(timeIndex), timeStride.value());
    if (!timeOffset.has_value()) return std::nullopt;

    const std::optional<size_t> instrumentOffset = checkedMultiply(
        static_cast<size_t>(instrumentIndex), static_cast<size_t>(factorCount));
    if (!instrumentOffset.has_value()) return std::nullopt;

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
    if (rowIndex < 0 || columnIndex < 0 || columnCount <= 0) return std::nullopt;
    const std::optional<size_t> rowOffset = checkedMultiply(
        static_cast<size_t>(rowIndex), static_cast<size_t>(columnCount));
    if (!rowOffset.has_value()) return std::nullopt;
    if (rowOffset.value() > (std::numeric_limits<size_t>::max() - static_cast<size_t>(columnIndex))) {
        return std::nullopt;
    }
    return rowOffset.value() + static_cast<size_t>(columnIndex);
}

[[nodiscard]] bool hasSufficientMatrixSize(
    const std::vector<signal_value_t>& matrix,
    NumericConstMatrixView view) noexcept
{
    const std::optional<size_t> expectedMatrixSize = checkedMultiply(
        static_cast<size_t>(view.rowCount), static_cast<size_t>(view.columnCount));
    return expectedMatrixSize.has_value() && matrix.size() >= expectedMatrixSize.value();
}

/// @brief 并行任务线程数：逻辑核 - 2，至少 1，最多 kMaxParallelFactorCount
int32_t computeParallelThreadCount() noexcept
{
    int32_t hc = static_cast<int32_t>(std::thread::hardware_concurrency());
    if (hc <= 2) return 1;
    return (std::min)(hc - 2, kMaxParallelFactorCount);
}

/// @brief 单个因子的计算结果（用于并行任务间传递）
struct SingleFactorSliceResult final {
    FactorError error{FactorError::None};
    std::vector<signal_value_t> factorMatrix;
};

// ========================================================================
// GenerateWorkflow：核心工作流类（并行 fillRawTensor 改造）
// ========================================================================
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
            spec_.requestedFactors, spec_.dateRange, spec_.instrumentUniverse);
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
            if (!node.isValid()) return FactorError::InvalidFormula;
            nodeByFactorId_.emplace(node.factor.value, &node);
        }

        dateAxisSelection_ = buildDateAxisSelection(spec_.dateRange, marketDataView_.dates());
        if (!dateAxisSelection_.isValid()) return FactorError::InsufficientData;

        timeCount_ = static_cast<int32_t>(dateAxisSelection_.dates.size());
        instrumentCount_ = static_cast<int32_t>(spec_.instrumentUniverse.size());
        factorCount_ = static_cast<int32_t>(spec_.requestedFactors.size());

        if (exceedsMemoryBudget(timeCount_, instrumentCount_, factorCount_, spec_.runtimeBudget)) {
            return FactorError::MemoryExceeded;
        }

        const std::optional<size_t> flatCount = buildTensorFlatCount(timeCount_, instrumentCount_, factorCount_);
        if (!flatCount.has_value()) return FactorError::MemoryExceeded;

        rawTensor_.values = std::vector<signal_value_t>(flatCount.value(), kDefaultSignalValue);
        rawTensor_.mask = std::vector<uint8_t>(flatCount.value(), kMissingMaskValue);
        rawTensor_.timeCount = timeCount_;
        rawTensor_.instrumentCount = instrumentCount_;
        rawTensor_.factorCount = factorCount_;

        closeView_ = marketDataView_.close();
        if (!closeView_.isValid()) return FactorError::InsufficientData;
        if (closeView_.rowCount < timeCount_ || closeView_.columnCount < instrumentCount_) {
            return FactorError::InsufficientData;
        }
        return FactorError::None;
    }

    /// @brief 并行填充 tensor（Phase 3 核心改造）
    [[nodiscard]] FactorError fillRawTensor()
    {
        if (factorCount_ <= 0) return FactorError::None;

        const int32_t threadCount = computeParallelThreadCount();

        // 当因子数少或线程资源有限时退化回串行
        if (factorCount_ < 4 || threadCount <= 1) {
            return fillRawTensorSerial();
        }

        return fillRawTensorParallel(threadCount);
    }

    /// @brief 串行回退路径
    [[nodiscard]] FactorError fillRawTensorSerial()
    {
        for (int32_t factorIndex = 0; factorIndex < factorCount_; ++factorIndex) {
            if (hasTimedOut()) {
                hasPartialResult_ = completedFactorCount_ < static_cast<uint32_t>(factorCount_);
                break;
            }
            const FactorError err = computeAndFillOneFactor(factorIndex);
            if (err != FactorError::None) return err;
            ++completedFactorCount_;
        }
        return FactorError::None;
    }

    /// @brief 并行路径：使用 std::async 并行计算所有因子
    [[nodiscard]] FactorError fillRawTensorParallel(int32_t threadCount)
    {
        // 限制并发因子数
        const int32_t effectiveFactorCount = (std::min)(factorCount_, kMaxParallelFactorCount);

        std::vector<std::future<SingleFactorSliceResult>> futures;
        futures.reserve(static_cast<size_t>(effectiveFactorCount));

        // 启动并行任务
        for (int32_t fi = 0; fi < effectiveFactorCount; ++fi) {
            futures.push_back(std::async(std::launch::async,
                [this, fi]() -> SingleFactorSliceResult {
                    SingleFactorSliceResult result;
                    if (hasTimedOut()) {
                        result.error = FactorError::Timeout;
                        return result;
                    }
                    const FactorId factorId = spec_.requestedFactors[static_cast<size_t>(fi)];
                    const auto nodeIt = nodeByFactorId_.find(factorId.value);
                    if (nodeIt == nodeByFactorId_.end()) {
                        result.error = FactorError::InternalError;
                        return result;
                    }
                    const ComputePlanNode& node = *nodeIt->second;
                    if (node.fieldName.empty()) {
                        result.error = FactorError::InsufficientData;
                        return result;
                    }
                    auto fieldOpt = marketDataView_.getField(node.fieldName);
                    if (!fieldOpt.has_value()) {
                        result.error = FactorError::InsufficientData;
                        return result;
                    }
                    const NumericConstMatrixView fieldView = fieldOpt.value();
                    const FactorResult<std::vector<signal_value_t>> matrixResult =
                        factorComputeDispatcher_.evaluateOnClose(fieldView, node.computeFunctionToken);
                    if (!matrixResult.hasValue()) {
                        result.error = matrixResult.error();
                        return result;
                    }
                    result.factorMatrix = matrixResult.value();
                    return result;
                }));
        }

        // 收集结果并填充到 tensor
        for (int32_t fi = 0; fi < effectiveFactorCount; ++fi) {
            SingleFactorSliceResult result = futures[static_cast<size_t>(fi)].get();
            if (result.error != FactorError::None) {
                return result.error;
            }

            const FactorError fillErr = fillSingleFactorSlice(fi, result.factorMatrix);
            if (fillErr != FactorError::None) return fillErr;

            ++completedFactorCount_;
        }

        // 如果有超出并发上限的剩余因子，串行处理
        for (int32_t fi = effectiveFactorCount; fi < factorCount_; ++fi) {
            if (hasTimedOut()) {
                hasPartialResult_ = completedFactorCount_ < static_cast<uint32_t>(factorCount_);
                break;
            }
            const FactorError err = computeAndFillOneFactor(fi);
            if (err != FactorError::None) return err;
            ++completedFactorCount_;
        }

        return FactorError::None;
    }

    /// @brief 计算并填充单个因子
    /// 根据因子注册时声明的 fieldName 获取对应的数据列进行运算
    /// 字段不可用时返回 InsufficientData
    [[nodiscard]] FactorError computeAndFillOneFactor(int32_t factorIndex)
    {
        const FactorId factorId = spec_.requestedFactors[static_cast<size_t>(factorIndex)];
        const auto nodeIt = nodeByFactorId_.find(factorId.value);
        if (nodeIt == nodeByFactorId_.end()) return FactorError::InternalError;

        const ComputePlanNode& node = *nodeIt->second;

        // 因子必须声明 fieldName，据此从行情视图获取对应的数据列
        if (node.fieldName.empty()) return FactorError::InsufficientData;
        const std::optional<NumericConstMatrixView> fieldOpt =
            marketDataView_.getField(node.fieldName);
        if (!fieldOpt.has_value()) return FactorError::InsufficientData;
        const NumericConstMatrixView fieldView = fieldOpt.value();

        if (!fieldView.isValid()) {
            return FactorError::InsufficientData;
        }

        const FactorResult<std::vector<signal_value_t>> factorMatrixResult =
            factorComputeDispatcher_.evaluateOnClose(fieldView, node.computeFunctionToken);
        if (!factorMatrixResult.hasValue()) return factorMatrixResult.error();

        const std::vector<signal_value_t>& factorMatrix = factorMatrixResult.value();
        if (!hasSufficientMatrixSize(factorMatrix, fieldView)) return FactorError::InternalError;

        return fillSingleFactorSlice(factorIndex, factorMatrix);
    }

    [[nodiscard]] FactorError fillSingleFactorSlice(
        int32_t factorIndex, const std::vector<signal_value_t>& factorMatrix)
    {
        for (int32_t timeIndex = 0; timeIndex < timeCount_; ++timeIndex) {
            for (int32_t instrumentIndex = 0; instrumentIndex < instrumentCount_; ++instrumentIndex) {
                const int32_t sourceRowIndex = dateAxisSelection_.sourceRowIndices[static_cast<size_t>(timeIndex)];
                if (sourceRowIndex < 0 || sourceRowIndex >= closeView_.rowCount) {
                    return FactorError::InsufficientData;
                }
                const std::optional<size_t> matrixFlat = flattenMatrixIndex(
                    sourceRowIndex, instrumentIndex, closeView_.columnCount);
                const std::optional<size_t> tensorFlat = flattenIndex(
                    timeIndex, instrumentIndex, factorIndex, instrumentCount_, factorCount_);
                if (!matrixFlat.has_value() || !tensorFlat.has_value()
                    || matrixFlat.value() >= factorMatrix.size()
                    || tensorFlat.value() >= rawTensor_.values.size()
                    || tensorFlat.value() >= rawTensor_.mask.size()) {
                    return FactorError::InternalError;
                }
                const signal_value_t computedValue = factorMatrix[matrixFlat.value()];
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

// ========================================================================
// QueryWorkflow（保持串行，无变化）
// ========================================================================
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
        const FactorResult<std::vector<signal_value_t>> factorMatrixResult =
            factorComputeDispatcher_.evaluateOnClose(context.closeView, context.node.computeFunctionToken);
        if (!factorMatrixResult.hasValue()) {
            return FactorResult<SignalValue>::failure(factorMatrixResult.error());
        }
        const std::vector<signal_value_t>& factorMatrix = factorMatrixResult.value();
        const std::optional<size_t> matrixFlat =
            flattenMatrixIndex(context.dateIndex, context.instrumentIndex, context.closeView.columnCount);
        if (!hasSufficientMatrixSize(factorMatrix, context.closeView)
            || !matrixFlat.has_value() || matrixFlat.value() >= factorMatrix.size()) {
            return FactorResult<SignalValue>::failure(FactorError::InternalError);
        }
        const signal_value_t value = factorMatrix[matrixFlat.value()];
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

// ========================================================================
// FactorComputeEngine 构造与公开方法
// ========================================================================
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
        spec, cacheKey, factorRegistry_, signalSetAssembler_,
        factorComputeDispatcher_, marketDataView_, *signalCache_, *postProcessingPipeline_);
    const FactorResult<SignalSet> generateResult = workflow.run();
    if (!generateResult.hasValue()) {
        return generateResult;
    }

    const SignalSet signalSet = generateResult.value();
    const FactorResult<AnalysisReport> analysisResult =
        analysisModule_->analyze(signalSet, spec, workflow.closeView());
    captureLatestAnalysisState(analysisResult);

    return FactorResult<SignalSet>::success(signalSet);
}

FactorResult<SignalValue>
FactorComputeEngine::query(const QuerySpec& spec) const
{
    const QueryWorkflow workflow(spec, factorRegistry_, factorComputeDispatcher_, marketDataView_);
    return workflow.run();
}

// ... latestFactorQualityMetrics16, latestFactorQualityDiagnostics16 等方法保持不变
std::optional<FactorQualityMetrics16View>
FactorComputeEngine::latestFactorQualityMetrics16() const noexcept
{
    if (!latestAnalysisReport_.has_value()) return std::nullopt;
    return buildFactorQualityMetrics16View(latestAnalysisReport_.value());
}

std::optional<FactorQualityMetrics16DiagnosticsView>
FactorComputeEngine::latestFactorQualityDiagnostics16() const noexcept
{
    if (!latestAnalysisReport_.has_value()) return std::nullopt;
    return buildFactorQualityMetrics16DiagnosticsView(latestAnalysisReport_.value());
}

std::optional<FactorQualityMetrics16Snapshot>
FactorComputeEngine::latestFactorQualitySnapshot16() const noexcept
{
    if (!latestAnalysisReport_.has_value()) return std::nullopt;
    return buildFactorQualityMetrics16Snapshot(latestAnalysisReport_.value());
}

void FactorComputeEngine::resetLatestAnalysisState() noexcept
{
    latestAnalysisReport_.reset();
    latestAnalysisError_.reset();
}

void FactorComputeEngine::captureLatestAnalysisState(
    const FactorResult<AnalysisReport>& analysisResult) noexcept
{
    if (analysisResult.hasValue()) {
        latestAnalysisReport_ = analysisResult.value();
        latestAnalysisError_.reset();
        return;
    }
    latestAnalysisReport_.reset();
    latestAnalysisError_ = analysisResult.error();
}

FactorResult<SignalSet>
FactorComputeEngine::incrementalUpdate(
    const SignalSet& baseResult,
    const DeltaMarketData& deltaData)
{
    if (!deltaData.isValid() || !baseResult.isValid()) {
        return FactorResult<SignalSet>::failure(FactorError::InvalidUniverse);
    }
    // 增量模式：从 baseResult 提取 spec 信息
    GenerateSpec spec;
    spec.mode = SignalEngineMode::Incremental;
    spec.dateRange.from = deltaData.date;
    spec.dateRange.to = deltaData.date;
    spec.instrumentUniverse = deltaData.instruments;
    // 从 baseResult 的 signalIds 反推 requestedFactors
    spec.requestedFactors.reserve(baseResult.signalIds.size());
    for (const auto& sid : baseResult.signalIds) {
        FactorId fid;
        fid.value = sid.value;
        spec.requestedFactors.push_back(fid);
    }
    // 设置默认预算
    spec.runtimeBudget.timeoutMilliseconds = 1000;
    spec.runtimeBudget.memoryLimitBytes = 256ULL * 1024ULL * 1024ULL;
    spec.chunkPolicy.dateChunkSize = 1;
    spec.chunkPolicy.instrumentChunkSize = static_cast<uint32_t>(deltaData.instruments.size());
    // 调用 generate 计算新日期
    return generate(spec);
}

} // namespace factor::compute
