#include "domain/factor/include/FactorMetricsCalculator.h"

#include "domain/factor/include/FactorIcUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace factor {

void FactorBacktestMetrics::computeCoreRating()
{
    coreRating = FactorBacktestMetricsCalculator::evaluateCoreRating(*this);
}

FactorBacktestMetrics::Rating FactorBacktestMetricsCalculator::evaluateCoreRating(const FactorBacktestMetrics& metrics)
{
    const bool hasGroupSpread = hasPositiveTopBottomSpread(metrics.groupAnnualReturns);
    const bool strictMonotonic = hasStrictMonotonicGroupReturns(metrics.groupAnnualReturns);
    const double monotonicityAbsScore = std::abs(metrics.monotonicityScore);

    const bool excellent = metrics.rankIcMean > 0.05
        && metrics.icPValue < 0.001
        && metrics.rankIcir > 0.8
        && metrics.icWinRate > 0.75
        && monotonicityAbsScore > 0.95
        && hasGroupSpread
        && strictMonotonic;
    if (excellent) {
        return FactorBacktestMetrics::Rating::EXCELLENT;
    }

    const bool good = metrics.rankIcMean > 0.03
        && metrics.icPValue < 0.01
        && metrics.rankIcir > 0.5
        && metrics.icWinRate > 0.65
        && monotonicityAbsScore > 0.85
        && hasGroupSpread
        && strictMonotonic;
    if (good) {
        return FactorBacktestMetrics::Rating::GOOD;
    }

    // PASS = 与 ratingChecks 绿色判定一致: IC>0.05, IR>0.3, winRate>0.55
    const bool pass = metrics.rankIcMean > 0.05
        && metrics.rankIcir > 0.3
        && metrics.icWinRate > 0.55
        && metrics.icPValue < 0.05
        && hasGroupSpread;
    if (pass) {
        return FactorBacktestMetrics::Rating::PASS;
    }

    return FactorBacktestMetrics::Rating::FAIL;
}

double FactorBacktestMetricsCalculator::annualizationFactorForPeriods(int forwardDays)
{
    return 252.0 / static_cast<double>((std::max)(1, forwardDays));
}

bool FactorBacktestMetricsCalculator::hasStrictMonotonicGroupReturns(const std::vector<double>& groupReturns)
{
    if (groupReturns.size() < 3) {
        return false;
    }

    for (size_t index = 1; index < groupReturns.size(); ++index) {
        if (groupReturns[index] > groupReturns[index - 1]) {
            return false;
        }
    }
    return true;
}

double FactorBacktestMetricsCalculator::calculateRankIcMean(const Inputs& inputs)
{
    return inputs.icirResult.icMean;
}

double FactorBacktestMetricsCalculator::calculateRankIcStd(const Inputs& inputs)
{
    return inputs.icirResult.icStd;
}

double FactorBacktestMetricsCalculator::calculateRankIcir(const Inputs& inputs)
{
    return inputs.icirResult.ir;
}

double FactorBacktestMetricsCalculator::calculateIcWinRate(const Inputs& inputs)
{
    return inputs.icirResult.icPositiveRatio;
}

double FactorBacktestMetricsCalculator::calculateFactorMonotonicityScore(const Inputs& inputs)
{
    return calculateGroupMonotonicityScore(inputs.groupResult.groupReturns);
}

double FactorBacktestMetricsCalculator::calculateResearchLongShortSharpe(const Inputs& inputs)
{
    return calculateSharpeFromPeriodicReturns(inputs.rawLongShortSeries,
                                             inputs.config.forwardDays,
                                             inputs.config.riskFreeRate);
}

double FactorBacktestMetricsCalculator::calculateResearchLongShortAnnualReturn(const Inputs& inputs)
{
    return inputs.groupResult.longShortReturn * annualizationFactorForPeriods(inputs.config.forwardDays);
}

double FactorBacktestMetricsCalculator::calculateExecutionLongShortMaxDrawdown(const Inputs& inputs)
{
    return calculateMaxDrawdown(inputs.adjustedLongShortSeries);
}

int FactorBacktestMetricsCalculator::calculateIcHalfLife(const Inputs& inputs)
{
    return estimateHalfLifeDays(inputs.icirResult.icSeries);
}

double FactorBacktestMetricsCalculator::calculateAnnualTurnover(const Inputs& inputs)
{
    return calculateAveragePeriodicReturn(inputs.turnoverSeries) * annualizationFactorForPeriods(inputs.config.forwardDays);
}

double FactorBacktestMetricsCalculator::calculateCostAdjustedSharpe(const Inputs& inputs)
{
    return calculateSharpeFromPeriodicReturns(inputs.costAdjustedLongShortSeries,
                                             inputs.config.forwardDays,
                                             inputs.config.riskFreeRate);
}

double FactorBacktestMetricsCalculator::calculateMetricAlpha(const Inputs& inputs)
{
    return inputs.benchmarkSummary && inputs.benchmarkSummary->hasValidAlignment
        ? inputs.benchmarkSummary->alpha
        : inputs.alpha;
}

double FactorBacktestMetricsCalculator::calculateIcTStat(const Inputs& inputs)
{
    return calculateIcTStatFromSeries(calculateRankIcMean(inputs),
                                      calculateRankIcStd(inputs),
                                      inputs.icirResult.icSeries.size());
}

double FactorBacktestMetricsCalculator::calculateIcPValue(const Inputs& inputs)
{
    return calculateTwoSidedStudentTPValue(calculateIcTStat(inputs), inputs.icirResult.icSeries.size());
}

double FactorBacktestMetricsCalculator::calculateMetricMonthlyWinRate(const Inputs& inputs)
{
    return calculateMonthlyWinRate(inputs.costAdjustedLongShortSeries, inputs.longShortDates);
}

int FactorBacktestMetricsCalculator::calculateNumGroups(const Inputs& inputs)
{
    return inputs.groupResult.groupReturns.empty()
        ? (std::max)(1, inputs.config.numGroups)
        : static_cast<int>(inputs.groupResult.groupReturns.size());
}

std::vector<double> FactorBacktestMetricsCalculator::calculateGroupAnnualReturns(const Inputs& inputs)
{
    std::vector<double> annualReturns;
    annualReturns.reserve(inputs.groupReturnSeriesByGroup.size());
    for (const auto& groupSeries : inputs.groupReturnSeriesByGroup) {
        if (groupSeries.empty()) {
            continue;
        }
        annualReturns.push_back(calculateAveragePeriodicReturn(groupSeries)
                                * annualizationFactorForPeriods(inputs.config.forwardDays));
    }
    return annualReturns;
}

std::vector<double> FactorBacktestMetricsCalculator::calculateGroupSharpes(const Inputs& inputs)
{
    std::vector<double> sharpes;
    sharpes.reserve(inputs.groupReturnSeriesByGroup.size());
    for (const auto& groupSeries : inputs.groupReturnSeriesByGroup) {
        if (groupSeries.empty()) {
            continue;
        }
        sharpes.push_back(calculateSharpeFromPeriodicReturns(groupSeries,
                                                             inputs.config.forwardDays,
                                                             inputs.config.riskFreeRate));
    }
    return sharpes;
}

double FactorBacktestMetricsCalculator::calculateGroupMonotonicityScore(const std::vector<double>& groupReturns)
{
    if (groupReturns.size() < 2) {
        return 0.0;
    }

    const size_t count = groupReturns.size();
    const double meanX = (static_cast<double>(count) + 1.0) / 2.0;
    const double meanY = factor::icir::calculateMean(groupReturns);

    double covariance = 0.0;
    double varianceX = 0.0;
    double varianceY = 0.0;
    for (size_t index = 0; index < count; ++index) {
        const double x = static_cast<double>(index + 1);
        const double dx = x - meanX;
        const double dy = groupReturns[index] - meanY;
        covariance += dx * dy;
        varianceX += dx * dx;
        varianceY += dy * dy;
    }

    if (varianceX <= 0.0 || varianceY <= 0.0) {
        return 0.0;
    }

    return covariance / std::sqrt(varianceX * varianceY);
}

double FactorBacktestMetricsCalculator::calculateAveragePeriodicReturn(const std::vector<double>& periodicReturns)
{
    return factor::icir::calculateMean(periodicReturns);
}

double FactorBacktestMetricsCalculator::calculatePeriodicReturnStdDev(const std::vector<double>& periodicReturns,
                                                                      double meanReturn)
{
    return factor::icir::calculateStdDev(periodicReturns, meanReturn);
}

double FactorBacktestMetricsCalculator::calculatePeriodRiskFreeRate(int forwardDays, double riskFreeRate)
{
    return riskFreeRate / annualizationFactorForPeriods(forwardDays);
}

double FactorBacktestMetricsCalculator::calculateSharpeFromPeriodicReturns(const std::vector<double>& periodicReturns,
                                                                           int forwardDays,
                                                                           double riskFreeRate)
{
    if (periodicReturns.empty()) {
        return 0.0;
    }

    const double annualizationFactor = annualizationFactorForPeriods(forwardDays);
    const double meanReturn = calculateAveragePeriodicReturn(periodicReturns);
    const double stdReturn = calculatePeriodicReturnStdDev(periodicReturns, meanReturn);
    if (stdReturn <= 0.0) {
        return 0.0;
    }

    const double periodRiskFreeRate = calculatePeriodRiskFreeRate(forwardDays, riskFreeRate);
    return ((meanReturn - periodRiskFreeRate) / stdReturn) * std::sqrt(annualizationFactor);
}

int FactorBacktestMetricsCalculator::estimateHalfLifeDays(const std::vector<double>& series)
{
    if (series.size() < 2) {
        return 0;
    }

    const double mean = factor::icir::calculateMean(series);
    double numerator = 0.0;
    double denominator = 0.0;
    for (size_t index = 1; index < series.size(); ++index) {
        numerator += (series[index - 1] - mean) * (series[index] - mean);
        denominator += (series[index - 1] - mean) * (series[index - 1] - mean);
    }

    if (denominator <= 0.0) {
        return 0;
    }

    const double autocorrelation = numerator / denominator;
    if (!std::isfinite(autocorrelation) || autocorrelation <= 0.0 || autocorrelation >= 1.0) {
        return 0;
    }

    const double halfLife = std::log(0.5) / std::log(autocorrelation);
    if (!std::isfinite(halfLife) || halfLife <= 0.0) {
        return 0;
    }

    return static_cast<int>(std::round(halfLife));
}

double FactorBacktestMetricsCalculator::calculateMonthlyWinRate(const std::vector<double>& periodicReturns,
                                                                const std::vector<std::string>& periodicDates)
{
    const size_t count = (std::min)(periodicReturns.size(), periodicDates.size());
    if (count == 0) {
        return 0.0;
    }

    std::string activeMonth;
    double activeMonthlyNetValue = 1.0;
    int winMonths = 0;
    int totalMonths = 0;

    auto flushMonth = [&]() {
        if (activeMonth.empty()) {
            return;
        }
        ++totalMonths;
        if (activeMonthlyNetValue > 1.0) {
            ++winMonths;
        }
    };

    for (size_t index = 0; index < count; ++index) {
        if (periodicDates[index].size() < 7) {
            continue;
        }

        const std::string monthKey = periodicDates[index].substr(0, 7);
        if (activeMonth.empty()) {
            activeMonth = monthKey;
            activeMonthlyNetValue = 1.0;
        } else if (monthKey != activeMonth) {
            flushMonth();
            activeMonth = monthKey;
            activeMonthlyNetValue = 1.0;
        }

        activeMonthlyNetValue *= (1.0 + periodicReturns[index]);
    }

    flushMonth();
    if (totalMonths == 0) {
        return 0.0;
    }

    return static_cast<double>(winMonths) / static_cast<double>(totalMonths);
}

double FactorBacktestMetricsCalculator::calculateTopBottomSpreadReturn(const Inputs& inputs)
{
    if (inputs.groupResult.groupReturns.size() < 2) {
        return 0.0;
    }
    return inputs.groupResult.groupReturns.front() - inputs.groupResult.groupReturns.back();
}

bool FactorBacktestMetricsCalculator::calculateSpreadSignConsistency(const Inputs& inputs)
{
    const double spread = calculateTopBottomSpreadReturn(inputs);
    const double icMean = inputs.icirResult.icMean;
    // 同号：spread 为正且 IC 为正(因子值越大收益越高)，或两者都为负(因子值越小收益越高)
    return (spread > 0.0 && icMean > 0.0) || (spread < 0.0 && icMean < 0.0);
}

FactorBacktestMetrics FactorBacktestMetricsCalculator::buildFactorMetrics(const Inputs& inputs)
{
    FactorBacktestMetrics metrics;
    metrics.rankIcMean = calculateRankIcMean(inputs);
    metrics.rankIcStd = calculateRankIcStd(inputs);
    metrics.rankIcir = calculateRankIcir(inputs);
    metrics.icWinRate = calculateIcWinRate(inputs);
    metrics.icPValue = calculateIcPValue(inputs);
    metrics.monotonicityScore = calculateFactorMonotonicityScore(inputs);
    metrics.longShortSharpe = calculateResearchLongShortSharpe(inputs);
    metrics.longShortAnnualReturn = calculateResearchLongShortAnnualReturn(inputs);
    metrics.longShortMaxDrawdown = calculateExecutionLongShortMaxDrawdown(inputs);
    metrics.icHalfLife = calculateIcHalfLife(inputs);
    metrics.annualTurnover = calculateAnnualTurnover(inputs);
    metrics.costAdjustedSharpe = calculateCostAdjustedSharpe(inputs);
    metrics.alpha = calculateMetricAlpha(inputs);
    metrics.icTStat = calculateIcTStat(inputs);
    metrics.monthlyWinRate = calculateMetricMonthlyWinRate(inputs);
    metrics.numGroups = calculateNumGroups(inputs);
    metrics.topBottomSpreadReturn = calculateTopBottomSpreadReturn(inputs);
    metrics.spreadSignMatchIc = calculateSpreadSignConsistency(inputs);
    metrics.groupAnnualReturns = calculateGroupAnnualReturns(inputs);
    metrics.groupSharpes = calculateGroupSharpes(inputs);

    metrics.computeCoreRating();
    return metrics;
}

void FactorBacktestMetricsCalculator::populateResultMetrics(BacktestResult& result, const Inputs& inputs)
{
    result.annualReturn = calculateExecutionAnnualReturn(inputs);
    result.sharpeRatio = calculateExecutionSharpeRatio(inputs);
    result.maxDrawdown = calculateExecutionMaxDrawdown(inputs);
    result.winRate = calculateExecutionWinRate(inputs);
    result.profitFactor = calculateExecutionProfitFactor(inputs);
    result.turnoverRate = calculateExecutionTurnoverRate(inputs);

    if (inputs.benchmarkSummary && inputs.benchmarkSummary->hasValidAlignment) {
        result.benchmarkAnnualReturn = inputs.benchmarkSummary->benchmarkAnnualReturn;
        result.excessAnnualReturn = inputs.benchmarkSummary->excessAnnualReturn;
        result.trackingError = inputs.benchmarkSummary->trackingError;
        result.informationRatio = inputs.benchmarkSummary->informationRatio;
        result.beta = inputs.benchmarkSummary->beta;
        result.alpha = inputs.benchmarkSummary->alpha;
    }

    result.volatility = calculateExecutionVolatility(inputs);
    result.downsideDeviation = calculateExecutionDownsideDeviation(inputs);
    result.sortinoRatio = calculateExecutionSortinoRatio(inputs);
    result.calmarRatio = calculateExecutionCalmarRatio(inputs);
    result.valueAtRisk = calculateExecutionValueAtRisk(inputs, 0.95);
    result.conditionalVaR = calculateExecutionConditionalVaR(inputs, 0.95);
    result.riskTriggeredCount = inputs.riskTriggeredCount;
    result.factorMetrics = buildFactorMetrics(inputs);
}

double FactorBacktestMetricsCalculator::calculateExecutionAnnualReturn(const Inputs& inputs)
{
    return calculateCompoundedAnnualReturn(resolveExecutionPeriodicReturns(inputs), inputs.config.forwardDays);
}

double FactorBacktestMetricsCalculator::calculateExecutionSharpeRatio(const Inputs& inputs)
{
    return calculateSharpeFromPeriodicReturns(resolveExecutionPeriodicReturns(inputs),
                                             inputs.config.forwardDays,
                                             inputs.config.riskFreeRate);
}

double FactorBacktestMetricsCalculator::calculateExecutionMaxDrawdown(const Inputs& inputs)
{
    return calculateMaxDrawdown(resolveExecutionPeriodicReturns(inputs));
}

double FactorBacktestMetricsCalculator::calculateExecutionWinRate(const Inputs& inputs)
{
    return calculateWinRate(resolveExecutionPeriodicReturns(inputs));
}

double FactorBacktestMetricsCalculator::calculateExecutionProfitFactor(const Inputs& inputs)
{
    return calculateProfitFactor(resolveExecutionPeriodicReturns(inputs));
}

double FactorBacktestMetricsCalculator::calculateExecutionTurnoverRate(const Inputs& inputs)
{
    return calculateAnnualTurnover(inputs) ;
}

double FactorBacktestMetricsCalculator::calculateExecutionVolatility(const Inputs& inputs)
{
    const auto& executionReturns = resolveExecutionPeriodicReturns(inputs);
    const double meanReturn = calculateAveragePeriodicReturn(executionReturns);
    const double stdReturn = calculatePeriodicReturnStdDev(executionReturns, meanReturn);
    return stdReturn * std::sqrt(annualizationFactorForPeriods(inputs.config.forwardDays));
}

double FactorBacktestMetricsCalculator::calculateExecutionDownsideDeviation(const Inputs& inputs)
{
    return calculateDownsideDeviation(resolveExecutionPeriodicReturns(inputs))
        * std::sqrt(annualizationFactorForPeriods(inputs.config.forwardDays));
}

double FactorBacktestMetricsCalculator::calculateExecutionSortinoRatio(const Inputs& inputs)
{
    const double downsideDeviation = calculateExecutionDownsideDeviation(inputs);
    if (downsideDeviation <= 0.0) {
        return 0.0;
    }

    const double averageReturn = calculateAveragePeriodicReturn(resolveExecutionPeriodicReturns(inputs));
    const double averageExcessReturn = averageReturn - calculatePeriodRiskFreeRate(inputs.config.forwardDays,
                                                                                   inputs.config.riskFreeRate);
    return (averageExcessReturn / downsideDeviation)
        * std::sqrt(annualizationFactorForPeriods(inputs.config.forwardDays));
}

double FactorBacktestMetricsCalculator::calculateExecutionCalmarRatio(const Inputs& inputs)
{
    const double maxDrawdown = calculateExecutionMaxDrawdown(inputs);
    return maxDrawdown > 0.0 ? calculateExecutionAnnualReturn(inputs) / maxDrawdown : 0.0;
}

double FactorBacktestMetricsCalculator::calculateExecutionValueAtRisk(const Inputs& inputs, double confidenceLevel)
{
    return calculateValueAtRisk(resolveExecutionPeriodicReturns(inputs), confidenceLevel);
}

double FactorBacktestMetricsCalculator::calculateExecutionConditionalVaR(const Inputs& inputs, double confidenceLevel)
{
    return calculateConditionalVaR(resolveExecutionPeriodicReturns(inputs), confidenceLevel);
}

FactorBacktestMetricsCalculator::BenchmarkComparisonSummary FactorBacktestMetricsCalculator::calculateBenchmarkComparison(
    const std::vector<double>& strategyReturns,
    const std::vector<std::string>& strategyDates,
    int forwardDays,
    double riskFreeRate,
    const std::function<double(const std::string&)>& benchmarkLookup)
{
    BenchmarkComparisonSummary summary;
    const double annualizationFactor = annualizationFactorForPeriods(forwardDays);

    size_t alignedSampleCount = 0;
    std::vector<double> alignedStrategyReturns;
    std::vector<double> alignedBenchmarkReturns;
    double alignedStrategySum = 0.0;
    double alignedBenchmarkSum = 0.0;
    double alignedStrategySquareSum = 0.0;
    double alignedBenchmarkSquareSum = 0.0;
    double alignedProductSum = 0.0;
    double alignedExcessSum = 0.0;
    double alignedExcessSquareSum = 0.0;

    const size_t alignedCount = (std::min)(strategyReturns.size(), strategyDates.size());
    alignedStrategyReturns.reserve(alignedCount);
    alignedBenchmarkReturns.reserve(alignedCount);
    for (size_t index = 0; index < alignedCount; ++index) {
        const double benchmarkReturn = benchmarkLookup(strategyDates[index]);
        if (!std::isfinite(benchmarkReturn)) {
            continue;
        }

        const double strategyReturn = strategyReturns[index];
        if (!std::isfinite(strategyReturn)) {
            continue;
        }

        const double excessReturn = strategyReturn - benchmarkReturn;

        ++alignedSampleCount;
        alignedStrategyReturns.push_back(strategyReturn);
        alignedBenchmarkReturns.push_back(benchmarkReturn);
        alignedStrategySum += strategyReturn;
        alignedBenchmarkSum += benchmarkReturn;
        alignedStrategySquareSum += strategyReturn * strategyReturn;
        alignedBenchmarkSquareSum += benchmarkReturn * benchmarkReturn;
        alignedProductSum += strategyReturn * benchmarkReturn;
        alignedExcessSum += excessReturn;
        alignedExcessSquareSum += excessReturn * excessReturn;
    }

    if (alignedSampleCount == 0) {
        return summary;
    }

    const double benchmarkMean = alignedBenchmarkSum / static_cast<double>(alignedSampleCount);
    const double alignedStrategyMean = alignedStrategySum / static_cast<double>(alignedSampleCount);
    summary.benchmarkAnnualReturn = calculateCompoundedAnnualReturn(alignedBenchmarkReturns, forwardDays);
    const double strategyAnnualReturn = calculateCompoundedAnnualReturn(alignedStrategyReturns, forwardDays);
    summary.excessAnnualReturn = strategyAnnualReturn - summary.benchmarkAnnualReturn;

    const double excessMean = alignedExcessSum / static_cast<double>(alignedSampleCount);
    double excessVariance = 0.0;
    if (alignedSampleCount > 1) {
        excessVariance = (alignedExcessSquareSum - static_cast<double>(alignedSampleCount) * excessMean * excessMean)
            / static_cast<double>(alignedSampleCount - 1);
    }
    const double excessStd = excessVariance > 0.0 ? std::sqrt(excessVariance) : 0.0;
    summary.trackingError = excessStd * std::sqrt(annualizationFactor);
    summary.informationRatio = summary.trackingError > 0.0
        ? (summary.excessAnnualReturn / summary.trackingError)
        : 0.0;

    double benchmarkVariance = 0.0;
    if (alignedSampleCount > 1) {
        benchmarkVariance = (alignedBenchmarkSquareSum - static_cast<double>(alignedSampleCount) * benchmarkMean * benchmarkMean)
            / static_cast<double>(alignedSampleCount - 1);
    }
    if (benchmarkVariance > 0.0) {
        const double covariance = (alignedProductSum - static_cast<double>(alignedSampleCount) * alignedStrategyMean * benchmarkMean)
            / static_cast<double>(alignedSampleCount - 1);
        summary.beta = covariance / benchmarkVariance;
        summary.alpha = summary.benchmarkAnnualReturn
            + summary.excessAnnualReturn
            - (riskFreeRate + summary.beta * (summary.benchmarkAnnualReturn - riskFreeRate));
    }

    summary.hasValidAlignment = true;
    return summary;
}

double FactorBacktestMetricsCalculator::incompleteBetaContinuedFraction(double a, double b, double x)
{
    constexpr double kIncompleteBetaTolerance = 3.0e-14;
    constexpr double kIncompleteBetaMinValue = 1.0e-30;
    constexpr int kIncompleteBetaMaxIterations = 200;

    const double qab = a + b;
    const double qap = a + 1.0;
    const double qam = a - 1.0;

    double c = 1.0;
    double d = 1.0 - (qab * x / qap);
    if (std::abs(d) < kIncompleteBetaMinValue) {
        d = kIncompleteBetaMinValue;
    }
    d = 1.0 / d;
    double h = d;

    for (int iteration = 1; iteration <= kIncompleteBetaMaxIterations; ++iteration) {
        const int iteration2 = iteration * 2;

        double aa = static_cast<double>(iteration) * (b - static_cast<double>(iteration)) * x
            / ((qam + static_cast<double>(iteration2)) * (a + static_cast<double>(iteration2)));
        d = 1.0 + (aa * d);
        if (std::abs(d) < kIncompleteBetaMinValue) {
            d = kIncompleteBetaMinValue;
        }
        c = 1.0 + (aa / c);
        if (std::abs(c) < kIncompleteBetaMinValue) {
            c = kIncompleteBetaMinValue;
        }
        d = 1.0 / d;
        h *= d * c;

        aa = -(a + static_cast<double>(iteration)) * (qab + static_cast<double>(iteration)) * x
            / ((a + static_cast<double>(iteration2)) * (qap + static_cast<double>(iteration2)));
        d = 1.0 + (aa * d);
        if (std::abs(d) < kIncompleteBetaMinValue) {
            d = kIncompleteBetaMinValue;
        }
        c = 1.0 + (aa / c);
        if (std::abs(c) < kIncompleteBetaMinValue) {
            c = kIncompleteBetaMinValue;
        }
        d = 1.0 / d;
        const double delta = d * c;
        h *= delta;
        if (std::abs(delta - 1.0) <= kIncompleteBetaTolerance) {
            break;
        }
    }

    return h;
}

double FactorBacktestMetricsCalculator::regularizedIncompleteBeta(double a, double b, double x)
{
    if (x <= 0.0) {
        return 0.0;
    }
    if (x >= 1.0) {
        return 1.0;
    }

    const double logFront = std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b)
        + (a * std::log(x)) + (b * std::log(1.0 - x));
    const double front = std::exp(logFront);

    if (x < (a + 1.0) / (a + b + 2.0)) {
        return front * incompleteBetaContinuedFraction(a, b, x) / a;
    }
    return 1.0 - (front * incompleteBetaContinuedFraction(b, a, 1.0 - x) / b);
}

double FactorBacktestMetricsCalculator::calculateCompoundedAnnualReturn(const std::vector<double>& periodicReturns,
                                                                        int forwardDays)
{
    double cumulativeNetValue = 1.0;
    size_t validPeriodCount = 0;
    for (double periodicReturn : periodicReturns) {
        if (!std::isfinite(periodicReturn)) {
            continue;
        }

        cumulativeNetValue *= (1.0 + periodicReturn);
        ++validPeriodCount;
        if (!std::isfinite(cumulativeNetValue) || cumulativeNetValue <= 0.0) {
            return -1.0;
        }
    }

    if (validPeriodCount == 0) {
        return 0.0;
    }

    const double annualizationFactor = annualizationFactorForPeriods(forwardDays);
    const double compoundedAnnualReturn = std::pow(cumulativeNetValue,
                                                   annualizationFactor / static_cast<double>(validPeriodCount))
        - 1.0;
    return std::isfinite(compoundedAnnualReturn) ? compoundedAnnualReturn : -1.0;
}

const std::vector<double>& FactorBacktestMetricsCalculator::resolveExecutionPeriodicReturns(const Inputs& inputs)
{
    if (inputs.executionPeriodicReturns != nullptr) {
        return *inputs.executionPeriodicReturns;
    }
    return inputs.adjustedLongShortSeries;
}

const std::vector<std::string>& FactorBacktestMetricsCalculator::resolveExecutionDates(const Inputs& inputs)
{
    if (inputs.executionDates != nullptr) {
        return *inputs.executionDates;
    }
    return inputs.longShortDates;
}

double FactorBacktestMetricsCalculator::calculateIcTStatFromSeries(double icMean,
                                                                    double icStd,
                                                                    size_t sampleCount)
{
    if (sampleCount < 2 || !std::isfinite(icMean) || !std::isfinite(icStd) || icStd <= 0.0) {
        return 0.0;
    }

    return icMean * std::sqrt(static_cast<double>(sampleCount)) / icStd;
}

double FactorBacktestMetricsCalculator::calculateTwoSidedStudentTPValue(double tStat, size_t sampleCount)
{
    if (sampleCount < 2 || !std::isfinite(tStat)) {
        return 1.0;
    }

    const double degreesOfFreedom = static_cast<double>(sampleCount - 1);
    const double squaredT = tStat * tStat;
    const double x = degreesOfFreedom / (degreesOfFreedom + squaredT);
    const double pValue = regularizedIncompleteBeta(degreesOfFreedom / 2.0, 0.5, x);
    return (std::min)(1.0, (std::max)(0.0, pValue));
}

bool FactorBacktestMetricsCalculator::hasPositiveTopBottomSpread(const std::vector<double>& groupReturns)
{
    return groupReturns.size() >= 2 && groupReturns.front() > groupReturns.back();
}

double FactorBacktestMetricsCalculator::calculateMaxDrawdown(const std::vector<double>& periodicReturns)
{
    if (periodicReturns.empty()) {
        return 0.0;
    }

    double cumulativeNetValue = 1.0;
    double peakNetValue = 1.0;
    double maxDrawdown = 0.0;
    for (double periodicReturn : periodicReturns) {
        cumulativeNetValue *= (1.0 + periodicReturn);
        if (!std::isfinite(cumulativeNetValue) || cumulativeNetValue <= 0.0) {
            return 1.0;
        }

        peakNetValue = (std::max)(peakNetValue, cumulativeNetValue);
        if (peakNetValue <= 0.0) {
            continue;
        }

        const double drawdown = (peakNetValue - cumulativeNetValue) / peakNetValue;
        maxDrawdown = (std::max)(maxDrawdown, drawdown);
    }

    return (std::min)(1.0, maxDrawdown);
}

double FactorBacktestMetricsCalculator::calculateDownsideDeviation(const std::vector<double>& returns, double threshold)
{
    if (returns.empty()) {
        return 0.0;
    }
    double sumSqNeg = 0.0;
    for (double value : returns) {
        if (value < threshold) {
            const double diff = value - threshold;
            sumSqNeg += diff * diff;
        }
    }
    return std::sqrt(sumSqNeg / static_cast<double>(returns.size()));
}

double FactorBacktestMetricsCalculator::calculateValueAtRisk(const std::vector<double>& returns, double confidenceLevel)
{
    if (returns.empty()) {
        return 0.0;
    }
    std::vector<double> sorted = returns;
    const size_t index = static_cast<size_t>(std::floor((1.0 - confidenceLevel) * static_cast<double>(sorted.size())));
    const size_t clampedIndex = (std::min)(index, sorted.size() - 1);
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(clampedIndex), sorted.end());
    return -sorted[clampedIndex];
}

double FactorBacktestMetricsCalculator::calculateConditionalVaR(const std::vector<double>& returns, double confidenceLevel)
{
    if (returns.empty()) {
        return 0.0;
    }
    std::vector<double> sorted = returns;
    const size_t cutoff = static_cast<size_t>(std::floor((1.0 - confidenceLevel) * static_cast<double>(sorted.size())));
    const size_t n = (std::max)(cutoff, static_cast<size_t>(1));
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(n - 1), sorted.end());
    double sum = 0.0;
    for (size_t index = 0; index < n; ++index) {
        sum += sorted[index];
    }
    return -(sum / static_cast<double>(n));
}

double FactorBacktestMetricsCalculator::calculateWinRate(const std::vector<double>& periodicReturns)
{
    if (periodicReturns.empty()) {
        return 0.0;
    }

    const auto wins = std::count_if(periodicReturns.begin(), periodicReturns.end(), [](double value) {
        return value > 0.0;
    });
    return static_cast<double>(wins) / static_cast<double>(periodicReturns.size());
}

double FactorBacktestMetricsCalculator::calculateProfitFactor(const std::vector<double>& periodicReturns)
{
    if (periodicReturns.empty()) {
        return 0.0;
    }

    double grossProfit = 0.0;
    double grossLossAbs = 0.0;
    for (double periodicReturn : periodicReturns) {
        if (periodicReturn > 0.0) {
            grossProfit += periodicReturn;
        } else if (periodicReturn < 0.0) {
            grossLossAbs += -periodicReturn;
        }
    }

    if (grossLossAbs <= 1e-12) {
        return grossProfit > 0.0 ? std::numeric_limits<double>::infinity() : 0.0;
    }

    return grossProfit / grossLossAbs;
}

// ═══════════════════════════════════════════════════════════════
// 通用绩效指标（回测 + 实盘共用）
// ═══════════════════════════════════════════════════════════════

double FactorBacktestMetricsCalculator::calculateVolatility(
    const std::vector<double>& dailyReturns)
{
    if (dailyReturns.empty()) return 0.0;

    double sum = 0.0;
    for (double r : dailyReturns) sum += r;
    const double mean = sum / static_cast<double>(dailyReturns.size());

    double sqSum = 0.0;
    for (double r : dailyReturns) {
        const double diff = r - mean;
        sqSum += diff * diff;
    }
    return std::sqrt(sqSum / static_cast<double>(dailyReturns.size())) * std::sqrt(250.0);
}

double FactorBacktestMetricsCalculator::calculateAnnualizedReturn(
    double finalEquity, double initialCapital, int totalDays)
{
    if (initialCapital <= 0.0 || totalDays <= 0) return 0.0;
    const double result = std::pow(finalEquity / initialCapital,
                                   250.0 / static_cast<double>(totalDays)) - 1.0;
    return std::isfinite(result) ? result : 0.0;
}

double FactorBacktestMetricsCalculator::calculateSharpeRatio(
    double annualizedReturn, double annualizedVolatility)
{
    return (annualizedVolatility > 1e-12) ? annualizedReturn / annualizedVolatility : 0.0;
}

double FactorBacktestMetricsCalculator::calculateSortinoRatio(
    double annualizedReturn, double dailyDownsideDeviation)
{
    const double annualizedDownside = dailyDownsideDeviation * std::sqrt(250.0);
    return (annualizedDownside > 1e-12) ? annualizedReturn / annualizedDownside : 0.0;
}

double FactorBacktestMetricsCalculator::calculateCalmarRatio(
    double annualizedReturn, double maxDrawdown)
{
    return (maxDrawdown > 1e-9) ? annualizedReturn / maxDrawdown : 0.0;
}

FactorBacktestMetricsCalculator::BenchmarkComparisonSummary
FactorBacktestMetricsCalculator::calculateBenchmarkMetrics(
    const std::vector<double>& strategyDailyReturns,
    const std::vector<double>& benchmarkDailyReturns)
{
    BenchmarkComparisonSummary result;
    const size_t n = std::min(strategyDailyReturns.size(), benchmarkDailyReturns.size());
    if (n == 0) return result;

    double sSum = 0.0, bSum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sSum += strategyDailyReturns[i];
        bSum += benchmarkDailyReturns[i];
    }
    const double sM = sSum / static_cast<double>(n);
    const double bM = bSum / static_cast<double>(n);

    double cov = 0.0, bVar = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double sDiff = strategyDailyReturns[i] - sM;
        const double bDiff = benchmarkDailyReturns[i] - bM;
        cov  += sDiff * bDiff;
        bVar += bDiff * bDiff;
    }

    result.beta  = (bVar > 1e-12) ? cov / bVar : 0.0;
    result.alpha = (sM - result.beta * bM) * 250.0;  // 年化

    double te2 = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double d = strategyDailyReturns[i] - benchmarkDailyReturns[i];
        te2 += d * d;
    }
    result.trackingError = std::sqrt(te2 / static_cast<double>(n)) * std::sqrt(250.0);
    result.informationRatio = (result.trackingError > 1e-12)
        ? (sM - bM) / (result.trackingError / std::sqrt(250.0)) : 0.0;

    result.hasValidAlignment = true;
    return result;
}

FactorBacktestMetrics::Rating FactorQuality::evaluate(const FactorBacktestMetrics& metrics)
{
    return FactorBacktestMetricsCalculator::evaluateCoreRating(metrics);
}

} // namespace factor

