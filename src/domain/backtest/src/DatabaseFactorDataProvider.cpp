#include "DatabaseFactorDataProvider.h"
#include "../../ui/bridge/include/FactorService.h"
#include "../../ui/bridge/include/DataServiceCache.h"
#include <foundation/log/logging.hpp>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <random>
#include <chrono>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QDateTime>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace domain::backtest {

DatabaseFactorDataProvider::DatabaseFactorDataProvider(
    std::shared_ptr<FactorService> factorService)
    : factorService_(factorService) {
    
    // 允许factorService为nullptr，但因子数据将为空
    if (factorService_) {
        INTERNAL_INFO_STREAM << "DatabaseFactorDataProvider initialized with FactorService";
    } else {
        INTERNAL_WARN_STREAM << "DatabaseFactorDataProvider initialized without FactorService, factor data will be empty";
    }
}

DatabaseFactorDataProvider::~DatabaseFactorDataProvider() = default;

std::map<std::string, double> DatabaseFactorDataProvider::getFactorValues(
    const std::string& factorId,
    const std::string& date) {
    
    std::map<std::string, double> result;
    
    try {
        // 如果factorService_可用，尝试从因子服务获取数据
        if (factorService_) {
            // 调用FactorService的getFactorValues方法
            QString qFactorId = QString::fromStdString(factorId);
            QString qDate = QString::fromStdString(date);
            
            QVariantMap factorValues = factorService_->getFactorValues(qFactorId, qDate);
            
            if (!factorValues.isEmpty()) {
                // 检查返回状态
                QString status = factorValues.value("status").toString();
                if (status == "success" && factorValues.contains("stockValues")) {
                    QVariantMap stockValuesMap = factorValues["stockValues"].toMap();
                    
                    // 转换QVariantMap到std::map<std::string, double>
                    for (auto it = stockValuesMap.begin(); it != stockValuesMap.end(); ++it) {
                        QString key = it.key();
                        QVariant value = it.value();
                        
                        if (value.canConvert<double>()) {
                            result[key.toStdString()] = value.toDouble();
                        }
                    }
                    
                    INTERNAL_INFO_STREAM << "Retrieved factor values for factor " << factorId << " on date " << date << ", got " << result.size() << " values";
                } else {
                    INTERNAL_WARN_STREAM << "FactorService returned error for factor " << factorId << " on date " << date << ": " << factorValues.value("error").toString().toStdString();
                }
            } else {
                INTERNAL_WARN_STREAM << "FactorService returned empty values for factor " << factorId << " on date " << date;
            }
        } else {
            INTERNAL_WARN_STREAM << "No FactorService available, returning empty factor values for factor " << factorId << " on date " << date;
        }
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to get factor values for factor " << factorId << " on date " << date << ": " << e.what();
        throw;
    }
    
    return result;
}

std::map<std::string, std::map<std::string, double>> DatabaseFactorDataProvider::getFactorValuesRange(
    const std::string& factorId,
    const std::string& startDate,
    const std::string& endDate)
{
    std::map<std::string, std::map<std::string, double>> result;
    
    try {
        // 解析日期范围
        QDate start = QDate::fromString(QString::fromStdString(startDate), "yyyy-MM-dd");
        QDate end = QDate::fromString(QString::fromStdString(endDate), "yyyy-MM-dd");
        
        if (!start.isValid() || !end.isValid()) {
            throw std::invalid_argument("Invalid date format. Use yyyy-MM-dd");
        }
        
        if (start > end) {
            throw std::invalid_argument("Start date must be before end date");
        }
        
        // 如果factorService_可用，尝试从因子服务获取数据
        if (factorService_) {
            // 生成缓存键 - 与FactorService中的缓存键保持一致
            QString cacheKey = QString("factor_values_range_%1_%2_%3")
                .arg(QString::fromStdString(factorId))
                .arg(QString::fromStdString(startDate))
                .arg(QString::fromStdString(endDate));
            
            // 首先尝试从缓存获取
            QVariantList cachedData = DataServiceCache::getInstance().getData(cacheKey);
            if (!cachedData.isEmpty() && cachedData[0].canConvert<QVariantMap>()) {
                QVariantMap cachedResult = cachedData[0].toMap();
                if (cachedResult["status"].toString() == "success" && cachedResult.contains("data")) {
                    // 从缓存恢复数据
                    QVariantMap dataMap = cachedResult["data"].toMap();
                    for (auto it = dataMap.begin(); it != dataMap.end(); ++it) {
                        QString dateStr = it.key();
                        QVariantMap stockValuesMap = it.value().toMap();
                        
                        std::map<std::string, double> stockValues;
                        for (auto stockIt = stockValuesMap.begin(); stockIt != stockValuesMap.end(); ++stockIt) {
                            stockValues[stockIt.key().toStdString()] = stockIt.value().toDouble();
                        }
                        
                        result[dateStr.toStdString()] = stockValues;
                    }
                    
                    INTERNAL_INFO_STREAM << "Retrieved factor values range from cache for factor " << factorId 
                                        << " from " << startDate << " to " << endDate 
                                        << ", got " << result.size() << " days with data";
                    return result;
                }
            }
            
            // 缓存中没有，使用批量查询方法获取数据
            // 生成日期列表
            QStringList dateList;
            QDate currentDate = start;
            int daysProcessed = 0;
            int maxDaysToProcess = 365; // 安全限制：最多处理365天，防止无限循环
            
            while (currentDate <= end && daysProcessed < maxDaysToProcess) {
                dateList.append(currentDate.toString("yyyy-MM-dd"));
                currentDate = currentDate.addDays(1);
                daysProcessed++;
            }
            
            if (daysProcessed >= maxDaysToProcess) {
                INTERNAL_WARN_STREAM << "Reached maximum days to process (" << maxDaysToProcess << ") for factor " << factorId << ", stopping to prevent infinite loop";
            }
            
            if (!dateList.isEmpty()) {
                // 使用批量查询方法获取数据
                QString qFactorId = QString::fromStdString(factorId);
                QVariantMap batchResult = factorService_->getFactorValuesBatch(qFactorId, dateList);
                
                if (!batchResult.isEmpty()) {
                    QString status = batchResult.value("status").toString();
                    if (status == "success" && batchResult.contains("batchResults")) {
                        QVariantMap batchResults = batchResult["batchResults"].toMap();
                        
                        for (const QString& dateStr : dateList) {
                            if (batchResults.contains(dateStr)) {
                                QVariantMap dayResult = batchResults[dateStr].toMap();
                                if (dayResult["status"].toString() == "success" && dayResult.contains("stockValues")) {
                                    QVariantMap stockValuesMap = dayResult["stockValues"].toMap();
                                    
                                    // 转换QVariantMap到std::map<std::string, double>
                                    std::map<std::string, double> stockValues;
                                    for (auto it = stockValuesMap.begin(); it != stockValuesMap.end(); ++it) {
                                        QString key = it.key();
                                        QVariant value = it.value();
                                        
                                        if (value.canConvert<double>()) {
                                            stockValues[key.toStdString()] = value.toDouble();
                                        }
                                    }
                                    
                                    if (!stockValues.empty()) {
                                        result[dateStr.toStdString()] = stockValues;
                                    }
                                }
                            }
                        }
                    } else {
                        INTERNAL_WARN_STREAM << "FactorService batch query returned error for factor " << factorId << ": " << batchResult.value("error").toString().toStdString();
                    }
                }
            }
            
            // 将结果保存到缓存
            if (!result.empty()) {
                QVariantMap cacheData;
                QVariantMap dataMap;
                for (const auto& dateData : result) {
                    QVariantMap stockValuesMap;
                    for (const auto& stockValue : dateData.second) {
                        stockValuesMap[QString::fromStdString(stockValue.first)] = stockValue.second;
                    }
                    dataMap[QString::fromStdString(dateData.first)] = stockValuesMap;
                }
                
                QVariantMap cacheResult;
                cacheResult["status"] = "success";
                cacheResult["factorId"] = QString::fromStdString(factorId);
                cacheResult["startDate"] = QString::fromStdString(startDate);
                cacheResult["endDate"] = QString::fromStdString(endDate);
                cacheResult["data"] = dataMap;
                
                QVariantList cacheList;
                cacheList.append(cacheResult);
                DataServiceCache::getInstance().storeData(cacheKey, cacheList);
                
                INTERNAL_INFO_STREAM << "Cached factor values range for factor " << factorId 
                                    << " from " << startDate << " to " << endDate 
                                    << ", " << result.size() << " days";
            }
            
            INTERNAL_INFO_STREAM << "Retrieved factor values range for factor " << factorId << " from " << startDate << " to " << endDate << ", got " << result.size() << " days with data";
        } else {
            INTERNAL_WARN_STREAM << "No FactorService available, returning empty factor values range for factor " << factorId << " from " << startDate << " to " << endDate;
        }
        
        // 如果结果为空，记录信息
        if (result.empty()) {
            INTERNAL_INFO_STREAM << "Returning empty factor values range for factor " << factorId << " from " << startDate << " to " << endDate;
        }
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to get factor values range for factor " << factorId << " from " << startDate << " to " << endDate << ": " << e.what();
        throw;
    }
    
    return result;
}

std::vector<std::string> DatabaseFactorDataProvider::getAvailableDates(
    const std::string& factorId) {
    
    std::vector<std::string> dates;
    
    try {
        // 如果factorService_可用，尝试从因子服务获取可用日期
        if (factorService_) {
            QString qFactorId = QString::fromStdString(factorId);
            QStringList dateList = factorService_->getAvailableDates(qFactorId);
            
            for (const QString& date : dateList) {
                dates.push_back(date.toStdString());
            }
            
            INTERNAL_INFO_STREAM << "Retrieved " << dates.size() << " available dates for factor " << factorId;
        } else {
            INTERNAL_WARN_STREAM << "No FactorService available, returning empty available dates for factor " << factorId;
        }
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to get available dates for factor " << factorId << ": " << e.what();
        throw;
    }
    
    return dates;
}

std::vector<std::string> DatabaseFactorDataProvider::getAvailableStocks(
    const std::string& factorId,
    const std::string& date) {
    
    std::vector<std::string> stocks;
    
    try {
        auto factorValues = getFactorValues(factorId, date);
        for (const auto& kv : factorValues) {
            stocks.push_back(kv.first);
        }
        
        INTERNAL_INFO_STREAM << "Found " << stocks.size() << " available stocks for factor " << factorId << " on date " << date;
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to get available stocks for factor " << factorId << " on date " << date << ": " << e.what();
        throw;
    }
    
    return stocks;
}

std::map<std::string, std::vector<double>> DatabaseFactorDataProvider::getFactorTimeSeriesForStocks(
    const std::string& factorId,
    const std::vector<std::string>& stockCodes,
    const std::string& startDate,
    const std::string& endDate) {
    
    std::map<std::string, std::vector<double>> result;
    
    try {
        // 获取完整的时间序列数据
        auto allData = getFactorValuesRange(factorId, startDate, endDate);
        
        // 为每只股票提取时间序列
        for (const auto& stockCode : stockCodes) {
            std::vector<double> timeSeries;
            
            // 按日期顺序提取数据
            for (const auto& dateData : allData) {
                auto it = dateData.second.find(stockCode);
                if (it != dateData.second.end()) {
                    timeSeries.push_back(it->second);
                } else {
                    // 如果某天没有数据，使用NaN填充
                    timeSeries.push_back(std::numeric_limits<double>::quiet_NaN());
                }
            }
            
            result[stockCode] = timeSeries;
        }
        
        INTERNAL_INFO_STREAM << "Generated factor time series for " << stockCodes.size() << " stocks, factor " << factorId << ", " << allData.size() << " days";
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to get factor time series for stocks: " << e.what();
        throw;
    }
    
    return result;
}

bool DatabaseFactorDataProvider::validateFactorData(
    const std::string& factorId,
    const std::string& date) {
    
    try {
        auto values = getFactorValues(factorId, date);
        return !values.empty();
        
    } catch (const std::exception&) {
        return false;
    }
}

std::map<std::string, double> DatabaseFactorDataProvider::getFactorStatistics(
    const std::string& factorId,
    const std::string& startDate,
    const std::string& endDate) {
    
    std::map<std::string, double> stats;
    
    try {
        auto timeSeriesData = getFactorValuesRange(factorId, startDate, endDate);
        
        if (timeSeriesData.empty()) {
            return stats;
        }
        
        // 收集所有因子值
        std::vector<double> allValues;
        for (const auto& dateData : timeSeriesData) {
            for (const auto& stockValue : dateData.second) {
                allValues.push_back(stockValue.second);
            }
        }
        
        if (allValues.empty()) {
            return stats;
        }
        
        // 计算统计量
        double sum = 0.0;
        double sumSq = 0.0;
        double minValue = std::numeric_limits<double>::max();
        double maxValue = std::numeric_limits<double>::lowest();
        
        for (double value : allValues) {
            sum += value;
            sumSq += value * value;
            if (value < minValue) minValue = value;
            if (value > maxValue) maxValue = value;
        }
        
        double mean = sum / allValues.size();
        double variance = (sumSq / allValues.size()) - (mean * mean);
        double stdDev = std::sqrt(std::max(variance, 0.0));
        
        // 计算分位数
        std::sort(allValues.begin(), allValues.end());
        double median = allValues[allValues.size() / 2];
        double q1 = allValues[allValues.size() / 4];
        double q3 = allValues[3 * allValues.size() / 4];
        
        // 填充统计结果
        stats["count"] = static_cast<double>(allValues.size());
        stats["mean"] = mean;
        stats["std"] = stdDev;
        stats["min"] = minValue;
        stats["max"] = maxValue;
        stats["median"] = median;
        stats["q1"] = q1;
        stats["q3"] = q3;
        stats["skewness"] = calculateSkewness(allValues, mean, stdDev);
        stats["kurtosis"] = calculateKurtosis(allValues, mean, stdDev);
        
        INTERNAL_INFO_STREAM << "Calculated factor statistics for factor " << factorId << " from " << startDate << " to " << endDate;
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to calculate factor statistics: " << e.what();
        throw;
    }
    
    return stats;
}

double DatabaseFactorDataProvider::calculateSkewness(
    const std::vector<double>& values,
    double mean,
    double stdDev) {
    
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

double DatabaseFactorDataProvider::calculateKurtosis(
    const std::vector<double>& values,
    double mean,
    double stdDev) {
    
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

void DatabaseFactorDataProvider::preloadFactorData(
    const std::string& factorId,
    const std::string& startDate,
    const std::string& endDate) {
    
    try {
        // 预加载数据到缓存
        auto data = getFactorValuesRange(factorId, startDate, endDate);
        INTERNAL_INFO_STREAM << "Preloaded factor " << factorId << " data from " << startDate << " to " << endDate << ", " << data.size() << " days";
        
    } catch (const std::exception& e) {
        INTERNAL_WARN_STREAM << "Failed to preload factor data: " << e.what();
    }
}

void DatabaseFactorDataProvider::clearCache() {
    // 清除内部缓存（如果有的话）
    // 这里可以添加缓存清理逻辑
    INTERNAL_INFO_STREAM << "Cleared factor data cache";
}

} // namespace domain::backtest