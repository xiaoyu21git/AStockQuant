#pragma once

#include <QVariantList>
#include <QVariantMap>

#include "../../../domain/factor/include/FactorBacktestExecutor.h"

class FactorBacktestResultContract final
{
public:
    static QVariantMap buildMetrics(const factor::BacktestResult& result);

private:
    static QVariantMap buildRatingGate(const QString& key,
                                       const QString& label,
                                       bool passed,
                                       const QString& actualText,
                                       const QString& thresholdText);
    static QVariantMap buildMetricCard(const QString& key,
                                       const QString& title,
                                       const QString& subtitle,
                                       const QVariant& value,
                                       const QString& format,
                                       const QString& direction,
                                       double goodThreshold,
                                       const QString& thresholdText,
                                       const QString& tier,
                                       int units);
    static QVariantMap buildSectionDescriptor(const QString& title, const QString& subtitle);
    static QVariantMap buildAuxiliarySectionDescriptor();
    static bool hasPositiveTopBottomSpread(const std::vector<double>& values);
    static bool hasStrictMonotonicSpread(const std::vector<double>& values);
    static QString buildRatingLabel(factor::FactorBacktestMetrics::Rating rating);
    static QVariantList buildLabeledSeries(const std::vector<double>& values, const QString& prefix);
    static QVariantList buildStringList(const std::vector<std::string>& values);
    static QVariantMap buildGroupChart(const QString& key,
                                       const QString& title,
                                       const QString& subtitle,
                                       const QVariantList& series,
                                       bool isPercent);
    static QVariantList buildGroupCharts(const factor::FactorBacktestMetrics& metrics);
    static QVariantList buildCoreMetrics(const factor::FactorBacktestMetrics& metrics);
    static QVariantList buildOptionalMetrics(const factor::BacktestResult& result,
                                             double documentedLongShortAnnualReturn,
                                             double documentedLongShortMaxDrawdown,
                                             double documentedAnnualTurnover);
    static QVariantList buildAuxiliaryMetrics(const factor::FactorBacktestMetrics& metrics);
    static QVariantList buildRatingGates(const factor::FactorBacktestMetrics& metrics);
    static QString buildRatingSummary(const factor::FactorBacktestMetrics& metrics);
    static QVariantMap buildFactorQualityMetrics(const factor::BacktestResult& result);
    static QVariantMap buildResearchMetrics(const factor::BacktestResult& result);
    static QVariantMap buildExecutionMetrics(const factor::BacktestResult& result);
    static QVariantMap buildIcMetrics(const factor::BacktestResult& result);
    static QVariantList buildGroupMetrics(const factor::BacktestResult& result);
    static QVariantList buildDoubleList(const std::vector<double>& values);
    static QVariantMap buildReturnSeries(const factor::BacktestResult& result);
    static QVariantMap buildGroupResult(const factor::BacktestResult& result, size_t index);
    static double annualizationFactorForForwardDays(int forwardDays);
    static double topGroupReturn(const factor::BacktestResult& result);
    static double bottomGroupReturn(const factor::BacktestResult& result);
    static double spreadReturn(const factor::BacktestResult& result);
    static double longShortAnnualReturn(const factor::BacktestResult& result);
    static double executionAnnualReturn(const factor::BacktestResult& result);
    static double dataCoverage(const factor::BacktestResult& result);
    static double sharpeRatio(const factor::BacktestResult& result);
    static double maxDrawdown(const factor::BacktestResult& result);
    static double winRate(const factor::BacktestResult& result);
    static double profitFactor(const factor::BacktestResult& result);
    static double turnoverRate(const factor::BacktestResult& result);
    static double benchmarkAnnualReturn(const factor::BacktestResult& result);
    static double excessAnnualReturn(const factor::BacktestResult& result);
    static double trackingError(const factor::BacktestResult& result);
    static double informationRatio(const factor::BacktestResult& result);
    static double alpha(const factor::BacktestResult& result);
    static double beta(const factor::BacktestResult& result);
    static double volatility(const factor::BacktestResult& result);
    static double downsideDeviation(const factor::BacktestResult& result);
    static double sortinoRatio(const factor::BacktestResult& result);
    static double calmarRatio(const factor::BacktestResult& result);
    static double valueAtRisk(const factor::BacktestResult& result);
    static double conditionalVaR(const factor::BacktestResult& result);
    static int riskTriggeredCount(const factor::BacktestResult& result);
    static QString riskControlSummary(const factor::BacktestResult& result);
    static QString actualStartDate(const factor::BacktestResult& result);
    static int warmupTrimmedTradingDays(const factor::BacktestResult& result);
    static double monotonicity(const factor::BacktestResult& result);
    static double discrimination(const factor::BacktestResult& result);
    static double icValue(const factor::BacktestResult& result);
    static double icStd(const factor::BacktestResult& result);
    static double irValue(const factor::BacktestResult& result);
    static double icPositiveRate(const factor::BacktestResult& result);
    static int icSampleCount(const factor::BacktestResult& result);
    static double populationStdDev(const std::vector<double>& values);
    static double monotonicityScore(const std::vector<double>& values);
};