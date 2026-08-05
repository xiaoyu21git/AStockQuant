#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace factor {

struct HistoricalDataPoint {
    std::string date;
    double value = 0.0;
};

class HistoricalView {
public:
    virtual ~HistoricalView() = default;

    virtual bool hasField(const std::string& field) const = 0;

    virtual std::optional<double> getValue(const std::string& symbol,
                                           const std::string& date,
                                           const std::string& field) const = 0;

    /// @param includeNaN 为 true 时保留 NaN 数据点（保持时间轴连续无缺口），false 时只返回有限值（兼容历史行为）
    virtual std::vector<HistoricalDataPoint> getSeries(const std::string& symbol,
                                                       const std::string& startDate,
                                                       const std::string& endDate,
                                                       const std::string& field,
                                                       bool includeNaN = false) const = 0;

    /// @brief 便捷方法：从 anchorDate 往前取 window 个交易日的数据
    virtual std::vector<HistoricalDataPoint> getSeries(const std::string& symbol,
                                                       const std::string& anchorDate,
                                                       int window,
                                                       const std::string& field,
                                                       bool includeNaN = false) const
    {
        (void)symbol; (void)anchorDate; (void)window; (void)field; (void)includeNaN;
        return {};
    }

    virtual std::vector<std::string> getAvailableSymbols(const std::string& date) const = 0;

    virtual std::unordered_map<std::string, double> getCrossSection(
        const std::string& date,
        const std::string& field,
        const std::vector<std::string>& symbols = {}) const = 0;

    virtual std::unordered_map<std::string, std::unordered_map<std::string, double>> getBatchCrossSections(
        const std::string& date,
        const std::vector<std::string>& symbols,
        const std::vector<std::string>& fields) const = 0;
};

} // namespace factor