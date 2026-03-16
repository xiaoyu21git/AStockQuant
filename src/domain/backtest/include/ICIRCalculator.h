#pragma once

#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <numeric>
#include "FactorBacktestTypes.h"
#include "BacktestResult.h"

namespace domain::backtest {

class ICIRCalculator {
public:
    ICIRCalculator();
    
    // 计算IC/IR
    ICIRResult calculateICIR(
        const std::vector<FactorGroup>& groups,
        const std::vector<engine::BacktestResult>& backtestResults);
    
    // 计算Rank IC
    double calculateRankIC(
        const std::map<std::string, double>& factorValues,
        const std::map<std::string, double>& returns);
    
    // 计算分组收益单调性
    double calculateMonotonicity(
        const std::vector<FactorGroup>& groups,
        const std::vector<engine::BacktestResult>& backtestResults);
    
    // 计算分组收益区分度
    double calculateDiscrimination(
        const std::vector<FactorGroup>& groups,
        const std::vector<engine::BacktestResult>& backtestResults);
    
    // 统计检验
    struct StatisticalTestResult {
        double tStat;
        double pValue;
        bool isSignificant;
        
        StatisticalTestResult() : tStat(0.0), pValue(1.0), isSignificant(false) {}
    };
    
    StatisticalTestResult testFactorSignificance(
        const ICIRResult& icirResult);
    
    // 计算分组收益统计
    struct GroupReturnStats {
        std::vector<double> groupReturns;
        std::vector<double> groupFactorValues;
        std::vector<double> groupWeights;
        
        double meanReturn;
        double stdReturn;
        double meanFactorValue;
        double stdFactorValue;
    };
    
    GroupReturnStats calculateGroupReturnStats(
        const std::vector<FactorGroup>& groups,
        const std::vector<engine::BacktestResult>& backtestResults);
    
private:
    // 内部辅助方法
    std::vector<double> calculateGroupReturns(
        const std::vector<engine::BacktestResult>& backtestResults);
    
    std::vector<double> calculateGroupFactorValues(
        const std::vector<FactorGroup>& groups);
    
    double calculateCorrelation(
        const std::vector<double>& x,
        const std::vector<double>& y);
    
    double calculateSpearmanCorrelation(
        const std::vector<double>& x,
        const std::vector<double>& y);
    
    // 统计函数
    double calculateMean(const std::vector<double>& values);
    double calculateStd(const std::vector<double>& values);
    double calculateTStat(double correlation, int sampleSize);
    double calculatePValue(double tStat, int degreesOfFreedom);
    
    // 排名函数
    std::vector<double> calculateRanks(const std::vector<double>& values);
    
    // 单调性检验
    double calculateKendallTau(
        const std::vector<double>& factorValues,
        const std::vector<double>& returns);
};

} // namespace domain::backtest