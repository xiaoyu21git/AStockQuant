#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "FactorBacktestService.h"

// 前向声明
class FactorService;

namespace domain::backtest {

class DatabaseFactorDataProvider : public FactorDataProvider {
public:
    explicit DatabaseFactorDataProvider(std::shared_ptr<FactorService> factorService);
    virtual ~DatabaseFactorDataProvider();
    
    // FactorDataProvider接口实现
    std::map<std::string, double> getFactorValues(
        const std::string& factorId,
        const std::string& date) override;
    
    std::map<std::string, std::map<std::string, double>> getFactorValuesRange(
        const std::string& factorId,
        const std::string& startDate,
        const std::string& endDate) override;
    
    std::vector<std::string> getAvailableDates(
        const std::string& factorId) override;
    
    // 扩展接口
    std::vector<std::string> getAvailableStocks(
        const std::string& factorId,
        const std::string& date);
    
    std::map<std::string, std::vector<double>> getFactorTimeSeriesForStocks(
        const std::string& factorId,
        const std::vector<std::string>& stockCodes,
        const std::string& startDate,
        const std::string& endDate);
    
    bool validateFactorData(
        const std::string& factorId,
        const std::string& date);
    
    std::map<std::string, double> getFactorStatistics(
        const std::string& factorId,
        const std::string& startDate,
        const std::string& endDate);
    
    void preloadFactorData(
        const std::string& factorId,
        const std::string& startDate,
        const std::string& endDate);
    
    void clearCache();
    
private:
    std::shared_ptr<FactorService> factorService_;
    
    double calculateSkewness(
        const std::vector<double>& values,
        double mean,
        double stdDev);
    
    double calculateKurtosis(
        const std::vector<double>& values,
        double mean,
        double stdDev);
};

} // namespace domain::backtest