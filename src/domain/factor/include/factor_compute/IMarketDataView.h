#pragma once

#include "FactorSignalTypes.h"
#include "IFactorOperatorLibrary.h"

#include <memory>
#include <optional>
#include <string>

namespace factor::compute {

/// @brief 市场数据视图接口
///
/// 核心职责：提供列式只读行情数据访问。
/// 支持全量映射（ParquetMarketDataView）和子视图切片（SubMarketDataView）。
class IMarketDataView {
public:
    virtual ~IMarketDataView() = default;

    [[nodiscard]] virtual NumericConstMatrixView open() const = 0;
    [[nodiscard]] virtual NumericConstMatrixView high() const = 0;
    [[nodiscard]] virtual NumericConstMatrixView low() const = 0;
    [[nodiscard]] virtual NumericConstMatrixView close() const = 0;
    [[nodiscard]] virtual NumericConstMatrixView volume() const = 0;

    /// @brief 按字段名访问任意字段矩阵
    [[nodiscard]] virtual std::optional<NumericConstMatrixView>
    getField(const std::string& fieldName) const = 0;

    [[nodiscard]] virtual const std::vector<DateKey>& dates() const = 0;
    [[nodiscard]] virtual const std::vector<InstrumentId>& instruments() const = 0;
    [[nodiscard]] virtual const std::vector<std::string>& symbolStrings() const = 0;

    /// @brief 获取时间子集的视图（零拷贝）
    /// @param dateRange 时间范围
    /// @return 子视图，与原视图共享底层内存映射
    [[nodiscard]] virtual std::unique_ptr<IMarketDataView>
    slice(DateRange dateRange) const = 0;

    /// @brief 获取标的子集的视图（零拷贝）
    /// @param instrumentIds 标的列表
    /// @return 子视图，与原视图共享底层内存映射
    [[nodiscard]] virtual std::unique_ptr<IMarketDataView>
    slice(const std::vector<InstrumentId>& instrumentIds) const = 0;
};

} // namespace factor::compute