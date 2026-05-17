#include "domain/factor/include/FactorBacktestMetricsCalculator.h"

#include "domain/factor/include/FactorBacktestExecutor.h"
#include "domain/factor/include/FactorBacktestIcUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace factor {

namespace {

FactorBacktestMetrics::Rating ratingFromMetrics(const FactorBacktestMetrics& metrics)
{
    const bool excellent = metrics.rankIcir >= 1.0
        && metrics.icWinRate >= 0.65
        && metrics.longShortSharpe >= 2.5
        && metrics.isMonotonic;
    if (excellent) {
        return FactorBacktestMetrics::Rating::EXCELLENT;
    }

    const bool good = metrics.rankIcir >= 0.5
        && metrics.icWinRate >= 0.55
        && metrics.longShortSharpe >= 1.5
        && metrics.isMonotonic;
    if (good) {
        return FactorBacktestMetrics::Rating::GOOD;
    }

    const bool pass = metrics.rankIcir >= 0.3
        && metrics.icWinRate >= 0.52
        && metrics.longShortSharpe >= 0.5
        && metrics.isMonotonic;
    if (pass) {
        return FactorBacktestMetrics::Rating::PASS;
    }

    return FactorBacktestMetrics::Rating::FAIL;
}

} // namespace

void FactorBacktestMetrics::computeRating()
{
    overallRating = ratingFromMetrics(*this);
}

double FactorBacktestMetricsCalculator::annualizationFactorForPeriods(int forwardDays)
{
    return 252.0 / static_cast<double>((std::max)(1, forwardDays));
}

bool FactorBacktestMetricsCalculator::isMonotonicGroupReturnSeries(const std::vector<double>& groupReturns)
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

double FactorBacktestMetricsCalculator::calculateSharpeFromPeriodicReturns(const std::vector<double>& periodicReturns,
                                                                           int forwardDays,
                                                                           double riskFreeRate)
{
    if (periodicReturns.empty()) {
        return 0.0;
    }

    const double annualizationFactor = annualizationFactorForPeriods(forwardDays);
    const double meanReturn = factor::icir::calculateMean(periodicReturns);
    const double stdReturn = factor::icir::calculateStdDev(periodicReturns, meanReturn);
    if (stdReturn <= 0.0) {
        return 0.0;
    }

    const double periodRiskFreeRate = riskFreeRate / annualizationFactor;
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

FactorBacktestMetrics FactorBacktestMetricsCalculator::buildFactorMetrics(const Inputs& inputs)
{
    FactorBacktestMetrics metrics;
    const double annualizationFactor = annualizationFactorForPeriods(inputs.config.forwardDays);

    metrics.rankIcMean = inputs.icirResult.icMean;
    metrics.rankIcStd = inputs.icirResult.icStd;
    metrics.rankIcir = inputs.icirResult.ir;
    metrics.icWinRate = inputs.icirResult.icPositiveRatio;
    metrics.isMonotonic = isMonotonicGroupReturnSeries(inputs.groupResult.groupReturns);
    metrics.longShortSharpe = calculateSharpeFromPeriodicReturns(inputs.rawLongShortSeries,
                                                                 inputs.config.forwardDays,
                                                                 inputs.config.riskFreeRate);
    metrics.longShortAnnualReturn = factor::icir::calculateMean(inputs.rawLongShortSeries) * annualizationFactor;
    metrics.longShortMaxDrawdown = calculateMaxDrawdown(inputs.rawLongShortSeries);
    metrics.icHalfLife = estimateHalfLifeDays(inputs.icirResult.icSeries);
    metrics.annualTurnover = factor::icir::calculateMean(inputs.turnoverSeries) * annualizationFactor;
    metrics.costAdjustedSharpe = calculateSharpeFromPeriodicReturns(inputs.costAdjustedLongShortSeries,
                                                                    inputs.config.forwardDays,
                                                                    inputs.config.riskFreeRate);
    metrics.alpha = inputs.benchmarkSummary && inputs.benchmarkSummary->hasValidAlignment
        ? inputs.benchmarkSummary->alpha
        : inputs.alpha;
    if (!inputs.icirResult.icSeries.empty() && inputs.icirResult.icStd > 0.0) {
        metrics.icTStat = inputs.icirResult.icMean
            / (inputs.icirResult.icStd / std::sqrt(static_cast<double>(inputs.icirResult.icSeries.size())));
    }
    metrics.monthlyWinRate = calculateMonthlyWinRate(inputs.costAdjustedLongShortSeries, inputs.longShortDates);
    metrics.numGroups = inputs.groupResult.groupReturns.empty()
        ? (std::max)(1, inputs.config.numGroups)
        : static_cast<int>(inputs.groupResult.groupReturns.size());

    for (const auto& groupSeries : inputs.groupReturnSeriesByGroup) {
        if (groupSeries.empty()) {
            continue;
        }
        metrics.groupAnnualReturns.push_back(factor::icir::calculateMean(groupSeries) * annualizationFactor);
        metrics.groupSharpes.push_back(calculateSharpeFromPeriodicReturns(groupSeries,
                                                                          inputs.config.forwardDays,
                                                                          inputs.config.riskFreeRate));
    }

    metrics.computeRating();
    return metrics;
}

void FactorBacktestMetricsCalculator::populateResultMetrics(BacktestResult& result, const Inputs& inputs)
{
    const double annualizationFactor = annualizationFactorForPeriods(inputs.config.forwardDays);
    const double periodRiskFreeRate = inputs.config.riskFreeRate / annualizationFactor;
    const double averageLongShort = factor::icir::calculateMean(inputs.adjustedLongShortSeries);
    const double longShortStd = factor::icir::calculateStdDev(inputs.adjustedLongShortSeries, averageLongShort);
    const double averageTurnover = factor::icir::calculateMean(inputs.turnoverSeries);
    const double averageExcessReturn = averageLongShort - periodRiskFreeRate;

    result.annualReturn = averageLongShort * annualizationFactor;
    result.sharpeRatio = longShortStd > 0.0 ? (averageExcessReturn / longShortStd) * std::sqrt(annualizationFactor) : 0.0;
    result.maxDrawdown = calculateMaxDrawdown(inputs.adjustedLongShortSeries);
    result.winRate = calculateWinRate(inputs.adjustedLongShortSeries);
    result.profitFactor = calculateProfitFactor(inputs.adjustedLongShortSeries);
    result.turnoverRate = averageTurnover * annualizationFactor * 100.0;

    if (inputs.benchmarkSummary && inputs.benchmarkSummary->hasValidAlignment) {
        result.benchmarkAnnualReturn = inputs.benchmarkSummary->benchmarkAnnualReturn;
        result.excessAnnualReturn = inputs.benchmarkSummary->excessAnnualReturn;
        result.trackingError = inputs.benchmarkSummary->trackingError;
        result.informationRatio = inputs.benchmarkSummary->informationRatio;
        result.beta = inputs.benchmarkSummary->beta;
        result.alpha = inputs.benchmarkSummary->alpha;
    }

    result.volatility = longShortStd * std::sqrt(annualizationFactor);
    result.downsideDeviation = calculateDownsideDeviation(inputs.adjustedLongShortSeries) * std::sqrt(annualizationFactor);
    result.sortinoRatio = result.downsideDeviation > 0.0
        ? (averageExcessReturn / result.downsideDeviation) * std::sqrt(annualizationFactor)
        : 0.0;
    result.calmarRatio = result.maxDrawdown > 0.0 ? result.annualReturn / result.maxDrawdown : 0.0;
    result.valueAtRisk = calculateValueAtRisk(inputs.adjustedLongShortSeries, 0.95);
    result.conditionalVaR = calculateConditionalVaR(inputs.adjustedLongShortSeries, 0.95);
    result.riskTriggeredCount = inputs.riskTriggeredCount;
    result.factorMetrics = buildFactorMetrics(inputs);
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
    double alignedStrategySum = 0.0;
    double alignedBenchmarkSum = 0.0;
    double alignedStrategySquareSum = 0.0;
    double alignedBenchmarkSquareSum = 0.0;
    double alignedProductSum = 0.0;
    double alignedExcessSum = 0.0;
    double alignedExcessSquareSum = 0.0;

    const size_t alignedCount = (std::min)(strategyReturns.size(), strategyDates.size());
    for (size_t index = 0; index < alignedCount; ++index) {
        const double benchmarkReturn = benchmarkLookup(strategyDates[index]);
        if (!std::isfinite(benchmarkReturn)) {
            continue;
        }

        const double strategyReturn = strategyReturns[index];
        const double excessReturn = strategyReturn - benchmarkReturn;

        ++alignedSampleCount;
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
    summary.benchmarkAnnualReturn = benchmarkMean * annualizationFactor;
    summary.excessAnnualReturn = (alignedStrategyMean - benchmarkMean) * annualizationFactor;

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
        peakNetValue = (std::max)(peakNetValue, cumulativeNetValue);
        if (peakNetValue <= 0.0) {
            continue;
        }

        const double drawdown = (peakNetValue - cumulativeNetValue) / peakNetValue;
        maxDrawdown = (std::max)(maxDrawdown, drawdown);
    }

    return maxDrawdown;
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

FactorBacktestMetrics::Rating FactorQuality::evaluate(const FactorBacktestMetrics& metrics)
{
    return ratingFromMetrics(metrics);
}

} // namespace factor
