#include "FactorBacktestResultContract.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

int executionLagTradingDays(const factor::BacktestResult& result)
{
    return result.config.marketEnvironmentProfile == factor::MarketEnvironmentProfile::CN_A_SHARE ? 1 : 0;
}

QString signalDateSemantics(const factor::BacktestResult&)
{
    return QStringLiteral("factor_observation_date");
}

QString executionDateSemantics(const factor::BacktestResult& result)
{
    return executionLagTradingDays(result) > 0
        ? QStringLiteral("next_trading_day_after_signal")
        : QStringLiteral("same_trading_day_as_signal");
}

} // namespace

QVariantMap FactorBacktestResultContract::buildRatingGate(const QString& key,
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

QVariantMap FactorBacktestResultContract::buildMetricCard(const QString& key,
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

QVariantMap FactorBacktestResultContract::buildSectionDescriptor(const QString& title, const QString& subtitle)
{
    QVariantMap section;
    section[QStringLiteral("title")] = title;
    section[QStringLiteral("subtitle")] = subtitle;
    return section;
}

QVariantMap FactorBacktestResultContract::buildAuxiliarySectionDescriptor()
{
    QVariantMap section;
    section[QStringLiteral("title")] = QStringLiteral("辅助判断");
    section[QStringLiteral("collapsedSubtitle")] = QStringLiteral("这里只放诊断项和分组剖面，默认折叠。" );
    section[QStringLiteral("expandedSubtitle")] = QStringLiteral("这里只放诊断项和分组剖面，当前已展开。" );
    return section;
}

bool FactorBacktestResultContract::hasPositiveTopBottomSpread(const std::vector<double>& values)
{
    return values.size() >= 2 && values.front() > values.back();
}

bool FactorBacktestResultContract::hasStrictMonotonicSpread(const std::vector<double>& values)
{
    if (values.size() < 3) {
        return false;
    }

    for (size_t index = 1; index < values.size(); ++index) {
        if (values[index] >= values[index - 1]) {
            return false;
        }
    }
    return true;
}

QString FactorBacktestResultContract::buildRatingLabel(factor::FactorBacktestMetrics::Rating rating)
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

QVariantList FactorBacktestResultContract::buildLabeledSeries(const std::vector<double>& values, const QString& prefix)
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

QVariantList FactorBacktestResultContract::buildStringList(const std::vector<std::string>& values)
{
    QVariantList list;
    list.reserve(static_cast<int>(values.size()));
    for (const auto& value : values) {
        list.append(QString::fromStdString(value));
    }
    return list;
}

QVariantMap FactorBacktestResultContract::buildGroupChart(const QString& key,
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

QVariantList FactorBacktestResultContract::buildGroupCharts(const factor::FactorBacktestMetrics& metrics)
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

QVariantList FactorBacktestResultContract::buildCoreMetrics(const factor::FactorBacktestMetrics& metrics)
{
    QVariantList cards;
    cards.append(buildMetricCard(QStringLiteral("rankIcMean"),
                                 QStringLiteral("Rank IC均值"),
                                 QStringLiteral("核心1：先看因子对未来收益有没有预测力"),
                                 metrics.rankIcMean,
                                 QStringLiteral("number3"),
                                 QStringLiteral("high"),
                                 0.03,
                                 QStringLiteral("合格 > 0.020，良好 > 0.030，优秀 > 0.050"),
                                 QStringLiteral("core"),
                                 3));
    cards.append(buildMetricCard(QStringLiteral("icPValue"),
                                 QStringLiteral("IC p值"),
                                 QStringLiteral("核心2：严格按 t 检验双侧 p 值看统计显著性"),
                                 metrics.icPValue,
                                 QStringLiteral("number3"),
                                 QStringLiteral("low"),
                                 0.05,
                                 QStringLiteral("优秀 < 0.001，良好 < 0.010，合格 < 0.050"),
                                 QStringLiteral("core"),
                                 3));
    cards.append(buildMetricCard(QStringLiteral("rankIcir"),
                                 QStringLiteral("Rank ICIR"),
                                 QStringLiteral("核心3：看 IC 强度和稳定性是否足够持续"),
                                 metrics.rankIcir,
                                 QStringLiteral("number3"),
                                 QStringLiteral("high"),
                                 0.5,
                                 QStringLiteral("合格 > 0.300，良好 > 0.500，优秀 > 0.800"),
                                 QStringLiteral("core"),
                                 3));
    cards.append(buildMetricCard(QStringLiteral("icWinRate"),
                                 QStringLiteral("IC 胜率"),
                                 QStringLiteral("核心4：看信号方向是不是大多数时间都站对"),
                                 metrics.icWinRate,
                                 QStringLiteral("percent1"),
                                 QStringLiteral("high"),
                                 0.55,
                                 QStringLiteral("合格 > 55.0%，良好 > 65.0%，优秀 > 75.0%"),
                                 QStringLiteral("core"),
                                 3));
    cards.append(buildMetricCard(QStringLiteral("monotonicityScore"),
                                 QStringLiteral("分组单调相关系数"),
                                 QStringLiteral("核心5：按 |r| 和 Top > Bottom 判断分组逻辑是否一致"),
                                 std::abs(metrics.monotonicityScore),
                                 QStringLiteral("number3"),
                                 QStringLiteral("high"),
                                 0.7,
                                 QStringLiteral("合格 |r| > 0.700 且 Top > Bottom，良好 |r| > 0.850 且严格单调，优秀 |r| > 0.950 且严格单调"),
                                 QStringLiteral("core"),
                                 3));
    return cards;
}

QVariantList FactorBacktestResultContract::buildOptionalMetrics(const factor::BacktestResult& result,
                                                                double documentedLongShortAnnualReturn,
                                                                double documentedLongShortMaxDrawdown,
                                                                double documentedAnnualTurnover)
{
    const auto& metrics = result.factorMetrics;
    QVariantList cards;
    cards.append(buildMetricCard(QStringLiteral("longShortSharpe"),
                                 QStringLiteral("研究多空夏普"),
                                 QStringLiteral("重要参考：看研究口径原始多空价差的风险调整收益"),
                                 metrics.longShortSharpe,
                                 QStringLiteral("number2"),
                                 QStringLiteral("high"),
                                 1.5,
                                 QStringLiteral("合格 >= 1.00，良好 >= 2.00，优秀 >= 3.00"),
                                 QStringLiteral("optional"),
                                 1));
    cards.append(buildMetricCard(QStringLiteral("longShortAnnualReturn"),
                                 QStringLiteral("研究多空年化"),
                                 QStringLiteral("重要参考：原始多空价差的线性年化，不扣成本，不含风控"),
                                 documentedLongShortAnnualReturn,
                                 QStringLiteral("percent2"),
                                 QStringLiteral("high"),
                                 0.1,
                                 QStringLiteral("合格 >= 5.0%，良好 >= 10.0%，优秀 >= 20.0%"),
                                 QStringLiteral("optional"),
                                 1));
    cards.append(buildMetricCard(QStringLiteral("executionAnnualReturn"),
                                 QStringLiteral("执行复合年化"),
                                 QStringLiteral("重要参考：风控后执行净值路径的复合年化，必须与研究线性年化分开看"),
                                 result.annualReturn,
                                 QStringLiteral("percent2"),
                                 QStringLiteral("high"),
                                 0.1,
                                 QStringLiteral("参考 >= 5.0%，良好 >= 10.0%，优秀 >= 20.0%"),
                                 QStringLiteral("optional"),
                                 1));
    cards.append(buildMetricCard(QStringLiteral("longShortMaxDrawdown"),
                                 QStringLiteral("执行多空最大回撤"),
                                 QStringLiteral("重要参考：看风控后执行序列的路径风险，对应执行复合年化那条净值路径"),
                                 documentedLongShortMaxDrawdown,
                                 QStringLiteral("percent2"),
                                 QStringLiteral("low"),
                                 0.15,
                                 QStringLiteral("优秀 < 10.0%，良好 < 15.0%，合格 < 25.0%"),
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
                                 QStringLiteral("执行年化换手率"),
                                 QStringLiteral("重要参考：看执行口径下的调仓压力"),
                                 documentedAnnualTurnover,
                                 QStringLiteral("percent2"),
                                 QStringLiteral("low"),
                                 1.0,
                                 QStringLiteral("优秀 < 50.0%，良好 < 100.0%，合格 < 200.0%"),
                                 QStringLiteral("optional"),
                                 1));
    return cards;
}

QVariantList FactorBacktestResultContract::buildAuxiliaryMetrics(const factor::FactorBacktestMetrics& metrics)
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
                                 1));
    cards.append(buildMetricCard(QStringLiteral("costAdjustedSharpe"),
                                 QStringLiteral("执行成本后夏普"),
                                 QStringLiteral("辅助判断：看执行口径扣成本后的可用性"),
                                 metrics.costAdjustedSharpe,
                                 QStringLiteral("number2"),
                                 QStringLiteral("high"),
                                 0.8,
                                 QStringLiteral("参考 >= 0.80 / 1.50"),
                                 QStringLiteral("auxiliary"),
                                 1));
    cards.append(buildMetricCard(QStringLiteral("alpha"),
                                 QStringLiteral("基准Alpha"),
                                 QStringLiteral("辅助判断：相对基准收益分解后的 alpha"),
                                 metrics.alpha,
                                 QStringLiteral("percent2"),
                                 QStringLiteral("high"),
                                 0.0,
                                 QStringLiteral("参考 >= 0.0%"),
                                 QStringLiteral("auxiliary"),
                                 1));
    cards.append(buildMetricCard(QStringLiteral("monthlyWinRate"),
                                 QStringLiteral("执行月度胜率"),
                                 QStringLiteral("辅助判断：看执行口径在不同月份是否稳定"),
                                 metrics.monthlyWinRate,
                                 QStringLiteral("percent1"),
                                 QStringLiteral("high"),
                                 0.55,
                                 QStringLiteral("参考 >= 55.0%"),
                                 QStringLiteral("auxiliary"),
                                 1));
    return cards;
}

QVariantList FactorBacktestResultContract::buildRatingGates(const factor::FactorBacktestMetrics& metrics)
{
    QVariantList gates;
    const double monotonicityAbsScore = std::abs(metrics.monotonicityScore);
    const bool hasGroupSpread = hasPositiveTopBottomSpread(metrics.groupAnnualReturns);
    const bool strictMonotonic = hasStrictMonotonicSpread(metrics.groupAnnualReturns);

    gates.append(buildRatingGate(QStringLiteral("rankIcMean"),
                                 QStringLiteral("Rank IC均值"),
                                 metrics.rankIcMean > 0.02,
                                 QString::number(metrics.rankIcMean, 'f', 3),
                                 QStringLiteral("> 0.020 / 0.030 / 0.050")));
    gates.append(buildRatingGate(QStringLiteral("icPValue"),
                                 QStringLiteral("IC p值"),
                                 metrics.icPValue < 0.05,
                                 QString::number(metrics.icPValue, 'f', 3),
                                 QStringLiteral("< 0.050 / 0.010 / 0.001")));
    gates.append(buildRatingGate(QStringLiteral("rankIcir"),
                                 QStringLiteral("Rank ICIR"),
                                 metrics.rankIcir > 0.3,
                                 QString::number(metrics.rankIcir, 'f', 3),
                                 QStringLiteral("> 0.300 / 0.500 / 0.800")));
    gates.append(buildRatingGate(QStringLiteral("icWinRate"),
                                 QStringLiteral("IC 胜率"),
                                 metrics.icWinRate > 0.55,
                                 QStringLiteral("%1%").arg(QString::number(metrics.icWinRate * 100.0, 'f', 1)),
                                 QStringLiteral("> 55.0% / 65.0% / 75.0%")));
    gates.append(buildRatingGate(QStringLiteral("monotonicityScore"),
                                 QStringLiteral("分组单调相关系数"),
                                 monotonicityAbsScore > 0.7 && hasGroupSpread,
                                 QStringLiteral("|r|=%1，Top%2Bottom，%3")
                                     .arg(QString::number(monotonicityAbsScore, 'f', 3))
                                     .arg(hasGroupSpread ? QStringLiteral(">") : QStringLiteral("<="))
                                     .arg(strictMonotonic ? QStringLiteral("严格单调") : QStringLiteral("未严格单调")),
                                 QStringLiteral("|r| > 0.700 / 0.850 / 0.950 且 Top > Bottom")));
    return gates;
}

QString FactorBacktestResultContract::buildRatingSummary(const factor::FactorBacktestMetrics& metrics)
{
    switch (metrics.coreRating) {
    case factor::FactorBacktestMetrics::Rating::EXCELLENT:
        return QStringLiteral("已满足你定义的 5 个核心指标优秀标准：预测力、显著性、稳定性、方向胜率和分组单调性全部达标。");
    case factor::FactorBacktestMetrics::Rating::GOOD:
        return QStringLiteral("已满足你定义的 5 个核心指标良好标准，可以进入重点观察与组合验证。");
    case factor::FactorBacktestMetrics::Rating::PASS:
        return QStringLiteral("已满足你定义的 5 个核心指标合格标准，但还没有进入更高一档的稳定性和单调性区间。");
    case factor::FactorBacktestMetrics::Rating::FAIL:
    default:
        return QStringLiteral("未满足你定义的 5 个核心指标合格标准，只要任一核心项不达标就不能通过。");
    }
}

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
    const double canonicalMonotonicity = monotonicity(result);
    const double canonicalLongShortAnnualReturn = longShortAnnualReturn(result);
    const double canonicalLongShortMaxDrawdown = maxDrawdown(result);
    const double canonicalAnnualTurnover = turnoverRate(result);

    QVariantMap quality;
    quality[QStringLiteral("rankIcMean")] = metrics.rankIcMean;
    quality[QStringLiteral("rankIcStd")] = metrics.rankIcStd;
    quality[QStringLiteral("rankIcir")] = metrics.rankIcir;
    quality[QStringLiteral("icWinRate")] = metrics.icWinRate;
    quality[QStringLiteral("icPValue")] = metrics.icPValue;
    quality[QStringLiteral("monotonicityScore")] = canonicalMonotonicity;
    quality[QStringLiteral("longShortSharpe")] = metrics.longShortSharpe;
    quality[QStringLiteral("longShortAnnualReturn")] = canonicalLongShortAnnualReturn;
    quality[QStringLiteral("longShortMaxDrawdown")] = canonicalLongShortMaxDrawdown;
    quality[QStringLiteral("icHalfLife")] = metrics.icHalfLife;
    quality[QStringLiteral("annualTurnover")] = canonicalAnnualTurnover;
    quality[QStringLiteral("costAdjustedSharpe")] = metrics.costAdjustedSharpe;
    quality[QStringLiteral("alpha")] = metrics.alpha;
    quality[QStringLiteral("icTStat")] = metrics.icTStat;
    quality[QStringLiteral("monthlyWinRate")] = metrics.monthlyWinRate;
    quality[QStringLiteral("numGroups")] = metrics.numGroups;
    quality[QStringLiteral("coreRating")] = static_cast<int>(metrics.coreRating);
    quality[QStringLiteral("coreRatingMethod")] = QStringLiteral("rank_ic_mean_ic_p_value_rank_icir_ic_win_rate_monotonicity_score");
    quality[QStringLiteral("coreRatingTitle")] = QStringLiteral("核心评级");
    quality[QStringLiteral("coreRatingLabel")] = buildRatingLabel(metrics.coreRating);
    quality[QStringLiteral("coreRatingSummary")] = buildRatingSummary(metrics);
    quality[QStringLiteral("coreRatingChecks")] = buildRatingGates(metrics);
    quality[QStringLiteral("coreSection")] = buildSectionDescriptor(
        QStringLiteral("必须看"),
        QStringLiteral("当前页面按 5 个核心指标展示：Rank IC均值、IC p值、Rank ICIR、IC胜率和分组单调相关系数。"));
    quality[QStringLiteral("optionalSection")] = buildSectionDescriptor(
        QStringLiteral("重要参考"),
        QStringLiteral("研究多空年化是原始价差线性年化；执行复合年化和执行路径风险都基于风控后执行序列。"));
    quality[QStringLiteral("auxiliarySection")] = buildAuxiliarySectionDescriptor();
    quality[QStringLiteral("coreMetrics")] = buildCoreMetrics(metrics);
    quality[QStringLiteral("optionalMetrics")] = buildOptionalMetrics(result,
                                                                       canonicalLongShortAnnualReturn,
                                                                       canonicalLongShortMaxDrawdown,
                                                                       canonicalAnnualTurnover);
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
    execution[QStringLiteral("executionLagTradingDays")] = executionLagTradingDays(result);
    execution[QStringLiteral("signalDateSemantics")] = signalDateSemantics(result);
    execution[QStringLiteral("executionDateSemantics")] = executionDateSemantics(result);
    return execution;
}

QVariantMap FactorBacktestResultContract::buildIcMetrics(const factor::BacktestResult& result)
{
    QVariantMap ic;
    ic[QStringLiteral("value")] = icValue(result);
    ic[QStringLiteral("std")] = icStd(result);
    ic[QStringLiteral("ir")] = irValue(result);
    ic[QStringLiteral("positiveRate")] = icPositiveRate(result);
    ic[QStringLiteral("tStat")] = result.factorMetrics.icTStat;
    ic[QStringLiteral("pValue")] = result.factorMetrics.icPValue;
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
    return result.turnoverRate ;
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