#include "FactorBacktestResultContract.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <numeric>

QVariantList FactorBacktestResultContract::buildGroupResults(const factor::BacktestResult& result)
{
    QVariantList groups;
    const size_t groupCount = result.groupResult.groupReturns.size();
    groups.reserve(static_cast<int>(groupCount));
    for (size_t index = 0; index < groupCount; ++index) {
        groups.append(buildGroupResult(result, index));
    }
    return groups;
}

QVariantMap FactorBacktestResultContract::buildIcirResult(const factor::BacktestResult& result)
{
    QVariantMap icirResult;
    icirResult[QStringLiteral("icValue")] = icValue(result);
    icirResult[QStringLiteral("icStd")] = icStd(result);
    icirResult[QStringLiteral("irValue")] = irValue(result);
    icirResult[QStringLiteral("icPositiveRate")] = icPositiveRate(result);
    icirResult[QStringLiteral("icSampleCount")] = icSampleCount(result);
    return icirResult;
}

QVariantMap FactorBacktestResultContract::buildSummaryStats(const factor::BacktestResult& result)
{
    QVariantMap summary;
    summary[QStringLiteral("topGroupReturn")] = topGroupReturn(result);
    summary[QStringLiteral("bottomGroupReturn")] = bottomGroupReturn(result);
    summary[QStringLiteral("spreadReturn")] = spreadReturn(result);
    summary[QStringLiteral("longShortAnnualReturn")] = longShortAnnualReturn(result);
    summary[QStringLiteral("executionAnnualReturn")] = executionAnnualReturn(result);
    summary[QStringLiteral("dataCoverage")] = dataCoverage(result);
    summary[QStringLiteral("sharpeRatio")] = sharpeRatio(result);
    summary[QStringLiteral("maxDrawdown")] = maxDrawdown(result);
    summary[QStringLiteral("winRate")] = winRate(result);
    summary[QStringLiteral("profitFactor")] = profitFactor(result);
    summary[QStringLiteral("turnoverRate")] = turnoverRate(result);
    summary[QStringLiteral("benchmarkAnnualReturn")] = benchmarkAnnualReturn(result);
    summary[QStringLiteral("excessAnnualReturn")] = excessAnnualReturn(result);
    summary[QStringLiteral("trackingError")] = trackingError(result);
    summary[QStringLiteral("informationRatio")] = informationRatio(result);
    summary[QStringLiteral("alpha")] = alpha(result);
    summary[QStringLiteral("beta")] = beta(result);
    summary[QStringLiteral("volatility")] = volatility(result);
    summary[QStringLiteral("downsideDeviation")] = downsideDeviation(result);
    summary[QStringLiteral("sortinoRatio")] = sortinoRatio(result);
    summary[QStringLiteral("calmarRatio")] = calmarRatio(result);
    summary[QStringLiteral("valueAtRisk")] = valueAtRisk(result);
    summary[QStringLiteral("conditionalVaR")] = conditionalVaR(result);
    summary[QStringLiteral("riskTriggeredCount")] = riskTriggeredCount(result);
    summary[QStringLiteral("riskControlSummary")] = riskControlSummary(result);
    summary[QStringLiteral("actualStartDate")] = actualStartDate(result);
    summary[QStringLiteral("warmupTrimmedTradingDays")] = warmupTrimmedTradingDays(result);
    summary[QStringLiteral("monotonicity")] = monotonicity(result);
    summary[QStringLiteral("discrimination")] = discrimination(result);
    return summary;
}

QVariantMap FactorBacktestResultContract::buildGroupResult(const factor::BacktestResult& result, size_t index)
{
    QVariantMap group;
    const double groupReturn = index < result.groupResult.groupReturns.size()
        ? result.groupResult.groupReturns[index]
        : 0.0;
    group[QStringLiteral("groupIndex")] = static_cast<int>(index + 1);
    group[QStringLiteral("returnRate")] = groupReturn;
    group[QStringLiteral("annualizedReturn")] = groupReturn * annualizationFactorForForwardDays(result.config.forwardDays);
    if (index < result.groupResult.groupStockCounts.size()) {
        group[QStringLiteral("stockCount")] = result.groupResult.groupStockCounts[index];
    }
    if (index < result.groupResult.minFactorValues.size()) {
        group[QStringLiteral("minFactorValue")] = result.groupResult.minFactorValues[index];
    }
    if (index < result.groupResult.maxFactorValues.size()) {
        group[QStringLiteral("maxFactorValue")] = result.groupResult.maxFactorValues[index];
    }
    return group;
}

double FactorBacktestResultContract::annualizationFactorForForwardDays(int forwardDays)
{
    return 252.0 / static_cast<double>((std::max)(1, forwardDays));
}

double FactorBacktestResultContract::topGroupReturn(const factor::BacktestResult& result)
{
    return result.groupResult.topGroupReturn;
}

double FactorBacktestResultContract::bottomGroupReturn(const factor::BacktestResult& result)
{
    return result.groupResult.bottomGroupReturn;
}

double FactorBacktestResultContract::spreadReturn(const factor::BacktestResult& result)
{
    return result.groupResult.longShortReturn;
}

double FactorBacktestResultContract::longShortAnnualReturn(const factor::BacktestResult& result)
{
    return result.groupResult.longShortReturn * annualizationFactorForForwardDays(result.config.forwardDays);
}

double FactorBacktestResultContract::executionAnnualReturn(const factor::BacktestResult& result)
{
    return result.annualReturn;
}

double FactorBacktestResultContract::dataCoverage(const factor::BacktestResult& result)
{
    return result.dataCoverage;
}

double FactorBacktestResultContract::sharpeRatio(const factor::BacktestResult& result)
{
    return result.sharpeRatio;
}

double FactorBacktestResultContract::maxDrawdown(const factor::BacktestResult& result)
{
    return result.maxDrawdown;
}

double FactorBacktestResultContract::winRate(const factor::BacktestResult& result)
{
    return result.winRate;
}

double FactorBacktestResultContract::profitFactor(const factor::BacktestResult& result)
{
    return result.profitFactor;
}

double FactorBacktestResultContract::turnoverRate(const factor::BacktestResult& result)
{
    return result.turnoverRate;
}

double FactorBacktestResultContract::benchmarkAnnualReturn(const factor::BacktestResult& result)
{
    return result.benchmarkAnnualReturn;
}

double FactorBacktestResultContract::excessAnnualReturn(const factor::BacktestResult& result)
{
    return result.excessAnnualReturn;
}

double FactorBacktestResultContract::trackingError(const factor::BacktestResult& result)
{
    return result.trackingError;
}

double FactorBacktestResultContract::informationRatio(const factor::BacktestResult& result)
{
    return result.informationRatio;
}

double FactorBacktestResultContract::alpha(const factor::BacktestResult& result)
{
    return result.alpha;
}

double FactorBacktestResultContract::beta(const factor::BacktestResult& result)
{
    return result.beta;
}

double FactorBacktestResultContract::volatility(const factor::BacktestResult& result)
{
    return result.volatility;
}

double FactorBacktestResultContract::downsideDeviation(const factor::BacktestResult& result)
{
    return result.downsideDeviation;
}

double FactorBacktestResultContract::sortinoRatio(const factor::BacktestResult& result)
{
    return result.sortinoRatio;
}

double FactorBacktestResultContract::calmarRatio(const factor::BacktestResult& result)
{
    return result.calmarRatio;
}

double FactorBacktestResultContract::valueAtRisk(const factor::BacktestResult& result)
{
    return result.valueAtRisk;
}

double FactorBacktestResultContract::conditionalVaR(const factor::BacktestResult& result)
{
    return result.conditionalVaR;
}

int FactorBacktestResultContract::riskTriggeredCount(const factor::BacktestResult& result)
{
    return result.riskTriggeredCount;
}

QString FactorBacktestResultContract::riskControlSummary(const factor::BacktestResult& result)
{
    return QString::fromStdString(result.riskControlSummary);
}

QString FactorBacktestResultContract::actualStartDate(const factor::BacktestResult& result)
{
    return QString::fromStdString(result.actualStartDate);
}

int FactorBacktestResultContract::warmupTrimmedTradingDays(const factor::BacktestResult& result)
{
    return result.warmupTrimmedTradingDays;
}

double FactorBacktestResultContract::monotonicity(const factor::BacktestResult& result)
{
    return monotonicityScore(result.groupResult.groupReturns);
}

double FactorBacktestResultContract::discrimination(const factor::BacktestResult& result)
{
    return populationStdDev(result.groupResult.groupReturns);
}

double FactorBacktestResultContract::icValue(const factor::BacktestResult& result)
{
    return result.icirResult.icMean;
}

double FactorBacktestResultContract::icStd(const factor::BacktestResult& result)
{
    return result.icirResult.icStd;
}

double FactorBacktestResultContract::irValue(const factor::BacktestResult& result)
{
    return result.icirResult.ir;
}

double FactorBacktestResultContract::icPositiveRate(const factor::BacktestResult& result)
{
    return result.icirResult.icPositiveRatio;
}

int FactorBacktestResultContract::icSampleCount(const factor::BacktestResult& result)
{
    return static_cast<int>(result.icirResult.icSeries.size());
}

double FactorBacktestResultContract::populationStdDev(const std::vector<double>& values)
{
    if (values.empty()) {
        return 0.0;
    }

    const double mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    double squaredDiffSum = 0.0;
    for (double value : values) {
        const double diff = value - mean;
        squaredDiffSum += diff * diff;
    }

    return std::sqrt(squaredDiffSum / static_cast<double>(values.size()));
}

double FactorBacktestResultContract::monotonicityScore(const std::vector<double>& values)
{
    if (values.size() < 2) {
        return 0.0;
    }

    const size_t count = values.size();
    const double meanX = (static_cast<double>(count) + 1.0) / 2.0;
    const double meanY = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(count);

    double covariance = 0.0;
    double varianceX = 0.0;
    double varianceY = 0.0;
    for (size_t index = 0; index < count; ++index) {
        const double x = static_cast<double>(index + 1);
        const double dx = x - meanX;
        const double dy = values[index] - meanY;
        covariance += dx * dy;
        varianceX += dx * dx;
        varianceY += dy * dy;
    }

    if (varianceX <= 0.0 || varianceY <= 0.0) {
        return 0.0;
    }

    return covariance / std::sqrt(varianceX * varianceY);
}