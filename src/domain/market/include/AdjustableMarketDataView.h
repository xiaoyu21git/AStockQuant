#pragma once

#include "factor_compute/IMarketDataView.h"
#include "factor_compute/IFactorOperatorLibrary.h"

#include <memory>
#include <string>
#include <vector>

namespace domain::market {

/// @brief 复权装饰器：包装任意 IMarketDataView，对 OHLCV 应用复权因子
///
/// 不拷贝原始数据，通过内部缓存的复权因子向量计算调整后的 OHLCV。
/// 其他字段（通过 getField 访问）透传原始视图。
class AdjustableMarketDataView final : public factor::compute::IMarketDataView {
public:
    /// @param source 原始行情视图（生命周期需覆盖本装饰器）
    /// @param adjustFactors 与 source.dates() 一一对应的复权因子序列
    /// @throws std::invalid_argument 如果 factors 长度与 dates 不匹配
    AdjustableMarketDataView(
        const factor::compute::IMarketDataView& source,
        std::vector<factor::compute::signal_value_t> adjustFactors);

    ~AdjustableMarketDataView() override = default;

    // 禁止拷贝
    AdjustableMarketDataView(const AdjustableMarketDataView&) = delete;
    AdjustableMarketDataView& operator=(const AdjustableMarketDataView&) = delete;

    [[nodiscard]] factor::compute::NumericConstMatrixView open()   const override;
    [[nodiscard]] factor::compute::NumericConstMatrixView high()    const override;
    [[nodiscard]] factor::compute::NumericConstMatrixView low()     const override;
    [[nodiscard]] factor::compute::NumericConstMatrixView close()   const override;
    [[nodiscard]] factor::compute::NumericConstMatrixView volume()  const override;

    [[nodiscard]] std::optional<factor::compute::NumericConstMatrixView>
    getField(const std::string& fieldName) const override;

    [[nodiscard]] const std::vector<factor::compute::DateKey>& dates() const override;
    [[nodiscard]] const std::vector<factor::compute::InstrumentId>& instruments() const override;

    [[nodiscard]] std::unique_ptr<factor::compute::IMarketDataView>
    slice(factor::compute::DateRange dateRange) const override;

    [[nodiscard]] std::unique_ptr<factor::compute::IMarketDataView>
    slice(const std::vector<factor::compute::InstrumentId>& instrumentIds) const override;

private:
    /// 构建复权后的矩阵视图：source[j][i] * factors[j]
    [[nodiscard]] factor::compute::NumericConstMatrixView applyAdjust(
        const factor::compute::NumericConstMatrixView& src) const;

    const factor::compute::IMarketDataView& source_;
    std::vector<factor::compute::signal_value_t> factors_;

    // 惰性缓存：复权后的 close/open/high/low/volume
    mutable std::vector<factor::compute::signal_value_t> adjustedClose_;
    mutable std::vector<factor::compute::signal_value_t> adjustedOpen_;
    mutable std::vector<factor::compute::signal_value_t> adjustedHigh_;
    mutable std::vector<factor::compute::signal_value_t> adjustedLow_;
    mutable std::vector<factor::compute::signal_value_t> adjustedVolume_;
};

} // namespace domain::market