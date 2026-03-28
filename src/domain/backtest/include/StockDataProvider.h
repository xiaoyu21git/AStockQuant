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
    
    virtual std::map<std::string, std::map<std::string, double>> getFactorValuesRange(
        const std::string& factorId,
        const std::string& startDate,
        const std::string& endDate) = 0;
};

// Cache manager interface
class CacheManager {
public:
    virtual ~CacheManager() = default;
    
    // Templated cache get method with default implementation
    template<typename T>
    std::optional<T> getFromCache(const std::string& cacheKey) {
        // Default implementation returns empty optional
        // Derived classes should override if they support the type
        return std::nullopt;
    }
    
    // Templated cache put method with default implementation
    template<typename T>
    void putToCache(const std::string& cacheKey,
                   const T& result,
                   int ttl = 3600) {
        // Default implementation does nothing
        // Derived classes should override if they support the type
    }
    
    // Non-templated virtual methods for type-erased caching
    // These can be overridden by derived classes
    virtual std::optional<std::string> getStringFromCache(const std::string& cacheKey) {
        return std::nullopt;
    }
    
    virtual void putStringToCache(const std::string& cacheKey,
                                  const std::string& result,
                                  int ttl = 3600) {
        // Default implementation does nothing
    }
    
    virtual void invalidateCache(const std::string& pattern) = 0;
    
    virtual void clearAllCache() = 0;
};

} // namespace domain::backtest