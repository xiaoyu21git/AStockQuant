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

    virtual std::vector<HistoricalDataPoint> getSeries(const std::string& symbol,
                                                       const std::string& startDate,
                                                       const std::string& endDate,
                                                       const std::string& field) const = 0;

    virtual std::vector<std::string> getAvailableSymbols(const std::string& date) const = 0;

    virtual std::unordered_map<std::string, double> getCrossSection(
        const std::string& date,
        const std::string& field,
        const std::vector<std::string>& symbols = {}) const = 0;

    virtual std::unordered_map<std::string, std::unordered_map<std::string, double>> getBatchCrossSections(
        const std::string& date,
        const std::vector<std::string>& symbols,
        const std::vector<std::string>& fields) const = 0;

    virtual std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> getBatchTimeSeries(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate,
        const std::vector<std::string>& fields) const = 0;

    virtual std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> getBatchTimeSeries(
        const std::vector<std::string>& symbols,
        const std::string& anchorDate,
        int window,
        const std::vector<std::string>& fields) const
    {
        (void)symbols;
        (void)anchorDate;
        (void)window;
        (void)fields;
        return {};
    }
};

} // namespace factor