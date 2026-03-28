#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct FactorDataPoint {
    std::string date;
    double value = 0.0;
};

class FactorDataProvider {
public:
    virtual ~FactorDataProvider() = default;

    virtual bool hasField(const std::string& field) const = 0;

    virtual std::optional<double> getValue(const std::string& symbol,
                                           const std::string& date,
                                           const std::string& field) const = 0;

    virtual std::vector<FactorDataPoint> getSeries(const std::string& symbol,
                                                   const std::string& startDate,
                                                   const std::string& endDate,
                                                   const std::string& field) const = 0;

    virtual std::vector<std::string> getAvailableSymbols(const std::string& date) const = 0;

    virtual std::unordered_map<std::string, double> getCrossSection(
        const std::string& date,
        const std::string& field,
        const std::vector<std::string>& symbols = {}) const = 0;
};