#include "FactorAnalyticsTypes.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace domain::backtest {

bool FactorBacktestConfig::validate() const {
    // 妫€鏌ュ熀鏈弬鏁?
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
    
    // 鍏冩暟鎹?
    json << "  \"taskId\": \"" << taskId << "\",\n";
    json << "  \"executionTime\": " << executionTime << ",\n";
    
    // 閰嶇疆淇℃伅
    json << "  \"config\": {\n";
    json << "    \"factorId\": \"" << config.factorId << "\",\n";
    json << "    \"factorName\": \"" << config.factorName << "\",\n";
    json << "    \"startDate\": \"" << config.startDate << "\",\n";
    json << "    \"endDate\": \"" << config.endDate << "\",\n";
    json << "    \"numGroups\": " << config.numGroups << ",\n";
    json << "    \"initialCapital\": " << config.initialCapital << "\n";
    json << "  },\n";
    
    // 鍒嗙粍淇℃伅
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
    
    // 缁╂晥鎸囨爣
    json << "  \"icirResult\": {\n";
    json << "    \"icValue\": " << icirResult.icValue << ",\n";
    json << "    \"irValue\": " << icirResult.irValue << ",\n";
    json << "    \"icTStat\": " << icirResult.icTStat << ",\n";
    json << "    \"icPValue\": " << icirResult.icPValue << ",\n";
    json << "    \"icPositiveRate\": " << icirResult.icPositiveRate << ",\n";
    json << "    \"isSignificant\": " << (icirResult.isSignificant ? "true" : "false") << "\n";
    json << "  },\n";
    
    // 姹囨€荤粺璁?
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
        
        // 杩欓噷搴旇瀹炵幇JSON瑙ｆ瀽閫昏緫
        // 鐢变簬鏃堕棿鍏崇郴锛岃繖閲屽彧杩斿洖绌虹粨鏋?
        
    } catch (const std::exception&) {
        // 蹇界暐寮傚父
    }
    
    return result;
}

void FactorBacktestResult::calculateSummaryStats() {
    // 纭繚鏈夊垎缁勪俊鎭?
    if (groups.empty()) {
        return;
    }
    
    // 璁＄畻鍒嗙粍鏀剁泭 - 濡傛灉鏈夊洖娴嬬粨鏋滃氨浣跨敤锛屽惁鍒欑敤0
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
        // 娌℃湁鍥炴祴缁撴灉鏃讹紝浣跨敤榛樿鍊?
        groupReturns.resize(groups.size(), 0.0);
    }
    
    if (groupReturns.empty()) {
        return;
    }
    
    // 鎵惧埌鏈€楂樺拰鏈€浣庡垎缁勬敹鐩?
    auto maxIt = std::max_element(groupReturns.begin(), groupReturns.end());
    auto minIt = std::min_element(groupReturns.begin(), groupReturns.end());
    
    summary.topGroupReturn = *maxIt;
    summary.bottomGroupReturn = *minIt;
    summary.spreadReturn = summary.topGroupReturn - summary.bottomGroupReturn;
    
    // 璁＄畻鍗曡皟鎬э紙浣跨敤鍒嗙粍鍥犲瓙鍊煎拰鏀剁泭鐨勭浉鍏虫€э級
    std::vector<double> groupFactorValues;
    for (const auto& group : groups) {
        groupFactorValues.push_back(group.getAverageFactorValue());
    }
    
    if (groupFactorValues.size() == groupReturns.size()) {
        // 璁＄畻鐨皵閫婄浉鍏崇郴鏁?
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
    
    // 璁＄畻鍖哄垎搴︼紙鏀剁泭鏍囧噯宸級
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
    
    // 璁＄畻鑳滅巼锛堟鏀剁泭姣斾緥锛?
    int winCount = 0;
    for (double ret : groupReturns) {
        if (ret > 0) {
            winCount++;
        }
    }
    summary.winRate = static_cast<double>(winCount) / groupReturns.size();
    
    // 璁＄畻澶忔櫘姣旂巼锛堢畝鍖栫増锛?
    double returnStd = std::sqrt(variance);
    if (returnStd > 1e-10) {
        summary.sharpeRatio = meanReturn / returnStd;
    }
    
    // 璁＄畻鏈€澶у洖鎾わ紙绠€鍖栫増锛?
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
