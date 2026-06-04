#pragma once

#include "FactorSignalTypes.h"
#include "IFactorOperatorLibrary.h"

#include <optional>
#include <string>

namespace factor::compute {

class IMarketDataView {
public:
    virtual ~IMarketDataView() = default;

    [[nodiscard]] virtual NumericConstMatrixView open() const = 0;
    [[nodiscard]] virtual NumericConstMatrixView high() const = 0;
    [[nodiscard]] virtual NumericConstMatrixView low() const = 0;
    [[nodiscard]] virtual NumericConstMatrixView close() const = 0;
    [[nodiscard]] virtual NumericConstMatrixView volume() const = 0;

    /// @brief 按字段名访问任意字段矩阵（如 pb_ratio, pe_ratio, market_cap, roe 等）
    /// @param fieldName 字段名（与因子 requiredFields 中的声明一致）
    /// @return 字段矩阵视图，若字段不存在则返回 std::nullopt
    [[nodiscard]] virtual std::optional<NumericConstMatrixView>
    getField(const std::string& fieldName) const = 0;

    [[nodiscard]] virtual const std::vector<DateKey>& dates() const = 0;
    [[nodiscard]] virtual const std::vector<InstrumentId>& instruments() const = 0;
};

} // namespace factor::compute

