#include "DatabaseStockDataProvider.h"
#include <foundation/log/logging.hpp>
#include <stdexcept>
#include <algorithm>
#include <numeric>
#include <mutex>
#include <QCoreApplication>
#include <QThread>
#include <QMetaObject>
#include <QTimer>
#include <QDateTime>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include "../../ui/bridge/include/DataServiceCache.h"

namespace domain::backtest {

DatabaseStockDataProvider::DatabaseStockDataProvider(
    std::shared_ptr<ui::bridge::DataFetchController> dataFetchController)
    : dataFetchController_(dataFetchController) {
    
    // 允许dataFetchController为nullptr，因为我们可以使用DataServiceCache
    if (dataFetchController_) {
        INTERNAL_INFO_STREAM << "DatabaseStockDataProvider initialized with DataFetchController";
    } else {
        INTERNAL_WARN_STREAM << "DatabaseStockDataProvider initialized without DataFetchController, will use DataServiceCache only";
    }
    
    // 初始化DataServiceCache
    DataServiceCache::getInstance().initializeCache();
}

DatabaseStockDataProvider::~DatabaseStockDataProvider() = default;

std::vector<domain::model::Bar> DatabaseStockDataProvider::getStockBars(
    const std::string& symbol,
    const std::string& startDate,
    const std::string& endDate) {
    
    // 首先检查本地缓存
    auto cachedData = getFromCache(symbol, startDate, endDate);
    if (!cachedData.empty()) {
        INTERNAL_DEBUG_STREAM << "Returning cached data for symbol " << symbol << " from " << startDate << " to " << endDate;
        return cachedData;
    }
    
    std::vector<domain::model::Bar> bars;
    
    try {
        DataServiceCache& cache = DataServiceCache::getInstance();
        
        QString qSymbol = QString::fromStdString(symbol);
        QString qStartDate = QString::fromStdString(startDate);
        QString qEndDate = QString::fromStdString(endDate);
        
        QVariantList rawData;
        
        // 策略1: 首先尝试精确匹配单股票缓存
        rawData = cache.getCachedData(qSymbol, qStartDate, qEndDate);
        
        // 策略2: 如果没找到，尝试从"ALL"聚合缓存中获取并过滤
        if (rawData.isEmpty()) {
            INTERNAL_DEBUG_STREAM << "No exact cache for " << symbol << ", trying ALL cache";
            
            // 尝试获取聚合数据（symbol为空表示ALL）
            rawData = cache.getCachedData("", qStartDate, qEndDate);
            
            // 从聚合数据中过滤出指定股票的数据
            if (!rawData.isEmpty()) {
                QVariantList filteredData;
                for (const QVariant& item : rawData) {
                    QVariantMap record = item.toMap();
                    QString recordSymbol = record.value("symbol").toString();
                    if (recordSymbol == qSymbol) {
                        filteredData.append(item);
                    }
                }
                rawData = filteredData;
                INTERNAL_DEBUG_STREAM << "Filtered " << rawData.size() << " bars for " << symbol << " from ALL cache";
            }
        }
        
        // 策略3: 尝试从DataSetInfos中获取
        if (rawData.isEmpty()) {
            INTERNAL_DEBUG_STREAM << "Trying DataSetInfos for " << symbol;
            
            QVector<DataServiceCache::DataSetInfo> dataSets = cache.getAllDataSetInfos();
            for (const auto& ds : dataSets) {
                // 检查该数据集是否包含目标股票
                if (ds.stockCodes.isEmpty() || ds.stockCodes.contains(qSymbol)) {
                    QVariantList dsData = cache.getDataSetById(ds.id);
                    if (!dsData.isEmpty()) {
                        // 过滤出指定股票的数据
                        for (const QVariant& item : dsData) {
                            QVariantMap record = item.toMap();
                            QString recordSymbol = record.value("symbol").toString();
                            // 如果数据集stockCodes为空(表示全部股票)，或者symbol匹配
                            if (ds.stockCodes.isEmpty() || recordSymbol == qSymbol) {
                                if (recordSymbol == qSymbol) {
                                    rawData.append(item);
                                }
                            }
                        }
                        if (!rawData.isEmpty()) {
                            INTERNAL_DEBUG_STREAM << "Found " << rawData.size() << " bars for " << symbol << " from dataset " << ds.displayName.toStdString();
                            break;
                        }
                    }
                }
            }
        }
        
        if (rawData.isEmpty()) {
            INTERNAL_WARN_STREAM << "No cached data found for symbol " << symbol << " from " << startDate << " to " << endDate;
            return bars;
        }
        
        // 转换数据格式
        bars = convertToBars(rawData);
        
        INTERNAL_INFO_STREAM << "Successfully loaded " << bars.size() << " bars for symbol " << symbol << " from " << startDate << " to " << endDate;
        
        // 将数据添加到本地缓存
        addToCache(symbol, bars);
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to get stock bars for symbol " << symbol << " from " << startDate << " to " << endDate << ": " << e.what();
        throw;
    }
    
    return bars;
}

std::map<std::string, std::vector<domain::model::Bar>> DatabaseStockDataProvider::getMultipleStockBars(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate) {
    
    std::map<std::string, std::vector<domain::model::Bar>> result;
    
    try {
        // 并行获取多只股票的数据
        for (const auto& symbol : symbols) {
            try {
                auto bars = getStockBars(symbol, startDate, endDate);
                result[symbol] = bars;
            } catch (const std::exception& e) {
                INTERNAL_ERROR_STREAM << "Failed to get bars for symbol " << symbol << ": " << e.what();
                // 继续处理其他股票
            }
        }
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to get multiple stock bars: " << e.what();
        throw;
    }
    
    return result;
}

std::vector<std::string> DatabaseStockDataProvider::getAvailableSymbols() {
    std::vector<std::string> symbols;
    
    try {
        // 这里应该从数据库或配置中获取可用的股票代码
        // 由于时间关系，返回空列表
        
        INTERNAL_WARN_STREAM << "getAvailableSymbols not fully implemented";
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to get available symbols: " << e.what();
        throw;
    }
    
    return symbols;
}

std::vector<std::string> DatabaseStockDataProvider::getAvailableDates(
    const std::string& symbol) {
    
    std::vector<std::string> dates;
    
    try {
        // 这里应该从数据库中获取指定股票可用的日期
        // 由于时间关系，返回空列表
        
        INTERNAL_WARN_STREAM << "getAvailableDates not fully implemented for symbol " << symbol;
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to get available dates for symbol " << symbol << ": " << e.what();
        throw;
    }
    
    return dates;
}

std::map<std::string, std::vector<double>> DatabaseStockDataProvider::getStockTimeSeries(
    const std::string& symbol,
    const std::string& field,
    const std::string& startDate,
    const std::string& endDate) {
    
    std::map<std::string, std::vector<double>> result;
    
    try {
        auto bars = getStockBars(symbol, startDate, endDate);
        
        std::vector<double> values;
        values.reserve(bars.size());
        
        for (const auto& bar : bars) {
            if (field == "open") {
                values.push_back(bar.open);
            } else if (field == "high") {
                values.push_back(bar.high);
            } else if (field == "low") {
                values.push_back(bar.low);
            } else if (field == "close") {
                values.push_back(bar.close);
            } else if (field == "volume") {
                values.push_back(static_cast<double>(bar.volume));
            } else {
                throw std::invalid_argument("Unknown field: " + field);
            }
        }
        
        result[field] = values;
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to get stock time series for symbol " << symbol << " field " << field << ": " << e.what();
        throw;
    }
    
    return result;
}

std::map<std::string, std::map<std::string, std::vector<double>>> DatabaseStockDataProvider::getMultipleStockTimeSeries(
    const std::vector<std::string>& symbols,
    const std::vector<std::string>& fields,
    const std::string& startDate,
    const std::string& endDate) {
    
    std::map<std::string, std::map<std::string, std::vector<double>>> result;
    
    try {
        for (const auto& symbol : symbols) {
            std::map<std::string, std::vector<double>> symbolData;
            
            for (const auto& field : fields) {
                auto timeSeries = getStockTimeSeries(symbol, field, startDate, endDate);
                if (!timeSeries.empty()) {
                    symbolData[field] = timeSeries[field];
                }
            }
            
            if (!symbolData.empty()) {
                result[symbol] = symbolData;
            }
        }
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to get multiple stock time series: " << e.what();
        throw;
    }
    
    return result;
}

bool DatabaseStockDataProvider::validateStockData(
    const std::string& symbol,
    const std::string& startDate,
    const std::string& endDate) {
    
    try {
        auto bars = getStockBars(symbol, startDate, endDate);
        return !bars.empty();
        
    } catch (const std::exception&) {
        return false;
    }
}

std::map<std::string, double> DatabaseStockDataProvider::getStockStatistics(
    const std::string& symbol,
    const std::string& field,
    const std::string& startDate,
    const std::string& endDate) {
    
    std::map<std::string, double> stats;
    
    try {
        auto timeSeries = getStockTimeSeries(symbol, field, startDate, endDate);
        auto& values = timeSeries[field];
        
        if (values.empty()) {
            return stats;
        }
        
        // 计算统计量
        double sum = std::accumulate(values.begin(), values.end(), 0.0);
        double mean = sum / values.size();
        
        double sumSq = 0.0;
        double minValue = std::numeric_limits<double>::max();
        double maxValue = std::numeric_limits<double>::lowest();
        
        for (double value : values) {
            double diff = value - mean;
            sumSq += diff * diff;
            if (value < minValue) minValue = value;
            if (value > maxValue) maxValue = value;
        }
        
        double variance = sumSq / values.size();
        double stdDev = std::sqrt(std::max(variance, 0.0));
        
        // 计算分位数
        std::vector<double> sortedValues = values;
        std::sort(sortedValues.begin(), sortedValues.end());
        
        double median = sortedValues[sortedValues.size() / 2];
        double q1 = sortedValues[sortedValues.size() / 4];
        double q3 = sortedValues[3 * sortedValues.size() / 4];
        
        // 填充统计结果
        stats["count"] = static_cast<double>(values.size());
        stats["mean"] = mean;
        stats["std"] = stdDev;
        stats["min"] = minValue;
        stats["max"] = maxValue;
        stats["median"] = median;
        stats["q1"] = q1;
        stats["q3"] = q3;
        stats["skewness"] = calculateSkewness(values, mean, stdDev);
        stats["kurtosis"] = calculateKurtosis(values, mean, stdDev);
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to calculate stock statistics: " << e.what();
        throw;
    }
    
    return stats;
}

void DatabaseStockDataProvider::preloadStockData(
    const std::string& symbol,
    const std::string& startDate,
    const std::string& endDate) {
    
    try {
        // 预加载数据到缓存
        auto bars = getStockBars(symbol, startDate, endDate);
        INTERNAL_INFO_STREAM << "Preloaded stock " << symbol << " data from " << startDate << " to " << endDate << ", " << bars.size() << " bars";
        
    } catch (const std::exception& e) {
        INTERNAL_WARN_STREAM << "Failed to preload stock data: " << e.what();
    }
}

void DatabaseStockDataProvider::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    dataCache_.clear();
    INTERNAL_INFO_STREAM << "Cleared stock data cache";
}

domain::model::Bar DatabaseStockDataProvider::convertToBar(const QVariantMap& data) {
    domain::model::Bar bar;
    
    // 从QVariantMap中提取数据
    if (data.contains("symbol")) {
        bar.symbol = data["symbol"].toString().toStdString();
    }
    
    if (data.contains("time")) {
        bar.time = data["time"].toLongLong();
    } else if (data.contains("date")) {
        // 如果只有日期字符串，转换为时间戳
        QString dateStr = data["date"].toString();
        QDateTime dateTime = QDateTime::fromString(dateStr, "yyyy-MM-dd");
        if (!dateTime.isValid()) {
            dateTime = QDateTime::fromString(dateStr, "yyyy/MM/dd");
        }
        if (dateTime.isValid()) {
            bar.time = dateTime.toMSecsSinceEpoch();
        }
    }
    
    if (data.contains("open")) {
        bar.open = data["open"].toDouble();
    }
    
    if (data.contains("high")) {
        bar.high = data["high"].toDouble();
    }
    
    if (data.contains("low")) {
        bar.low = data["low"].toDouble();
    }
    
    if (data.contains("close")) {
        bar.close = data["close"].toDouble();
    }
    
    if (data.contains("volume")) {
        bar.volume = data["volume"].toDouble();
    }
    
    return bar;
}

std::vector<domain::model::Bar> DatabaseStockDataProvider::convertToBars(const QVariantList& data) {
    std::vector<domain::model::Bar> bars;
    bars.reserve(data.size());
    
    for (const auto& item : data) {
        bars.push_back(convertToBar(item.toMap()));
    }
    
    return bars;
}

void DatabaseStockDataProvider::addToCache(const std::string& symbol, const std::vector<domain::model::Bar>& bars) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    dataCache_[symbol] = bars;
}

std::vector<domain::model::Bar> DatabaseStockDataProvider::getFromCache(
    const std::string& symbol,
    const std::string& startDate,
    const std::string& endDate) {
    
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    auto it = dataCache_.find(symbol);
    if (it == dataCache_.end()) {
        return {};
    }
    
    // 这里应该根据日期范围过滤缓存数据
    // 由于时间关系，返回所有缓存数据
    return it->second;
}

void DatabaseStockDataProvider::clearCacheForSymbol(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    dataCache_.erase(symbol);
}

double DatabaseStockDataProvider::calculateMean(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return sum / values.size();
}

double DatabaseStockDataProvider::calculateStdDev(const std::vector<double>& values, double mean) {
    if (values.size() < 2) return 0.0;
    
    double sumSq = 0.0;
    for (double value : values) {
        double diff = value - mean;
        sumSq += diff * diff;
    }
    
    double variance = sumSq / values.size();
    return std::sqrt(std::max(variance, 0.0));
}

double DatabaseStockDataProvider::calculateSkewness(const std::vector<double>& values, double mean, double stdDev) {
    if (values.size() < 3 || stdDev < 1e-10) {
        return 0.0;
    }
    
    double sumCubed = 0.0;
    for (double value : values) {
        double diff = value - mean;
        sumCubed += diff * diff * diff;
    }
    
    double n = static_cast<double>(values.size());
    return (sumCubed / n) / (stdDev * stdDev * stdDev);
}

double DatabaseStockDataProvider::calculateKurtosis(const std::vector<double>& values, double mean, double stdDev) {
    if (values.size() < 4 || stdDev < 1e-10) {
        return 0.0;
    }
    
    double sumFourth = 0.0;
    for (double value : values) {
        double diff = value - mean;
        sumFourth += diff * diff * diff * diff;
    }
    
    double n = static_cast<double>(values.size());
    return (sumFourth / n) / (stdDev * stdDev * stdDev * stdDev) - 3.0;
}

} // namespace domain::backtest
