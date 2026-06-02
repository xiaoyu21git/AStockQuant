#include "factor_compute/FactorSignalAdapter.h"
#include "factor_compute/ParallelChunkScheduler.h"

#include "foundation/thread/ThreadPoolExecutor.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace factor::compute {

namespace {

constexpr double kDefaultSignalValue = 0.0;
constexpr uint8_t kMissingMaskValue = 1U;
constexpr uint8_t kPresentMaskValue = 0U;

bool isMissingValue(double value) {
    return std::isnan(value);
}

} // namespace

// ─── 构造函数 ───

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
    , threadPool_(nullptr)        // 无线程池 → 串行执行
    , bufferPool_(nullptr)
    , ownedBufferPool_(std::make_unique<SignalTensorBufferPool>())
{
}

FactorSignalAdapter::FactorSignalAdapter(
    const IFactorRegistry& registry,
    const IFactorComputeDispatcher& dispatcher,
    const IPostProcessingPipeline& postProcessingPipeline,
    const IFactorSignalSetAssembler& assembler,
    const IMarketDataView& marketDataView,
    ISignalCache& signalCache,
    const IAnalysisModule* analysisModule,
    foundation::thread::ThreadPoolExecutor* threadPool,
    SignalTensorBufferPool* bufferPool)
    : registry_(registry)
    , dispatcher_(dispatcher)
    , postProcessingPipeline_(postProcessingPipeline)
    , assembler_(assembler)
    , marketDataView_(marketDataView)
    , signalCache_(signalCache)
    , analysisModule_(analysisModule)
    , threadPool_(threadPool)
    , bufferPool_(bufferPool ? bufferPool : nullptr)
    , ownedBufferPool_(bufferPool ? nullptr : std::make_unique<SignalTensorBufferPool>())
{
}

// ─── generate（主入口，集成了预算闸门 + 线程池 + 池化）───

FactorResult<SignalSet>
FactorSignalAdapter::generate(const GenerateSpec& spec)
{
    resetAnalysisState();

    // ═══ 阶段 1: 参数校验 ═══
    if (!spec.isValid()) {
        return FactorResult<SignalSet>::failure(FactorError::InvalidUniverse);
    }

    RuntimeBudgetGuard budgetGuard(spec.runtimeBudget);

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

    // ═══ 阶段 2: 缓存检查 ═══
    {
        auto budgetResult = budgetGuard.checkBudget(BudgetStage::Validate);
        if (budgetResult.exceeded) {
            return FactorResult<SignalSet>::failure(FactorError::Timeout);
        }
    }

    const std::optional<SignalSet> cached = signalCache_.load(cacheKey);
    if (cached.has_value()) {
        return FactorResult<SignalSet>::success(cached.value());
    }

    // ═══ 阶段 3: Registry 构建计算计划 ═══
    const FactorResult<ComputePlan> planResult = registry_.buildPlan(
        spec.requestedFactors, spec.dateRange, spec.instrumentUniverse);
    if (!planResult.hasValue()) {
        return FactorResult<SignalSet>::failure(planResult.error());
    }
    const ComputePlan& plan = planResult.value();
    if (plan.nodes.empty()) {
        return FactorResult<SignalSet>::failure(FactorError::InvalidFormula);
    }

    // ═══ 阶段 4: 验证行情视图 ═══
    const NumericConstMatrixView closeView = marketDataView_.close();
    if (!closeView.isValid()) {
        return FactorResult<SignalSet>::failure(FactorError::InsufficientData);
    }

    // ═══ 阶段 5: 并行计算 ═══
    const auto computeResult = computeParallel(spec, cacheKey, plan, closeView, marketDataView_.dates());
    if (!computeResult.hasValue()) {
        return computeResult;
    }
    SignalSet signalSet = computeResult.value();

    // ═══ 阶段 6: 缓存（仅完整结果）═══
    if (!signalSet.isPartial) {
        signalCache_.store(cacheKey, signalSet);
    }

    // ═══ 阶段 7: 分析（可选）═══
    if (analysisModule_ != nullptr && !signalSet.isPartial) {
        const FactorResult<AnalysisReport> analysisResult =
            analysisModule_->analyze(signalSet, spec, closeView);
        captureAnalysisResult(analysisResult);
    }

    return FactorResult<SignalSet>::success(signalSet);
}

// ─── computeParallel（集成线程池 + 预算闸门 + 池化）───

FactorResult<SignalSet>
FactorSignalAdapter::computeParallel(
    const GenerateSpec& spec,
    const SignalCacheKey& cacheKey,
    const ComputePlan& plan,
    NumericConstMatrixView closeView,
    const std::vector<DateKey>& marketDates)
{
    (void)cacheKey; // 缓存已在 generate() 处理

    const int32_t timeCount = static_cast<int32_t>(marketDates.size());
    const int32_t instrumentCount = static_cast<int32_t>(spec.instrumentUniverse.size());
    const int32_t factorCount = static_cast<int32_t>(spec.requestedFactors.size());

    if (timeCount <= 0 || instrumentCount <= 0 || factorCount <= 0) {
        return FactorResult<SignalSet>::failure(FactorError::InvalidUniverse);
    }

    // 构建因子节点索引映射
    std::unordered_map<uint32_t, const ComputePlanNode*> nodeByFactorId;
    for (const auto& node : plan.nodes) {
        nodeByFactorId[node.factor.value] = &node;
    }

    // ═══ 池化：从池中获取 or 分配新的 rawTensor ═══
    SignalTensorBufferPool* pool = bufferPool_ ? bufferPool_ : ownedBufferPool_.get();

    SignalTensorBuffer rawTensor;
    bool usePool = (pool != nullptr && pool->cachedCount() > 0);
    if (usePool) {
        rawTensor = pool->acquire(timeCount, instrumentCount, factorCount);
    } else {
        const size_t flatSize = static_cast<size_t>(timeCount)
            * static_cast<size_t>(instrumentCount)
            * static_cast<size_t>(factorCount);
        rawTensor.values.resize(flatSize, kDefaultSignalValue);
        rawTensor.mask.resize(flatSize, 1U);
        rawTensor.timeCount = timeCount;
        rawTensor.instrumentCount = instrumentCount;
        rawTensor.factorCount = factorCount;
    }

    // ═══ 预准备：为每个因子生成不同的 ComputeToken 并预先验证结果矩阵 ═══
    // 约束（设计文档 Section 6.3）：并行归约协议 — 分块顺序固定
    // 同一输出单元总是按递增 instrument index 归并

    std::vector<FactorId> factorIds;
    factorIds.reserve(static_cast<size_t>(factorCount));
    for (int32_t idx = 0; idx < factorCount; ++idx) {
        factorIds.push_back(spec.requestedFactors[static_cast<size_t>(idx)]);
    }

    // 构建分块计划（用于归约顺序）
    ChunkPolicy chunkPolicy = spec.chunkPolicy;
    if (!chunkPolicy.isValid()) {
        chunkPolicy.dateChunkSize = 64U;
        chunkPolicy.instrumentChunkSize = 1024U;
    }
    ParallelChunkScheduler chunkScheduler;
    const ParallelChunkPlan chunkPlan = chunkScheduler.buildPlan(
        timeCount, instrumentCount, 5, chunkPolicy);

    // ═══ 并行执行（通过 foundation 线程池）═══
    std::atomic<uint32_t> completedFactorCount{0U};
    std::atomic<bool> hasPartial{false};
    std::atomic<bool> hasError{false};
    FactorError firstError{FactorError::None};
    std::mutex errorMutex;

    const auto startTime = std::chrono::steady_clock::now();

    if (threadPool_ != nullptr) {
        // ── 并行路径：每个因子一个任务提交到线程池 ──
        std::vector<std::future<bool>> futures;
        futures.reserve(static_cast<size_t>(factorCount));

        for (int32_t factorIdx = 0; factorIdx < factorCount; ++factorIdx) {
            // 提交前检查预算
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime);
            if (elapsed.count() > spec.runtimeBudget.timeoutMilliseconds) {
                hasPartial.store(true);
                break;
            }

            const FactorId factorId = factorIds[static_cast<size_t>(factorIdx)];
            const auto it = nodeByFactorId.find(factorId.value);
            if (it == nodeByFactorId.end()) {
                return FactorResult<SignalSet>::failure(FactorError::InternalError);
            }
            const uint32_t computeToken = it->second->computeFunctionToken;

            // 通过 promise/future 获取每个因子的计算结果
            auto promise = std::make_shared<std::promise<bool>>();
            futures.push_back(promise->get_future());

            threadPool_->post([&, factorIdx, factorId, computeToken, promise]() {
                // 预算检查
                const auto taskElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startTime);
                if (taskElapsed.count() > spec.runtimeBudget.timeoutMilliseconds) {
                    hasPartial.store(true);
                    promise->set_value(false);
                    return;
                }

                const FactorResult<std::vector<double>> matrixResult =
                    dispatcher_.evaluateOnClose(closeView, computeToken);
                if (!matrixResult.hasValue()) {
                    std::lock_guard<std::mutex> lock(errorMutex);
                    if (!hasError.load()) {
                        hasError.store(true);
                        firstError = matrixResult.error();
                    }
                    promise->set_value(false);
                    return;
                }

                const std::vector<double>& matrix = matrixResult.value();
                const size_t expectedSize = static_cast<size_t>(closeView.rowCount)
                    * static_cast<size_t>(closeView.columnCount);
                if (matrix.size() < expectedSize) {
                    std::lock_guard<std::mutex> lock(errorMutex);
                    if (!hasError.load()) {
                        hasError.store(true);
                        firstError = FactorError::InternalError;
                    }
                    promise->set_value(false);
                    return;
                }

                // 填充张量切片（按固定顺序归约）
                for (const auto& block : chunkPlan.blocks) {
                    for (int32_t d = 0; d < block.dateCount; ++d) {
                        const int32_t t = block.dateStart + d;
                        for (int32_t i = 0; i < block.instrumentCount; ++i) {
                            const int32_t instIdx = block.instrumentStart + i;
                            const size_t flatIdx = static_cast<size_t>(t)
                                * static_cast<size_t>(instrumentCount)
                                * static_cast<size_t>(factorCount)
                                + static_cast<size_t>(instIdx)
                                * static_cast<size_t>(factorCount)
                                + static_cast<size_t>(factorIdx);
                            const size_t matrixIdx = static_cast<size_t>(t)
                                * static_cast<size_t>(closeView.columnCount)
                                + static_cast<size_t>(instIdx);

                            if (matrixIdx >= matrix.size()
                                || flatIdx >= rawTensor.values.size()
                                || flatIdx >= rawTensor.mask.size()) {
                                continue;
                            }

                            const double value = matrix[matrixIdx];
                            rawTensor.values[flatIdx] = isMissingValue(value) ? kDefaultSignalValue : value;
                            rawTensor.mask[flatIdx] = isMissingValue(value) ? kMissingMaskValue : kPresentMaskValue;
                        }
                    }
                }

                completedFactorCount.fetch_add(1U);
                promise->set_value(true);
            });
        }

        // 等待所有任务完成
        for (auto& future : futures) {
            future.wait();
        }
    } else {
        // ── 串行路径（无线程池时）───
        for (int32_t factorIdx = 0; factorIdx < factorCount; ++factorIdx) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime);
            if (elapsed.count() > spec.runtimeBudget.timeoutMilliseconds) {
                hasPartial.store(true);
                break;
            }

            const FactorId factorId = factorIds[static_cast<size_t>(factorIdx)];
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
            for (const auto& block : chunkPlan.blocks) {
                for (int32_t d = 0; d < block.dateCount; ++d) {
                    const int32_t t = block.dateStart + d;
                    for (int32_t i = 0; i < block.instrumentCount; ++i) {
                        const int32_t instIdx = block.instrumentStart + i;
                        const size_t flatIdx = static_cast<size_t>(t)
                            * static_cast<size_t>(instrumentCount)
                            * static_cast<size_t>(factorCount)
                            + static_cast<size_t>(instIdx)
                            * static_cast<size_t>(factorCount)
                            + static_cast<size_t>(factorIdx);
                        const size_t matrixIdx = static_cast<size_t>(t)
                            * static_cast<size_t>(closeView.columnCount)
                            + static_cast<size_t>(instIdx);

                        if (matrixIdx >= matrix.size()
                            || flatIdx >= rawTensor.values.size()
                            || flatIdx >= rawTensor.mask.size()) {
                            continue;
                        }

                        const double value = matrix[matrixIdx];
                        rawTensor.values[flatIdx] = isMissingValue(value) ? kDefaultSignalValue : value;
                        rawTensor.mask[flatIdx] = isMissingValue(value) ? kMissingMaskValue : kPresentMaskValue;
                    }
                }
            }
            completedFactorCount.fetch_add(1U);
        }
    }

    if (hasError.load()) {
        return FactorResult<SignalSet>::failure(firstError);
    }

    // ═══ 后处理 ═══
    const FactorResult<SignalTensorBuffer> processedResult =
        postProcessingPipeline_.run(std::move(rawTensor), spec);
    if (!processedResult.hasValue()) {
        return FactorResult<SignalSet>::failure(processedResult.error());
    }

    const SignalTensorBuffer& processed = processedResult.value();
    if (!processed.isValid()) {
        return FactorResult<SignalSet>::failure(FactorError::InternalError);
    }

    // ═══ 装配 SignalSet ═══
    AssembleContext assembleCtx;
    assembleCtx.dates = marketDates;
    assembleCtx.instruments = spec.instrumentUniverse;
    assembleCtx.factors = spec.requestedFactors;
    assembleCtx.progress.plannedFactorCount = static_cast<uint32_t>(factorCount);
    assembleCtx.progress.completedFactorCount = completedFactorCount.load();
    assembleCtx.isPartial = hasPartial.load();

    SignalSet signalSet = assembler_.assemble(processed.asView(), assembleCtx);
    signalSet.isPartial = hasPartial.load();

    return FactorResult<SignalSet>::success(signalSet);
}

// ─── query ───

FactorResult<SignalValue>
FactorSignalAdapter::query(const QuerySpec& spec) const
{
    if (!spec.isValid()) {
        return FactorResult<SignalValue>::failure(FactorError::InvalidUniverse);
    }

    const DateRange dateRange{spec.date, spec.date};
    const std::vector<InstrumentId> universe{spec.instrument};
    const std::vector<FactorId> factors{spec.factor};

    const FactorResult<ComputePlan> planResult = registry_.buildPlan(factors, dateRange, universe);
    if (!planResult.hasValue()) {
        return FactorResult<SignalValue>::failure(planResult.error());
    }

    const NumericConstMatrixView closeView = marketDataView_.close();
    if (!closeView.isValid()) {
        return FactorResult<SignalValue>::failure(FactorError::InsufficientData);
    }

    const auto& marketDates = marketDataView_.dates();
    const auto& marketInstruments = marketDataView_.instruments();

    int32_t dateIdx = -1;
    for (size_t i = 0; i < marketDates.size(); ++i) {
        if (marketDates[i].value == spec.date.value) {
            dateIdx = static_cast<int32_t>(i);
            break;
        }
    }

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
    sv.value = isMissingValue(value) ? kDefaultSignalValue : value;
    sv.isMissing = isMissingValue(value);
    return FactorResult<SignalValue>::success(sv);
}

void FactorSignalAdapter::resetAnalysisState() noexcept {
    latestAnalysisReport_.reset();
    latestAnalysisError_.reset();
}

void FactorSignalAdapter::captureAnalysisResult(const FactorResult<AnalysisReport>& result) noexcept {
    if (result.hasValue()) {
        latestAnalysisReport_ = result.value();
        latestAnalysisError_.reset();
    } else {
        latestAnalysisReport_.reset();
        latestAnalysisError_ = result.error();
    }
}

} // namespace factor::compute