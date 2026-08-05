#pragma once

#include <functional>
#include <string>
#include <vector>

#include "FactorBacktestTypes.h"

namespace factor {

class FactorBacktestMetricsCalculator final
{
public:
    struct BenchmarkComparisonSummary {
        bool hasValidAlignment = false;
        double benchmarkAnnualReturn = 0.0;
        double excessAnnualReturn = 0.0;
        double trackingError = 0.0;
        double informationRatio = 0.0;
        double beta = 0.0;
        double alpha = 0.0;
    };

    struct Inputs {
        const BacktestConfig& config;
        const ICIRResult& icirResult;
        const GroupBacktestResult& groupResult;
        const std::vector<double>& rawLongShortSeries;
        const std::vector<double>& costAdjustedLongShortSeries;
        const std::vector<double>& adjustedLongShortSeries;
        const std::vector<double>& turnoverSeries;
        const std::vector<std::string>& longShortDates;
        const std::vector<std::vector<double>>& groupReturnSeriesByGroup;
        const std::vector<double>* executionPeriodicReturns = nullptr;
        const std::vector<std::string>* executionDates = nullptr;
        double alpha = 0.0;
        const BenchmarkComparisonSummary* benchmarkSummary = nullptr;
        int riskTriggeredCount = 0;
    };

    static void populateResultMetrics(BacktestResult& result, const Inputs& inputs);

    static FactorBacktestMetrics::Rating evaluateCoreRating(const FactorBacktestMetrics& metrics);

    static BenchmarkComparisonSummary calculateBenchmarkComparison(
        const std::vector<double>& strategyReturns,
        const std::vector<std::string>& strategyDates,
        int forwardDays,
        double riskFreeRate,
        const std::function<double(const std::string&)>& benchmarkLookup);

    static double calculateMaxDrawdown(const std::vector<double>& periodicReturns);
    static double calculateDownsideDeviation(const std::vector<double>& returns, double threshold = 0.0);
    static double calculateValueAtRisk(const std::vector<double>& returns, double confidenceLevel = 0.95);
    static double calculateConditionalVaR(const std::vector<double>& returns, double confidenceLevel = 0.95);
    static double calculateWinRate(const std::vector<double>& periodicReturns);
    static double calculateProfitFactor(const std::vector<double>& periodicReturns);

    /// @brief 年化波动率 = std(dailyReturns) × sqrt(250)
    static double calculateVolatility(const std::vector<double>& dailyReturns);

    /// @brief 年化收益率 = (finalEquity / initialCapital)^(250/totalDays) - 1
    static double calculateAnnualizedReturn(double finalEquity, double initialCapital, int totalDays);

    /// @brief 夏普比率 = 年化收益 / 年化波动率
    static double calculateSharpeRatio(double annualizedReturn, double annualizedVolatility);

    /// @brief 索提诺比率 = 年化收益 / (日下行标准差 × sqrt(250))
    static double calculateSortinoRatio(double annualizedReturn, double dailyDownsideDeviation);

    /// @brief 卡玛比率 = 年化收益 / 最大回撤
    static double calculateCalmarRatio(double annualizedReturn, double maxDrawdown);

    /// @brief 基准对比（输入已对齐的日收益率序列，无需日期对齐）
    static BenchmarkComparisonSummary calculateBenchmarkMetrics(
        const std::vector<double>& strategyDailyReturns,
        const std::vector<double>& benchmarkDailyReturns);

private:
    static FactorBacktestMetrics buildFactorMetrics(const Inputs& inputs);
    static double calculateRankIcMean(const Inputs& inputs);
    static double calculateRankIcStd(const Inputs& inputs);
    static double calculateRankIcir(const Inputs& inputs);
    static double calculateIcWinRate(const Inputs& inputs);
    static double calculateFactorMonotonicityScore(const Inputs& inputs);
    static double calculateResearchLongShortSharpe(const Inputs& inputs);
    static double calculateResearchLongShortAnnualReturn(const Inputs& inputs);
    static double calculateExecutionLongShortMaxDrawdown(const Inputs& inputs);
    static int calculateIcHalfLife(const Inputs& inputs);
    static double calculateAnnualTurnover(const Inputs& inputs);
    static double calculateCostAdjustedSharpe(const Inputs& inputs);
    static double calculateMetricAlpha(const Inputs& inputs);
    static double calculateIcTStat(const Inputs& inputs);
    static double calculateIcPValue(const Inputs& inputs);
    static double calculateMetricMonthlyWinRate(const Inputs& inputs);
    static int calculateNumGroups(const Inputs& inputs);
    static std::vector<double> calculateGroupAnnualReturns(const Inputs& inputs);
    static std::vector<double> calculateGroupSharpes(const Inputs& inputs);

    static double calculateExecutionAnnualReturn(const Inputs& inputs);
    static double calculateExecutionSharpeRatio(const Inputs& inputs);
    static double calculateExecutionMaxDrawdown(const Inputs& inputs);
    static double calculateExecutionWinRate(const Inputs& inputs);
    static double calculateExecutionProfitFactor(const Inputs& inputs);
    static double calculateExecutionTurnoverRate(const Inputs& inputs);
    static double calculateExecutionVolatility(const Inputs& inputs);
    static double calculateExecutionDownsideDeviation(const Inputs& inputs);
    static double calculateExecutionSortinoRatio(const Inputs& inputs);
    static double calculateExecutionCalmarRatio(const Inputs& inputs);
    static double calculateExecutionValueAtRisk(const Inputs& inputs, double confidenceLevel);
    static double calculateExecutionConditionalVaR(const Inputs& inputs, double confidenceLevel);

    static double calculateAveragePeriodicReturn(const std::vector<double>& periodicReturns);
    static double calculatePeriodicReturnStdDev(const std::vector<double>& periodicReturns,
                                                double meanReturn);
    static double calculatePeriodRiskFreeRate(int forwardDays, double riskFreeRate);
    static double incompleteBetaContinuedFraction(double a, double b, double x);
    static double regularizedIncompleteBeta(double a, double b, double x);
    static double calculateCompoundedAnnualReturn(const std::vector<double>& periodicReturns,
                                                  int forwardDays);
    static const std::vector<double>& resolveExecutionPeriodicReturns(const Inputs& inputs);
    static const std::vector<std::string>& resolveExecutionDates(const Inputs& inputs);
    static double calculateIcTStatFromSeries(double icMean, double icStd, size_t sampleCount);
    static double calculateTwoSidedStudentTPValue(double tStat, size_t sampleCount);
    static bool hasPositiveTopBottomSpread(const std::vector<double>& groupReturns);
    static bool hasStrictMonotonicGroupReturns(const std::vector<double>& groupReturns);
    static double calculateGroupMonotonicityScore(const std::vector<double>& groupReturns);
    static double calculateSharpeFromPeriodicReturns(const std::vector<double>& periodicReturns,
                                                     int forwardDays,
                                                     double riskFreeRate);
    static int estimateHalfLifeDays(const std::vector<double>& series);
    static double calculateMonthlyWinRate(const std::vector<double>& periodicReturns,
                                          const std::vector<std::string>& periodicDates);
    static double calculateTopBottomSpreadReturn(const Inputs& inputs);
    static bool calculateSpreadSignConsistency(const Inputs& inputs);
    static double annualizationFactorForPeriods(int forwardDays);
};

} // namespace factor
