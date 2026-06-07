#pragma once

#include "FactorSignalTypes.h"
#include "ISignalEngine.h"

namespace factor::compute {

class IFactorComputeEngine {
public:
    virtual ~IFactorComputeEngine() = default;

    /// @brief 设置计算模式
    virtual void setComputeMode(ComputeMode mode) noexcept = 0;

    /// @brief 批量生成因子信号集
    [[nodiscard]] virtual FactorResult<SignalSet>
    generate(const GenerateSpec& spec) = 0;

    /// @brief 增量更新因子信号（实盘场景）
    [[nodiscard]] virtual FactorResult<SignalSet>
    incrementalUpdate(
        const SignalSet& baseResult,
        const DeltaMarketData& deltaData) = 0;

    /// @brief 单点查询因子信号值
    [[nodiscard]] virtual FactorResult<SignalValue>
    query(const QuerySpec& spec) const = 0;
};

} // namespace factor::compute

