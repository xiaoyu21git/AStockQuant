#pragma once

#include "IMarketDataView.h"

#include <memory>
#include <vector>

namespace factor::compute {

/// @brief 行情视图切片（设计文档 Section 5.2：SubView）
///
/// 约束：
/// - 对原 IMarketDataView 进行时间和标的维度的子集过滤。
/// - 不拷贝底层数据，仅通过索引映射实现零拷贝视图。
/// - 持有原视图引用，调用方需保证原视图生命周期覆盖 SubView。
class SubMarketDataView final : public IMarketDataView {
public:
    /// @brief 构造切片视图
    ///
    /// @param source 原始行情视图（生命周期需覆盖本切片）
    /// @param dateSubset 需要的日期子集（必须全部存在于 source.dates()）
    /// @param instrumentSubset 需要的标的子集（必须全部存在于 source.instruments()）
    /// @throws std::invalid_argument 如果 dateSubset 或 instrumentSubset 为空，
    ///         或包含 source 中不存在的元素
    explicit SubMarketDataView(
        const IMarketDataView& source,
        std::vector<DateKey> dateSubset,
        std::vector<InstrumentId> instrumentSubset);

    ~SubMarketDataView() override;

    // 禁止拷贝和移动
    SubMarketDataView(const SubMarketDataView&) = delete;
    SubMarketDataView& operator=(const SubMarketDataView&) = delete;
    SubMarketDataView(SubMarketDataView&&) = delete;
    SubMarketDataView& operator=(SubMarketDataView&&) = delete;

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