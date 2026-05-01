#pragma once

#include "FactorBacktestExecutor.h"

#include <arrow/api.h>
#include <arrow/compute/api.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace factor {

class ArrowMarketData final {
public:
    // 从回测缓存行构建 Arrow 表，并同时生成按 symbol/date 的查询索引。
    static std::shared_ptr<ArrowMarketData> fromCachedBars(const std::vector<factor::CachedMarketBar>& bars);

    std::shared_ptr<arrow::ChunkedArray> getColumn(const std::string& field) const;

    double getValue(const std::string& symbol,
                    const std::string& date,
                    const std::string& field) const;

    std::shared_ptr<arrow::DoubleArray> getTimeSeries(const std::string& symbol,
                                                      const std::string& field,
                                                      int lookbackWindow) const;

    std::vector<std::vector<double>> getBatchTimeSeries(const std::vector<std::string>& symbols,
                                                        const std::string& field,
                                                        int window,
                                                        const std::string& anchorDate) const;

    std::vector<FactorDataPoint> getSeries(const std::string& symbol,
                                           const std::string& startDate,
                                           const std::string& endDate,
                                           const std::string& field) const;

    std::vector<std::string> getAvailableSymbols(const std::string& date) const;

    std::unordered_map<std::string, double> getCrossSection(const std::string& date,
                                                            const std::string& field,
                                                            const std::vector<std::string>& symbols = {}) const;

    std::unordered_map<std::string, std::unordered_map<std::string, double>> getBatchCrossSections(
        const std::string& date,
        const std::vector<std::string>& symbols,
        const std::vector<std::string>& fields) const;

    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> getBatchTimeSeries(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate,
        const std::vector<std::string>& fields) const;

    const std::vector<std::string>& symbols() const { return symbols_; }
    const std::vector<std::string>& dates() const { return dates_; }
    int symbolIndex(const std::string& symbol) const;
    int dateIndex(const std::string& date) const;

private:
    struct NormalizedBar {
        std::string symbol;
        std::string tradeDate;
        double close = 0.0;
        std::unordered_map<std::string, double> numericFields;
    };

    ArrowMarketData() = default;

    static std::shared_ptr<arrow::Array> takeArray(const std::shared_ptr<arrow::Array>& values,
                                                   const std::vector<int64_t>& indices);

    std::vector<int64_t> selectRowsForSymbol(const std::string& symbol,
                                             const std::string& startDate,
                                             const std::string& endDate) const;
    std::vector<int64_t> selectRowsForSymbolWindow(const std::string& symbol,
                                                   const std::string& anchorDate,
                                                   int window) const;

    std::shared_ptr<arrow::Table> table_;
    std::vector<std::string> symbols_;
    std::vector<std::string> dates_;
    std::vector<std::string> fieldNames_;
    std::unordered_map<std::string, int> symbolToIndex_;
    std::unordered_map<std::string, int> dateToIndex_;
    std::unordered_map<std::string, std::unordered_map<std::string, int64_t>> rowIndexByDateSymbol_;
    std::unordered_map<std::string, std::vector<int64_t>> symbolToRowIndices_;
    std::unordered_map<std::string, std::vector<std::string>> symbolToDates_;
    std::unordered_map<std::string, std::vector<std::string>> dateToSymbols_;
};

} // namespace factor