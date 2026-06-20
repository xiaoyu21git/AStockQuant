#pragma once

#include "IMarketDataView.h"

#include <memory>
#include <string>
#include <vector>

namespace factor::compute {

/// @brief Arrow IPC 文件内存映射列式行情视图
///
/// 读取 DataCache 写入的 .arrow 文件 (Arrow IPC File format)
/// 将 float64 列转换为 float32，通过 Eigen::Map 暴露只读视图。
class ArrowMarketDataView final : public IMarketDataView {
public:
    explicit ArrowMarketDataView(const std::string& arrowPath);
    ~ArrowMarketDataView() override;

    ArrowMarketDataView(const ArrowMarketDataView&) = delete;
    ArrowMarketDataView& operator=(const ArrowMarketDataView&) = delete;
    ArrowMarketDataView(ArrowMarketDataView&&) = delete;
    ArrowMarketDataView& operator=(ArrowMarketDataView&&) = delete;

    [[nodiscard]] NumericConstMatrixView open() const override;
    [[nodiscard]] NumericConstMatrixView high() const override;
    [[nodiscard]] NumericConstMatrixView low() const override;
    [[nodiscard]] NumericConstMatrixView close() const override;
    [[nodiscard]] NumericConstMatrixView volume() const override;

    [[nodiscard]] std::optional<NumericConstMatrixView>
    getField(const std::string& fieldName) const override;

    [[nodiscard]] const std::vector<DateKey>& dates() const override;
    [[nodiscard]] const std::vector<InstrumentId>& instruments() const override;

    [[nodiscard]] std::unique_ptr<IMarketDataView>
    slice(DateRange dateRange) const override;

    [[nodiscard]] std::unique_ptr<IMarketDataView>
    slice(const std::vector<InstrumentId>& instrumentIds) const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace factor::compute
