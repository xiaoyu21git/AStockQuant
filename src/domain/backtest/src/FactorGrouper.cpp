#include "FactorGrouper.h"
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace domain::backtest {

std::vector<FactorGroup> FactorGrouper::groupByFactorValue(
    const std::map<std::string, double>& factorValues,
    int numGroups) {
    
    if (factorValues.empty() || numGroups <= 0) {
        return {};
    }
    
    // 获取最小值和最大值
    double minValue = (std::numeric_limits<double>::max)();
    double maxValue = std::numeric_limits<double>::lowest();
    
    for (const auto& kv : factorValues) {
        if (kv.second < minValue) minValue = kv.second;
        if (kv.second > maxValue) maxValue = kv.second;
    }
    
    // 如果所有值都相同，则全部放入一个分组
    if (std::abs(maxValue - minValue) < 1e-10) {
        std::vector<std::string> allStocks;
        for (const auto& kv : factorValues) {
            allStocks.push_back(kv.first);
        }
        
        FactorGroup group = createGroup(1, "Group 1", minValue, maxValue, allStocks);
        return {group};
    }
    
    // 计算每个分组的区间大小
    double range = maxValue - minValue;
    double interval = range / numGroups;
    
    std::vector<FactorGroup> groups;
    
    // 创建分组
    for (int i = 0; i < numGroups; ++i) {
        double groupMin = minValue + i * interval;
        double groupMax = (i == numGroups - 1) ? maxValue : minValue + (i + 1) * interval;
        
        std::vector<std::string> groupStocks;
        for (const auto& kv : factorValues) {
            double value = kv.second;
            // 处理边界情况：最后一个分组包含最大值
            if ((i == numGroups - 1 && value >= groupMin && value <= groupMax) ||
                (value >= groupMin && value < groupMax)) {
                groupStocks.push_back(kv.first);
            }
        }
        
        if (!groupStocks.empty()) {
            std::stringstream groupName;
            groupName << "Group " << (i + 1) << " [" 
                     << std::fixed << std::setprecision(4) << groupMin 
                     << ", " << groupMax << "]";
            
            FactorGroup group = createGroup(i + 1, groupName.str(), groupMin, groupMax, groupStocks);
            groups.push_back(group);
        }
    }
    
    return groups;
}

std::vector<FactorGroup> FactorGrouper::groupByQuantile(
    const std::map<std::string, double>& factorValues,
    int numGroups) {
    
    if (factorValues.empty() || numGroups <= 0) {
        return {};
    }
    
    // 获取排序后的因子值
    auto sortedValues = sortFactorValues(factorValues);
    
    // 计算分位数
    std::vector<double> quantiles = calculateQuantiles(
        [&sortedValues]() {
            std::vector<double> values;
            for (const auto& kv : sortedValues) {
                values.push_back(kv.second);
            }
            return values;
        }(), numGroups);
    
    std::vector<FactorGroup> groups;
    size_t totalStocks = sortedValues.size();
    size_t stocksPerGroup = totalStocks / numGroups;
    size_t remainder = totalStocks % numGroups;
    
    size_t startIdx = 0;
    for (int i = 0; i < numGroups; ++i) {
        // 计算当前分组的大小（平均分配，余数分配到前几个分组）
        size_t groupSize = stocksPerGroup;
        if (i < remainder) {
            groupSize++;
        }
        
        if (groupSize == 0 || startIdx >= totalStocks) {
            break;
        }
        
        size_t endIdx = startIdx + groupSize;
        if (endIdx > totalStocks) {
            endIdx = totalStocks;
        }
        
        // 获取分组股票
        std::vector<std::string> groupStocks;
        double minValue = (std::numeric_limits<double>::max)();
        double maxValue = std::numeric_limits<double>::lowest();
        
        for (size_t j = startIdx; j < endIdx; ++j) {
            const auto& stock = sortedValues[j];
            groupStocks.push_back(stock.first);
            
            if (stock.second < minValue) minValue = stock.second;
            if (stock.second > maxValue) maxValue = stock.second;
        }
        
        // 创建分组
        std::stringstream groupName;
        groupName << "Quantile " << (i + 1) << "/" << numGroups 
                 << " [" << std::fixed << std::setprecision(4) 
                 << minValue << ", " << maxValue << "]";
        
        FactorGroup group = createGroup(i + 1, groupName.str(), minValue, maxValue, groupStocks);
        groups.push_back(group);
        
        startIdx = endIdx;
    }
    
    return groups;
}

std::vector<FactorGroup> FactorGrouper::groupByCustomRules(
    const std::map<std::string, double>& factorValues,
    const std::vector<double>& thresholds) {
    
    if (factorValues.empty() || thresholds.empty()) {
        return {};
    }
    
    // 对阈值进行排序
    std::vector<double> sortedThresholds = thresholds;
    std::sort(sortedThresholds.begin(), sortedThresholds.end());
    
    std::vector<FactorGroup> groups;
    
    // 创建第一个分组（小于第一个阈值）
    double prevThreshold = std::numeric_limits<double>::lowest();
    
    for (size_t i = 0; i <= sortedThresholds.size(); ++i) {
        double currentThreshold = (i < sortedThresholds.size()) ? sortedThresholds[i] 
                                                                : (std::numeric_limits<double>::max)();
        
        std::vector<std::string> groupStocks;
        double minValue = (std::numeric_limits<double>::max)();
        double maxValue = std::numeric_limits<double>::lowest();
        
        for (const auto& kv : factorValues) {
            double value = kv.second;
            if (value >= prevThreshold && 
                (i == sortedThresholds.size() || value < currentThreshold)) {
                groupStocks.push_back(kv.first);
                
                if (value < minValue) minValue = value;
                if (value > maxValue) maxValue = value;
            }
        }
        
        if (!groupStocks.empty()) {
            std::stringstream groupName;
            groupName << "Custom Group " << (i + 1);
            if (prevThreshold == std::numeric_limits<double>::lowest()) {
                groupName << " (< " << std::fixed << std::setprecision(4) << currentThreshold << ")";
            } else if (currentThreshold == (std::numeric_limits<double>::max)()) {
                groupName << " (>= " << std::fixed << std::setprecision(4) << prevThreshold << ")";
            } else {
                groupName << " [" << std::fixed << std::setprecision(4) 
                         << prevThreshold << ", " << currentThreshold << ")";
            }
            
            FactorGroup group = createGroup(i + 1, groupName.str(), minValue, maxValue, groupStocks);
            groups.push_back(group);
        }
        
        prevThreshold = currentThreshold;
    }
    
    return groups;
}

std::vector<FactorGroup> FactorGrouper::group(
    const std::map<std::string, double>& factorValues,
    GroupingMethod method,
    int numGroups,
    const std::vector<double>& customThresholds) {
    
    switch (method) {
        case GroupingMethod::QUANTILE:
            return groupByQuantile(factorValues, numGroups);
        case GroupingMethod::EQUAL_VALUE:
            return groupByFactorValue(factorValues, numGroups);
        case GroupingMethod::CUSTOM:
            return groupByCustomRules(factorValues, customThresholds);
        default:
            throw std::invalid_argument("Unknown grouping method");
    }
}

std::vector<double> FactorGrouper::calculateQuantiles(
    const std::vector<double>& values, int numGroups) {
    
    if (values.empty() || numGroups <= 0) {
        return {};
    }
    
    std::vector<double> sortedValues = values;
    std::sort(sortedValues.begin(), sortedValues.end());
    
    std::vector<double> quantiles;
    for (int i = 1; i < numGroups; ++i) {
        double position = (sortedValues.size() * i) / static_cast<double>(numGroups);
        int idx = static_cast<int>(position);
        
        if (idx >= sortedValues.size() - 1) {
            quantiles.push_back(sortedValues.back());
        } else {
            double fraction = position - idx;
            double quantile = sortedValues[idx] * (1 - fraction) + sortedValues[idx + 1] * fraction;
            quantiles.push_back(quantile);
        }
    }
    
    return quantiles;
}

std::map<std::string, double> FactorGrouper::normalizeFactorValues(
    const std::map<std::string, double>& factorValues) {
    
    if (factorValues.empty()) {
        return {};
    }
    
    // 计算均值和标准差
    double sum = 0.0;
    double sumSq = 0.0;
    
    for (const auto& kv : factorValues) {
        sum += kv.second;
        sumSq += kv.second * kv.second;
    }
    
    double mean = sum / factorValues.size();
    double variance = (sumSq / factorValues.size()) - (mean * mean);
    double stdDev = std::sqrt((std::max)(variance, 0.0));
    
    // 标准化因子值
    std::map<std::string, double> normalizedValues;
    for (const auto& kv : factorValues) {
        if (stdDev > 1e-10) {
            normalizedValues[kv.first] = (kv.second - mean) / stdDev;
        } else {
            normalizedValues[kv.first] = 0.0;
        }
    }
    
    return normalizedValues;
}

std::vector<std::pair<std::string, double>> FactorGrouper::sortFactorValues(
    const std::map<std::string, double>& factorValues) {
    
    std::vector<std::pair<std::string, double>> sortedPairs;
    sortedPairs.reserve(factorValues.size());
    
    for (const auto& kv : factorValues) {
        sortedPairs.emplace_back(kv.first, kv.second);
    }
    
    // 按因子值排序
    std::sort(sortedPairs.begin(), sortedPairs.end(),
              [](const auto& a, const auto& b) {
                  return a.second < b.second;
              });
    
    return sortedPairs;
}

FactorGroup FactorGrouper::createGroup(int groupId, 
                                      const std::string& groupName,
                                      double minValue, 
                                      double maxValue,
                                      const std::vector<std::string>& stockCodes) {
    
    FactorGroup group;
    group.groupId = groupId;
    group.groupName = groupName;
    group.minFactorValue = minValue;
    group.maxFactorValue = maxValue;
    group.stockCodes = stockCodes;
    group.stockCount = stockCodes.size();
    
    return group;
}

} // namespace domain::backtest