#pragma once

#include "ISignalEngine.h"
#include "IFactorRegistry.h"
#include "IFactorComputeDispatcher.h"
#include "IPostProcessingPipeline.h"
#include "IFactorSignalSetAssembler.h"
#include "IMarketDataView.h"
#include "ISignalCache.h"
#include "IAnalysisModule.h"
#include "RuntimeBudgetGuard.h"
#include "SignalTensorBufferPool.h"

#include <memory>

// 前向声明 foundation 线程池（避免在 factor_compute 头文件中引入 foundation 依赖）
// 实际类型为 foundation::thread::ThreadPoolExecutor
namespace foundation::thread {
    class ThreadPoolExecutor;
}

namespace factor::compute {

/// @brief 因子信号适配器（P2-T1 + P4 集成）
///
/// 将因子服务适配为 ISignalEngine 接口，集成：
/// - 多线程并行计算（复用 foundation::thread::ThreadPoolExecutor）
/// - 预算闸门阶段检查（RuntimeBudgetGuard）
/// - 张量缓冲区池化（SignalTensorBufferPool）
///
/// 约束（实施任务清单 P2-T1 + 设计文档 Section 6.3）：
/// - 适配器仅做字段映射和阶段编排，不复制因子算法。
/// - 分块顺序固定：先日期块排序，再标的块排序（确定性并行）。
/// - 浮点归约统一使用 double 并采用固定二叉树归约拓扑。
/// - 相同输入、相同版本、相同线程配置下输出位级可复现。
class FactorSignalAdapter final : public ISignalEngine {
public:
    /// @brief 构造适配器（默认构造，独立线程池 + 独立池化）
    FactorSignalAdapter(
        const IFactorRegistry& registry,
        const IFactorComputeDispatcher& dispatcher,
        const IPostProcessingPipeline& postProcessingPipeline,
        const IFactorSignalSetAssembler& assembler,
        const IMarketDataView& marketDataView,
        ISignalCache& signalCache,
        const IAnalysisModule* analysisModule = nullptr);

    /// @brief 构造适配器（注入 foundation 线程池，复用全局线程资源）
    /// @param threadPool foundation 线程池指针，传 nullptr 则退化为串行执行
    FactorSignalAdapter(
        const IFactorRegistry& registry,
        const IFactorComputeDispatcher& dispatcher,
        const IPostProcessingPipeline& postProcessingPipeline,
        const IFactorSignalSetAssembler& assembler,
        const IMarketDataView& marketDataView,
        ISignalCache& signalCache,
        const IAnalysisModule* analysisModule,
        foundation::thread::ThreadPoolExecutor* threadPool,
        SignalTensorBufferPool* bufferPool = nullptr);

    ~FactorSignalAdapter() override = default;

    FactorSignalAdapter(const FactorSignalAdapter&) = delete;
    FactorSignalAdapter& operator=(const FactorSignalAdapter&) = delete;
    FactorSignalAdapter(FactorSignalAdapter&&) = delete;
    FactorSignalAdapter& operator=(FactorSignalAdapter&&) = delete;

    /// @brief 编排全链路：预算检查 → 缓存检查 → 构建计划 → 并行计算 → 后处理 → 装配 → 缓存 → 分析
    [[nodiscard]] FactorResult<SignalSet>
    generate(const GenerateSpec& spec) override;

    /// @brief 单点查询
    [[nodiscard]] FactorResult<SignalValue>
    query(const QuerySpec& spec) const override;

    /// @brief 最近一次 generate 的分析报告
    [[nodiscard]] const std::optional<AnalysisReport>&
    latestAnalysisReport() const noexcept { return latestAnalysisReport_; }

    /// @brief 最近一次 generate 的分析错误
    [[nodiscard]] const std::optional<FactorError>&
    latestAnalysisError() const noexcept { return latestAnalysisError_; }

private:
    void resetAnalysisState() noexcept;
    void captureAnalysisResult(const FactorResult<AnalysisReport>& result) noexcept;

    /// @brief 并行计算阶段
    /// 使用 ParallelChunkScheduler 构建分块计划，通过 foundation 线程池并行执行
    [[nodiscard]] FactorResult<SignalSet>
    computeParallel(
        const GenerateSpec& spec,
        const SignalCacheKey& cacheKey,
        const ComputePlan& plan,
        NumericConstMatrixView closeView,
        const std::vector<DateKey>& marketDates);

    const IFactorRegistry& registry_;
    const IFactorComputeDispatcher& dispatcher_;
    const IPostProcessingPipeline& postProcessingPipeline_;
    const IFactorSignalSetAssembler& assembler_;
    const IMarketDataView& marketDataView_;
    ISignalCache& signalCache_;
    const IAnalysisModule* analysisModule_;

    foundation::thread::ThreadPoolExecutor* threadPool_{nullptr};
    SignalTensorBufferPool* bufferPool_{nullptr};

    // 独立拥有的默认实例（当外部不注入时自动创建）
    std::unique_ptr<SignalTensorBufferPool> ownedBufferPool_;

    std::optional<AnalysisReport> latestAnalysisReport_;
    std::optional<FactorError> latestAnalysisError_;
};

} // namespace factor::compute
