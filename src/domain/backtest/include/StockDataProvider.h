#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include "../../model/include/Bar.h"

namespace domain::backtest {

// Forward declaration
class FactorBacktestResult;

// Stock data provider interface
class StockDataProvider {
public:
    virtual ~StockDataProvider() = default;
    
    virtual std::vector<domain::model::Bar> getStockBars(
        const std::string& symbol,
        const std::string& startDate,
        const std::string& endDate) = 0;
    
    virtual std::map<std::string, std::vector<domain::model::Bar>> getMultipleStockBars(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate) = 0;
    
    virtual std::vector<std::string> getAvailableSymbols() = 0;
};

// Factor data provider interface
class FactorDataProvider {
public:
    virtual ~FactorDataProvider() = default;
    
    virtual std::map<std::string, double> getFactorValues(
        const std::string& factorId,
        const std::string& date) = 0;
    
    virtual std::map<std::string, std::map<std::string, double>> getFactorValuesRange(
        const std::string& factorId,
        const std::string& startDate,
        const std::string& endDate) = 0;
    
    virtual std::vector<std::string> getAvailableDates(
        const std::string& factorId) = 0;
};

// Cache manager interface
class CacheManager {
public:
    virtual ~CacheManager() = default;
    
    virtual std::optional<FactorBacktestResult> getFromCache(
        const std::string& cacheKey) = 0;
    
    virtual void putToCache(
        const std::string& cacheKey,
        const FactorBacktestResult& result,
        int ttl = 3600) = 0;
    
    virtual void invalidateCache(const std::string& pattern) = 0;
    
    virtual void clearAllCache() = 0;
};

} // namespace domain::backtest