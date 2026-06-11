#pragma once

#include "StrategyServiceTypes.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace domain::strategy {

/// @brief 因子计算服务接口 — 策略只需调用 compute() 获取因子原始值
///
/// 策略完全不感知因子内部如何计算（FactorInstanceManager / HistoricalView / BaseFactor）。
/// 加权合成由策略端 MultiFactorSelectionStrategy 完成。
class IFactorSvc {
public:
    virtual ~IFactorSvc() = default;

    /// @brief 计算一批因子的原始值
    /// @param date         交易日 YYYYMMDD 整数格式
    /// @param factorIds    需要计算的因子 ID 列表
    /// @param symbolIds    需要计算的标的 ID 列表
    /// @return 因子快照列表 — 每个快照包含 { symbolId, factorId, rawValue, version }
[[nodiscard]] virtual std::unordered_map<std::uint32_t, double> getValues(
    ::domain::strategies::FactorId factorId,
    std::int32_t date,
    const std::vector<std::uint32_t>& symbolIds) = 0;
};

} // namespace domain::strategy