#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include "StockDataProvider.h"
#include <QVariantMap>
#include <QVariantList>

// 前向声明
namespace ui::bridge {
    class DataFetchController;
}

namespace domain::backtest {

class DatabaseStockDataProvider : public StockDataProvider {
public:
    explicit DatabaseStockDataProvider(std::shared_ptr<ui::bridge::DataFetchController> dataFetchController);
    virtual ~DatabaseStockDataProvider();
    
    // StockDataProvider接口实现
    std::vector<domain::model::Bar> getStockBars(
        const std::string& symbol,
        const std::string& startDate,
        const std::string& endDate) override;
    
    std::map<std::string, std::vector<domain::model::Bar>> getMultipleStockBars(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate) override;
    
    std::vector<std::string> getAvailableSymbols() override;
    
    // 扩展接口
    std::vector<std::string> getAvailableDates(
        const std::string& symbol);
    
    std::map<std::string, std::vector<double>> getStockTimeSeries(
        const std::string& symbol,
        const std::string& field,  // "open", "high", "low", "close", "volume"
        const std::string& startDate,
        const std::string& endDate);
    
    std::map<std::string, std::map<std::string, std::vector<double>>> getMultipleStockTimeSeries(
        const std::vector<std::string>& symbols,
        const std::vector<std::string>& fields,
        const std::string& startDate,
        const std::string& endDate);
    
    bool validateStockData(
        const std::string& symbol,
        const std::string& startDate,
        const std::string& endDate);
    
    std::map<std::string, double> getStockStatistics(
        const std::string& symbol,
        const std::string& field,
        const std::string& startDate,
        const std::string& endDate);
    
    void preloadStockData(
        const std::string& symbol,
        const std::string& startDate,
        const std::string& endDate);
    
    void clearCache();
    
private:
    std::shared_ptr<ui::bridge::DataFetchController> dataFetchController_;
    
    // 辅助函数
    domain::model::Bar convertToBar(const QVariantMap& data);
    std::vector<domain::model::Bar> convertToBars(const QVariantList& data);
    
    // 数据缓存
    std::map<std::string, std::vector<domain::model::Bar>> dataCache_;
    std::mutex cacheMutex_;
    
    // 缓存管理
    void addToCache(const std::string& symbol, const std::vector<domain::model::Bar>& bars);
    std::vector<domain::model::Bar> getFromCache(const std::string& symbol, 
                                                const std::string& startDate,
                                                const std::string& endDate);
    void clearCacheForSymbol(const std::string& symbol);
    
    // 统计计算
    double calculateMean(const std::vector<double>& values);
    double calculateStdDev(const std::vector<double>& values, double mean);
    double calculateSkewness(const std::vector<double>& values, double mean, double stdDev);
    double calculateKurtosis(const std::vector<double>& values, double mean, double stdDev);
};

} // namespace domain::backtest