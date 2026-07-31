#pragma once

#include "HistoricalView.h"
#include "factor_compute/IMarketDataView.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace factor::compute {

/// @brief 将 factor::compute::IMarketDataView 适配为 factor::HistoricalView
///
/// 设计约束：
/// - 日期/标的索引在构造时一次性构建，后续 O(1) 查找
/// - getCrossSection() 按行索引切片矩阵，O(instruments) 复杂度
/// - 不拷贝数据，直接引用底层 double* 指针
/// - 支持通过 DataService 进行数据库回退查询
class CachedMarketDataViewHistoricalAdapter final : public factor::HistoricalView {
public:
    using DbFallbackFn = std::function<std::unordered_map<std::string, double>(
        const std::string& date, const std::string& field,
        const std::vector<std::string>& symbols)>;

    explicit CachedMarketDataViewHistoricalAdapter(
        const factor::compute::IMarketDataView& marketDataView);

    /// @brief 设置数据库回退查询回调（用于缓存未覆盖日期）
    void setDbFallback(DbFallbackFn callback) { dbFallback_ = std::move(callback); }
    DbFallbackFn dbFallback_;

    [[nodiscard]] bool hasField(const std::string& field) const override;

    [[nodiscard]] std::optional<double> getValue(
        const std::string& symbol,
        const std::string& date,
        const std::string& field) const override;

    [[nodiscard]] std::vector<factor::HistoricalDataPoint> getSeries(
        const std::string& symbol,
        const std::string& startDate,
        const std::string& endDate,
        const std::string& field,
        bool includeNaN = false) const override;

    /// @brief 便捷方法：从 anchorDate 往前取 window 个交易日的数据
    [[nodiscard]] std::vector<factor::HistoricalDataPoint> getSeries(
        const std::string& symbol,
        const std::string& anchorDate,
        int window,
        const std::string& field,
        bool includeNaN = false) const override;

    [[nodiscard]] std::vector<std::string> getAvailableSymbols(
        const std::string& date) const override;

    [[nodiscard]] std::unordered_map<std::string, double> getCrossSection(
        const std::string& date,
        const std::string& field,
        const std::vector<std::string>& symbols = {}) const override;

    [[nodiscard]] std::unordered_map<std::string, std::unordered_map<std::string, double>>
    getBatchCrossSections(
        const std::string& date,
        const std::vector<std::string>& symbols,
        const std::vector<std::string>& fields) const override;

private:
    [[nodiscard]] int32_t findDateIndex(const std::string& date) const;
    [[nodiscard]] int32_t findSymbolIndex(const std::string& symbol) const;

    const factor::compute::IMarketDataView& view_;
    std::unordered_map<std::string, int32_t> dateToIndex_;
    std::unordered_map<std::string, int32_t> symbolToIndex_;
    std::vector<std::string> dates_;
    std::vector<std::string> symbols_;

};

} // namespace factor::compute