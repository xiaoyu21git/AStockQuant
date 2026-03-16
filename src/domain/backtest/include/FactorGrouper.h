#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <algorithm>
#include <cmath>
#include "FactorBacktestTypes.h"

namespace domain::backtest {

class FactorGrouper {
public:
    FactorGrouper() = default;
    
    // 按因子值分组
    std::vector<FactorGroup> groupByFactorValue(
        const std::map<std::string, double>& factorValues,
        int numGroups);
    
    // 按分位数分组
    std::vector<FactorGroup> groupByQuantile(
        const std::map<std::string, double>& factorValues,
        int numGroups);
    
    // 自定义分组规则
    std::vector<FactorGroup> groupByCustomRules(
        const std::map<std::string, double>& factorValues,
        const std::vector<double>& thresholds);
    
    // 根据配置进行分组
    std::vector<FactorGroup> group(
        const std::map<std::string, double>& factorValues,
        GroupingMethod method,
        int numGroups = 10,
        const std::vector<double>& customThresholds = {});
    
private:
    // 内部辅助方法
    std::vector<double> calculateQuantiles(
        const std::vector<double>& values, int numGroups);
    
    std::map<std::string, double> normalizeFactorValues(
        const std::map<std::string, double>& factorValues);
    
    // 排序因子值
    std::vector<std::pair<std::string, double>> sortFactorValues(
        const std::map<std::string, double>& factorValues);
    
    // 创建分组
    FactorGroup createGroup(int groupId, 
                           const std::string& groupName,
                           double minValue, 
                           double maxValue,
                           const std::vector<std::string>& stockCodes);
};

} // namespace domain::backtest