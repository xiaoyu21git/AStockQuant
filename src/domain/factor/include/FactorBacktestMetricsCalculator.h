#pragma once

#include <functional>
#include <string>
#include <vector>

namespace factor {

struct BacktestConfig;
struct BacktestResult;
struct ICIRResult;
struct GroupBacktestResult;
struct FactorBacktestMetrics;

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
        double alpha = 0.0;
        const BenchmarkComparisonSummary* benchmarkSummary = nullptr;
        int riskTriggeredCount = 0;
    };

    static void populateResultMetrics(BacktestResult& result, const Inputs& inputs);

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

private:
    static FactorBacktestMetrics buildFactorMetrics(const Inputs& inputs);
    static bool isMonotonicGroupReturnSeries(const std::vector<double>& groupReturns);
    static double calculateSharpeFromPeriodicReturns(const std::vector<double>& periodicReturns,
                                                     int forwardDays,
                                                     double riskFreeRate);
    static int estimateHalfLifeDays(const std::vector<double>& series);
    static double calculateMonthlyWinRate(const std::vector<double>& periodicReturns,
                                          const std::vector<std::string>& periodicDates);
    static double annualizationFactorForPeriods(int forwardDays);
};

} // namespace factor
