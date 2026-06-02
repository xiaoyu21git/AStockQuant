#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <algorithm>
#include <cmath>
#include "FactorAnalyticsTypes.h"

namespace domain::backtest {

class FactorGrouper {
public:
    FactorGrouper() = default;
    
    // 鎸夊洜瀛愬€煎垎缁?
    std::vector<FactorGroup> groupByFactorValue(
        const std::map<std::string, double>& factorValues,
        int numGroups);
    
    // 鎸夊垎浣嶆暟鍒嗙粍
    std::vector<FactorGroup> groupByQuantile(
        const std::map<std::string, double>& factorValues,
        int numGroups);
    
    // 鑷畾涔夊垎缁勮鍒?
    std::vector<FactorGroup> groupByCustomRules(
        const std::map<std::string, double>& factorValues,
        const std::vector<double>& thresholds);
    
    // 鏍规嵁閰嶇疆杩涜鍒嗙粍
    std::vector<FactorGroup> group(
        const std::map<std::string, double>& factorValues,
        GroupingMethod method,
        int numGroups = 10,
        const std::vector<double>& customThresholds = {});
    
private:
    // 鍐呴儴杈呭姪鏂规硶
    std::vector<double> calculateQuantiles(
        const std::vector<double>& values, int numGroups);
    
    std::map<std::string, double> normalizeFactorValues(
        const std::map<std::string, double>& factorValues);
    
    // 鎺掑簭鍥犲瓙鍊?
    std::vector<std::pair<std::string, double>> sortFactorValues(
        const std::map<std::string, double>& factorValues);
    
    // 鍒涘缓鍒嗙粍
    FactorGroup createGroup(int groupId, 
                           const std::string& groupName,
                           double minValue, 
                           double maxValue,
                           const std::vector<std::string>& stockCodes);
};

} // namespace domain::backtest
