#pragma once

#include "IFactorOperatorLibrary.h"

#include <string>
#include <vector>

namespace factor::compute {

class IMarketDataView;

class IFactorComputeDispatcher {
public:
    virtual ~IFactorComputeDispatcher() = default;

    /// @brief 在 close 字段上计算（向后兼容）
    [[nodiscard]] virtual FactorResult<std::vector<signal_value_t>> evaluateOnClose(
        NumericConstMatrixView closeView,
        uint32_t computeToken) const = 0;

    /// @brief 在指定命名字段上计算（如 pb_ratio, pe_ratio, roe 等）
    /// @param marketDataView 完整的市场数据视图
    /// @param fieldName 字段名称
    /// @param computeToken 计算令牌
    [[nodiscard]] virtual FactorResult<std::vector<signal_value_t>> evaluateOnField(
        const IMarketDataView& marketDataView,
        const std::string& fieldName,
        uint32_t computeToken) const = 0;
};

} // namespace factor::compute

