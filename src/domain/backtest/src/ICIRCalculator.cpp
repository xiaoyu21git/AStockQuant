#include "ICIRCalculator.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <stdexcept>

namespace domain::backtest {

ICIRCalculator::ICIRCalculator() = default;

ICIRResult ICIRCalculator::calculateICIR(
    const std::vector<FactorGroup>& groups,
    const std::vector<engine::BacktestResult>& backtestResults) {
    
    ICIRResult result;
    
    if (groups.empty() || backtestResults.empty() || groups.size() != backtestResults.size()) {
        return result;
    }
    
    // 计算分组收益和因子值
    std::vector<double> groupReturns = calculateGroupReturns(backtestResults);
    std::vector<double> groupFactorValues = calculateGroupFactorValues(groups);
    
    if (groupReturns.empty() || groupFactorValues.empty()) {
        return result;
    }
    
    // 计算IC（皮尔逊相关系数）
    result.icValue = calculateCorrelation(groupFactorValues, groupReturns);
    
    // 计算Rank IC（斯皮尔曼相关系数）
    result.icSeries.push_back(result.icValue);
    
    // 计算IR（信息比率）
    if (result.icSeries.size() > 1) {
        double meanIC = calculateMean(result.icSeries);
        double stdIC = calculateStd(result.icSeries);
        if (stdIC > 1e-10) {
            result.irValue = meanIC / stdIC;
        }
    } else {
        result.irValue = result.icValue;
    }
    
    // 计算统计显著性
    int sampleSize = static_cast<int>(groupReturns.size());
    result.icTStat = calculateTStat(result.icValue, sampleSize);
    result.icPValue = calculatePValue(result.icTStat, sampleSize - 2);
    result.isSignificant = (result.icPValue < 0.05);
    
    // 计算IC正比例
    int positiveCount = 0;
    for (double ic : result.icSeries) {
        if (ic > 0) positiveCount++;
    }
    result.icPositiveRate = static_cast<double>(positiveCount) / result.icSeries.size();
    
    return result;
}

double ICIRCalculator::calculateRankIC(
    const std::map<std::string, double>& factorValues,
    const std::map<std::string, double>& returns) {
    
    if (factorValues.empty() || returns.empty()) {
        return 0.0;
    }
    
    // 获取共同的股票代码
    std::vector<std::string> commonStocks;
    std::vector<double> factorVec;
    std::vector<double> returnVec;
    
    for (const auto& kv : factorValues) {
        const std::string& stock = kv.first;
        auto it = returns.find(stock);
        if (it != returns.end()) {
            commonStocks.push_back(stock);
            factorVec.push_back(kv.second);
            returnVec.push_back(it->second);
        }
    }
    
    if (commonStocks.size() < 2) {
        return 0.0;
    }
    
    // 计算斯皮尔曼相关系数
    return calculateSpearmanCorrelation(factorVec, returnVec);
}

double ICIRCalculator::calculateMonotonicity(
    const std::vector<FactorGroup>& groups,
    const std::vector<engine::BacktestResult>& backtestResults) {
    
    if (groups.empty() || backtestResults.empty() || groups.size() != backtestResults.size()) {
        return 0.0;
    }
    
    std::vector<double> groupReturns = calculateGroupReturns(backtestResults);
    std::vector<double> groupFactorValues = calculateGroupFactorValues(groups);
    
    if (groupReturns.size() < 2) {
        return 0.0;
    }
    
    // 计算肯德尔τ系数
    return calculateKendallTau(groupFactorValues, groupReturns);
}

double ICIRCalculator::calculateDiscrimination(
    const std::vector<FactorGroup>& groups,
    const std::vector<engine::BacktestResult>& backtestResults) {
    
    if (groups.empty() || backtestResults.empty() || groups.size() != backtestResults.size()) {
        return 0.0;
    }
    
    std::vector<double> groupReturns = calculateGroupReturns(backtestResults);
    
    if (groupReturns.size() < 2) {
        return 0.0;
    }
    
    // 计算最高分组和最低分组的收益差
    double maxReturn = *std::max_element(groupReturns.begin(), groupReturns.end());
    double minReturn = *std::min_element(groupReturns.begin(), groupReturns.end());
    
    return maxReturn - minReturn;
}

ICIRCalculator::StatisticalTestResult ICIRCalculator::testFactorSignificance(
    const ICIRResult& icirResult) {
    
    StatisticalTestResult result;
    
    if (!icirResult.isValid()) {
        return result;
    }
    
    result.tStat = icirResult.icTStat;
    result.pValue = icirResult.icPValue;
    result.isSignificant = icirResult.icPValue < 0.05;
    
    return result;
}

ICIRCalculator::GroupReturnStats ICIRCalculator::calculateGroupReturnStats(
    const std::vector<FactorGroup>& groups,
    const std::vector<engine::BacktestResult>& backtestResults) {
    
    GroupReturnStats stats;
    
    if (groups.empty() || backtestResults.empty() || groups.size() != backtestResults.size()) {
        return stats;
    }
    
    stats.groupReturns = calculateGroupReturns(backtestResults);
    stats.groupFactorValues = calculateGroupFactorValues(groups);
    
    // 计算权重（按股票数量）
    stats.groupWeights.resize(groups.size());
    size_t totalStocks = 0;
    for (const auto& group : groups) {
        totalStocks += group.stockCount;
    }
    
    for (size_t i = 0; i < groups.size(); ++i) {
        stats.groupWeights[i] = static_cast<double>(groups[i].stockCount) / totalStocks;
    }
    
    // 计算统计量
    stats.meanReturn = calculateMean(stats.groupReturns);
    stats.stdReturn = calculateStd(stats.groupReturns);
    stats.meanFactorValue = calculateMean(stats.groupFactorValues);
    stats.stdFactorValue = calculateStd(stats.groupFactorValues);
    
    return stats;
}

std::vector<double> ICIRCalculator::calculateGroupReturns(
    const std::vector<engine::BacktestResult>& backtestResults) {
    
    std::vector<double> returns;
    returns.reserve(backtestResults.size());
    
    for (const auto& result : backtestResults) {
        // 使用总收益率作为分组收益
        double totalReturn = 0.0;
        if (result.performance().total_return != 0.0) {
            totalReturn = result.performance().total_return;
        }
        returns.push_back(totalReturn);
    }
    
    return returns;
}

std::vector<double> ICIRCalculator::calculateGroupFactorValues(
    const std::vector<FactorGroup>& groups) {
    
    std::vector<double> factorValues;
    factorValues.reserve(groups.size());
    
    for (const auto& group : groups) {
        // 使用分组因子值的平均值
        factorValues.push_back(group.getAverageFactorValue());
    }
    
    return factorValues;
}

double ICIRCalculator::calculateCorrelation(
    const std::vector<double>& x,
    const std::vector<double>& y) {
    
    if (x.size() != y.size() || x.size() < 2) {
        return 0.0;
    }
    
    size_t n = x.size();
    double sumX = 0.0, sumY = 0.0, sumXY = 0.0;
    double sumX2 = 0.0, sumY2 = 0.0;
    
    for (size_t i = 0; i < n; ++i) {
        sumX += x[i];
        sumY += y[i];
        sumXY += x[i] * y[i];
        sumX2 += x[i] * x[i];
        sumY2 += y[i] * y[i];
    }
    
    double numerator = n * sumXY - sumX * sumY;
    double denominator = std::sqrt((n * sumX2 - sumX * sumX) * (n * sumY2 - sumY * sumY));
    
    if (std::abs(denominator) < 1e-10) {
        return 0.0;
    }
    
    return numerator / denominator;
}

double ICIRCalculator::calculateSpearmanCorrelation(
    const std::vector<double>& x,
    const std::vector<double>& y) {
    
    if (x.size() != y.size() || x.size() < 2) {
        return 0.0;
    }
    
    // 计算排名
    std::vector<double> rankX = calculateRanks(x);
    std::vector<double> rankY = calculateRanks(y);
    
    // 计算排名相关系数
    return calculateCorrelation(rankX, rankY);
}

double ICIRCalculator::calculateMean(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return sum / values.size();
}

double ICIRCalculator::calculateStd(const std::vector<double>& values) {
    if (values.size() < 2) {
        return 0.0;
    }
    
    double mean = calculateMean(values);
    double sumSq = 0.0;
    
    for (double value : values) {
        double diff = value - mean;
        sumSq += diff * diff;
    }
    
    return std::sqrt(sumSq / (values.size() - 1));
}

double ICIRCalculator::calculateTStat(double correlation, int sampleSize) {
    if (sampleSize < 3 || std::abs(correlation) >= 1.0) {
        return 0.0;
    }
    
    double r2 = correlation * correlation;
    if (r2 >= 1.0) {
        return 0.0;
    }
    
    return correlation * std::sqrt((sampleSize - 2) / (1 - r2));
}

double ICIRCalculator::calculatePValue(double tStat, int degreesOfFreedom) {
    if (degreesOfFreedom <= 0) {
        return 1.0;
    }
    
    // 简化版：使用t分布近似
    // 在实际应用中应该使用更精确的t分布计算
    double absT = std::abs(tStat);
    double p = 2.0 * (1.0 - 0.5 * (1.0 + std::erf(absT / std::sqrt(2.0))));
    
    return (std::min)((std::max)(p, 0.0), 1.0);
}

std::vector<double> ICIRCalculator::calculateRanks(const std::vector<double>& values) {
    std::vector<double> ranks(values.size());
    
    // 创建索引向量
    std::vector<size_t> indices(values.size());
    std::iota(indices.begin(), indices.end(), 0);
    
    // 按值排序索引
    std::sort(indices.begin(), indices.end(),
              [&values](size_t i1, size_t i2) { return values[i1] < values[i2]; });
    
    // 分配排名（处理并列情况）
    size_t currentRank = 1;
    while (currentRank <= indices.size()) {
        // 查找相同值的范围
        size_t start = currentRank - 1;
        size_t end = start;
        
        while (end + 1 < indices.size() && 
               std::abs(values[indices[end + 1]] - values[indices[start]]) < 1e-10) {
            end++;
        }
        
        // 计算平均排名
        double avgRank = (start + end + 2) / 2.0;
        
        // 分配排名
        for (size_t i = start; i <= end; ++i) {
            ranks[indices[i]] = avgRank;
        }
        
        currentRank = end + 2;
    }
    
    return ranks;
}

double ICIRCalculator::calculateKendallTau(
    const std::vector<double>& factorValues,
    const std::vector<double>& returns) {
    
    if (factorValues.size() != returns.size() || factorValues.size() < 2) {
        return 0.0;
    }
    
    size_t n = factorValues.size();
    int concordant = 0;
    int discordant = 0;
    
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            double factorDiff = factorValues[j] - factorValues[i];
            double returnDiff = returns[j] - returns[i];
            
            if (factorDiff * returnDiff > 0) {
                concordant++;
            } else if (factorDiff * returnDiff < 0) {
                discordant++;
            }
            // 如果相等，忽略
        }
    }
    
    int totalPairs = concordant + discordant;
    if (totalPairs == 0) {
        return 0.0;
    }
    
    return static_cast<double>(concordant - discordant) / totalPairs;
}

} // namespace domain::backtest