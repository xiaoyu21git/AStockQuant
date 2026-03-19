#include "FactorBacktestTypes.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace domain::backtest {

bool FactorBacktestConfig::validate() const {
    // 检查基本参数
    if (factorId.empty()) {
        return false;
    }
    
    if (startDate.empty() || endDate.empty()) {
        return false;
    }
    
    if (startDate > endDate) {
        return false;
    }
    
    if (numGroups <= 0) {
        return false;
    }
    
    if (initialCapital <= 0) {
        return false;
    }
    
    if (transactionCost < 0 || transactionCost > 1.0) {
        return false;
    }
    
    if (slippage < 0 || slippage > 1.0) {
        return false;
    }
    
    if (maxThreads <= 0) {
        return false;
    }
    
    if (cacheTTL < 0) {
        return false;
    }
    
    return true;
}

std::string FactorBacktestConfig::getValidationErrors() const {
    std::stringstream errors;
    
    if (factorId.empty()) {
        errors << "Factor ID cannot be empty. ";
    }
    
    if (startDate.empty()) {
        errors << "Start date cannot be empty. ";
    }
    
    if (endDate.empty()) {
        errors << "End date cannot be empty. ";
    }
    
    if (startDate > endDate) {
        errors << "Start date cannot be after end date. ";
    }
    
    if (numGroups <= 0) {
        errors << "Number of groups must be positive. ";
    }
    
    if (initialCapital <= 0) {
        errors << "Initial capital must be positive. ";
    }
    
    if (transactionCost < 0 || transactionCost > 1.0) {
        errors << "Transaction cost must be between 0 and 1. ";
    }
    
    if (slippage < 0 || slippage > 1.0) {
        errors << "Slippage must be between 0 and 1. ";
    }
    
    if (maxThreads <= 0) {
        errors << "Max threads must be positive. ";
    }
    
    if (cacheTTL < 0) {
        errors << "Cache TTL cannot be negative. ";
    }
    
    return errors.str();
}

std::string FactorBacktestResult::toJson() const {
    std::stringstream json;
    json << std::fixed << std::setprecision(6);
    
    json << "{\n";
    
    // 元数据
    json << "  \"taskId\": \"" << taskId << "\",\n";
    json << "  \"executionTime\": " << executionTime << ",\n";
    
    // 配置信息
    json << "  \"config\": {\n";
    json << "    \"factorId\": \"" << config.factorId << "\",\n";
    json << "    \"factorName\": \"" << config.factorName << "\",\n";
    json << "    \"startDate\": \"" << config.startDate << "\",\n";
    json << "    \"endDate\": \"" << config.endDate << "\",\n";
    json << "    \"numGroups\": " << config.numGroups << ",\n";
    json << "    \"initialCapital\": " << config.initialCapital << "\n";
    json << "  },\n";
    
    // 分组信息
    json << "  \"groups\": [\n";
    for (size_t i = 0; i < groups.size(); ++i) {
        const auto& group = groups[i];
        json << "    {\n";
        json << "      \"groupId\": \"" << group.groupId << "\",\n";
        json << "      \"groupName\": \"" << group.groupName << "\",\n";
        json << "      \"minFactorValue\": " << group.minFactorValue << ",\n";
        json << "      \"maxFactorValue\": " << group.maxFactorValue << ",\n";
        json << "      \"stockCount\": " << group.stockCount << "\n";
        json << "    }";
        if (i < groups.size() - 1) {
            json << ",";
        }
        json << "\n";
    }
    json << "  ],\n";
    
    // 绩效指标
    json << "  \"icirResult\": {\n";
    json << "    \"icValue\": " << icirResult.icValue << ",\n";
    json << "    \"irValue\": " << icirResult.irValue << ",\n";
    json << "    \"icTStat\": " << icirResult.icTStat << ",\n";
    json << "    \"icPValue\": " << icirResult.icPValue << ",\n";
    json << "    \"icPositiveRate\": " << icirResult.icPositiveRate << ",\n";
    json << "    \"isSignificant\": " << (icirResult.isSignificant ? "true" : "false") << "\n";
    json << "  },\n";
    
    // 汇总统计
    json << "  \"summary\": {\n";
    json << "    \"topGroupReturn\": " << summary.topGroupReturn << ",\n";
    json << "    \"bottomGroupReturn\": " << summary.bottomGroupReturn << ",\n";
    json << "    \"spreadReturn\": " << summary.spreadReturn << ",\n";
    json << "    \"monotonicity\": " << summary.monotonicity << ",\n";
    json << "    \"discrimination\": " << summary.discrimination << ",\n";
    json << "    \"winRate\": " << summary.winRate << ",\n";
    json << "    \"sharpeRatio\": " << summary.sharpeRatio << ",\n";
    json << "    \"maxDrawdown\": " << summary.maxDrawdown << "\n";
    json << "  }\n";
    
    json << "}";
    
    return json.str();
}

bool FactorBacktestResult::saveToFile(const std::string& filepath) const {
    try {
        std::string jsonStr = toJson();
        
        std::ofstream file(filepath);
        if (!file.is_open()) {
            return false;
        }
        
        file << jsonStr;
        file.close();
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

FactorBacktestResult FactorBacktestResult::loadFromFile(const std::string& filepath) {
    FactorBacktestResult result;
    
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return result;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string jsonStr = buffer.str();
        
        // 这里应该实现JSON解析逻辑
        // 由于时间关系，这里只返回空结果
        
    } catch (const std::exception&) {
        // 忽略异常
    }
    
    return result;
}

void FactorBacktestResult::calculateSummaryStats() {
    // 确保有分组信息
    if (groups.empty()) {
        return;
    }
    
    // 计算分组收益 - 如果有回测结果就使用，否则用0
    std::vector<double> groupReturns;
    if (!groupBacktestResults.empty()) {
        for (const auto& result : groupBacktestResults) {
            double totalReturn = 0.0;
            if (result.performance().total_return != 0.0) {
                totalReturn = result.performance().total_return;
            }
            groupReturns.push_back(totalReturn);
        }
    } else {
        // 没有回测结果时，使用默认值0
        groupReturns.resize(groups.size(), 0.0);
    }
    
    if (groupReturns.empty()) {
        return;
    }
    
    // 找到最高和最低分组收益
    auto maxIt = std::max_element(groupReturns.begin(), groupReturns.end());
    auto minIt = std::min_element(groupReturns.begin(), groupReturns.end());
    
    summary.topGroupReturn = *maxIt;
    summary.bottomGroupReturn = *minIt;
    summary.spreadReturn = summary.topGroupReturn - summary.bottomGroupReturn;
    
    // 计算单调性（使用分组因子值和收益的相关性）
    std::vector<double> groupFactorValues;
    for (const auto& group : groups) {
        groupFactorValues.push_back(group.getAverageFactorValue());
    }
    
    if (groupFactorValues.size() == groupReturns.size()) {
        // 计算皮尔逊相关系数
        double sumX = 0.0, sumY = 0.0, sumXY = 0.0;
        double sumX2 = 0.0, sumY2 = 0.0;
        size_t n = groupFactorValues.size();
        
        for (size_t i = 0; i < n; ++i) {
            sumX += groupFactorValues[i];
            sumY += groupReturns[i];
            sumXY += groupFactorValues[i] * groupReturns[i];
            sumX2 += groupFactorValues[i] * groupFactorValues[i];
            sumY2 += groupReturns[i] * groupReturns[i];
        }
        
        double numerator = n * sumXY - sumX * sumY;
        double denominator = std::sqrt((n * sumX2 - sumX * sumX) * (n * sumY2 - sumY * sumY));
        
        if (std::abs(denominator) > 1e-10) {
            summary.monotonicity = numerator / denominator;
        }
    }
    
    // 计算区分度（收益标准差）
    double meanReturn = 0.0;
    for (double ret : groupReturns) {
        meanReturn += ret;
    }
    meanReturn /= groupReturns.size();
    
    double variance = 0.0;
    for (double ret : groupReturns) {
        double diff = ret - meanReturn;
        variance += diff * diff;
    }
    variance /= groupReturns.size();
    
    summary.discrimination = std::sqrt(variance);
    
    // 计算胜率（正收益比例）
    int winCount = 0;
    for (double ret : groupReturns) {
        if (ret > 0) {
            winCount++;
        }
    }
    summary.winRate = static_cast<double>(winCount) / groupReturns.size();
    
    // 计算夏普比率（简化版）
    double returnStd = std::sqrt(variance);
    if (returnStd > 1e-10) {
        summary.sharpeRatio = meanReturn / returnStd;
    }
    
    // 计算最大回撤（简化版）
    double maxDrawdown = 0.0;
    double peak = groupReturns[0];
    
    for (double ret : groupReturns) {
        if (ret > peak) {
            peak = ret;
        }
        double drawdown = (peak - ret) / peak;
        if (drawdown > maxDrawdown) {
            maxDrawdown = drawdown;
        }
    }
    
    summary.maxDrawdown = maxDrawdown;
}

} // namespace domain::backtest