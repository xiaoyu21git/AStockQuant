#pragma once

#include "FactorSignalTypes.h"
#include "IFactorSignalSetAssembler.h"

namespace factor::compute {

/// @brief 因子服务顶层接口（SignalEngine 插件合同）
///
/// 设计文档 Section 4.3：
/// - 作为唯一入口，接收 generate 和 query 请求。
/// - 实现方负责参数校验、预算闸门、阶段调度、错误汇总和结果返回。
/// - 不直接承载具体算法实现，只负责编排各子模块。
class ISignalEngine {
public:
    virtual ~ISignalEngine() = default;

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

    /// @brief 单点查询因子信号值
    ///
    /// 对指定的 (日期, 标的, 因子) 三元组返回单个信号值。
    /// 不走缓存，直接执行最小计算计划。
    [[nodiscard]] virtual FactorResult<SignalValue>
    query(const QuerySpec& spec) const = 0;
};

} // namespace factor::compute