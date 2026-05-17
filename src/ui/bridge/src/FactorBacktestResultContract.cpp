#include "FactorBacktestResultContract.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

QVariantMap buildRatingGate(const QString& key,
                            const QString& label,
                            bool passed,
                            const QString& actualText,
                            const QString& thresholdText)
{
    QVariantMap gate;
    gate[QStringLiteral("key")] = key;
    gate[QStringLiteral("label")] = label;
    gate[QStringLiteral("passed")] = passed;
    gate[QStringLiteral("actualText")] = actualText;
    gate[QStringLiteral("thresholdText")] = thresholdText;
    return gate;
}

QVariantMap buildMetricCard(const QString& key,
                            const QString& title,
                            const QString& subtitle,
                            const QVariant& value,
                            const QString& format,
                            const QString& direction,
                            double goodThreshold,
                            const QString& thresholdText,
                            const QString& tier,
                            int units)
{
    QVariantMap card;
    card[QStringLiteral("key")] = key;
    card[QStringLiteral("title")] = title;
    card[QStringLiteral("subtitle")] = subtitle;
    card[QStringLiteral("value")] = value;
    card[QStringLiteral("format")] = format;
    card[QStringLiteral("direction")] = direction;
    card[QStringLiteral("goodThreshold")] = goodThreshold;
    card[QStringLiteral("thresholdText")] = thresholdText;
    card[QStringLiteral("tier")] = tier;
    card[QStringLiteral("units")] = units;
    return card;
}

QVariantMap buildSectionDescriptor(const QString& title, const QString& subtitle)
{
    QVariantMap section;
    section[QStringLiteral("title")] = title;
    section[QStringLiteral("subtitle")] = subtitle;
    return section;
}

QVariantMap buildAuxiliarySectionDescriptor()
{
    QVariantMap section;
    section[QStringLiteral("title")] = QStringLiteral("辅助判断");
    section[QStringLiteral("collapsedSubtitle")] = QStringLiteral("这里只放诊断项和分组剖面，默认折叠。" );
    section[QStringLiteral("expandedSubtitle")] = QStringLiteral("这里只放诊断项和分组剖面，当前已展开。" );
    return section;
}

QString buildRatingLabel(factor::FactorBacktestMetrics::Rating rating)
{
    switch (rating) {
    case factor::FactorBacktestMetrics::Rating::EXCELLENT:
        return QStringLiteral("优秀");
    case factor::FactorBacktestMetrics::Rating::GOOD:
        return QStringLiteral("良好");
    case factor::FactorBacktestMetrics::Rating::PASS:
        return QStringLiteral("合格");
    case factor::FactorBacktestMetrics::Rating::FAIL:
    default:
        return QStringLiteral("不合格");
    }
}

QVariantList buildLabeledSeries(const std::vector<double>& values, const QString& prefix)
{
    QVariantList series;
    series.reserve(static_cast<int>(values.size()));
    for (size_t index = 0; index < values.size(); ++index) {
        QVariantMap point;
        point[QStringLiteral("label")] = prefix + QString::number(static_cast<int>(index + 1));
        point[QStringLiteral("value")] = values[index];
        series.append(point);
    }
    return series;
}

QVariantList buildStringList(const std::vector<std::string>& values)
{
    QVariantList list;
    list.reserve(static_cast<int>(values.size()));
    for (const auto& value : values) {
        list.append(QString::fromStdString(value));
    }
    return list;
}

QVariantMap buildGroupChart(const QString& key,
                            const QString& title,
                            const QString& subtitle,
                            const QVariantList& series,
                            bool isPercent)
{
    QVariantMap chart;
    chart[QStringLiteral("key")] = key;
    chart[QStringLiteral("title")] = title;
    chart[QStringLiteral("subtitle")] = subtitle;
    chart[QStringLiteral("series")] = series;
    chart[QStringLiteral("isPercent")] = isPercent;
    return chart;
}

QVariantList buildGroupCharts(const factor::FactorBacktestMetrics& metrics)
{
    QVariantList charts;
    charts.append(buildGroupChart(QStringLiteral("groupAnnualReturns"),
                                  QStringLiteral("分组年化梯度"),
                                  QStringLiteral("检查高分组到低分组是否保持收益阶梯"),
                                  buildLabeledSeries(metrics.groupAnnualReturns, QStringLiteral("G")),
                                  true));
    charts.append(buildGroupChart(QStringLiteral("groupSharpes"),
                                  QStringLiteral("分组夏普梯度"),
                                  QStringLiteral("检查各分组风险调整后收益质量"),
                                  buildLabeledSeries(metrics.groupSharpes, QStringLiteral("G")),
                                  false));
    return charts;
}

QVariantList buildCoreMetrics(const factor::FactorBacktestMetrics& metrics)
{
    QVariantList cards;
    cards.append(buildMetricCard(QStringLiteral("rankIcir"),
                                 QStringLiteral("Rank ICIR"),
                                 QStringLiteral("必须看：IC 强度与稳定性"),
                                 metrics.rankIcir,
                                 QStringLiteral("number3"),
                                 QStringLiteral("high"),
                                 0.5,
                                 QStringLiteral("合格 >= 0.300，良好 >= 0.500，优秀 >= 1.000"),
                                 QStringLiteral("core"),
                                 3));
    cards.append(buildMetricCard(QStringLiteral("icWinRate"),
                                 QStringLiteral("IC 胜率"),
                                 QStringLiteral("必须看：方向稳定性"),
                                 metrics.icWinRate,
                                 QStringLiteral("percent1"),
                                 QStringLiteral("high"),
                                 0.55,
                                 QStringLiteral("合格 >= 52.0%，良好 >= 55.0%，优秀 >= 65.0%"),
                                 QStringLiteral("core"),
                                 3));
    cards.append(buildMetricCard(QStringLiteral("isMonotonic"),
                                 QStringLiteral("分组单调性"),
                                 QStringLiteral("必须看：分层收益是否递增"),
                                 metrics.isMonotonic,
                                 QStringLiteral("bool"),
                                 QStringLiteral("high"),
                                 0.0,
                                 QStringLiteral("必须通过"),
                                 QStringLiteral("core"),
                                 3));
    cards.append(buildMetricCard(QStringLiteral("longShortSharpe"),
                                 QStringLiteral("多空夏普"),
                                 QStringLiteral("必须看：多空组合风险调整后质量"),
                                 metrics.longShortSharpe,
                                 QStringLiteral("number2"),
                                 QStringLiteral("high"),
                                 1.5,
                                 QStringLiteral("合格 >= 0.50，良好 >= 1.50，优秀 >= 2.50"),
                                 QStringLiteral("core"),
                                 3));
    return cards;
}

QVariantList buildOptionalMetrics(const factor::FactorBacktestMetrics& metrics)
{
    QVariantList cards;
    cards.append(buildMetricCard(QStringLiteral("longShortAnnualReturn"),
                                 QStringLiteral("多空年化收益"),
                                 QStringLiteral("重要参考：看收益弹性"),
                                 metrics.longShortAnnualReturn,
                                 QStringLiteral("percent2"),
                                 QStringLiteral("high"),
                                 0.1,
                                 QStringLiteral("合格 >= 5.0%，良好 >= 15.0%，优秀 >= 30.0%"),
                                 QStringLiteral("optional"),
                                 1));
    cards.append(buildMetricCard(QStringLiteral("longShortMaxDrawdown"),
                                 QStringLiteral("多空最大回撤"),
                                 QStringLiteral("重要参考：看路径风险"),
                                 metrics.longShortMaxDrawdown,
                                 QStringLiteral("percent2"),
                                 QStringLiteral("low"),
                                 0.15,
                                 QStringLiteral("优秀 < 8.0%，良好 < 15.0%，合格 < 25.0%"),
                                 QStringLiteral("optional"),
                                 1));
    cards.append(buildMetricCard(QStringLiteral("icHalfLife"),
                                 QStringLiteral("IC 半衰期"),
                                 QStringLiteral("重要参考：看信号衰减速度"),
                                 metrics.icHalfLife,
                                 QStringLiteral("days"),
                                 QStringLiteral("high"),
                                 15.0,
                                 QStringLiteral("合格 >= 5 天，良好 >= 15 天，优秀 >= 30 天"),
                                 QStringLiteral("optional"),
                                 1));
    cards.append(buildMetricCard(QStringLiteral("annualTurnover"),
                                 QStringLiteral("年化换手率"),
                                 QStringLiteral("重要参考：看执行压力"),
                                 metrics.annualTurnover,
                                 QStringLiteral("percent2"),
                                 QStringLiteral("low"),
                                 2.0,
                                 QStringLiteral("优秀 < 200.0%，良好 < 500.0%，合格 < 1000.0%"),
                                 QStringLiteral("optional"),
                                 1));
    return cards;
}

QVariantList buildAuxiliaryMetrics(const factor::FactorBacktestMetrics& metrics)
{
    QVariantList cards;
    cards.append(buildMetricCard(QStringLiteral("icTStat"),
                                 QStringLiteral("IC t 统计量"),
                                 QStringLiteral("辅助判断：统计显著性"),
                                 metrics.icTStat,
                                 QStringLiteral("number2"),
                                 QStringLiteral("high"),
                                 2.0,
                                 QStringLiteral("参考 >= 2.00"),
                                 QStringLiteral("auxiliary"),
                                 2));
    cards.append(buildMetricCard(QStringLiteral("costAdjustedSharpe"),
                                 QStringLiteral("成本后夏普"),
                                 QStringLiteral("辅助判断：看交易成本后的可用性"),
                                 metrics.costAdjustedSharpe,
                                 QStringLiteral("number2"),
                                 QStringLiteral("high"),
                                 0.8,
                                 QStringLiteral("参考 >= 0.80 / 1.50"),
                                 QStringLiteral("auxiliary"),
                                 2));
    cards.append(buildMetricCard(QStringLiteral("alpha"),
                                 QStringLiteral("Alpha"),
                                 QStringLiteral("辅助判断：相对基准的纯收益"),
                                 metrics.alpha,
                                 QStringLiteral("percent2"),
                                 QStringLiteral("high"),
                                 0.0,
                                 QStringLiteral("参考 >= 0.0%"),
                                 QStringLiteral("auxiliary"),
                                 2));
    cards.append(buildMetricCard(QStringLiteral("monthlyWinRate"),
                                 QStringLiteral("月度胜率稳定性"),
                                 QStringLiteral("辅助判断：看不同月份是否稳"),
                                 metrics.monthlyWinRate,
                                 QStringLiteral("percent1"),
                                 QStringLiteral("high"),
                                 0.55,
                                 QStringLiteral("参考 >= 55.0%"),
                                 QStringLiteral("auxiliary"),
                                 2));
    return cards;
}

QVariantList buildRatingGates(const factor::FactorBacktestMetrics& metrics)
{
    QVariantList gates;
    gates.append(buildRatingGate(QStringLiteral("rankIcir"),
                                 QStringLiteral("Rank ICIR"),
                                 metrics.rankIcir >= 0.5,
                                 QString::number(metrics.rankIcir, 'f', 3),
                                 QStringLiteral(">= 0.300 / 0.500 / 1.000")));
    gates.append(buildRatingGate(QStringLiteral("icWinRate"),
                                 QStringLiteral("IC 胜率"),
                                 metrics.icWinRate >= 0.55,
                                 QStringLiteral("%1%").arg(QString::number(metrics.icWinRate * 100.0, 'f', 1)),
                                 QStringLiteral(">= 52.0% / 55.0% / 65.0%")));
    gates.append(buildRatingGate(QStringLiteral("isMonotonic"),
                                 QStringLiteral("分组单调性"),
                                 metrics.isMonotonic,
                                 metrics.isMonotonic ? QStringLiteral("通过") : QStringLiteral("未通过"),
                                 QStringLiteral("必须通过")));
    gates.append(buildRatingGate(QStringLiteral("longShortSharpe"),
                                 QStringLiteral("多空夏普"),
                                 metrics.longShortSharpe >= 1.5,
                                 QString::number(metrics.longShortSharpe, 'f', 2),
                                 QStringLiteral(">= 0.50 / 1.50 / 2.50")));
    return gates;
}

QString buildRatingSummary(const factor::FactorBacktestMetrics& metrics)
{
    switch (metrics.overallRating) {
    case factor::FactorBacktestMetrics::Rating::EXCELLENT:
        return QStringLiteral("已满足优秀门槛：Rank ICIR、IC 胜率、分组单调性和多空夏普全部达到优秀标准。");
    case factor::FactorBacktestMetrics::Rating::GOOD:
        return QStringLiteral("已满足良好门槛，可进入组合重点观察。评级严格由四个核心门槛决定。");
    case factor::FactorBacktestMetrics::Rating::PASS:
        return QStringLiteral("已达到合格门槛，可以进入进一步验证。评级严格由四个核心门槛决定。");
    case factor::FactorBacktestMetrics::Rating::FAIL:
    default:
        return QStringLiteral("未达到合格门槛。四个核心门槛中任一项不达标，都不会通过。");
    }
}

} // namespace

QVariantMap FactorBacktestResultContract::buildMetrics(const factor::BacktestResult& result)
{
    QVariantMap metrics;
    metrics[QStringLiteral("factorQuality")] = buildFactorQualityMetrics(result);
    metrics[QStringLiteral("research")] = buildResearchMetrics(result);
    metrics[QStringLiteral("execution")] = buildExecutionMetrics(result);
    metrics[QStringLiteral("ic")] = buildIcMetrics(result);
    metrics[QStringLiteral("groups")] = buildGroupMetrics(result);
    return metrics;
}

QVariantMap FactorBacktestResultContract::buildFactorQualityMetrics(const factor::BacktestResult& result)
{
    const auto& metrics = result.factorMetrics;

    QVariantMap quality;
    quality[QStringLiteral("rankIcMean")] = metrics.rankIcMean;
    quality[QStringLiteral("rankIcStd")] = metrics.rankIcStd;
    quality[QStringLiteral("rankIcir")] = metrics.rankIcir;
    quality[QStringLiteral("icWinRate")] = metrics.icWinRate;
    quality[QStringLiteral("isMonotonic")] = metrics.isMonotonic;
    quality[QStringLiteral("longShortSharpe")] = metrics.longShortSharpe;
    quality[QStringLiteral("longShortAnnualReturn")] = metrics.longShortAnnualReturn;
    quality[QStringLiteral("longShortMaxDrawdown")] = metrics.longShortMaxDrawdown;
    quality[QStringLiteral("icHalfLife")] = metrics.icHalfLife;
    quality[QStringLiteral("annualTurnover")] = metrics.annualTurnover;
    quality[QStringLiteral("costAdjustedSharpe")] = metrics.costAdjustedSharpe;
    quality[QStringLiteral("alpha")] = metrics.alpha;
    quality[QStringLiteral("icTStat")] = metrics.icTStat;
    quality[QStringLiteral("monthlyWinRate")] = metrics.monthlyWinRate;
    quality[QStringLiteral("numGroups")] = metrics.numGroups;
    quality[QStringLiteral("overallRating")] = static_cast<int>(metrics.overallRating);
    quality[QStringLiteral("ratingMethod")] = QStringLiteral("rank_icir_ic_winrate_monotonicity_longshort_sharpe");
    quality[QStringLiteral("ratingTitle")] = QStringLiteral("因子质量");
    quality[QStringLiteral("ratingLabel")] = buildRatingLabel(metrics.overallRating);
    quality[QStringLiteral("ratingSummary")] = buildRatingSummary(metrics);
    quality[QStringLiteral("ratingGates")] = buildRatingGates(metrics);
    quality[QStringLiteral("coreSection")] = buildSectionDescriptor(
        QStringLiteral("必须看"),
        QStringLiteral("只保留 Rank ICIR、IC 胜率、单调性和多空夏普这四项必看门槛。"));
    quality[QStringLiteral("optionalSection")] = buildSectionDescriptor(
        QStringLiteral("重要参考"),
        QStringLiteral("只保留多空年化收益、多空最大回撤、IC 半衰期和年化换手率。"));
    quality[QStringLiteral("auxiliarySection")] = buildAuxiliarySectionDescriptor();
    quality[QStringLiteral("coreMetrics")] = buildCoreMetrics(metrics);
    quality[QStringLiteral("optionalMetrics")] = buildOptionalMetrics(metrics);
    quality[QStringLiteral("auxiliaryMetrics")] = buildAuxiliaryMetrics(metrics);
    quality[QStringLiteral("groupCharts")] = buildGroupCharts(metrics);
    quality[QStringLiteral("groupAnnualReturns")] = buildDoubleList(metrics.groupAnnualReturns);
    quality[QStringLiteral("groupSharpes")] = buildDoubleList(metrics.groupSharpes);
    quality[QStringLiteral("returnSeries")] = buildReturnSeries(result);
    return quality;
}

QVariantMap FactorBacktestResultContract::buildResearchMetrics(const factor::BacktestResult& result)
{
    QVariantMap research;
    research[QStringLiteral("dataCoverage")] = dataCoverage(result);
    research[QStringLiteral("topGroupReturn")] = topGroupReturn(result);
    research[QStringLiteral("bottomGroupReturn")] = bottomGroupReturn(result);
    research[QStringLiteral("spreadReturn")] = spreadReturn(result);
    research[QStringLiteral("annualizedSpreadReturn")] = longShortAnnualReturn(result);
    research[QStringLiteral("monotonicity")] = monotonicity(result);
    research[QStringLiteral("discrimination")] = discrimination(result);
    return research;
}

QVariantMap FactorBacktestResultContract::buildExecutionMetrics(const factor::BacktestResult& result)
{
    QVariantMap execution;
    execution[QStringLiteral("annualReturn")] = executionAnnualReturn(result);
    execution[QStringLiteral("sharpeRatio")] = sharpeRatio(result);
    execution[QStringLiteral("maxDrawdown")] = maxDrawdown(result);
    execution[QStringLiteral("winRate")] = winRate(result);
    execution[QStringLiteral("profitFactor")] = profitFactor(result);
    execution[QStringLiteral("turnoverRate")] = turnoverRate(result);
    execution[QStringLiteral("benchmarkAnnualReturn")] = benchmarkAnnualReturn(result);
    execution[QStringLiteral("excessAnnualReturn")] = excessAnnualReturn(result);
    execution[QStringLiteral("trackingError")] = trackingError(result);
    execution[QStringLiteral("informationRatio")] = informationRatio(result);
    execution[QStringLiteral("alpha")] = alpha(result);
    execution[QStringLiteral("beta")] = beta(result);
    execution[QStringLiteral("volatility")] = volatility(result);
    execution[QStringLiteral("downsideDeviation")] = downsideDeviation(result);
    execution[QStringLiteral("sortinoRatio")] = sortinoRatio(result);
    execution[QStringLiteral("calmarRatio")] = calmarRatio(result);
    execution[QStringLiteral("valueAtRisk")] = valueAtRisk(result);
    execution[QStringLiteral("conditionalVaR")] = conditionalVaR(result);
    execution[QStringLiteral("riskTriggeredCount")] = riskTriggeredCount(result);
    execution[QStringLiteral("riskControlSummary")] = riskControlSummary(result);
    execution[QStringLiteral("actualStartDate")] = actualStartDate(result);
    execution[QStringLiteral("warmupTrimmedTradingDays")] = warmupTrimmedTradingDays(result);
    return execution;
}

QVariantMap FactorBacktestResultContract::buildIcMetrics(const factor::BacktestResult& result)
{
    QVariantMap ic;
    ic[QStringLiteral("value")] = icValue(result);
    ic[QStringLiteral("std")] = icStd(result);
    ic[QStringLiteral("ir")] = irValue(result);
    ic[QStringLiteral("positiveRate")] = icPositiveRate(result);
    ic[QStringLiteral("sampleCount")] = icSampleCount(result);
    return ic;
}

QVariantList FactorBacktestResultContract::buildGroupMetrics(const factor::BacktestResult& result)
{
    QVariantList groups;
    const size_t groupCount = result.groupResult.groupReturns.size();
    groups.reserve(static_cast<int>(groupCount));
    for (size_t index = 0; index < groupCount; ++index) {
        groups.append(buildGroupResult(result, index));
    }
    return groups;
}

QVariantMap FactorBacktestResultContract::buildReturnSeries(const factor::BacktestResult& result)
{
    QVariantMap series;
    series[QStringLiteral("dates")] = buildStringList(result.longShortDates);
    series[QStringLiteral("rawReturns")] = buildDoubleList(result.rawLongShortSeries);
    series[QStringLiteral("costAdjustedReturns")] = buildDoubleList(result.costAdjustedLongShortSeries);
    series[QStringLiteral("riskAdjustedReturns")] = buildDoubleList(result.riskAdjustedLongShortSeries);
    return series;
}

QVariantList FactorBacktestResultContract::buildDoubleList(const std::vector<double>& values)
{
    QVariantList list;
    list.reserve(static_cast<int>(values.size()));
    for (double value : values) {
        list.append(value);
    }
    return list;
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