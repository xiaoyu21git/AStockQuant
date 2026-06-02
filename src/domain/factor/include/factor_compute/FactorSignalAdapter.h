#pragma once

#include "ISignalEngine.h"
#include "IFactorRegistry.h"
#include "IFactorComputeDispatcher.h"
#include "IPostProcessingPipeline.h"
#include "IFactorSignalSetAssembler.h"
#include "IMarketDataView.h"
#include "ISignalCache.h"
#include "IAnalysisModule.h"

#include <memory>

namespace factor::compute {

/// @brief P2-T1：因子信号适配器
///
/// 将现有因子服务（IFactorRegistry + IFactorComputeDispatcher + IPostProcessingPipeline +
/// IFactorSignalSetAssembler + IMarketDataView + ISignalCache + IAnalysisModule）适配为
/// 统一 ISignalEngine 接口（设计文档 Section 4.3）。
///
/// 约束（实施任务清单 P2-T1）：
/// - 适配器仅做字段映射和阶段编排，不复制因子算法。
/// - SignalSet 支持批量时间窗输出。
/// - isPartial 标志正确传递部分结果。
class FactorSignalAdapter final : public ISignalEngine {
public:
    /// @brief 构造适配器
    ///
    /// @param registry 因子注册器（注入）
    /// @param dispatcher 因子计算调度器（注入）
    /// @param postProcessingPipeline 后处理管线（注入）
    /// @param assembler 信号装配器（注入）
    /// @param marketDataView 行情数据视图（注入）
    /// @param signalCache 信号缓存（注入）
    /// @param analysisModule 分析模块（注入，可选，传 nullptr 则跳过分析）
    FactorSignalAdapter(
        const IFactorRegistry& registry,
        const IFactorComputeDispatcher& dispatcher,
        const IPostProcessingPipeline& postProcessingPipeline,
        const IFactorSignalSetAssembler& assembler,
        const IMarketDataView& marketDataView,
        ISignalCache& signalCache,
        const IAnalysisModule* analysisModule = nullptr);

    ~FactorSignalAdapter() override = default;

    FactorSignalAdapter(const FactorSignalAdapter&) = delete;
    FactorSignalAdapter& operator=(const FactorSignalAdapter&) = delete;
    FactorSignalAdapter(FactorSignalAdapter&&) = delete;
    FactorSignalAdapter& operator=(FactorSignalAdapter&&) = delete;

    /// @brief 编排全链路：缓存检查 → 构建计划 → 计算 → 后处理 → 装配 → 缓存 → 分析
    [[nodiscard]] FactorResult<SignalSet>
    generate(const GenerateSpec& spec) override;

    /// @brief 单点查询
    [[nodiscard]] FactorResult<SignalValue>
    query(const QuerySpec& spec) const override;

    /// @brief 最近一次 generate 的分析报告（如果 analysisModule 非空且 generate 成功）
    [[nodiscard]] const std::optional<AnalysisReport>&
    latestAnalysisReport() const noexcept { return latestAnalysisReport_; }

    /// @brief 最近一次 generate 的分析错误
    [[nodiscard]] const std::optional<FactorError>&
    latestAnalysisError() const noexcept { return latestAnalysisError_; }

private:
    void resetAnalysisState() noexcept;
    void captureAnalysisResult(const FactorResult<AnalysisReport>& result) noexcept;

    const IFactorRegistry& registry_;
    const IFactorComputeDispatcher& dispatcher_;
    const IPostProcessingPipeline& postProcessingPipeline_;
    const IFactorSignalSetAssembler& assembler_;
    const IMarketDataView& marketDataView_;
    ISignalCache& signalCache_;
    const IAnalysisModule* analysisModule_;

    std::optional<AnalysisReport> latestAnalysisReport_;
    std::optional<FactorError> latestAnalysisError_;
};

} // namespace factor::compute