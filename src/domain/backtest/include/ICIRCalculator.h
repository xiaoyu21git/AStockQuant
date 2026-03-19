#pragma once

#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <numeric>
#include "FactorBacktestTypes.h"
// BacktestResult already included in FactorBacktestTypes.h

namespace domain::backtest {

class ICIRCalculator {
public:
    ICIRCalculator();
    
    // Calculate IC/IR
    ICIRResult calculateICIR(
        const std::vector<FactorGroup>& groups,
        const std::vector<engine::BacktestResult>& backtestResults);
    
    // Calculate Rank IC
    double calculateRankIC(
        const std::map<std::string, double>& factorValues,
        const std::map<std::string, double>& returns);
    
    // Calculate group return monotonicity
    double calculateMonotonicity(
        const std::vector<FactorGroup>& groups,
        const std::vector<engine::BacktestResult>& backtestResults);
    
    // Calculate group return discrimination
    double calculateDiscrimination(
        const std::vector<FactorGroup>& groups,
        const std::vector<engine::BacktestResult>& backtestResults);
    
    // Statistical test
    struct StatisticalTestResult {
        double tStat;
        double pValue;
        bool isSignificant;
        
        StatisticalTestResult() : tStat(0.0), pValue(1.0), isSignificant(false) {}
    };
    
    StatisticalTestResult testFactorSignificance(
        const ICIRResult& icirResult);
    
    // Calculate group return statistics
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
    // Internal helper methods
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
    
    // Statistical functions
    double calculateMean(const std::vector<double>& values);
    double calculateStd(const std::vector<double>& values);
    double calculateTStat(double correlation, int sampleSize);
    double calculatePValue(double tStat, int degreesOfFreedom);
    
    // Ranking functions
    std::vector<double> calculateRanks(const std::vector<double>& values);
    
    // Monotonicity test
    double calculateKendallTau(
        const std::vector<double>& factorValues,
        const std::vector<double>& returns);
};

} // namespace domain::backtest