#include "AdjustableMarketDataView.h"

#include <stdexcept>

namespace domain::market {

AdjustableMarketDataView::AdjustableMarketDataView(
    const factor::compute::IMarketDataView& source,
    std::vector<factor::compute::signal_value_t> adjustFactors)
    : source_(source)
    , factors_(std::move(adjustFactors))
{
    if (factors_.size() != source_.dates().size()) {
        throw std::invalid_argument(
            "AdjustableMarketDataView: factor count mismatch, expected "
            + std::to_string(source_.dates().size())
            + " got " + std::to_string(factors_.size()));
    }
}

// ═══ applyAdjust: 对 source 矩阵逐行乘以复权因子 ═══

factor::compute::NumericConstMatrixView AdjustableMarketDataView::applyAdjust(
    const factor::compute::NumericConstMatrixView& src) const
{
    const int32_t rows = src.rowCount;
    const int32_t cols = src.columnCount;
    const int32_t stride = src.rowStride;
    const auto& dates = source_.dates();

    // 使用惰性缓存
    auto& cache = const_cast<std::vector<factor::compute::signal_value_t>&>(
        &src == &source_.close()  ? adjustedClose_ :
        &src == &source_.open()   ? adjustedOpen_  :
        &src == &source_.high()   ? adjustedHigh_  :
        &src == &source_.low()    ? adjustedLow_   :
                                     adjustedVolume_);

    if (cache.empty()) {
        cache.resize(static_cast<size_t>(rows) * static_cast<size_t>(cols));
        for (int32_t d = 0; d < rows; ++d) {
            float factor = (d < static_cast<int32_t>(factors_.size())) ? factors_[d] : 1.0f;
            for (int32_t s = 0; s < cols; ++s) {
                cache[d * cols + s] = src.data[d * stride + s] * factor;
            }
        }
    }

    factor::compute::NumericConstMatrixView v;
    v.data        = cache.data();
    v.rowCount    = rows;
    v.columnCount = cols;
    v.rowStride   = cols;
    return v;
}

factor::compute::NumericConstMatrixView AdjustableMarketDataView::open()  const { return applyAdjust(source_.open()); }
factor::compute::NumericConstMatrixView AdjustableMarketDataView::high()  const { return applyAdjust(source_.high()); }
factor::compute::NumericConstMatrixView AdjustableMarketDataView::low()   const { return applyAdjust(source_.low()); }
factor::compute::NumericConstMatrixView AdjustableMarketDataView::close() const { return applyAdjust(source_.close()); }
factor::compute::NumericConstMatrixView AdjustableMarketDataView::volume() const { return applyAdjust(source_.volume()); }

std::optional<factor::compute::NumericConstMatrixView>
AdjustableMarketDataView::getField(const std::string& fieldName) const {
    return source_.getField(fieldName);  // 透传非OHLCV字段
}

const std::vector<factor::compute::DateKey>& AdjustableMarketDataView::dates() const { return source_.dates(); }
const std::vector<factor::compute::InstrumentId>& AdjustableMarketDataView::instruments() const { return source_.instruments(); }
const std::vector<std::string>& AdjustableMarketDataView::symbolStrings() const { return source_.symbolStrings(); }

std::unique_ptr<factor::compute::IMarketDataView>
AdjustableMarketDataView::slice(factor::compute::DateRange dateRange) const {
    auto sub = source_.slice(dateRange);
    if (!sub) return nullptr;
    return std::make_unique<AdjustableMarketDataView>(*sub, factors_);
}

std::unique_ptr<factor::compute::IMarketDataView>
AdjustableMarketDataView::slice(const std::vector<factor::compute::InstrumentId>& ids) const {
    auto sub = source_.slice(ids);
    if (!sub) return nullptr;
    return std::make_unique<AdjustableMarketDataView>(*sub, factors_);
}

} // namespace domain::market