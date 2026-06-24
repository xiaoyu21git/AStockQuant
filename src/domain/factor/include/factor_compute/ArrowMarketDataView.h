#pragma once

#include "IMarketDataView.h"

#include <memory>
#include <string>
#include <vector>

namespace factor::compute {

/// @brief Arrow IPC 文件流式行情视图
///
/// 读取 DataCache 写入的 .arrow 文件 (Arrow IPC File format)
/// 构造时仅建立日期/标的索引（~30KB），不持有全量表。
/// 核心列 (open/high/low/close/volume) 首次访问时懒加载并缓存。
/// 额外字段通过 getField() 按需加载。
///
/// makeChunkView() 创建仅包含指定日期区间的自包含视图，
/// 数据直接从 Arrow batches 按需加载，适用于分块回测。
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
    [[nodiscard]] const std::vector<std::string>& symbolStrings() const;

    [[nodiscard]] std::unique_ptr<IMarketDataView>
    slice(DateRange dateRange) const override;

    [[nodiscard]] std::unique_ptr<IMarketDataView>
    slice(const std::vector<InstrumentId>& instrumentIds) const override;

    /// @brief 创建仅含指定日期区间的自包含分块视图
    /// @param dateRange  目标日期列表
    /// @param columns    需要加载的列名（不含 symbol/trade_date）
    /// @return 分块视图，独立持有数据，释放后内存回收
    /// 内存 = len(dateRange) × instrumentCount × len(columns) × 4 bytes
    [[nodiscard]] std::unique_ptr<IMarketDataView>
    makeChunkView(const std::vector<DateKey>& dateRange,
                  const std::vector<std::string>& columns) const;

    /// @brief 预加载指定列为全量矩阵（供非分块路径使用）
    void ensureColumns(const std::vector<std::string>& columnNames) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace factor::compute
