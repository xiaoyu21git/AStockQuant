#pragma once

#include "FactorSignalTypes.h"
#include "IFactorOperatorLibrary.h"
#include "IFactorSignalSetAssembler.h"

namespace factor::compute {

/// @brief 增量行情数据（实盘场景）
///
/// 每次收到新数据后，将最新一个时间片的行情通过此结构体传递给引擎。
struct DeltaMarketData final {
    DateKey date;                               ///< 当前日期
    std::vector<InstrumentId> instruments;      ///< 有数据更新的标的
    NumericConstMatrixView close;               ///< 增量收盘价 (1 x N matrix)
    NumericConstMatrixView high;                ///< 增量最高价
    NumericConstMatrixView low;                 ///< 增量最低价
    NumericConstMatrixView volume;              ///< 增量成交量

    [[nodiscard]] bool isValid() const noexcept
    {
        return date.isValid()
            && !instruments.empty()
            && close.isValid();
    }
};

/// @brief 因子服务顶层接口（SignalEngine 插件合同）
///
/// 设计文档 Section 4.3：
/// - 作为唯一入口，接收 generate 和 query 请求。
/// - 实现方负责参数校验、预算闸门、阶段调度、错误汇总和结果返回。
/// - 不直接承载具体算法实现，只负责编排各子模块。
class ISignalEngine {
public:
    virtual ~ISignalEngine() = default;

    /// @brief 设置计算模式
    virtual void setComputeMode(ComputeMode mode) noexcept = 0;

    /// @brief 批量生成因子信号集
    ///
    /// 编排全链路：缓存检查 → Registry 构建计算计划 → DataAdapter 提供行情视图 →
    /// ComputeEngine 批量计算 → PostProcessingPipeline 后处理 →
    /// SignalSetAssembler 装配 → 更新缓存 → 返回 SignalSet。
    ///
    /// 失败时返回 FactorError 枚举码：
    /// - InvalidUniverse：universe 为空或包含非法标的
    /// - InsufficientData：依赖字段在请求窗口内不可用
    /// - Timeout：超预算中断，返回部分结果
    /// - MemoryExceeded：预估内存超限
    /// - InternalError：内部异常
    [[nodiscard]] virtual FactorResult<SignalSet>
    generate(const GenerateSpec& spec) = 0;

    /// @brief 增量更新因子信号（实盘场景）
    ///
    /// 仅重新计算受增量数据影响的因子值，非全量重算。
    /// 首次调用前需要至少一次 generate() 以建立基准。
    ///
    /// @param baseResult 前一次全量计算结果（作为增量基准）
    /// @param deltaData  增量行情数据（最近一个时间片）
    /// @return 更新后的 SignalSet（仅包含变更值）
    [[nodiscard]] virtual FactorResult<SignalSet>
    incrementalUpdate(
        const SignalSet& baseResult,
        const DeltaMarketData& deltaData) = 0;

    /// @brief 单点查询因子信号值
    ///
    /// 对指定的 (日期, 标的, 因子) 三元组返回单个信号值。
    /// 不走缓存，直接执行最小计算计划。
    [[nodiscard]] virtual FactorResult<SignalValue>
    query(const QuerySpec& spec) const = 0;
};

} // namespace factor::compute
