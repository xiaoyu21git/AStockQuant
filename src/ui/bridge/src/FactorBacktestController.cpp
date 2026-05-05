#include "FactorBacktestController.h"

#include "../include/FactorBacktestPreflightUtils.h"
#include "../include/FactorRequirementInferenceUtils.h"
#include "../include/FactorBacktestWarmupUtils.h"
#include "../include/FactorInstanceResolutionUtils.h"
#include "../include/FactorService.h"
#include "../include/StrategyStructureResolvers.h"
#include "RiskConfigService.h"

#include "../include/DataServiceCache.h"
#include "../include/DatabaseConnectionManager.h"
#include "../../../cache/include/cache_facade.h"

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include "../../../domain/factor/include/ArrowMarketData.h"
#include "../../../domain/factor/include/DataAvailabilityChecker.h"
#include "../../../domain/factor/include/FactorBacktestExecutor.h"
#include "../../../domain/factor/include/FactorCacheManager.h"
#include "../../../domain/factor/include/FactorInstanceManager.h"
#include "foundation/thread/ThreadPoolExecutor.h"

#include <QDate>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonParseError>

#include <QMetaObject>
#include <QMutex>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSet>
#include <QStringList>
#include <QVariantMap>
#include <QTimer>

#include <future>
#include <chrono>
#include <cmath>
#include <map>
#include <stdexcept>
#include <thread>

namespace {

std::map<QString, QVariant> makePositionalParams(std::initializer_list<QVariant> values)
{
    std::map<QString, QVariant> params;
    for (const QVariant& value : values) {
        params.emplace(QString(), value);
    }
    return params;
}

std::map<QString, QVariant> makeNamedParams(std::initializer_list<std::pair<QString, QVariant>> values)
{
    std::map<QString, QVariant> params;
    for (const auto& item : values) {
        params.emplace(item.first, item.second);
    }
    return params;
}

QVariantList toVariantList(const std::vector<double>& values)
{
    QVariantList list;
    list.reserve(static_cast<int>(values.size()));
    for (double value : values) {
        list.append(value);
    }
    return list;
}

QSet<QString> loadTableColumns(const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                               const QString& tableName)
{
    QSet<QString> columns;
    if (!database || tableName.trimmed().isEmpty()) {
        return columns;
    }

    const auto result = database->executeQuery(
        "SELECT COLUMN_NAME AS column_name FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = :table_name",
        {{":table_name", tableName}});

    for (size_t index = 0; index < result.rowCount(); ++index) {
        columns.insert(result.getRow(index).getString("column_name").trimmed().toLower());
    }

    return columns;
}

double annualizationFactorForForwardDays(int forwardDays)
{
    return 252.0 / static_cast<double>((std::max)(1, forwardDays));
}

double calculateMean(const std::vector<double>& values)
{
    if (values.empty()) {
        return 0.0;
    }

    double sum = 0.0;
    for (double value : values) {
        sum += value;
    }
    return sum / static_cast<double>(values.size());
}

double calculateStdDev(const std::vector<double>& values, double mean)
{
    if (values.empty()) {
        return 0.0;
    }

    double variance = 0.0;
    for (double value : values) {
        const double diff = value - mean;
        variance += diff * diff;
    }

    variance /= static_cast<double>(values.size());
    return std::sqrt(variance);
}

double calculateGroupMonotonicity(const factor::BacktestResult& result)
{
    const auto& groupReturns = result.groupResult.groupReturns;
    if (groupReturns.size() < 2) {
        return 0.0;
    }

    std::vector<double> factorMidpoints;
    factorMidpoints.reserve(groupReturns.size());
    for (size_t index = 0; index < groupReturns.size(); ++index) {
        if (index < result.groupResult.minFactorValues.size()
                && index < result.groupResult.maxFactorValues.size()) {
            factorMidpoints.push_back((result.groupResult.minFactorValues[index]
                + result.groupResult.maxFactorValues[index]) / 2.0);
        } else {
            factorMidpoints.push_back(static_cast<double>(index + 1));
        }
    }

    const double meanX = calculateMean(factorMidpoints);
    const double meanY = calculateMean(groupReturns);
    double numerator = 0.0;
    double sumX2 = 0.0;
    double sumY2 = 0.0;
    for (size_t index = 0; index < groupReturns.size(); ++index) {
        const double centeredX = factorMidpoints[index] - meanX;
        const double centeredY = groupReturns[index] - meanY;
        numerator += centeredX * centeredY;
        sumX2 += centeredX * centeredX;
        sumY2 += centeredY * centeredY;
    }

    const double denominator = std::sqrt(sumX2 * sumY2);
    if (denominator <= 1e-12) {
        return 0.0;
    }

    return numerator / denominator;
}

double calculateGroupDiscrimination(const factor::BacktestResult& result)
{
    const auto& groupReturns = result.groupResult.groupReturns;
    if (groupReturns.empty()) {
        return 0.0;
    }

    return calculateStdDev(groupReturns, calculateMean(groupReturns));
}

void logBacktestGroupReturns(const QString& factorId, const factor::BacktestResult& result)
{
    const auto& groupReturns = result.groupResult.groupReturns;
    if (groupReturns.empty()) {
        qDebug() << "FactorBacktestController: 因子" << factorId << "没有可打印的分组收益率";
        return;
    }

    const int printCount = (std::min)(10, static_cast<int>(groupReturns.size()));
    qDebug().noquote() << QStringLiteral("FactorBacktestController: 因子 %1 分组收益率(前%2组):")
                              .arg(factorId.isEmpty() ? QStringLiteral("<unknown>") : factorId)
                              .arg(printCount);

    for (int index = 0; index < printCount; ++index) {
        qDebug().noquote() << QStringLiteral("  第%1组收益率: %2")
                                  .arg(index + 1)
                                  .arg(QString::number(groupReturns[static_cast<size_t>(index)], 'f', 6));
    }
}

QString buildSupportMapCacheKey(const QVariantList& factorIds,
                               const QString& startDate,
                               const QString& endDate,
                               const QString& dataSourceMode,
                               int selectedDatasetId,
                               const QVariantList& selectedStockPoolSymbols,
                               const QVariantMap& cacheSnapshot)
{
    QStringList factorTokens;
    factorTokens.reserve(factorIds.size());
    for (const QVariant& factorValue : factorIds) {
        const QString factorId = factorValue.toString().trimmed();
        if (!factorId.isEmpty()) {
            factorTokens.append(factorId);
        }
    }
    factorTokens.sort(Qt::CaseSensitive);

    QStringList stockPoolTokens;
    stockPoolTokens.reserve(selectedStockPoolSymbols.size());
    for (const QVariant& symbolValue : selectedStockPoolSymbols) {
        const QString symbol = symbolValue.toString().trimmed();
        if (!symbol.isEmpty()) {
            stockPoolTokens.append(symbol);
        }
    }
    stockPoolTokens.sort(Qt::CaseSensitive);
    const QString stockPoolKey = stockPoolTokens.join(QStringLiteral(","));

    QStringList availableFields;
    const QVariantList snapshotAvailableFields = cacheSnapshot.value(QStringLiteral("availableFields")).toList();
    availableFields.reserve(snapshotAvailableFields.size());
    for (const QVariant& fieldValue : snapshotAvailableFields) {
        const QString field = fieldValue.toString().trimmed();
        if (!field.isEmpty()) {
            availableFields.append(field);
        }
    }
    availableFields.sort(Qt::CaseSensitive);

    const QString snapshotKey = QStringLiteral("%1|%2")
        .arg(availableFields.join(QStringLiteral(",")))
        .arg(cacheSnapshot.value(QStringLiteral("tradeDateCount")).toInt());

    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
        .arg(dataSourceMode.trimmed(),
             startDate.trimmed(),
             endDate.trimmed(),
             QString::number(selectedDatasetId),
             factorTokens.join(QStringLiteral(",")),
             stockPoolKey,
             snapshotKey);
}

QString buildSupportMapScopeKey(const QString& startDate,
                                const QString& endDate,
                                const QString& dataSourceMode,
                                int selectedDatasetId,
                                const QVariantList& selectedStockPoolSymbols,
                                const QVariantMap& cacheSnapshot)
{
    QStringList stockPoolTokens;
    stockPoolTokens.reserve(selectedStockPoolSymbols.size());
    for (const QVariant& symbolValue : selectedStockPoolSymbols) {
        const QString symbol = symbolValue.toString().trimmed();
        if (!symbol.isEmpty()) {
            stockPoolTokens.append(symbol);
        }
    }
    stockPoolTokens.sort(Qt::CaseSensitive);

    const QString stockPoolKey = stockPoolTokens.join(QStringLiteral(","));

    QStringList availableFields;
    const QVariantList snapshotAvailableFields = cacheSnapshot.value(QStringLiteral("availableFields")).toList();
    availableFields.reserve(snapshotAvailableFields.size());
    for (const QVariant& fieldValue : snapshotAvailableFields) {
        const QString field = fieldValue.toString().trimmed();
        if (!field.isEmpty()) {
            availableFields.append(field);
        }
    }
    availableFields.sort(Qt::CaseSensitive);

    const QString snapshotKey = QStringLiteral("%1|%2")
        .arg(availableFields.join(QStringLiteral(",")))
        .arg(cacheSnapshot.value(QStringLiteral("tradeDateCount")).toInt());

    return QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(dataSourceMode.trimmed(),
             startDate.trimmed(),
             endDate.trimmed(),
             QString::number(selectedDatasetId),
             stockPoolKey,
             snapshotKey);
}

bool supportMapContainsAllFactors(const QVariantMap& supportMap, const QVariantList& factorIds)
{
    for (const QVariant& factorValue : factorIds) {
        const QString factorId = factorValue.toString().trimmed();
        if (factorId.isEmpty()) {
            continue;
        }
        if (!supportMap.contains(factorId)) {
            return false;
        }
    }
    return true;
}

} // namespace

struct MetricPersistenceQualificationThresholds {
    double minAbsIc{0.03};
    double minAbsIr{0.50};
    double minAnnualReturn{0.15};
    double minInformationRatio{1.0};
    double minProfitFactor{1.5};
    double maxDrawdown{0.20};
    double maxTurnoverRate{400.0};
    double minTopBottomSpread{0.03};
    bool requireMonotonicOrTopSignificant{true};
};

double normalizedPercentRate(const QVariant& value, double fallback);

QVariant firstConfiguredValueFromSources(const QList<QVariantMap>& sources,
                                         const QStringList& keys)
{
    for (const QVariantMap& source : sources) {
        for (const QString& key : keys) {
            if (!source.contains(key)) {
                continue;
            }

            const QVariant value = source.value(key);
            if (!value.isValid() || value.isNull()) {
                continue;
            }
            if (value.typeId() == QMetaType::QString && value.toString().trimmed().isEmpty()) {
                continue;
            }
            return value;
        }
    }

    return {};
}

double configuredDoubleValue(const QList<QVariantMap>& sources,
                             const QStringList& keys,
                             double fallback)
{
    const QVariant configured = firstConfiguredValueFromSources(sources, keys);
    if (!configured.isValid()) {
        return fallback;
    }

    bool ok = false;
    const double numeric = configured.toDouble(&ok);
    if (!ok || !std::isfinite(numeric)) {
        return fallback;
    }
    return numeric;
}

double configuredPercentRateValue(const QList<QVariantMap>& sources,
                                  const QStringList& keys,
                                  double fallback)
{
    const QVariant configured = firstConfiguredValueFromSources(sources, keys);
    if (!configured.isValid()) {
        return fallback;
    }
    return normalizedPercentRate(configured, fallback);
}

MetricPersistenceQualificationThresholds resolveMetricPersistenceThresholds(const QVariantMap& appliedRiskConfig)
{
    MetricPersistenceQualificationThresholds thresholds;

    const QVariantMap runtimeRuleDefaults = appliedRiskConfig.value(QStringLiteral("runtimeRuleDefaults")).toMap();
    const QVariantMap ruleProfile = runtimeRuleDefaults.value(QStringLiteral("ruleProfile")).toMap();
    const QVariantMap backtestAssumptions = runtimeRuleDefaults.value(QStringLiteral("backtestAssumptions")).toMap();
    const QVariantMap validation = runtimeRuleDefaults.value(QStringLiteral("validation")).toMap();

    const QList<QVariantMap> sources = {
        appliedRiskConfig,
        runtimeRuleDefaults,
        ruleProfile,
        backtestAssumptions,
        validation
    };

    thresholds.minAbsIc = configuredPercentRateValue(
        sources,
        {QStringLiteral("metricPersistenceMinAbsIc"),
         QStringLiteral("metric_persistence_min_abs_ic"),
         QStringLiteral("factorMetricMinAbsIc"),
         QStringLiteral("factor_metric_min_abs_ic"),
         QStringLiteral("metricPersistenceMinIc"),
         QStringLiteral("metric_persistence_min_ic"),
         QStringLiteral("factorMetricMinIc"),
         QStringLiteral("factor_metric_min_ic")},
        thresholds.minAbsIc);
    thresholds.minAbsIr = configuredDoubleValue(
        sources,
        {QStringLiteral("metricPersistenceMinAbsIr"),
         QStringLiteral("metric_persistence_min_abs_ir"),
         QStringLiteral("factorMetricMinAbsIr"),
         QStringLiteral("factor_metric_min_abs_ir"),
         QStringLiteral("metricPersistenceMinIr"),
         QStringLiteral("metric_persistence_min_ir"),
         QStringLiteral("factorMetricMinIr"),
         QStringLiteral("factor_metric_min_ir")},
        thresholds.minAbsIr);
    thresholds.minAnnualReturn = configuredPercentRateValue(
        sources,
        {QStringLiteral("metricPersistenceMinAnnualReturn"),
         QStringLiteral("metric_persistence_min_annual_return"),
         QStringLiteral("factorMetricMinAnnualReturn"),
         QStringLiteral("factor_metric_min_annual_return")},
        thresholds.minAnnualReturn);
    thresholds.minInformationRatio = configuredDoubleValue(
        sources,
        {QStringLiteral("metricPersistenceMinInformationRatio"),
         QStringLiteral("metric_persistence_min_information_ratio"),
         QStringLiteral("factorMetricMinInformationRatio"),
         QStringLiteral("factor_metric_min_information_ratio")},
        thresholds.minInformationRatio);
    thresholds.minProfitFactor = configuredDoubleValue(
        sources,
        {QStringLiteral("metricPersistenceMinProfitFactor"),
         QStringLiteral("metric_persistence_min_profit_factor"),
         QStringLiteral("factorMetricMinProfitFactor"),
         QStringLiteral("factor_metric_min_profit_factor")},
        thresholds.minProfitFactor);
    thresholds.maxDrawdown = configuredPercentRateValue(
        sources,
        {QStringLiteral("metricPersistenceMaxDrawdown"),
         QStringLiteral("metric_persistence_max_drawdown"),
         QStringLiteral("factorMetricMaxDrawdown"),
         QStringLiteral("factor_metric_max_drawdown")},
        thresholds.maxDrawdown);
    thresholds.maxTurnoverRate = configuredDoubleValue(
        sources,
        {QStringLiteral("metricPersistenceMaxTurnoverRate"),
         QStringLiteral("metric_persistence_max_turnover_rate"),
         QStringLiteral("factorMetricMaxTurnoverRate"),
         QStringLiteral("factor_metric_max_turnover_rate")},
        thresholds.maxTurnoverRate);
    thresholds.minTopBottomSpread = configuredPercentRateValue(
        sources,
        {QStringLiteral("metricPersistenceMinTopBottomSpread"),
         QStringLiteral("metric_persistence_min_top_bottom_spread"),
         QStringLiteral("factorMetricMinTopBottomSpread"),
         QStringLiteral("factor_metric_min_top_bottom_spread")},
        thresholds.minTopBottomSpread);

    thresholds.minAbsIc = (std::max)(0.0, thresholds.minAbsIc);
    thresholds.minAbsIr = (std::max)(0.0, thresholds.minAbsIr);
    thresholds.maxDrawdown = (std::max)(0.0, thresholds.maxDrawdown);
    thresholds.maxTurnoverRate = (std::max)(0.0, thresholds.maxTurnoverRate);
    thresholds.minTopBottomSpread = (std::max)(0.0, thresholds.minTopBottomSpread);

    return thresholds;
}

bool isStrictlyMonotonicDescending(const std::vector<double>& values)
{
    if (values.size() < 2) {
        return false;
    }

    for (size_t index = 1; index < values.size(); ++index) {
        if (!(values[index - 1] > values[index])) {
            return false;
        }
    }
    return true;
}

bool isQualifiedForMetricPersistence(const factor::BacktestResult& result,
                                     const MetricPersistenceQualificationThresholds& thresholds)
{
    // 实盘可用门槛：强调预测能力、稳定性、区分度与策略质量。
    if (std::abs(result.icirResult.icMean) <= thresholds.minAbsIc) {
        return false;
    }
    if (std::abs(result.icirResult.ir) <= thresholds.minAbsIr) {
        return false;
    }
    if (result.annualReturn <= thresholds.minAnnualReturn) {
        return false;
    }
    if (result.informationRatio <= thresholds.minInformationRatio) {
        return false;
    }
    if (result.profitFactor <= thresholds.minProfitFactor) {
        return false;
    }
    if (result.maxDrawdown > thresholds.maxDrawdown) {
        return false;
    }
    if (result.turnoverRate > thresholds.maxTurnoverRate) {
        return false;
    }
    if (thresholds.requireMonotonicOrTopSignificant) {
        const bool monotonic = isStrictlyMonotonicDescending(result.groupResult.groupReturns);
        const bool topSignificant = result.groupResult.longShortReturn >= thresholds.minTopBottomSpread;
        if (!monotonic && !topSignificant) {
            return false;
        }
    }
    return true;
}

double normalizedPercentRate(const QVariant& value, double fallback = 0.0)
{
    bool ok = false;
    const double numeric = value.toDouble(&ok);
    if (!ok) {
        return fallback;
    }
    return numeric > 1.0 ? numeric / 100.0 : numeric;
}

double normalizeBacktestAssumptionRate(const QVariant& rawValue,
                                       double normalizedValue,
                                       double defaultValue,
                                       double reasonableUpperBound,
                                       const char* fieldName)
{
    double value = std::isfinite(normalizedValue) ? normalizedValue : defaultValue;
    bool normalizedByFractionHeuristic = false;

    bool rawOk = false;
    const double rawNumeric = rawValue.toDouble(&rawOk);
    if (rawOk && std::isfinite(rawNumeric) && rawNumeric > reasonableUpperBound && rawNumeric <= 1.0) {
        value = rawNumeric / 100.0;
        normalizedByFractionHeuristic = true;
    }

    if (!std::isfinite(value) || value < 0.0) {
        return defaultValue;
    }

    if (normalizedByFractionHeuristic) {
        qWarning() << "FactorBacktestController: 检测到" << fieldName
                   << "可能使用了百分数口径(0~1)，按百分比自动归一化: raw=" << rawNumeric
                   << "normalized=" << value;
    }

    return value;
}

double normalizedWinRateRatio(double value)
{
    if (!std::isfinite(value)) {
        return 0.0;
    }

    // 兼容历史百分比口径（0~100）
    if (std::abs(value) > 1.0) {
        value /= 100.0;
    }

    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

QVariantMap loadAppliedRiskConfiguration()
{
    if (auto* service = RiskConfigService::instance()) {
        return service->loadAppliedConfiguration();
    }
    return {};
}

QVariantMap loadCurrentRiskConfiguration()
{
    if (auto* service = RiskConfigService::instance()) {
        return service->loadCurrentConfiguration();
    }
    return {};
}

QVariantMap mergeRiskConfigurations(const QVariantMap& baseConfiguration,
                                    const QVariantMap& overrideConfiguration)
{
    QVariantMap merged = baseConfiguration;
    for (auto it = overrideConfiguration.begin(); it != overrideConfiguration.end(); ++it) {
        merged.insert(it.key(), it.value());
    }
    return merged;
}

QVariant firstConfiguredValue(const QVariantMap& map, const QStringList& keys)
{
    for (const QString& key : keys) {
        if (!map.contains(key)) {
            continue;
        }

        const QVariant value = map.value(key);
        if (!value.isValid() || value.isNull()) {
            continue;
        }
        if (value.typeId() == QMetaType::QString && value.toString().trimmed().isEmpty()) {
            continue;
        }
        return value;
    }
    return {};
}

QVariantMap defaultBacktestRuntimeParams()
{
    QVariantMap defaults;
    defaults[QStringLiteral("forwardDays")] = 1;
    defaults[QStringLiteral("rebalanceDays")] = 1;
    defaults[QStringLiteral("transactionCost")] = 0.001;
    defaults[QStringLiteral("slippageRate")] = 0.0;
    defaults[QStringLiteral("riskFreeRate")] = 0.0;
    defaults[QStringLiteral("benchmarkSymbol")] = QStringLiteral("000300.SH");
    return defaults;
}

int normalizedPositiveInt(const QVariant& value, int fallback)
{
    bool ok = false;
    const int numeric = value.toInt(&ok);
    if (!ok || numeric <= 0) {
        return fallback;
    }
    return numeric;
}

QString normalizedBenchmarkSymbol(const QVariant& value)
{
    const QString symbol = value.toString().trimmed().toUpper();
    return symbol.isEmpty() ? QStringLiteral("000300.SH") : symbol;
}

QString normalizeDataSourceMode(const QString& rawMode)
{
    const QString mode = rawMode.trimmed().toLower();
    if (mode == "cache") {
        return "cache";
    }
    if (mode == "database") {
        return "database";
    }
    return {};
}

QString normalizeTradeDateText(const QString& rawDateText)
{
    const QString trimmed = rawDateText.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QDateTime isoDateTime = QDateTime::fromString(trimmed, Qt::ISODate);
    if (isoDateTime.isValid()) {
        return isoDateTime.date().toString("yyyy-MM-dd");
    }

    const QStringList dateTimeFormats = {
        QStringLiteral("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("yyyy/MM/dd HH:mm:ss"),
        QStringLiteral("yyyy-MM-ddTHH:mm:ss"),
        QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz")
    };
    for (const QString& format : dateTimeFormats) {
        const QDateTime dateTime = QDateTime::fromString(trimmed, format);
        if (dateTime.isValid()) {
            return dateTime.date().toString("yyyy-MM-dd");
        }
    }

    const QStringList dateFormats = {
        QStringLiteral("yyyy-MM-dd"),
        QStringLiteral("yyyy/MM/dd")
    };
    for (const QString& format : dateFormats) {
        const QDate date = QDate::fromString(trimmed, format);
        if (date.isValid()) {
            return date.toString("yyyy-MM-dd");
        }
    }

    const int firstSpace = trimmed.indexOf(' ');
    if (firstSpace > 0) {
        const QDate date = QDate::fromString(trimmed.left(firstSpace), "yyyy-MM-dd");
        if (date.isValid()) {
            return date.toString("yyyy-MM-dd");
        }
    }

    return trimmed;
}

QStringList normalizeStockPoolSymbols(const QVariant& rawValue)
{
    QStringList rawList;
    if (rawValue.canConvert<QVariantList>()) {
        const QVariantList variantList = rawValue.toList();
        rawList.reserve(variantList.size());
        for (const QVariant& rawItem : variantList) {
            rawList.append(rawItem.toString());
        }
    } else if (rawValue.isValid() && !rawValue.isNull()) {
        rawList = rawValue.toString().split(QRegularExpression(QStringLiteral("[,;\\s，；]+")), Qt::SkipEmptyParts);
    }

    QStringList normalized;
    QSet<QString> seenSymbols;
    for (const QString& rawItem : rawList) {
        const QString symbol = rawItem.trimmed().toUpper();
        if (symbol.isEmpty() || seenSymbols.contains(symbol)) {
            continue;
        }
        seenSymbols.insert(symbol);
        normalized.append(symbol);
    }

    return normalized;
}

bool isLatestBacktestDataset(const DataServiceCache::DataSetInfo& info)
{
    if (info.id <= 0) {
        return false;
    }

    if (info.isBacktestReady) {
        return true;
    }

    if (info.tags.contains("daily_bar_full_v2") && info.tags.contains("factor_backtest_ready")) {
        return true;
    }

    // 兼容非完整日线但字段齐备的清洗缓存集，让财务/舆情/另类字段可进入统一预检。
    return info.sourceType.contains("cleaning", Qt::CaseInsensitive)
        && !info.availableFields.isEmpty()
        && !info.stockCodes.isEmpty();
}

struct FactorWarmupRequirement {
    QStringList requiredFields;
    QStringList optionalFields;
    int minDataPoints{0};
    int skipRecent{0};
};

QStringList jsonArrayToStringList(const QJsonValue& value)
{
    QStringList values;
    const QJsonArray array = value.toArray();
    values.reserve(array.size());
    for (const QJsonValue& item : array) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty()) {
            values.append(text);
        }
    }
    values.removeDuplicates();
    return values;
}

QString normalizeWarmupFieldName(const QString& rawField)
{
    return factor::bridge::normalizeRequirementFieldName(rawField);
}

QStringList normalizeWarmupFields(const QStringList& fields)
{
    QStringList normalized;
    for (const QString& rawField : fields) {
        const QString field = normalizeWarmupFieldName(rawField);
        if (field.isEmpty() || normalized.contains(field)) {
            continue;
        }
        normalized.append(field);
    }
    return normalized;
}

int requiredWarmupTradingDays(const FactorWarmupRequirement& requirement)
{
    return factor::warmup::requiredWarmupTradingDays(requirement.minDataPoints, requirement.skipRecent);
}

QStringList loadHistoricalTradeDates(const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                                     const QDate& anchorStartDate,
                                     const QStringList& stockCodes)
{
    QStringList tradeDates;
    if (!database || !anchorStartDate.isValid() || stockCodes.isEmpty()) {
        return tradeDates;
    }

    QStringList symbolPlaceholders;
    std::map<QString, QVariant> params{{":anchorStartDate", anchorStartDate.toString("yyyy-MM-dd")}};
    for (int index = 0; index < stockCodes.size(); ++index) {
        const QString placeholder = QString(":tradeDateSymbol%1").arg(index);
        symbolPlaceholders.append(placeholder);
        params.emplace(placeholder, stockCodes.at(index).trimmed());
    }

    const QString sql = QString(
        "SELECT DISTINCT trade_date FROM daily_bar "
        "WHERE trade_date < :anchorStartDate AND close > 0 AND symbol IN (%1) "
        "ORDER BY trade_date ASC"
    ).arg(symbolPlaceholders.join(", "));

    const auto result = database->executeQuery(sql, params);
    tradeDates.reserve(static_cast<int>(result.rowCount()));
    for (size_t rowIndex = 0; rowIndex < result.rowCount(); ++rowIndex) {
        const QString tradeDate = normalizeTradeDateText(result.getRow(rowIndex).getString("trade_date"));
        if (!tradeDate.isEmpty()) {
            tradeDates.append(tradeDate);
        }
    }
    tradeDates.removeDuplicates();
    return tradeDates;
}

FactorWarmupRequirement loadWarmupRequirementFromConfigText(const QString& configText,
                                                           const QString& instanceId)
{
    FactorWarmupRequirement requirement;
    if (instanceId.trimmed().isEmpty() || configText.trimmed().isEmpty()) {
        return requirement;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(configText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "FactorBacktestController: 解析因子配置失败" << instanceId << parseError.errorString();
        return requirement;
    }

    const QJsonObject config = doc.object();
    const QJsonObject calculation = config.value("calculation").toObject();
    const QJsonObject boundaryRules = config.value(QStringLiteral("boundaryRules")).toObject();
    const QJsonObject dataRequirements = config.value(QStringLiteral("dataRequirements")).toObject();

    requirement.requiredFields = jsonArrayToStringList(dataRequirements.value("required"));
    requirement.optionalFields = jsonArrayToStringList(dataRequirements.value("optional"));
    requirement.minDataPoints = boundaryRules.value("minDataPoints").toInt(
        calculation.value("window").toInt(
            calculation.value("lookbackPeriod").toInt(0)
        )
    );
    requirement.skipRecent = calculation.value("skipRecent").toInt(0);

    if (!requirement.requiredFields.contains("close")) {
        requirement.requiredFields.append("close");
    }
    requirement.requiredFields = normalizeWarmupFields(requirement.requiredFields);
    requirement.optionalFields.removeAll("close");
    requirement.optionalFields = normalizeWarmupFields(requirement.optionalFields);
    return requirement;
}

FactorWarmupRequirement loadWarmupRequirement(const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                                             const QString& instanceId)
{
    FactorWarmupRequirement requirement;
    if (!database || instanceId.trimmed().isEmpty()) {
        return requirement;
    }

    const auto result = database->executeQuery(
        "SELECT CAST(full_config AS CHAR) AS full_config FROM factor_instance WHERE instance_id = :instanceId LIMIT 1",
        makeNamedParams({{"instanceId", instanceId}})
    );
    if (result.isEmpty()) {
        return requirement;
    }

    return loadWarmupRequirementFromConfigText(result.getRow(0).getString("full_config"), instanceId);
}

FactorWarmupRequirement loadWarmupRequirement(const factor::FactorInstanceInfo& instanceInfo)
{
    return loadWarmupRequirementFromConfigText(
        QString::fromStdString(instanceInfo.config.toString()),
        QString::fromStdString(instanceInfo.instanceId));
}

FactorWarmupRequirement loadWarmupRequirement(const factor::FactorInstanceInfo& instanceInfo,
                                             const std::shared_ptr<factor::BaseFactor>& factorInstance)
{
    FactorWarmupRequirement requirement = loadWarmupRequirement(instanceInfo);
    if (!factorInstance) {
        return requirement;
    }

    requirement.requiredFields.clear();
    requirement.optionalFields.clear();

    const factor::DataRequirements dataRequirements = factorInstance->getDataRequirements();
    for (const std::string& rawField : dataRequirements.requiredFields) {
        const QString field = normalizeWarmupFieldName(QString::fromStdString(rawField));
        if (!field.isEmpty() && !requirement.requiredFields.contains(field)) {
            requirement.requiredFields.append(field);
        }
    }
    for (const std::string& rawField : dataRequirements.optionalFields) {
        const QString field = normalizeWarmupFieldName(QString::fromStdString(rawField));
        if (!field.isEmpty() && !requirement.optionalFields.contains(field)) {
            requirement.optionalFields.append(field);
        }
    }

    requirement.minDataPoints = factorInstance->getBoundaryRules().minDataPoints;
    if (!requirement.requiredFields.contains(QStringLiteral("close"))) {
        requirement.requiredFields.append(QStringLiteral("close"));
    }
    requirement.requiredFields = normalizeWarmupFields(requirement.requiredFields);
    requirement.optionalFields.removeAll(QStringLiteral("close"));
    requirement.optionalFields = normalizeWarmupFields(requirement.optionalFields);
    return requirement;
}

QStringList buildWarmupFieldList(const FactorWarmupRequirement& requirement)
{
    QStringList fields;
    auto appendField = [&fields](const QString& rawField) {
        const QString field = normalizeWarmupFieldName(rawField);
        if (field.isEmpty()) {
            return;
        }
        if (!fields.contains(field)) {
            fields.append(field);
        }
    };

    for (const QString& field : requirement.requiredFields) {
        appendField(field);
    }
    for (const QString& field : requirement.optionalFields) {
        appendField(field);
    }
    return fields;
}

size_t appendWindowWarmupRows(factor::ArrowMarketData::Builder& builder,
                              const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                              const QString& effectiveStartDate,
                              const QStringList& datasetStockCodes,
                              const QString& resolvedInstanceId,
                              const QDate& anchorStartDate,
                              const FactorWarmupRequirement& requirement)
{
    if (!database || effectiveStartDate.trimmed().isEmpty()) {
        return 0;
    }

    if (requirement.minDataPoints <= 1 && requirement.skipRecent <= 0) {
        return 0;
    }

    const int lookbackTradingDays = requiredWarmupTradingDays(requirement);
    if (lookbackTradingDays <= 0) {
        return 0;
    }

    const QDate configuredStartDate = QDate::fromString(effectiveStartDate, "yyyy-MM-dd");
    if (!configuredStartDate.isValid()) {
        return 0;
    }

    QStringList stockCodes = datasetStockCodes;
    stockCodes.removeDuplicates();
    if (stockCodes.isEmpty()) {
        return 0;
    }

    const QStringList warmupFields = buildWarmupFieldList(requirement);
    if (warmupFields.isEmpty()) {
        return 0;
    }

    const QDate resolvedAnchorStartDate = anchorStartDate.isValid() ? anchorStartDate : configuredStartDate;

    QString historyStartDate;
    const QStringList historicalTradeDates = loadHistoricalTradeDates(database, resolvedAnchorStartDate, stockCodes);
    const QDate preciseHistoryStartDate = factor::warmup::resolveWarmupHistoryStartDate(
        resolvedAnchorStartDate,
        historicalTradeDates,
        lookbackTradingDays);
    if (preciseHistoryStartDate.isValid()) {
        historyStartDate = preciseHistoryStartDate.toString("yyyy-MM-dd");
    } else {
        const int lookbackCalendarDays = factor::warmup::fallbackWarmupCalendarLookbackDays(lookbackTradingDays);
        historyStartDate = resolvedAnchorStartDate.addDays(-lookbackCalendarDays).toString("yyyy-MM-dd");
    }
    const QString historyEndDate = resolvedAnchorStartDate.addDays(-1).toString("yyyy-MM-dd");

    QStringList symbolPlaceholders;
    std::map<QString, QVariant> params{
        {":historyStartDate", historyStartDate},
        {":historyEndDate", historyEndDate}
    };
    for (int index = 0; index < stockCodes.size(); ++index) {
        const QString placeholder = QString(":symbol%1").arg(index);
        symbolPlaceholders.append(placeholder);
        params.emplace(placeholder, stockCodes.at(index).trimmed());
    }

    QStringList selectedColumns;
    const QSet<QString> dailyBarColumns = loadTableColumns(database, QStringLiteral("daily_bar"));
    selectedColumns.append("symbol");
    selectedColumns.append("trade_date");
    for (const QString& field : warmupFields) {
        if (field.trimmed().isEmpty()) {
            continue;
        }

        const QString selectExpression = factor::warmup::buildDailyBarSelectExpression(field, dailyBarColumns);
        if (selectExpression.isEmpty()) {
            throw std::runtime_error(
                QStringLiteral("daily_bar 缺少字段 %1 对应的预热列，adj_factor 需要 post_adjust_factor").arg(field).toStdString());
        }
        selectedColumns.append(selectExpression);
    }
    selectedColumns.removeDuplicates();

    const QString sql = QString(
        "SELECT %1 FROM daily_bar "
        "WHERE trade_date >= :historyStartDate AND trade_date <= :historyEndDate "
        "AND close > 0 AND symbol IN (%2) "
        "ORDER BY symbol ASC, trade_date ASC"
    ).arg(selectedColumns.join(", "), symbolPlaceholders.join(", "));

    const auto result = database->executeQuery(sql, params);
    if (result.isEmpty()) {
        qDebug() << "FactorBacktestController: 窗口因子缓存回测未补到预热历史"
                 << "instanceId=" << resolvedInstanceId
                 << "minDataPoints=" << requirement.minDataPoints
                 << "fields=" << warmupFields;
        return 0;
    }

    size_t appendedCount = 0;
    const size_t extraNumericFieldCount = warmupFields.size() > 1
        ? static_cast<size_t>(warmupFields.size() - 1)
        : 0U;
    for (size_t rowIndex = 0; rowIndex < result.rowCount(); ++rowIndex) {
        const auto& row = result.getRow(rowIndex);
        const QString symbol = row.getString("symbol").trimmed();
        const QString tradeDate = normalizeTradeDateText(row.getString("trade_date"));
        const double close = row.getDouble("close");
        if (symbol.isEmpty() || tradeDate.isEmpty() || !std::isfinite(close) || close <= 0.0) {
            continue;
        }

        std::unordered_map<std::string, double> numericFields;
        if (extraNumericFieldCount > 0) {
            numericFields.reserve(extraNumericFieldCount);
        }
        for (const QString& field : warmupFields) {
            if (field == QStringLiteral("close")) {
                continue;
            }
            const double numericValue = row.getDouble(field, std::numeric_limits<double>::quiet_NaN());
            if (!std::isfinite(numericValue)) {
                continue;
            }
            numericFields.emplace(field.toStdString(), numericValue);
        }
        if (builder.appendRow(symbol.toStdString(), tradeDate.toStdString(), close, numericFields)) {
            ++appendedCount;
        }
    }

    qDebug() << "FactorBacktestController: 窗口因子缓存回测追加预热历史"
             << "instanceId=" << resolvedInstanceId
             << "historyUsageStartAt=" << resolvedAnchorStartDate.toString("yyyy-MM-dd")
             << "firstHistoryTradeDate=" << historyStartDate
             << "anchorStartDate=" << resolvedAnchorStartDate.toString("yyyy-MM-dd")
             << "realStockCount=" << stockCodes.size()
             << "minDataPoints=" << requirement.minDataPoints
             << "skipRecent=" << requirement.skipRecent
             << "fields=" << warmupFields
             << "historyRows=" << static_cast<qulonglong>(appendedCount);

    return appendedCount;
}

QStringList normalizeSupportFields(const std::vector<std::string>& fields)
{
    QStringList normalized;
    for (const std::string& rawField : fields) {
        const QString field = factor::bridge::normalizeRequirementFieldName(QString::fromStdString(rawField));
        if (!field.isEmpty() && !normalized.contains(field)) {
            normalized.append(field);
        }
    }
    return normalized;
}

QStringList normalizeSupportFields(const QStringList& fields)
{
    QStringList normalized;
    for (const QString& rawField : fields) {
        const QString field = factor::bridge::normalizeRequirementFieldName(rawField);
        if (!field.isEmpty() && !normalized.contains(field)) {
            normalized.append(field);
        }
    }
    return normalized;
}

bool fieldRequiresPositiveValues(const QString& rawField)
{
    static const QSet<QString> positiveFields = {
        QStringLiteral("open"),
        QStringLiteral("high"),
        QStringLiteral("low"),
        QStringLiteral("close"),
        QStringLiteral("adj_close"),
        QStringLiteral("adj_factor"),
        QStringLiteral("volume"),
        QStringLiteral("turnover"),
        QStringLiteral("pe_ratio"),
        QStringLiteral("pb_ratio"),
        QStringLiteral("market_cap"),
        QStringLiteral("circulating_market_cap"),
        QStringLiteral("policy_strength"),
        QStringLiteral("policy_count"),
        QStringLiteral("popularity_score"),
        QStringLiteral("comment_count"),
        QStringLiteral("futures_close"),
        QStringLiteral("futures_volume"),
        QStringLiteral("open_interest")
    };
    return positiveFields.contains(rawField.trimmed().toLower());
}

QString datasetTradeDate(const QVariantMap& row)
{
    QString tradeDate = normalizeTradeDateText(row.value(QStringLiteral("trade_date")).toString());
    if (tradeDate.isEmpty()) {
        tradeDate = normalizeTradeDateText(row.value(QStringLiteral("date")).toString());
    }
    return tradeDate;
}

QVariantList toVariantList(const QStringList& values)
{
    QVariantList list;
    list.reserve(values.size());
    for (const QString& value : values) {
        list.append(value);
    }
    return list;
}

bool shouldFilterFactorOnSelection(const QString& rawCategory)
{
    const QString category = rawCategory.trimmed().toLower();
    static const QSet<QString> hardUnsupportedCategories = {
        QStringLiteral("instance-missing"),
        QStringLiteral("instance-create-failed"),
        QStringLiteral("unsupported-metric"),
        QStringLiteral("unsupported-type"),
        QStringLiteral("proxy-only-runtime"),
        QStringLiteral("data-unavailable"),
        QStringLiteral("missing-field"),
        QStringLiteral("missing-field-value"),
        QStringLiteral("invalid-field-value"),
        QStringLiteral("insufficient-history")
    };
    return hardUnsupportedCategories.contains(category);
}

QString normalizePreflightCategory(const QString& category)
{
    return category.trimmed().toLower();
}

QVariantMap buildValidationStateMap(const QString& statusKey,
                                    const QString& statusText,
                                    const QString& reason,
                                    const QString& detail,
                                    const QString& accentColor)
{
    QVariantMap state;
    state[QStringLiteral("statusKey")] = statusKey;
    state[QStringLiteral("statusText")] = statusText;
    state[QStringLiteral("reason")] = reason;
    state[QStringLiteral("detail")] = detail;
    state[QStringLiteral("accentColor")] = accentColor;
    return state;
}

QVariantMap buildPreflightCategoryMetaMap(const QString& rawCategory)
{
    const QString category = normalizePreflightCategory(rawCategory);

    QVariantMap meta;
    meta[QStringLiteral("key")] = category.isEmpty() ? QStringLiteral("precheck-failed") : category;
    meta[QStringLiteral("statusText")] = QStringLiteral("预检失败");
    meta[QStringLiteral("shortText")] = QStringLiteral("预检失败");
    meta[QStringLiteral("detail")] = QStringLiteral("当前未通过统一支持校验，暂时不能进入回测执行阶段。");
    meta[QStringLiteral("accentColor")] = QStringLiteral("#F59E0B");
    meta[QStringLiteral("chipBackground")] = QStringLiteral("#3F2D16");
    meta[QStringLiteral("chipBorder")] = QStringLiteral("#D97706");
    meta[QStringLiteral("chipText")] = QStringLiteral("#FDE68A");

    if (category == QStringLiteral("runtime-init-failed")) {
        meta[QStringLiteral("statusText")] = QStringLiteral("运行时初始化失败");
        meta[QStringLiteral("shortText")] = QStringLiteral("运行时异常");
        meta[QStringLiteral("detail")] = QStringLiteral("回测运行时没有初始化成功，本次无法判断因子支持性。");
        meta[QStringLiteral("accentColor")] = QStringLiteral("#F87171");
        meta[QStringLiteral("chipBackground")] = QStringLiteral("#3F1D24");
        meta[QStringLiteral("chipBorder")] = QStringLiteral("#DC2626");
        meta[QStringLiteral("chipText")] = QStringLiteral("#FECACA");
    } else if (category == QStringLiteral("instance-missing")) {
        meta[QStringLiteral("statusText")] = QStringLiteral("实例未解析");
        meta[QStringLiteral("shortText")] = QStringLiteral("实例缺失");
        meta[QStringLiteral("detail")] = QStringLiteral("没有找到可执行实例，请先检查 factor_instance 同步状态和实例绑定。");
        meta[QStringLiteral("accentColor")] = QStringLiteral("#F87171");
        meta[QStringLiteral("chipBackground")] = QStringLiteral("#3F1D24");
        meta[QStringLiteral("chipBorder")] = QStringLiteral("#DC2626");
        meta[QStringLiteral("chipText")] = QStringLiteral("#FECACA");
    } else if (category == QStringLiteral("instance-create-failed")) {
        meta[QStringLiteral("statusText")] = QStringLiteral("实例创建失败");
        meta[QStringLiteral("shortText")] = QStringLiteral("实例异常");
        meta[QStringLiteral("detail")] = QStringLiteral("实例创建阶段失败，通常是实例配置、注册信息或参数不完整。");
        meta[QStringLiteral("accentColor")] = QStringLiteral("#F87171");
        meta[QStringLiteral("chipBackground")] = QStringLiteral("#3F1D24");
        meta[QStringLiteral("chipBorder")] = QStringLiteral("#DC2626");
        meta[QStringLiteral("chipText")] = QStringLiteral("#FECACA");
    } else if (category == QStringLiteral("unsupported-type")) {
        meta[QStringLiteral("statusText")] = QStringLiteral("因子类型未接入");
        meta[QStringLiteral("shortText")] = QStringLiteral("类型未接入");
        meta[QStringLiteral("detail")] = QStringLiteral("当前运行时还没有接入该因子类型的回测执行链路。");
        meta[QStringLiteral("accentColor")] = QStringLiteral("#FB923C");
        meta[QStringLiteral("chipBackground")] = QStringLiteral("#3F2A17");
        meta[QStringLiteral("chipBorder")] = QStringLiteral("#EA580C");
        meta[QStringLiteral("chipText")] = QStringLiteral("#FED7AA");
    } else if (category == QStringLiteral("unsupported-metric")) {
        meta[QStringLiteral("statusText")] = QStringLiteral("指标未接入");
        meta[QStringLiteral("shortText")] = QStringLiteral("指标未接入");
        meta[QStringLiteral("detail")] = QStringLiteral("该因子当前选择的指标没有对应的回测实现。");
        meta[QStringLiteral("accentColor")] = QStringLiteral("#FB923C");
        meta[QStringLiteral("chipBackground")] = QStringLiteral("#3F2A17");
        meta[QStringLiteral("chipBorder")] = QStringLiteral("#EA580C");
        meta[QStringLiteral("chipText")] = QStringLiteral("#FED7AA");
    } else if (category == QStringLiteral("dataset-missing")) {
        meta[QStringLiteral("statusText")] = QStringLiteral("未选择缓存集");
        meta[QStringLiteral("shortText")] = QStringLiteral("未选缓存集");
        meta[QStringLiteral("detail")] = QStringLiteral("当前是缓存模式，但还没有选中可回测缓存集。");
        meta[QStringLiteral("accentColor")] = QStringLiteral("#94A3B8");
        meta[QStringLiteral("chipBackground")] = QStringLiteral("#1E293B");
        meta[QStringLiteral("chipBorder")] = QStringLiteral("#475569");
        meta[QStringLiteral("chipText")] = QStringLiteral("#CBD5E1");
    } else if (category == QStringLiteral("dataset-invalid")) {
        meta[QStringLiteral("statusText")] = QStringLiteral("缓存集无效");
        meta[QStringLiteral("shortText")] = QStringLiteral("缓存集无效");
        meta[QStringLiteral("detail")] = QStringLiteral("选中的缓存集缺少必要元数据，或者时间范围与内容不完整。");
    } else if (category == QStringLiteral("dataset-empty")) {
        meta[QStringLiteral("statusText")] = QStringLiteral("缓存集为空");
        meta[QStringLiteral("shortText")] = QStringLiteral("缓存为空");
        meta[QStringLiteral("detail")] = QStringLiteral("选中的缓存集没有可用于回测的股票或交易日样本。");
    } else if (category == QStringLiteral("stock-pool-mismatch")) {
        meta[QStringLiteral("statusText")] = QStringLiteral("股票池不匹配");
        meta[QStringLiteral("shortText")] = QStringLiteral("股票池不匹配");
        meta[QStringLiteral("detail")] = QStringLiteral("缓存集股票池与当前回测股票池没有有效重合，无法计算该因子。");
        meta[QStringLiteral("accentColor")] = QStringLiteral("#FB7185");
        meta[QStringLiteral("chipBackground")] = QStringLiteral("#3F1D24");
        meta[QStringLiteral("chipBorder")] = QStringLiteral("#E11D48");
        meta[QStringLiteral("chipText")] = QStringLiteral("#FECDD3");
    } else if (category == QStringLiteral("dataset-fields-missing") || category == QStringLiteral("missing-field")) {
        meta[QStringLiteral("statusText")] = QStringLiteral("缓存字段缺失");
        meta[QStringLiteral("shortText")] = QStringLiteral("字段缺失");
        meta[QStringLiteral("detail")] = QStringLiteral("缓存集中没有提供该因子计算所需的基础字段。");
    } else if (category == QStringLiteral("missing-field-value")) {
        meta[QStringLiteral("statusText")] = QStringLiteral("字段值为空");
        meta[QStringLiteral("shortText")] = QStringLiteral("字段值为空");
        meta[QStringLiteral("detail")] = QStringLiteral("字段本身存在，但最近交易日没有可用的非空值。");
        meta[QStringLiteral("accentColor")] = QStringLiteral("#FBBF24");
        meta[QStringLiteral("chipBackground")] = QStringLiteral("#3F3518");
        meta[QStringLiteral("chipBorder")] = QStringLiteral("#CA8A04");
        meta[QStringLiteral("chipText")] = QStringLiteral("#FEF08A");
    } else if (category == QStringLiteral("invalid-field-value")) {
        meta[QStringLiteral("statusText")] = QStringLiteral("字段值无效");
        meta[QStringLiteral("shortText")] = QStringLiteral("字段值无效");
        meta[QStringLiteral("detail")] = QStringLiteral("字段存在，但最近交易日的值全部为 0 或非正数，无法参与计算。");
        meta[QStringLiteral("accentColor")] = QStringLiteral("#FBBF24");
        meta[QStringLiteral("chipBackground")] = QStringLiteral("#3F3518");
        meta[QStringLiteral("chipBorder")] = QStringLiteral("#CA8A04");
        meta[QStringLiteral("chipText")] = QStringLiteral("#FEF08A");
    } else if (category == QStringLiteral("insufficient-history")) {
        meta[QStringLiteral("statusText")] = QStringLiteral("历史样本不足");
        meta[QStringLiteral("shortText")] = QStringLiteral("样本不足");
        meta[QStringLiteral("detail")] = QStringLiteral("结合预热窗口后，可用交易日仍不足以稳定计算该因子。");
        meta[QStringLiteral("accentColor")] = QStringLiteral("#FACC15");
        meta[QStringLiteral("chipBackground")] = QStringLiteral("#3F3518");
        meta[QStringLiteral("chipBorder")] = QStringLiteral("#CA8A04");
        meta[QStringLiteral("chipText")] = QStringLiteral("#FEF08A");
    } else if (category == QStringLiteral("data-unavailable")) {
        meta[QStringLiteral("statusText")] = QStringLiteral("底层数据不可用");
        meta[QStringLiteral("shortText")] = QStringLiteral("底层数据不可用");
        meta[QStringLiteral("detail")] = QStringLiteral("底层数据库中该因子所需数据不可用，或者数据校验没有通过。");
    } else if (category == QStringLiteral("supported")) {
        meta[QStringLiteral("statusText")] = QStringLiteral("可执行");
        meta[QStringLiteral("shortText")] = QStringLiteral("可执行");
        meta[QStringLiteral("detail")] = QStringLiteral("当前已经通过统一支持校验，可以进入回测执行阶段。");
        meta[QStringLiteral("accentColor")] = QStringLiteral("#22C55E");
        meta[QStringLiteral("chipBackground")] = QStringLiteral("#133226");
        meta[QStringLiteral("chipBorder")] = QStringLiteral("#16A34A");
        meta[QStringLiteral("chipText")] = QStringLiteral("#BBF7D0");
    }

    return meta;
}

QVariantMap findPreflightFailure(const QVariantList& preflightFailures, const QString& factorId)
{
    const QString normalizedFactorId = factorId.trimmed();
    if (normalizedFactorId.isEmpty()) {
        return {};
    }

    for (const QVariant& failureValue : preflightFailures) {
        const QVariantMap failure = failureValue.toMap();
        if (failure.value(QStringLiteral("factorId")).toString().trimmed() == normalizedFactorId) {
            return failure;
        }
    }

    return {};
}

QVariantMap findDisplayedBacktestResult(const QVariantMap& backtestResult, const QString& factorId)
{
    const QString normalizedFactorId = factorId.trimmed();
    if (normalizedFactorId.isEmpty() || backtestResult.isEmpty()) {
        return {};
    }

    const QVariantList results = backtestResult.value(QStringLiteral("results")).toList();
    for (const QVariant& itemValue : results) {
        const QVariantMap item = itemValue.toMap();
        const QVariantMap config = item.value(QStringLiteral("config")).toMap();
        const QString itemFactorId = item.value(QStringLiteral("factorId")).toString().trimmed();
        const QString configFactorId = config.value(QStringLiteral("factorId")).toString().trimmed();
        if (itemFactorId == normalizedFactorId || configFactorId == normalizedFactorId) {
            return item;
        }
    }

    const QVariantMap singleConfig = backtestResult.value(QStringLiteral("config")).toMap();
    if (singleConfig.value(QStringLiteral("factorId")).toString().trimmed() == normalizedFactorId) {
        return backtestResult;
    }

    return {};
}

QString configurableFactorRuntimeType(const factor::FactorInstanceInfo& info)
{
    return factor::bridge::configuredRequirementRuntimeType(info);
}

QStringList requirementFieldsFromFactor(const std::shared_ptr<factor::BaseFactor>& factorInstance)
{
    if (!factorInstance) {
        return {};
    }

    QStringList requiredFields;
    for (const std::string& field : factorInstance->getDataRequirements().requiredFields) {
        const QString fieldName = QString::fromStdString(field).trimmed();
        if (!fieldName.isEmpty()) {
            requiredFields.append(fieldName);
        }
    }
    return normalizeSupportFields(requiredFields);
}

QString normalizeSupportRuntimeType(const QString& rawFactorType,
                                   const factor::FactorInstanceInfo& info)
{
    QString runtimeType = factor::bridge::normalizeConfigurableFactorType(rawFactorType);
    if (!runtimeType.isEmpty()) {
        return runtimeType;
    }

    runtimeType = factor::bridge::normalizeConfigurableFactorType(QString::fromStdString(info.factorType));
    if (!runtimeType.isEmpty()) {
        return runtimeType;
    }

    return configurableFactorRuntimeType(info).trimmed();
}

struct CacheSupportContext {
    bool cacheMode{false};
    bool datasetReady{false};
    QString commonReason;
    QString commonCategory;
    QString effectiveStartDate;
    QString effectiveEndDate;
    QString latestTradeDate;
    QStringList availableFields;
    QSet<QString> availableFieldSet;
    QVariantMap fieldDiagnostics;
    QStringList effectiveStockCodes;
    int tradeDateCount{0};
    int tradeDateCountWithWarmup{0};
};

CacheSupportContext buildCacheSupportContext(
    const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
    int selectedDatasetId,
    const QString& startDate,
    const QString& endDate,
    const QVariantList& selectedStockPoolSymbols,
    const QVariantMap& cacheSnapshot)
{
    CacheSupportContext context;
    context.cacheMode = true;

    if (selectedDatasetId <= 0) {
        context.commonCategory = QStringLiteral("dataset-missing");
        context.commonReason = QStringLiteral("请先选择缓存集");
        return context;
    }

    const auto datasetInfo = DataServiceCache::getInstance().getDataSetInfo(selectedDatasetId);
    if (datasetInfo.id <= 0) {
        context.commonCategory = QStringLiteral("dataset-invalid");
        context.commonReason = QStringLiteral("所选缓存集无效，请重新选择");
        return context;
    }
    if (!isLatestBacktestDataset(datasetInfo)) {
        context.commonCategory = QStringLiteral("dataset-invalid");
        context.commonReason = QStringLiteral("所选缓存集当前不可用于因子回测，请重新生成并选择完整日线缓存集");
        return context;
    }

    const QVariantMap snapshotFieldDiagnostics = cacheSnapshot.value(QStringLiteral("fieldDiagnostics")).toMap();
    const QVariantList snapshotAvailableFields = cacheSnapshot.value(QStringLiteral("availableFields")).toList();
    if (!snapshotFieldDiagnostics.isEmpty() && !snapshotAvailableFields.isEmpty()) {
        QString snapshotLatestTradeDate;
        for (const QVariant& diagnosticValue : snapshotFieldDiagnostics) {
            const QVariantMap diagnostic = diagnosticValue.toMap();
            const QString tradeDate = diagnostic.value(QStringLiteral("latestTradeDate")).toString().trimmed();
            if (tradeDate.isEmpty()) {
                continue;
            }
            if (snapshotLatestTradeDate.isEmpty() || tradeDate > snapshotLatestTradeDate) {
                snapshotLatestTradeDate = tradeDate;
            }
        }

        context.effectiveStartDate = !cacheSnapshot.value(QStringLiteral("startDate")).toString().trimmed().isEmpty()
            ? cacheSnapshot.value(QStringLiteral("startDate")).toString().trimmed()
            : (datasetInfo.startDate.isValid() ? datasetInfo.startDate.toString(QStringLiteral("yyyy-MM-dd")) : QString());
        context.effectiveEndDate = !cacheSnapshot.value(QStringLiteral("endDate")).toString().trimmed().isEmpty()
            ? cacheSnapshot.value(QStringLiteral("endDate")).toString().trimmed()
            : (!snapshotLatestTradeDate.isEmpty()
                ? snapshotLatestTradeDate
                : (datasetInfo.endDate.isValid() ? datasetInfo.endDate.toString(QStringLiteral("yyyy-MM-dd")) : QString()));
        QStringList snapshotFields;
        snapshotFields.reserve(snapshotAvailableFields.size());
        for (const QVariant& fieldValue : snapshotAvailableFields) {
            const QString field = fieldValue.toString().trimmed();
            if (!field.isEmpty()) {
                snapshotFields.append(field);
            }
        }
        context.availableFields = normalizeSupportFields(snapshotFields);
        context.availableFieldSet = QSet<QString>(context.availableFields.begin(), context.availableFields.end());
        context.fieldDiagnostics = snapshotFieldDiagnostics;
        context.latestTradeDate = !cacheSnapshot.value(QStringLiteral("endDate")).toString().trimmed().isEmpty()
            ? cacheSnapshot.value(QStringLiteral("endDate")).toString().trimmed()
            : (!snapshotLatestTradeDate.isEmpty() ? snapshotLatestTradeDate : context.effectiveEndDate);
        context.tradeDateCount = cacheSnapshot.value(QStringLiteral("tradeDateCount")).toInt();
        context.tradeDateCountWithWarmup = context.tradeDateCount;

        const QStringList overrideStockCodes = normalizeStockPoolSymbols(selectedStockPoolSymbols);
        QSet<QString> overrideStockCodeSet;
        for (const QString& stockCode : overrideStockCodes) {
            overrideStockCodeSet.insert(stockCode);
        }

        for (const QString& stockCode : datasetInfo.stockCodes) {
            const QString normalizedStockCode = stockCode.trimmed().toUpper();
            if (normalizedStockCode.isEmpty()) {
                continue;
            }
            if (!overrideStockCodeSet.isEmpty() && !overrideStockCodeSet.contains(normalizedStockCode)) {
                continue;
            }
            context.effectiveStockCodes.append(normalizedStockCode);
        }

        if (!overrideStockCodeSet.isEmpty() && context.effectiveStockCodes.isEmpty()) {
            context.commonCategory = QStringLiteral("stock-pool-mismatch");
            context.commonReason = QStringLiteral("覆盖后的股票池不在当前缓存集范围内，请重新选择");
            return context;
        }

        context.datasetReady = true;
        Q_UNUSED(database)
        return context;
    }

    const QVariantList datasetRows = DataServiceCache::getInstance().getDataSetById(selectedDatasetId);
    if (datasetRows.isEmpty()) {
        context.commonCategory = QStringLiteral("dataset-empty");
        context.commonReason = QStringLiteral("所选缓存集为空，无法用于回测");
        return context;
    }

    context.effectiveStartDate = startDate.trimmed();
    context.effectiveEndDate = endDate.trimmed();
    if (context.effectiveStartDate.isEmpty() && datasetInfo.startDate.isValid()) {
        context.effectiveStartDate = datasetInfo.startDate.toString("yyyy-MM-dd");
    }
    if (context.effectiveEndDate.isEmpty() && datasetInfo.endDate.isValid()) {
        context.effectiveEndDate = datasetInfo.endDate.toString("yyyy-MM-dd");
    }

    const QStringList overrideStockCodes = normalizeStockPoolSymbols(selectedStockPoolSymbols);
    QSet<QString> overrideStockCodeSet;
    for (const QString& stockCode : overrideStockCodes) {
        overrideStockCodeSet.insert(stockCode);
    }

    for (const QString& stockCode : datasetInfo.stockCodes) {
        const QString normalizedStockCode = stockCode.trimmed().toUpper();
        if (normalizedStockCode.isEmpty()) {
            continue;
        }
        if (!overrideStockCodeSet.isEmpty() && !overrideStockCodeSet.contains(normalizedStockCode)) {
            continue;
        }
        context.effectiveStockCodes.append(normalizedStockCode);
    }

    if (!overrideStockCodeSet.isEmpty() && context.effectiveStockCodes.isEmpty()) {
        context.commonCategory = QStringLiteral("stock-pool-mismatch");
        context.commonReason = QStringLiteral("覆盖后的股票池不在当前缓存集范围内，请重新选择");
        return context;
    }

    context.availableFields = normalizeSupportFields(datasetInfo.availableFields);
    context.availableFieldSet = QSet<QString>(context.availableFields.begin(), context.availableFields.end());
    if (context.availableFields.isEmpty()) {
        context.commonCategory = QStringLiteral("dataset-fields-missing");
        context.commonReason = QStringLiteral("当前缓存集缺少字段信息");
        return context;
    }

    QHash<QString, int> nonNullCounts;
    QHash<QString, int> positiveCounts;
    QHash<QString, int> latestDateNonNullCounts;
    QHash<QString, int> latestDatePositiveCounts;
    QSet<QString> allowedSymbols(context.effectiveStockCodes.begin(), context.effectiveStockCodes.end());
    QSet<QString> tradeDates;
    for (const QVariant& rowVariant : datasetRows) {
        const QVariantMap row = rowVariant.toMap();
        if (row.isEmpty()) {
            continue;
        }

        const QString symbol = row.value(QStringLiteral("symbol")).toString().trimmed().toUpper();
        if (!allowedSymbols.isEmpty() && !allowedSymbols.contains(symbol)) {
            continue;
        }

        const QString tradeDate = datasetTradeDate(row);
        if (tradeDate.isEmpty()) {
            continue;
        }

        tradeDates.insert(tradeDate);
        if (context.latestTradeDate.isEmpty() || tradeDate > context.latestTradeDate) {
            context.latestTradeDate = tradeDate;
        }
    }

    for (const QVariant& rowVariant : datasetRows) {
        const QVariantMap row = rowVariant.toMap();
        if (row.isEmpty()) {
            continue;
        }

        const QString symbol = row.value(QStringLiteral("symbol")).toString().trimmed().toUpper();
        if (!allowedSymbols.isEmpty() && !allowedSymbols.contains(symbol)) {
            continue;
        }

        const QString tradeDate = datasetTradeDate(row);
        if (tradeDate.isEmpty()) {
            continue;
        }
        const bool onLatestDate = !context.latestTradeDate.isEmpty() && tradeDate == context.latestTradeDate;
        for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
            const QString field = factor::bridge::normalizeRequirementFieldName(it.key());
            if (field.isEmpty() || !it.value().isValid() || it.value().isNull()) {
                continue;
            }

            const QString textValue = it.value().toString().trimmed();
            if (textValue.isEmpty()) {
                continue;
            }

            nonNullCounts[field] += 1;
            if (onLatestDate) {
                latestDateNonNullCounts[field] += 1;
            }

            bool ok = false;
            const double numericValue = it.value().toDouble(&ok);
            if (ok && std::isfinite(numericValue) && numericValue > 0.0) {
                positiveCounts[field] += 1;
                if (onLatestDate) {
                    latestDatePositiveCounts[field] += 1;
                }
            }
        }
    }

    for (const QString& field : context.availableFields) {
        QVariantMap fieldInfo;
        fieldInfo["latestTradeDate"] = context.latestTradeDate;
        fieldInfo["nonNullCount"] = nonNullCounts.value(field, 0);
        fieldInfo["positiveCount"] = positiveCounts.value(field, 0);
        fieldInfo["latestDateNonNullCount"] = latestDateNonNullCounts.value(field, 0);
        fieldInfo["latestDatePositiveCount"] = latestDatePositiveCounts.value(field, 0);
        fieldInfo["requiresPositiveValues"] = fieldRequiresPositiveValues(field);
        context.fieldDiagnostics[field] = fieldInfo;
    }

    if (context.effectiveEndDate.isEmpty() && !context.latestTradeDate.isEmpty()) {
        context.effectiveEndDate = context.latestTradeDate;
    }

    Q_UNUSED(database)
    context.tradeDateCount = tradeDates.size();
    context.tradeDateCountWithWarmup = context.tradeDateCount;

    context.datasetReady = true;
    return context;
}

FactorBacktestController::FactorBacktestController(QObject *parent)
    : QObject(parent)
    , m_isRunning(false)
    , m_progress(0)
    , m_status("未初始化")
{
    qDebug() << "FactorBacktestController 创建";

    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(120);
    connect(m_progressTimer, &QTimer::timeout, this, &FactorBacktestController::pollBacktestProgress);

    setBacktestRuntimeParams(mergeRiskConfigurations(loadAppliedRiskConfiguration(), loadCurrentRiskConfiguration()));
}

FactorBacktestController::~FactorBacktestController()
{
    m_cancelRequested.store(true);
    shutdownBacktestInfrastructure();
    qDebug() << "FactorBacktestController 销毁";
}

void FactorBacktestController::setSelectedFactorIds(const QVariantList& factorIds)
{
    QVariantList normalizedFactorIds;
    QSet<QString> dedupe;
    for (const QVariant& factorIdValue : factorIds) {
        const QString factorId = factorIdValue.toString().trimmed();
        if (factorId.isEmpty() || dedupe.contains(factorId)) {
            continue;
        }
        dedupe.insert(factorId);
        normalizedFactorIds.append(factorId);
    }

    if (m_selectedFactorIds != normalizedFactorIds) {
        m_selectedFactorIds = normalizedFactorIds;
        emit selectedFactorIdsChanged(m_selectedFactorIds);
        qDebug() << "FactorBacktestController: 更新选择的因子ID，数量:" << m_selectedFactorIds.size();
    }

    if (normalizedFactorIds.isEmpty()) {
        if (!m_lastPreflightFailures.isEmpty()) {
            m_lastPreflightFailures.clear();
            emit lastPreflightFailuresChanged(m_lastPreflightFailures);
        }
        return;
    }

    if (!initializeRuntime() || !m_threadPool) {
        QVariantList failures;
        for (const QVariant& factorIdValue : normalizedFactorIds) {
            QVariantMap failure;
            failure["factorId"] = factorIdValue.toString().trimmed();
            failure["instanceId"] = QString();
            failure["reason"] = QStringLiteral("回测运行时初始化失败");
            failure["category"] = QStringLiteral("runtime-init-failed");
            failures.append(failure);
        }
        if (m_lastPreflightFailures != failures) {
            m_lastPreflightFailures = failures;
            emit lastPreflightFailuresChanged(m_lastPreflightFailures);
        }
        return;
    }

    const quint64 requestSeq = ++m_selectionSupportCheckSeq;
    QPointer<FactorBacktestController> safeController(this);
    try {
        m_threadPool->post([safeController, normalizedFactorIds, requestSeq]() {
            if (!safeController) {
                return;
            }

            const QVariantMap supportMap = safeController->buildFactorSupportMap(
                normalizedFactorIds,
                QString(),
                QString());

            QMetaObject::invokeMethod(
                safeController.data(),
                [safeController, normalizedFactorIds, supportMap, requestSeq]() {
                    if (!safeController || safeController->m_selectionSupportCheckSeq != requestSeq) {
                        return;
                    }

                    QVariantList filteredFactorIds;
                    filteredFactorIds.reserve(normalizedFactorIds.size());
                    QList<factor::bridge::BacktestPreflightFailure> filteredFailures;

                    for (const QVariant& factorIdValue : normalizedFactorIds) {
                        const QString requestedFactorId = factorIdValue.toString().trimmed();
                        const QVariantMap supportInfo = supportMap.value(requestedFactorId).toMap();
                        const bool supported = supportInfo.value(QStringLiteral("supported")).toBool();
                        if (supported) {
                            filteredFactorIds.append(requestedFactorId);
                            continue;
                        }

                        const QString category = supportInfo.value(QStringLiteral("category")).toString().trimmed();
                        if (!shouldFilterFactorOnSelection(category)) {
                            filteredFactorIds.append(requestedFactorId);
                            continue;
                        }

                        filteredFailures.append({
                            requestedFactorId,
                            supportInfo.value(QStringLiteral("instanceId")).toString().trimmed(),
                            supportInfo.value(QStringLiteral("reason")).toString().trimmed(),
                            category
                        });
                    }

                    const QVariantList failurePayload = factor::bridge::toVariantList(filteredFailures);
                    if (safeController->m_lastPreflightFailures != failurePayload) {
                        safeController->m_lastPreflightFailures = failurePayload;
                        emit safeController->lastPreflightFailuresChanged(safeController->m_lastPreflightFailures);
                    }

                    if (safeController->m_selectedFactorIds != filteredFactorIds) {
                        safeController->m_selectedFactorIds = filteredFactorIds;
                        emit safeController->selectedFactorIdsChanged(safeController->m_selectedFactorIds);
                        qDebug() << "FactorBacktestController: 选择阶段过滤后因子数量:"
                                 << safeController->m_selectedFactorIds.size();
                    }

                    const int filteredCount = normalizedFactorIds.size() - filteredFactorIds.size();
                    if (filteredCount > 0) {
                        qWarning() << "FactorBacktestController: 已在选择阶段过滤" << filteredCount
                                   << "个无数据支持因子(异步)";
                    }
                },
                Qt::QueuedConnection);
        });
    } catch (const std::exception& e) {
        qWarning() << "FactorBacktestController: 提交选择阶段支持检查任务失败:" << e.what();
    } catch (...) {
        qWarning() << "FactorBacktestController: 提交选择阶段支持检查任务失败(未知异常)";
    }
}

void FactorBacktestController::setSelectedDatasetId(int datasetId)
{
    if (m_selectedDatasetId != datasetId && !m_selectedFactorIds.isEmpty()) {
        qWarning() << "FactorBacktestController: 已选择因子，禁止切换缓存集:" << datasetId;
        return;
    }

    if (m_selectedDatasetId != datasetId) {
        m_selectedDatasetId = datasetId;
        emit selectedDatasetIdChanged(m_selectedDatasetId);
        qDebug() << "FactorBacktestController: 更新选择的数据集ID:" << m_selectedDatasetId;
    }
}

void FactorBacktestController::setDataSourceMode(const QString& dataSourceMode)
{
    const QString normalizedMode = normalizeDataSourceMode(dataSourceMode);
    if (normalizedMode.isEmpty()) {
        qWarning() << "FactorBacktestController: 非法数据源模式，拒绝更新:" << dataSourceMode;
        return;
    }
    if (m_dataSourceMode != normalizedMode && !m_selectedFactorIds.isEmpty()) {
        qWarning() << "FactorBacktestController: 已选择因子，禁止切换数据源模式:" << normalizedMode;
        return;
    }

    if (m_dataSourceMode != normalizedMode) {
        m_dataSourceMode = normalizedMode;
        emit dataSourceModeChanged(m_dataSourceMode);
        qDebug() << "FactorBacktestController: 更新数据源模式:" << m_dataSourceMode;
    }
}

void FactorBacktestController::setSelectedStockPoolSymbols(const QVariantList& stockPoolSymbols)
{
    QVariantList normalizedList;
    const QStringList normalizedSymbols = normalizeStockPoolSymbols(stockPoolSymbols);
    normalizedList.reserve(normalizedSymbols.size());
    for (const QString& symbol : normalizedSymbols) {
        normalizedList.append(symbol);
    }

    if (m_selectedStockPoolSymbols != normalizedList) {
        m_selectedStockPoolSymbols = normalizedList;
        emit selectedStockPoolSymbolsChanged(m_selectedStockPoolSymbols);
        qDebug() << "FactorBacktestController: 更新股票池覆盖，数量:" << m_selectedStockPoolSymbols.size();
    }
}

void FactorBacktestController::setBacktestRuntimeParams(const QVariantMap& backtestRuntimeParams)
{
    QVariantMap normalized = defaultBacktestRuntimeParams();
    for (auto it = backtestRuntimeParams.begin(); it != backtestRuntimeParams.end(); ++it) {
        normalized[it.key()] = it.value();
    }

    normalized[QStringLiteral("forwardDays")] = normalizedPositiveInt(normalized.value(QStringLiteral("forwardDays")), 1);
    normalized[QStringLiteral("rebalanceDays")] = normalizedPositiveInt(normalized.value(QStringLiteral("rebalanceDays")), 1);
    normalized[QStringLiteral("transactionCost")] = normalizedPercentRate(normalized.value(QStringLiteral("transactionCost")), 0.001);
    normalized[QStringLiteral("slippageRate")] = normalizedPercentRate(normalized.value(QStringLiteral("slippageRate")), 0.0);
    normalized[QStringLiteral("riskFreeRate")] = normalizedPercentRate(normalized.value(QStringLiteral("riskFreeRate")), 0.0);
    normalized[QStringLiteral("benchmarkSymbol")] = normalizedBenchmarkSymbol(normalized.value(QStringLiteral("benchmarkSymbol")));

    if (m_backtestRuntimeParams != normalized) {
        m_backtestRuntimeParams = normalized;
        emit backtestRuntimeParamsChanged(m_backtestRuntimeParams);
    }
}

void FactorBacktestController::startBacktest(const QString& groupText,
                                             const QString& startDate,
                                             const QString& endDate,
                                             const QVariantMap& cacheSnapshot)
{
    startBacktestWithFactors(m_selectedFactorIds, groupText, startDate, endDate, cacheSnapshot);
}

void FactorBacktestController::startBacktestWithFactors(const QVariantList& factorIds,
                                                        const QString& groupText,
                                                        const QString& startDate,
                                                        const QString& endDate,
                                                        const QVariantMap& cacheSnapshot)
{
    qDebug() << "开始回测，因子数量:" << factorIds.size() << "分组:" << groupText;

    auto setPreflightFailures = [this](const QVariantList& failures) {
        if (m_lastPreflightFailures == failures) {
            return;
        }
        m_lastPreflightFailures = failures;
        emit lastPreflightFailuresChanged(m_lastPreflightFailures);
    };

    auto failFast = [this](const QString& errorMessage) {
        m_isRunning = false;
        m_hasActiveTask = false;
        m_activeRequestedFactorId.clear();
        resetBatchState();
        m_cancelRequested.store(false);
        if (m_progressTimer) {
            m_progressTimer->stop();
        }
        resetResults();
        clearPersistedResult();
        m_progress = 0;
        m_status = errorMessage;
        emit isRunningChanged(m_isRunning);
        emit progressChanged(m_progress);
        emit statusChanged(m_status);
        emit backtestResultChanged(m_backtestResult);
        emit groupResultsChanged(m_groupResults);
        emit icirResultChanged(m_icirResult);
        emit summaryStatsChanged(m_summaryStats);
        emit backtestFailed(errorMessage);
    };

    if (!m_isRunning) {
        m_hasActiveTask = false;
        m_activeRequestedFactorId.clear();
        resetBatchState();
        if (m_progressTimer) {
            m_progressTimer->stop();
        }
    }

    setPreflightFailures({});

    if (factorIds.isEmpty()) {
        failFast("请选择至少一个因子");
        return;
    }

    if (m_isRunning) {
        failFast("已有回测任务正在运行");
        return;
    }

    if (!initializeRuntime()) {
        failFast("回测运行时初始化失败");
        return;
    }

    if (!m_threadPool) {
        failFast("回测线程池不可用");
        return;
    }

    const QVariantList requestedFactorIds = factorIds;
    const QVariantList normalizedFactorIds = normalizeFactorIds(requestedFactorIds);
    const QString requestedGroupText = groupText;
    const QString requestedStartDate = startDate;
    const QString requestedEndDate = endDate;

    if (m_supportMapRequestInFlight) {
        failFast("因子支持校验仍在进行中，请稍候再开始回测");
        return;
    }

    if (m_factorSupportMapCache.isEmpty()) {
        failFast("因子支持校验结果尚未预热，请先打开因子窗口并完成校验");
        return;
    }

    m_cancelRequested.store(false);
    m_isRunning = true;
    m_progress = 0;
    m_status = "正在开始回测";

    emit isRunningChanged(m_isRunning);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);

    resetResults();
    clearPersistedResult();
    m_batchFactorIds = normalizedFactorIds;
    m_batchResultMaps.clear();
    m_pendingGroupText = requestedGroupText;
    m_pendingStartDate = requestedStartDate;
    m_pendingEndDate = requestedEndDate;
    m_activeFactorIndex = 0;

    emit backtestResultChanged(m_backtestResult);
    emit groupResultsChanged(m_groupResults);
    emit icirResultChanged(m_icirResult);
    emit summaryStatsChanged(m_summaryStats);

    launchNextBacktestTask();
}

void FactorBacktestController::launchNextBacktestTask()
{
    if (m_activeFactorIndex < 0 || m_activeFactorIndex >= m_batchFactorIds.size()) {
        finalizeBacktestFailure("没有可执行的回测任务", false);
        return;
    }

    QPointer<FactorBacktestController> safeController(this);
    const QVariantList requestedFactorIds = m_batchFactorIds;
    const QString requestedGroupText = m_pendingGroupText;
    const QString requestedStartDate = m_pendingStartDate;
    const QString requestedEndDate = m_pendingEndDate;

    m_threadPool->post([safeController, requestedFactorIds, requestedGroupText, requestedStartDate, requestedEndDate]() {
        if (!safeController) {
            return;
        }

        try {
            auto* executor = safeController->m_executor.get();
            if (!executor) {
                QMetaObject::invokeMethod(
                    safeController.data(),
                    [safeController]() {
                        if (safeController) {
                            safeController->finalizeBacktestFailure(QStringLiteral("回测执行器未初始化"), false);
                        }
                    },
                    Qt::QueuedConnection);
                return;
            }

            struct ResolvedBatchFactor {
                QString requestedFactorId;
                QString resolvedInstanceId;
            };

            std::vector<ResolvedBatchFactor> resolvedFactors;
            resolvedFactors.reserve(static_cast<size_t>(requestedFactorIds.size()));

            for (size_t index = 0; index < static_cast<size_t>(requestedFactorIds.size()); ++index) {
                const QVariant factorIdValue = requestedFactorIds.at(static_cast<int>(index));
                const QString requestedFactorId = factorIdValue.toString().trimmed();
                if (requestedFactorId.isEmpty()) {
                    QMetaObject::invokeMethod(
                        safeController.data(),
                        [safeController]() {
                            if (safeController) {
                                safeController->finalizeBacktestFailure(
                                    QStringLiteral("因子支持校验结果缺少有效因子ID"),
                                    false);
                            }
                        },
                        Qt::QueuedConnection);
                    return;
                }

                    const QString resolvedInstanceId = safeController->resolveInstanceId(factorIdValue);
                if (resolvedInstanceId.isEmpty()) {
                    QMetaObject::invokeMethod(
                        safeController.data(),
                        [safeController, requestedFactorId]() {
                            if (safeController) {
                                    safeController->finalizeBacktestFailure(
                                        QString("因子实例ID未解析: %1").arg(requestedFactorId),
                                        false);
                            }
                        },
                        Qt::QueuedConnection);
                    return;
                }

                resolvedFactors.push_back({requestedFactorId, resolvedInstanceId});
            }

            if (resolvedFactors.empty()) {
                QMetaObject::invokeMethod(
                    safeController.data(),
                    [safeController]() {
                        if (safeController) {
                            safeController->finalizeBacktestFailure(QStringLiteral("未找到可用的因子实例"), false);
                        }
                    },
                    Qt::QueuedConnection);
                return;
            }

            const factor::BacktestConfig sharedBaseConfig = safeController->buildBacktestConfig(
                resolvedFactors.front().resolvedInstanceId,
                requestedGroupText,
                requestedStartDate,
                requestedEndDate);

            std::vector<PendingBacktestTask> pendingTasks;
            pendingTasks.reserve(resolvedFactors.size());

            for (size_t index = 0; index < resolvedFactors.size(); ++index) {
                const ResolvedBatchFactor& factorItem = resolvedFactors.at(index);

                factor::BacktestConfig config = sharedBaseConfig;
                config.instanceId = factorItem.resolvedInstanceId.toStdString();

                auto handle = executor->executeTrackedAsync(config);
                PendingBacktestTask task;
                task.requestedFactorId = factorItem.requestedFactorId;
                task.resolvedInstanceId = factorItem.resolvedInstanceId;
                task.batchIndex = index;
                task.taskId = handle.taskId;
                task.future = std::make_shared<std::future<factor::BacktestResult>>(std::move(handle.future));
                pendingTasks.push_back(std::move(task));
            }

            QMetaObject::invokeMethod(
                safeController.data(),
                [safeController, pendingTasks = std::move(pendingTasks)]() mutable {
                    if (!safeController) {
                        return;
                    }

                    safeController->m_pendingBacktestTasks = std::move(pendingTasks);
                    safeController->m_batchResultMaps.clear();
                    safeController->m_batchResultMaps.resize(static_cast<size_t>(safeController->m_batchFactorIds.size()));
                    safeController->m_activeFactorIndex = 0;
                    safeController->m_hasActiveTask = !safeController->m_pendingBacktestTasks.empty();
                    safeController->m_activeRequestedFactorId.clear();
                    safeController->m_progress = 0;
                    safeController->m_status = QStringLiteral("正在并行执行回测");
                    emit safeController->progressChanged(safeController->m_progress);
                    emit safeController->statusChanged(safeController->m_status);
                    emit safeController->backtestProgress(safeController->m_progress, safeController->m_status);
                    if (safeController->m_progressTimer && !safeController->m_progressTimer->isActive()) {
                        safeController->m_progressTimer->start();
                    }
                },
                Qt::QueuedConnection);
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(
                safeController.data(),
                [safeController, errorText = QString::fromUtf8(e.what())]() {
                    if (safeController) {
                        safeController->finalizeBacktestFailure(errorText, false);
                    }
                },
                Qt::QueuedConnection);
        } catch (...) {
            QMetaObject::invokeMethod(
                safeController.data(),
                [safeController]() {
                    if (safeController) {
                        safeController->finalizeBacktestFailure(QStringLiteral("并行回测启动失败"), false);
                    }
                },
                Qt::QueuedConnection);
        }
    });

}

int FactorBacktestController::parseGroupCount(const QString& groupText) const
{
    if (groupText.contains("20")) return 20;
    if (groupText.contains("10")) return 10;
    if (groupText.contains("5")) return 5;
    return 10;
}

QVariantMap FactorBacktestController::buildFactorSupportMap(const QVariantList& factorIds,
                                                            const QString& startDate,
                                                            const QString& endDate,
                                                            const QVariantMap& cacheSnapshot)
{
    QVariantMap supportMap;
    const bool useInjectedSupportMapRuntime = m_instanceInfoOverrideForTests && m_factorInstanceOverrideForTests;
    const QVariantList normalizedFactorIds = normalizeFactorIds(factorIds);
    const QString normalizedMode = normalizeDataSourceMode(m_dataSourceMode);
    const QString cacheKey = buildSupportMapCacheKey(
        normalizedFactorIds,
        startDate,
        endDate,
        normalizedMode,
        m_selectedDatasetId,
        m_selectedStockPoolSymbols,
        cacheSnapshot);

    static QMutex s_supportMapCacheMutex;
    static QString s_lastSupportMapCacheKey;
    static QVariantMap s_lastSupportMapCacheValue;

    {
        QMutexLocker locker(&s_supportMapCacheMutex);
        if (s_lastSupportMapCacheKey == cacheKey && !s_lastSupportMapCacheValue.isEmpty()) {
            return s_lastSupportMapCacheValue;
        }
    }

    if (normalizedFactorIds.isEmpty()) {
        return supportMap;
    }

    if (!useInjectedSupportMapRuntime && !initializeRuntime()) {
        for (const QVariant& factorIdValue : factorIds) {
            const QString requestedFactorId = factorIdValue.toString().trimmed();
            if (requestedFactorId.isEmpty()) {
                continue;
            }

            QVariantMap failure;
            failure["supported"] = false;
            failure["category"] = QStringLiteral("runtime-init-failed");
            failure["reason"] = QStringLiteral("回测运行时初始化失败");
            failure["instanceId"] = QString();
            failure["requiredFields"] = QVariantList();
            failure["missingFields"] = QVariantList();
            failure["sourceTable"] = QString();
            supportMap[requestedFactorId] = failure;
        }
        return supportMap;
    }

    if (normalizedMode.isEmpty()) {
        for (const QVariant& factorIdValue : normalizedFactorIds) {
            const QString requestedFactorId = factorIdValue.toString().trimmed();
            if (requestedFactorId.isEmpty()) {
                continue;
            }

            QVariantMap failure;
            failure["supported"] = false;
            failure["category"] = QStringLiteral("invalid-data-source-mode");
            failure["reason"] = QStringLiteral("数据源模式非法，仅允许 cache 或 database");
            failure["instanceId"] = QString();
            failure["requiredFields"] = QVariantList();
            failure["missingFields"] = QVariantList();
            failure["sourceTable"] = QString();
            supportMap[requestedFactorId] = failure;
        }
        return supportMap;
    }

    const CacheSupportContext cacheContext = normalizedMode == QStringLiteral("cache")
        ? buildCacheSupportContext(m_database, m_selectedDatasetId, startDate, endDate, m_selectedStockPoolSymbols, cacheSnapshot)
        : CacheSupportContext{};

    auto makeFailure = [](const QString& instanceId,
                          const QString& category,
                          const QString& reason,
                          const QStringList& requiredFields = {},
                          const QStringList& missingFields = {},
                          const QString& sourceTable = QString(),
                          const QString& runtimeType = QString()) {
        QVariantMap entry;
        entry["supported"] = false;
        entry["instanceId"] = instanceId;
        entry["category"] = category;
        entry["reason"] = reason;
        entry["requiredFields"] = toVariantList(requiredFields);
        entry["missingFields"] = toVariantList(missingFields);
        entry["sourceTable"] = sourceTable;
        entry["runtimeType"] = runtimeType;
        return entry;
    };

    auto makeSuccess = [](const QString& instanceId,
                          const QStringList& requiredFields,
                          const QString& sourceTable,
                          const QString& runtimeType) {
        QVariantMap entry;
        entry["supported"] = true;
        entry["instanceId"] = instanceId;
        entry["category"] = QStringLiteral("supported");
        entry["reason"] = QString();
        entry["requiredFields"] = toVariantList(requiredFields);
        entry["missingFields"] = QVariantList();
        entry["sourceTable"] = sourceTable;
        entry["runtimeType"] = runtimeType;
        return entry;
    };

    for (const QVariant& factorIdValue : normalizedFactorIds) {
        const QString requestedFactorId = factorIdValue.toString().trimmed();
        if (requestedFactorId.isEmpty()) {
            continue;
        }

        const QString resolvedInstanceId = resolveInstanceId(factorIdValue);
        if (resolvedInstanceId.isEmpty()) {
            supportMap[requestedFactorId] = makeFailure(QString(),
                                                        QStringLiteral("instance-missing"),
                                                        QStringLiteral("未解析到实例ID"));
            continue;
        }

        try {
            const factor::FactorInstanceInfo instanceInfo = getInstanceInfo(resolvedInstanceId);
            const std::shared_ptr<factor::BaseFactor> factorInstance = m_factorInstanceOverrideForTests
                ? m_factorInstanceOverrideForTests(resolvedInstanceId)
                : (m_instanceManager ? m_instanceManager->createInstance(resolvedInstanceId.toStdString()) : nullptr);
            if (!factorInstance) {
                supportMap[requestedFactorId] = makeFailure(
                    resolvedInstanceId,
                    QStringLiteral("instance-create-failed"),
                    QStringLiteral("未能创建因子实例，请检查实例是否已激活且定义与实例表保持同步"));
                continue;
            }

            const QString factorType = QString::fromStdString(instanceInfo.factorType).trimmed();
            const QStringList configuredFields = factor::bridge::configuredRequirementFields(instanceInfo);
            QStringList requiredFields = requirementFieldsFromFactor(factorInstance);
            if (requiredFields.isEmpty()) {
                requiredFields = configuredFields;
            }
            QString runtimeType = normalizeSupportRuntimeType(
                QString::fromStdString(factorInstance->getFactorType()),
                instanceInfo);
            const QVariantMap requirementCalculation = factor::bridge::extractRequirementCalculationMap(instanceInfo);
            const auto makeRuntimeFailure = [&](const QString& category,
                                               const QString& reason,
                                               const QStringList& failureRequiredFields = {},
                                               const QStringList& missingFields = {},
                                               const QString& sourceTable = QString()) {
                return makeFailure(resolvedInstanceId,
                                   category,
                                   reason,
                                   failureRequiredFields,
                                   missingFields,
                                   sourceTable,
                                   runtimeType);
            };

            const factor::bridge::SupportMapRequirementResolution requirementResolution =
                factor::bridge::resolveSupportMapRequirementResolution(
                    runtimeType,
                    requirementCalculation,
                    configuredFields);

            if (!requirementResolution.failureCategory.isEmpty()) {
                supportMap[requestedFactorId] = makeRuntimeFailure(
                    requirementResolution.failureCategory,
                    requirementResolution.failureReason,
                    requirementResolution.requiredFields);
                continue;
            }

            if (!requirementResolution.requiredFields.isEmpty()) {
                const QStringList resolvedRequiredFields = normalizeSupportFields(requirementResolution.requiredFields);
                if (requiredFields.isEmpty()) {
                    requiredFields = resolvedRequiredFields;
                } else {
                    for (const QString& resolvedField : resolvedRequiredFields) {
                        if (!requiredFields.contains(resolvedField)) {
                            requiredFields.append(resolvedField);
                        }
                    }
                }
            }

            if (runtimeType.isEmpty() || factorType.isEmpty()) {
                supportMap[requestedFactorId] = makeRuntimeFailure(
                    QStringLiteral("unsupported-type"),
                    QStringLiteral("当前因子缺少可识别的回测类型"));
                continue;
            }

            if (requiredFields.isEmpty()) {
                supportMap[requestedFactorId] = makeRuntimeFailure(
                    QStringLiteral("missing-field"),
                    QStringLiteral("因子配置缺少可用于支持校验的必需字段"));
                continue;
            }

            const QString sourceTable = factor::bridge::resolveRequirementSourceTable(instanceInfo,
                                                                                      requiredFields,
                                                                                      requirementResolution.explicitSourceTable);

            if (runtimeType == QStringLiteral("sentiment") && sourceTable.isEmpty()) {
                supportMap[requestedFactorId] = makeRuntimeFailure(
                    QStringLiteral("proxy-only-runtime"),
                    QStringLiteral("当前情绪因子缺少真实情绪数据表，运行时只剩市场宽度代理，已禁止进入回测"),
                    requiredFields,
                    {},
                    sourceTable);
                continue;
            }

            if (normalizedMode == QStringLiteral("cache")) {
                if (!cacheContext.datasetReady) {
                    supportMap[requestedFactorId] = makeRuntimeFailure(cacheContext.commonCategory,
                                                                       cacheContext.commonReason,
                                                                       requiredFields,
                                                                       {},
                                                                       sourceTable);
                    continue;
                }

                if (sourceTable != QStringLiteral("financial_indicator")) {
                    QStringList missingFields;
                    for (const QString& requiredField : requiredFields) {
                        if (!factor::bridge::requirementFieldSatisfiedByAvailableFields(requiredField,
                                                                                        cacheContext.availableFieldSet)) {
                            missingFields.append(requiredField);
                        }
                    }

                    if (!missingFields.isEmpty()) {
                        supportMap[requestedFactorId] = makeRuntimeFailure(
                            QStringLiteral("missing-field"),
                            QString("当前缓存缺少字段: %1").arg(missingFields.join(QStringLiteral(", "))),
                            requiredFields,
                            missingFields,
                            sourceTable);
                        continue;
                    }

                    bool diagnosticFailed = false;
                    for (const QString& requiredField : requiredFields) {
                        const QStringList diagnosticFields = factor::bridge::requirementDiagnosticFields(
                            requiredField,
                            cacheContext.availableFieldSet);
                        for (const QString& diagnosticField : diagnosticFields) {
                            const QVariantMap diagnostic = cacheContext.fieldDiagnostics.value(diagnosticField).toMap();
                            if (diagnostic.isEmpty()) {
                                continue;
                            }

                            const QString latestTradeDate = diagnostic.value(QStringLiteral("latestTradeDate")).toString();
                            const int latestNonNullCount = diagnostic.value(QStringLiteral("latestDateNonNullCount")).toInt();
                            const int latestPositiveCount = diagnostic.value(QStringLiteral("latestDatePositiveCount")).toInt();

                            if (latestNonNullCount <= 0) {
                                supportMap[requestedFactorId] = makeRuntimeFailure(
                                    QStringLiteral("missing-field-value"),
                                    latestTradeDate.isEmpty()
                                        ? QString("当前缓存没有可用字段值: %1").arg(requiredField)
                                        : QString("当前缓存在最近交易日 %1 没有可用字段值: %2").arg(latestTradeDate, requiredField),
                                    requiredFields,
                                    {requiredField},
                                    sourceTable);
                                diagnosticFailed = true;
                                break;
                            }

                            if (fieldRequiresPositiveValues(diagnosticField) && latestPositiveCount <= 0) {
                                supportMap[requestedFactorId] = makeRuntimeFailure(
                                    QStringLiteral("invalid-field-value"),
                                    latestTradeDate.isEmpty()
                                        ? QString("当前缓存中的 %1 全部为 0 或非正数").arg(requiredField)
                                        : QString("当前缓存在最近交易日 %1 的 %2 全部为 0 或非正数").arg(latestTradeDate, requiredField),
                                    requiredFields,
                                    {requiredField},
                                    sourceTable);
                                diagnosticFailed = true;
                                break;
                            }
                        }
                        if (diagnosticFailed) {
                            break;
                        }
                    }

                    if (diagnosticFailed) {
                        continue;
                    }
                }

                int requiredTradingDays = 0;
                if (m_requiredWarmupTradingDaysOverrideForTests.contains(resolvedInstanceId)) {
                    requiredTradingDays = m_requiredWarmupTradingDaysOverrideForTests.value(resolvedInstanceId);
                } else {
                    const FactorWarmupRequirement warmupRequirement = loadWarmupRequirement(instanceInfo, factorInstance);
                    requiredTradingDays = requiredWarmupTradingDays(warmupRequirement);
                }
                if (sourceTable != QStringLiteral("financial_indicator")
                        && requiredTradingDays > 1
                        && cacheContext.tradeDateCountWithWarmup > 0
                        && cacheContext.tradeDateCountWithWarmup < requiredTradingDays) {
                    supportMap[requestedFactorId] = makeRuntimeFailure(
                        QStringLiteral("insufficient-history"),
                        QString("当前日期范围结合预热历史后仅有 %1 个交易日样本，低于该因子所需的 %2 个交易日")
                            .arg(cacheContext.tradeDateCountWithWarmup)
                            .arg(requiredTradingDays),
                        requiredFields,
                        {},
                        sourceTable);
                    continue;
                }

                if (sourceTable == QStringLiteral("financial_indicator") && m_dataChecker) {
                    const factor::DataStatus dataStatus = m_dataChecker->checkFactorData(
                        instanceInfo.config,
                        resolvedInstanceId.toStdString(),
                        cacheContext.effectiveStartDate.toStdString(),
                        cacheContext.effectiveEndDate.toStdString());
                    if (!dataStatus.isValid()) {
                        supportMap[requestedFactorId] = makeRuntimeFailure(QStringLiteral("data-unavailable"),
                                                                           QString::fromStdString(dataStatus.message),
                                                                           requiredFields,
                                                                           {},
                                                                           sourceTable);
                        continue;
                    }
                }
            } else {
                const QString effectiveStartDate = startDate.trimmed();
                const QString effectiveEndDate = endDate.trimmed();
                if (effectiveStartDate.isEmpty() != effectiveEndDate.isEmpty()) {
                    supportMap[requestedFactorId] = makeRuntimeFailure(
                        QStringLiteral("invalid-backtest-window"),
                        QStringLiteral("回测开始/结束日期必须同时提供，禁止使用默认兜底日期"),
                        requiredFields,
                        {},
                        sourceTable);
                    continue;
                }

                if (!effectiveStartDate.isEmpty() && m_dataChecker) {
                    const factor::DataStatus dataStatus = m_dataChecker->checkFactorData(
                        instanceInfo.config,
                        resolvedInstanceId.toStdString(),
                        effectiveStartDate.toStdString(),
                        effectiveEndDate.toStdString());
                    if (!dataStatus.isValid()) {
                        supportMap[requestedFactorId] = makeRuntimeFailure(QStringLiteral("data-unavailable"),
                                                                           QString::fromStdString(dataStatus.message),
                                                                           requiredFields,
                                                                           {},
                                                                           sourceTable);
                        continue;
                    }
                }
            }

            supportMap[requestedFactorId] = makeSuccess(resolvedInstanceId, requiredFields, sourceTable, runtimeType);
        } catch (const std::exception& e) {
            const QString errorText = QString::fromUtf8(e.what());
            const bool isStringTypeError = errorText.contains(QStringLiteral("Not a string value"));
            const QString detail = isStringTypeError
                ? QStringLiteral("因子配置包含非字符串字段，请检查 factor_instance.full_config，instance_id=%1, factor_id=%2")
                      .arg(resolvedInstanceId, requestedFactorId)
                : errorText;
            supportMap[requestedFactorId] = makeFailure(
                resolvedInstanceId,
                isStringTypeError ? QStringLiteral("invalid-config-type") : QStringLiteral("instance-create-failed"),
                detail);
        } catch (...) {
            supportMap[requestedFactorId] = makeFailure(
                resolvedInstanceId,
                QStringLiteral("instance-create-failed"),
                QStringLiteral("因子支持校验发生未知异常"));
        }
    }

    if (!supportMap.isEmpty()) {
        QMutexLocker locker(&s_supportMapCacheMutex);
        s_lastSupportMapCacheKey = cacheKey;
        s_lastSupportMapCacheValue = supportMap;
    }

    return supportMap;
}

void FactorBacktestController::requestFactorSupportMapAsync(const QVariantList& factorIds,
                                                            const QString& startDate,
                                                            const QString& endDate,
                                                            const QVariantMap& cacheSnapshot,
                                                            quint64 requestId)
{
    if (factorIds.isEmpty()) {
        m_lastSupportMapCacheKey.clear();
        m_lastSupportMapScopeKey.clear();
        emit factorSupportMapReady(requestId, {});
        return;
    }

    m_lastSupportMapCacheKey = buildSupportMapCacheKey(
        normalizeFactorIds(factorIds),
        startDate,
        endDate,
        normalizeDataSourceMode(m_dataSourceMode),
        m_selectedDatasetId,
        m_selectedStockPoolSymbols,
        cacheSnapshot);
    m_lastSupportMapScopeKey = buildSupportMapScopeKey(
        startDate,
        endDate,
        normalizeDataSourceMode(m_dataSourceMode),
        m_selectedDatasetId,
        m_selectedStockPoolSymbols,
        cacheSnapshot);

    if (!m_threadPool) {
        QVariantMap supportMap;
        for (const QVariant& factorIdValue : factorIds) {
            const QString requestedFactorId = factorIdValue.toString().trimmed();
            if (requestedFactorId.isEmpty()) {
                continue;
            }

            QVariantMap failure;
            failure[QStringLiteral("supported")] = false;
            failure[QStringLiteral("category")] = QStringLiteral("runtime-init-failed");
            failure[QStringLiteral("reason")] = QStringLiteral("回测运行时初始化失败");
            failure[QStringLiteral("instanceId")] = QString();
            failure[QStringLiteral("requiredFields")] = QVariantList();
            failure[QStringLiteral("missingFields")] = QVariantList();
            failure[QStringLiteral("sourceTable")] = QString();
            supportMap[requestedFactorId] = failure;
        }
        emit factorSupportMapReady(requestId, supportMap);
        return;
    }

    QPointer<FactorBacktestController> safeController(this);
    try {
        m_threadPool->post([safeController, factorIds, startDate, endDate, cacheSnapshot, requestId]() {
            if (!safeController) {
                return;
            }

            QVariantMap supportMap;
            try {
                supportMap = safeController->buildFactorSupportMap(
                    factorIds,
                    startDate,
                    endDate,
                    cacheSnapshot);
            } catch (const std::exception& e) {
                for (const QVariant& factorIdValue : factorIds) {
                    const QString factorId = factorIdValue.toString().trimmed();
                    if (factorId.isEmpty()) {
                        continue;
                    }

                    QVariantMap failure;
                    failure[QStringLiteral("supported")] = false;
                    failure[QStringLiteral("category")] = QStringLiteral("instance-create-failed");
                    failure[QStringLiteral("reason")] = QString::fromUtf8(e.what());
                    failure[QStringLiteral("instanceId")] = QString();
                    failure[QStringLiteral("requiredFields")] = QVariantList();
                    failure[QStringLiteral("missingFields")] = QVariantList();
                    failure[QStringLiteral("sourceTable")] = QString();
                    supportMap[factorId] = failure;
                }
            } catch (...) {
                for (const QVariant& factorIdValue : factorIds) {
                    const QString factorId = factorIdValue.toString().trimmed();
                    if (factorId.isEmpty()) {
                        continue;
                    }

                    QVariantMap failure;
                    failure[QStringLiteral("supported")] = false;
                    failure[QStringLiteral("category")] = QStringLiteral("instance-create-failed");
                    failure[QStringLiteral("reason")] = QStringLiteral("因子支持校验发生未知异常");
                    failure[QStringLiteral("instanceId")] = QString();
                    failure[QStringLiteral("requiredFields")] = QVariantList();
                    failure[QStringLiteral("missingFields")] = QVariantList();
                    failure[QStringLiteral("sourceTable")] = QString();
                    supportMap[factorId] = failure;
                }
            }

            QMetaObject::invokeMethod(
                safeController.data(),
                [safeController, requestId, supportMap]() {
                    if (!safeController) {
                        return;
                    }
                    emit safeController->factorSupportMapReady(requestId, supportMap);
                },
                Qt::QueuedConnection);
        });
    } catch (const std::exception& e) {
        qWarning() << "FactorBacktestController: 提交支持图异步任务失败:" << e.what();
        QVariantMap supportMap;
        for (const QVariant& factorIdValue : factorIds) {
            const QString factorId = factorIdValue.toString().trimmed();
            if (factorId.isEmpty()) {
                continue;
            }

            QVariantMap failure;
            failure[QStringLiteral("supported")] = false;
            failure[QStringLiteral("category")] = QStringLiteral("instance-create-failed");
            failure[QStringLiteral("reason")] = QString::fromUtf8(e.what());
            failure[QStringLiteral("instanceId")] = QString();
            failure[QStringLiteral("requiredFields")] = QVariantList();
            failure[QStringLiteral("missingFields")] = QVariantList();
            failure[QStringLiteral("sourceTable")] = QString();
            supportMap[factorId] = failure;
        }
        emit factorSupportMapReady(requestId, supportMap);
    } catch (...) {
        qWarning() << "FactorBacktestController: 提交支持图异步任务失败(未知异常)";
        QVariantMap supportMap;
        for (const QVariant& factorIdValue : factorIds) {
            const QString factorId = factorIdValue.toString().trimmed();
            if (factorId.isEmpty()) {
                continue;
            }

            QVariantMap failure;
            failure[QStringLiteral("supported")] = false;
            failure[QStringLiteral("category")] = QStringLiteral("instance-create-failed");
            failure[QStringLiteral("reason")] = QStringLiteral("因子支持校验发生未知异常");
            failure[QStringLiteral("instanceId")] = QString();
            failure[QStringLiteral("requiredFields")] = QVariantList();
            failure[QStringLiteral("missingFields")] = QVariantList();
            failure[QStringLiteral("sourceTable")] = QString();
            supportMap[factorId] = failure;
        }
        emit factorSupportMapReady(requestId, supportMap);
    }
}

QVariantMap FactorBacktestController::preflightCategoryMeta(const QString& category) const
{
    return buildPreflightCategoryMetaMap(category);
}

QString FactorBacktestController::preflightFailureDetailText(const QVariantMap& failure,
                                                             const QString& factorDisplayName) const
{
    const QString fallbackName = factorDisplayName.trimmed().isEmpty()
        ? QStringLiteral("该因子")
        : factorDisplayName.trimmed();
    const QVariantMap meta = buildPreflightCategoryMetaMap(failure.value(QStringLiteral("category")).toString());
    return QStringLiteral("%1：%2").arg(fallbackName, meta.value(QStringLiteral("detail")).toString());
}

QVariantMap FactorBacktestController::factorValidationState(const QString& factorId,
                                                            const QString& factorDisplayName,
                                                            bool hasFactorDefinition,
                                                            const QVariantMap& supportInfo,
                                                            const QVariantList& preflightFailures,
                                                            const QVariantMap& backtestResult,
                                                            const QString& lastBacktestError,
                                                            const QVariantList& selectedFactorIds,
                                                            const QString& dataSourceMode,
                                                            bool hasAvailableCacheDataset,
                                                            int selectedDatasetId) const
{
    const QString normalizedFactorId = factorId.trimmed();
    const QString factorName = factorDisplayName.trimmed().isEmpty() ? normalizedFactorId : factorDisplayName.trimmed();

    if (!hasFactorDefinition) {
        return buildValidationStateMap(
            QStringLiteral("config-missing"),
            QStringLiteral("配置缺失"),
            QStringLiteral("未能读取因子定义"),
            QStringLiteral("%1 当前缺少完整配置，无法判断可执行性。").arg(factorName),
            QStringLiteral("#F59E0B"));
    }

    if (!supportInfo.isEmpty() && supportInfo.value(QStringLiteral("supported")).toBool() == false) {
        const QVariantMap supportMeta = buildPreflightCategoryMetaMap(supportInfo.value(QStringLiteral("category")).toString());
        return buildValidationStateMap(
            supportMeta.value(QStringLiteral("key")).toString(),
            supportMeta.value(QStringLiteral("statusText")).toString(),
            supportInfo.value(QStringLiteral("reason")).toString().trimmed().isEmpty()
                ? QStringLiteral("当前不支持该因子回测")
                : supportInfo.value(QStringLiteral("reason")).toString(),
            QStringLiteral("%1 当前处于“%2”状态。%3")
                .arg(factorName,
                     supportMeta.value(QStringLiteral("statusText")).toString(),
                     supportMeta.value(QStringLiteral("detail")).toString()),
            supportMeta.value(QStringLiteral("accentColor")).toString());
    }

    if (normalizeDataSourceMode(dataSourceMode) == QStringLiteral("cache")) {
        if (!hasAvailableCacheDataset) {
            return buildValidationStateMap(
                QStringLiteral("waiting-cache"),
                QStringLiteral("待选择缓存集"),
                QStringLiteral("当前没有可用缓存集"),
                QStringLiteral("请先生成并选择缓存集，之后再验证该因子。"),
                QStringLiteral("#64748B"));
        }

        if (selectedDatasetId <= 0) {
            return buildValidationStateMap(
                QStringLiteral("waiting-cache"),
                QStringLiteral("待选择缓存集"),
                QStringLiteral("尚未选择缓存集"),
                QStringLiteral("请选择一个缓存集后，系统才能校验字段支持情况。"),
                QStringLiteral("#64748B"));
        }
    }

    const QVariantMap preflightFailure = findPreflightFailure(preflightFailures, normalizedFactorId);
    if (!preflightFailure.isEmpty()) {
        const QVariantMap failureMeta = buildPreflightCategoryMetaMap(preflightFailure.value(QStringLiteral("category")).toString());
        const QString instanceId = preflightFailure.value(QStringLiteral("instanceId")).toString().trimmed();
        return buildValidationStateMap(
            failureMeta.value(QStringLiteral("key")).toString().isEmpty()
                ? QStringLiteral("preflight-failed")
                : failureMeta.value(QStringLiteral("key")).toString(),
            failureMeta.value(QStringLiteral("statusText")).toString(),
            preflightFailure.value(QStringLiteral("reason")).toString().trimmed().isEmpty()
                ? QStringLiteral("组合回测预检失败")
                : preflightFailure.value(QStringLiteral("reason")).toString(),
            instanceId.isEmpty()
                ? QStringLiteral("该因子未通过组合回测预检。%1").arg(failureMeta.value(QStringLiteral("detail")).toString())
                : QStringLiteral("实例 %1 未通过组合回测预检。%2").arg(instanceId, failureMeta.value(QStringLiteral("detail")).toString()),
            failureMeta.value(QStringLiteral("accentColor")).toString());
    }

    if (!lastBacktestError.trimmed().isEmpty()
            && selectedFactorIds.size() == 1
            && selectedFactorIds.first().toString().trimmed() == normalizedFactorId) {
        return buildValidationStateMap(
            QStringLiteral("backtest-failed"),
            QStringLiteral("回测失败"),
            lastBacktestError,
            QStringLiteral("该因子已经进入执行阶段，但最近一次回测未成功完成。"),
            QStringLiteral("#EF4444"));
    }

    const QVariantMap resultEntry = findDisplayedBacktestResult(backtestResult, normalizedFactorId);
    if (resultEntry.isEmpty()) {
        return buildValidationStateMap(
            QStringLiteral("ready"),
            QStringLiteral("可执行待验证"),
            QStringLiteral("已通过执行前校验"),
            QStringLiteral("该因子当前已满足配置与数据前置条件，下一步需要通过回测结果验证效果。"),
            QStringLiteral("#3B82F6"));
    }

    const QVariantMap resultSummary = resultEntry.value(QStringLiteral("summary")).toMap();
    const QVariantMap resultIcir = resultEntry.value(QStringLiteral("icirResult")).toMap();

    const double dataCoverage = resultSummary.value(QStringLiteral("dataCoverage")).toDouble();
    const double icValue = resultIcir.value(QStringLiteral("icValue")).toDouble();
    const double irValue = resultIcir.value(QStringLiteral("irValue")).toDouble();
    const double icPositiveRate = resultIcir.value(QStringLiteral("icPositiveRate")).toDouble();
    const double spreadReturn = resultSummary.value(QStringLiteral("spreadReturn")).toDouble();

    const bool meetsTarget = dataCoverage >= 0.9
        && std::abs(icValue) >= 0.02
        && irValue >= 0.3
        && icPositiveRate >= 0.5
        && spreadReturn > 0.0;

    if (meetsTarget) {
        return buildValidationStateMap(
            QStringLiteral("effective"),
            QStringLiteral("有效"),
            QStringLiteral("已满足当前目标阈值"),
            QStringLiteral("覆盖率、IC、IR、IC正率和多空收益差均达到当前设定目标。"),
            QStringLiteral("#EF4444"));
    }

    return buildValidationStateMap(
        QStringLiteral("weak"),
        QStringLiteral("效果偏弱"),
        QStringLiteral("已可执行，但未完全达到目标阈值"),
        QStringLiteral("建议继续观察数据覆盖率、IC/IR、IC正率和多空收益差，判断是否需要调整或下线。"),
        QStringLiteral("#F59E0B"));
}

bool FactorBacktestController::datasetSelectableForBacktest(const QVariantMap& dataset) const
{
    if (dataset.isEmpty() || !dataset.contains(QStringLiteral("id"))) {
        return false;
    }

    if (dataset.value(QStringLiteral("isBacktestReady")).toBool()) {
        return true;
    }

    QStringList rawFields;
    const QVariant availableFieldsValue = dataset.value(QStringLiteral("availableFields"));
    if (availableFieldsValue.typeId() == QMetaType::QStringList) {
        rawFields = availableFieldsValue.toStringList();
    } else {
        const QVariantList availableFields = availableFieldsValue.toList();
        for (const QVariant& fieldValue : availableFields) {
            rawFields.append(fieldValue.toString());
        }
    }

    const QStringList fields = normalizeSupportFields(rawFields);
    const QStringList stockCodes = normalizeStockPoolSymbols(dataset.value(QStringLiteral("stockCodes")));
    return !fields.isEmpty() && !stockCodes.isEmpty();
}

QVariantList FactorBacktestController::buildBacktestDatasetOptions(const QVariantList& datasetList) const
{
    QVariantList options;
    options.append(QVariantMap{{QStringLiteral("text"), QStringLiteral("请选择缓存集")},
                               {QStringLiteral("value"), -1},
                               {QStringLiteral("raw"), QVariantMap{}}});

    QSet<int> seenIds;
    auto appendOption = [&](const QVariantMap& dataset) {
        if (!datasetSelectableForBacktest(dataset)) {
            return;
        }

        bool idOk = false;
        const int datasetId = dataset.value(QStringLiteral("id")).toInt(&idOk);
        if (!idOk || datasetId <= 0 || seenIds.contains(datasetId)) {
            return;
        }

        QStringList parts;
        parts.append(QStringLiteral("#%1").arg(datasetId));
        const QString displayName = dataset.value(QStringLiteral("displayName")).toString().trimmed();
        const QString name = dataset.value(QStringLiteral("name")).toString().trimmed();
        parts.append(displayName.isEmpty()
            ? (name.isEmpty() ? QStringLiteral("未命名缓存集") : name)
            : displayName);

        const QString startDate = dataset.value(QStringLiteral("startDate")).toString().trimmed();
        const QString endDate = dataset.value(QStringLiteral("endDate")).toString().trimmed();
        if (!startDate.isEmpty() && !endDate.isEmpty()) {
            parts.append(QStringLiteral("(%1~%2)").arg(startDate, endDate));
        }

        options.append(QVariantMap{{QStringLiteral("text"), parts.join(QStringLiteral(" "))},
                                   {QStringLiteral("value"), datasetId},
                                   {QStringLiteral("raw"), dataset}});
        seenIds.insert(datasetId);
    };

    for (const QVariant& datasetValue : datasetList) {
        appendOption(datasetValue.toMap());
    }

    return options;
}

QVariantList FactorBacktestController::normalizeFactorIds(const QVariantList& factorIds) const
{
    QVariantList normalized;
    QSet<QString> seen;
    normalized.reserve(factorIds.size());

    for (const QVariant& factorValue : factorIds) {
        const QString factorId = factorValue.toString().trimmed();
        if (factorId.isEmpty() || seen.contains(factorId)) {
            continue;
        }

        seen.insert(factorId);
        normalized.append(factorId);
    }

    return normalized;
}

QVariantMap FactorBacktestController::filterFactorIdsBySupport(const QVariantList& factorIds,
                                                               const QVariantMap& supportMap) const
{
    QVariantList keptFactorIds;
    QVariantList removedFactorIds;

    keptFactorIds.reserve(factorIds.size());
    removedFactorIds.reserve(factorIds.size());

    for (const QVariant& factorValue : factorIds) {
        const QString factorId = factorValue.toString().trimmed();
        if (factorId.isEmpty()) {
            continue;
        }

        const QVariantMap supportInfo = supportMap.value(factorId).toMap();
        if (!supportInfo.isEmpty() && !supportInfo.value(QStringLiteral("supported")).toBool()) {
            removedFactorIds.append(factorId);
            continue;
        }

        keptFactorIds.append(factorId);
    }

    return QVariantMap{{QStringLiteral("keptFactorIds"), keptFactorIds},
                       {QStringLiteral("removedFactorIds"), removedFactorIds}};
}

int FactorBacktestController::beginFactorSupportMapRefresh(const QVariantList& factorIds,
                                                           const QString& startDate,
                                                           const QString& endDate,
                                                           const QVariantMap& cacheSnapshot)
{
    if (!initializeRuntime()) {
        ++m_supportMapRequestSeq;
        const int requestId = m_supportMapRequestSeq;

        m_lastSupportMapScopeKey = buildSupportMapScopeKey(
            startDate,
            endDate,
            normalizeDataSourceMode(m_dataSourceMode),
            m_selectedDatasetId,
            m_selectedStockPoolSymbols,
            cacheSnapshot);

        QVariantMap supportMap;
        for (const QVariant& factorValue : factorIds) {
            const QString factorId = factorValue.toString().trimmed();
            if (factorId.isEmpty()) {
                continue;
            }

            QVariantMap failure;
            failure[QStringLiteral("supported")] = false;
            failure[QStringLiteral("category")] = QStringLiteral("runtime-init-failed");
            failure[QStringLiteral("reason")] = QStringLiteral("回测运行时初始化失败");
            failure[QStringLiteral("instanceId")] = QString();
            failure[QStringLiteral("requiredFields")] = QVariantList();
            failure[QStringLiteral("missingFields")] = QVariantList();
            failure[QStringLiteral("sourceTable")] = QString();
            supportMap[factorId] = failure;
        }

        if (m_supportMapRequestInFlight) {
            m_supportMapRequestInFlight = false;
            emit supportMapRequestInFlightChanged(m_supportMapRequestInFlight);
        }

        if (m_factorSupportMapCache != supportMap) {
            m_factorSupportMapCache = supportMap;
            emit factorSupportMapCacheChanged(m_factorSupportMapCache);
        }

        emit factorSupportMapReady(requestId, supportMap);
        m_supportMapAppliedSeq = requestId;
        return requestId;
    }

    QVariantList normalizedFactorIds;
    QSet<QString> seenFactorIds;
    normalizedFactorIds.reserve(factorIds.size());
    for (const QVariant& factorValue : factorIds) {
        const QString factorId = factorValue.toString().trimmed();
        if (factorId.isEmpty() || seenFactorIds.contains(factorId)) {
            continue;
        }
        seenFactorIds.insert(factorId);
        normalizedFactorIds.append(factorId);
    }

    ++m_supportMapRequestSeq;
    const int requestId = m_supportMapRequestSeq;

    m_lastSupportMapScopeKey = buildSupportMapScopeKey(
        startDate,
        endDate,
        normalizeDataSourceMode(m_dataSourceMode),
        m_selectedDatasetId,
        m_selectedStockPoolSymbols,
        cacheSnapshot);

    if (!m_factorSupportMapCache.isEmpty()) {
        m_factorSupportMapCache.clear();
        emit factorSupportMapCacheChanged(m_factorSupportMapCache);
    }

    if (!m_supportMapRequestInFlight) {
        m_supportMapRequestInFlight = true;
        emit supportMapRequestInFlightChanged(m_supportMapRequestInFlight);
    }

    if (normalizedFactorIds.isEmpty()) {
        if (m_supportMapRequestInFlight) {
            m_supportMapRequestInFlight = false;
            emit supportMapRequestInFlightChanged(m_supportMapRequestInFlight);
        }
        if (!m_factorSupportMapCache.isEmpty()) {
            m_factorSupportMapCache.clear();
            emit factorSupportMapCacheChanged(m_factorSupportMapCache);
        }
        m_supportMapAppliedSeq = requestId;
        return requestId;
    }

    requestFactorSupportMapAsync(normalizedFactorIds, startDate, endDate, cacheSnapshot, static_cast<quint64>(requestId));
    return requestId;
}

bool FactorBacktestController::handleFactorSupportMapReady(int requestId,
                                                           const QVariantMap& supportMap)
{
    if (requestId < m_supportMapRequestSeq || requestId <= m_supportMapAppliedSeq) {
        return false;
    }

    if (requestId > m_supportMapRequestSeq) {
        m_supportMapRequestSeq = requestId;
    }
    m_supportMapAppliedSeq = requestId;

    if (m_supportMapRequestInFlight) {
        m_supportMapRequestInFlight = false;
        emit supportMapRequestInFlightChanged(m_supportMapRequestInFlight);
    }

    if (m_factorSupportMapCache != supportMap) {
        m_factorSupportMapCache = supportMap;
        emit factorSupportMapCacheChanged(m_factorSupportMapCache);
    }

    return true;
}

void FactorBacktestController::markPendingFilterAfterSupportMap()
{
    m_pendingFilterAfterSupportMap = true;
}

bool FactorBacktestController::takePendingFilterAfterSupportMap()
{
    const bool pending = m_pendingFilterAfterSupportMap;
    m_pendingFilterAfterSupportMap = false;
    return pending;
}

QVariantMap FactorBacktestController::buildStockPoolComparison(const QVariantMap& previousBacktestReport,
                                                               const QVariantMap& currentDatasetInfo) const
{
    const QVariantMap config = previousBacktestReport.value(QStringLiteral("config")).toMap();
    QVariant previousSymbolsSource = config.value(QStringLiteral("symbol_pool"));
    if (!previousSymbolsSource.isValid()) {
        previousSymbolsSource = config.value(QStringLiteral("symbolPool"));
    }
    if (!previousSymbolsSource.isValid()) {
        previousSymbolsSource = config.value(QStringLiteral("selectedSymbols"));
    }

    const QStringList previousSymbols = normalizeStockPoolSymbols(previousSymbolsSource);
    const QStringList currentSymbols = normalizeStockPoolSymbols(currentDatasetInfo.value(QStringLiteral("stockCodes")));

    QSet<QString> previousSet;
    QSet<QString> currentSet;
    for (const QString& symbol : previousSymbols) {
        previousSet.insert(symbol);
    }
    for (const QString& symbol : currentSymbols) {
        currentSet.insert(symbol);
    }

    QVariantList intersectionSymbols;
    QVariantList previousOnlySymbols;
    QVariantList currentOnlySymbols;

    for (const QString& symbol : previousSymbols) {
        if (currentSet.contains(symbol)) {
            intersectionSymbols.append(symbol);
        } else {
            previousOnlySymbols.append(symbol);
        }
    }

    for (const QString& symbol : currentSymbols) {
        if (!previousSet.contains(symbol)) {
            currentOnlySymbols.append(symbol);
        }
    }

    QVariantList previousList;
    QVariantList currentList;
    for (const QString& symbol : previousSymbols) {
        previousList.append(symbol);
    }
    for (const QString& symbol : currentSymbols) {
        currentList.append(symbol);
    }

    return QVariantMap{{QStringLiteral("previousSymbols"), previousList},
                       {QStringLiteral("currentSymbols"), currentList},
                       {QStringLiteral("intersectionSymbols"), intersectionSymbols},
                       {QStringLiteral("previousOnlySymbols"), previousOnlySymbols},
                       {QStringLiteral("currentOnlySymbols"), currentOnlySymbols}};
}

QString FactorBacktestController::stockPoolComparisonText(const QVariantList& selectedFactorIds,
                                                          const QVariantMap& comparison) const
{
    if (selectedFactorIds.isEmpty()) {
        return QStringLiteral("先选择因子后再进入二次回测比较。");
    }

    if (selectedFactorIds.size() > 1) {
        return QStringLiteral("当前是多因子组合回测。回测完成后，系统会针对每个因子分别与它自己的上一轮基线比较，再决定自动覆盖还是提示确认。");
    }

    const int previousCount = comparison.value(QStringLiteral("previousSymbols")).toList().size();
    const int currentCount = comparison.value(QStringLiteral("currentSymbols")).toList().size();
    const int intersectionCount = comparison.value(QStringLiteral("intersectionSymbols")).toList().size();
    const int previousOnlyCount = comparison.value(QStringLiteral("previousOnlySymbols")).toList().size();
    const int currentOnlyCount = comparison.value(QStringLiteral("currentOnlySymbols")).toList().size();

    if (previousCount <= 0) {
        return QStringLiteral("当前没有上一轮同因子回测基线，本次完成后会直接建立新的股票池基线。");
    }

    return QStringLiteral("上一轮股票池 %1 只，本次候选股票池 %2 只，交集 %3 只，上轮独有 %4 只，本轮新增 %5 只。")
        .arg(previousCount)
        .arg(currentCount)
        .arg(intersectionCount)
        .arg(previousOnlyCount)
        .arg(currentOnlyCount);
}

QVariantList FactorBacktestController::displayedBacktestResults(const QVariantMap& backtestResult) const
{
    if (backtestResult.isEmpty()) {
        return {};
    }

    const QVariantList results = backtestResult.value(QStringLiteral("results")).toList();
    if (!results.isEmpty()) {
        return results;
    }

    return {backtestResult};
}

QString FactorBacktestController::displayedBacktestResultName(const QVariantMap& entry) const
{
    if (entry.isEmpty()) {
        return QStringLiteral("未命名结果");
    }

    const QVariantMap config = entry.value(QStringLiteral("config")).toMap();
    const QString factorName = config.value(QStringLiteral("factorName")).toString().trimmed();
    if (!factorName.isEmpty()) {
        return factorName;
    }

    const QString configFactorId = config.value(QStringLiteral("factorId")).toString().trimmed();
    if (!configFactorId.isEmpty()) {
        return configFactorId;
    }

    const QString entryFactorId = entry.value(QStringLiteral("factorId")).toString().trimmed();
    if (!entryFactorId.isEmpty()) {
        return entryFactorId;
    }

    return QStringLiteral("未命名结果");
}

QVariantMap FactorBacktestController::resolveDisplayedBacktestState(const QVariantMap& backtestResult,
                                                                    int selectedResultIndex) const
{
    QVariantMap state;
    state[QStringLiteral("backtestResult")] = backtestResult;
    state[QStringLiteral("displayedBacktestResult")] = QVariantMap{};
    state[QStringLiteral("groupResults")] = QVariantList{};
    state[QStringLiteral("icirResult")] = QVariantMap{};
    state[QStringLiteral("summaryStats")] = QVariantMap{};
    state[QStringLiteral("selectedResultIndex")] = 0;

    if (backtestResult.isEmpty()) {
        return state;
    }

    const QVariantList results = backtestResult.value(QStringLiteral("results")).toList();
    if (!results.isEmpty()) {
        int index = selectedResultIndex;
        if (index < 0 || index >= results.size()) {
            index = 0;
        }

        const QVariantMap displayedResult = results.at(index).toMap();
        state[QStringLiteral("displayedBacktestResult")] = displayedResult;
        state[QStringLiteral("groupResults")] = displayedResult.value(QStringLiteral("groups")).toList();
        state[QStringLiteral("icirResult")] = displayedResult.value(QStringLiteral("icirResult")).toMap();
        state[QStringLiteral("summaryStats")] = displayedResult.value(QStringLiteral("summary")).toMap();
        state[QStringLiteral("selectedResultIndex")] = index;
        return state;
    }

    state[QStringLiteral("displayedBacktestResult")] = backtestResult;
    state[QStringLiteral("groupResults")] = backtestResult.value(QStringLiteral("groups")).toList();
    state[QStringLiteral("icirResult")] = backtestResult.value(QStringLiteral("icirResult")).toMap();
    state[QStringLiteral("summaryStats")] = backtestResult.value(QStringLiteral("summary")).toMap();
    state[QStringLiteral("selectedResultIndex")] = 0;
    return state;
}

QVariantMap FactorBacktestController::resolveRiskConfigurationSnapshot(const QVariantMap& displayedBacktestResult,
                                                                       const QVariantMap& appliedConfiguration,
                                                                       const QVariantMap& currentConfiguration) const
{
    Q_UNUSED(currentConfiguration)

    const QVariantMap resultConfig = displayedBacktestResult.value(QStringLiteral("config")).toMap();
    if (!resultConfig.isEmpty()) {
        return QVariantMap{{QStringLiteral("snapshot"), resultConfig},
                           {QStringLiteral("sourceLabel"), QStringLiteral("回测结果快照")}};
    }

    if (!appliedConfiguration.isEmpty()) {
        return QVariantMap{{QStringLiteral("snapshot"), appliedConfiguration},
                           {QStringLiteral("sourceLabel"), QStringLiteral("当前已应用风控")}};
    }

    return QVariantMap{{QStringLiteral("snapshot"), QVariantMap{}},
                       {QStringLiteral("sourceLabel"), QStringLiteral("未检测到已应用风控")}};
}

QString FactorBacktestController::riskConfigBenchmarkSymbol(const QVariantMap& snapshot,
                                                            const QString& fallbackSymbol) const
{
    const QString configured = firstConfiguredValue(
        snapshot,
        {QStringLiteral("benchmarkSymbol")}).toString().trimmed().toUpper();
    if (!configured.isEmpty()) {
        return configured;
    }

    const QString fallback = fallbackSymbol.trimmed().toUpper();
    return fallback.isEmpty() ? QStringLiteral("未配置") : fallback;
}

QVariantList FactorBacktestController::riskConfigMetricCards(const QVariantMap& snapshot) const
{
    auto formatPercentText = [](const QVariantMap& source, const QStringList& keys) -> QString {
        const QVariant raw = firstConfiguredValue(source, keys);
        if (!raw.isValid() || raw.isNull()) {
            return QStringLiteral("未配置");
        }

        const double numeric = normalizedPercentRate(raw, std::numeric_limits<double>::quiet_NaN());
        if (!std::isfinite(numeric)) {
            return QStringLiteral("未配置");
        }

        return QStringLiteral("%1%")
            .arg(QString::number(numeric * 100.0, 'f', 2));
    };

    auto formatText = [](const QVariantMap& source, const QStringList& keys) -> QString {
        const QString value = firstConfiguredValue(source, keys).toString().trimmed();
        return value.isEmpty() ? QStringLiteral("未配置") : value;
    };

    QVariantList cards;
    cards.append(QVariantMap{
        {QStringLiteral("title"), QStringLiteral("总暴露上限")},
        {QStringLiteral("value"), formatPercentText(snapshot, {QStringLiteral("maxTotalExposure"), QStringLiteral("max_total_exposure"), QStringLiteral("maxPositionRatio"), QStringLiteral("max_position_ratio")})},
        {QStringLiteral("description"), QStringLiteral("组合总仓位约束")},
        {QStringLiteral("trend"), QStringLiteral("neutral")},
        {QStringLiteral("color"), QStringLiteral("#38BDF8")}
    });
    cards.append(QVariantMap{
        {QStringLiteral("title"), QStringLiteral("单票上限")},
        {QStringLiteral("value"), formatPercentText(snapshot, {QStringLiteral("maxPositionPercent"), QStringLiteral("max_position_percent"), QStringLiteral("maxSinglePositionRatio"), QStringLiteral("max_single_position_ratio")})},
        {QStringLiteral("description"), QStringLiteral("单只标的仓位约束")},
        {QStringLiteral("trend"), QStringLiteral("neutral")},
        {QStringLiteral("color"), QStringLiteral("#34D399")}
    });
    cards.append(QVariantMap{
        {QStringLiteral("title"), QStringLiteral("最大回撤")},
        {QStringLiteral("value"), formatPercentText(snapshot, {QStringLiteral("maxDrawdownLimit"), QStringLiteral("max_drawdown_limit")})},
        {QStringLiteral("description"), QStringLiteral("回撤熔断阈值")},
        {QStringLiteral("trend"), QStringLiteral("down")},
        {QStringLiteral("color"), QStringLiteral("#F59E0B")}
    });
    cards.append(QVariantMap{
        {QStringLiteral("title"), QStringLiteral("止损阈值")},
        {QStringLiteral("value"), formatPercentText(snapshot, {QStringLiteral("stopLossPercent"), QStringLiteral("stop_loss"), QStringLiteral("stopLoss")})},
        {QStringLiteral("description"), QStringLiteral("单笔止损约束")},
        {QStringLiteral("trend"), QStringLiteral("down")},
        {QStringLiteral("color"), QStringLiteral("#EC4899")}
    });
    cards.append(QVariantMap{
        {QStringLiteral("title"), QStringLiteral("止盈阈值")},
        {QStringLiteral("value"), formatPercentText(snapshot, {QStringLiteral("takeProfitPercent"), QStringLiteral("take_profit"), QStringLiteral("takeProfit")})},
        {QStringLiteral("description"), QStringLiteral("单笔止盈约束")},
        {QStringLiteral("trend"), QStringLiteral("up")},
        {QStringLiteral("color"), QStringLiteral("#A78BFA")}
    });
    cards.append(QVariantMap{
        {QStringLiteral("title"), QStringLiteral("佣金/滑点")},
        {QStringLiteral("value"), QStringLiteral("%1 / %2").arg(
            formatPercentText(snapshot, {QStringLiteral("commissionRate"), QStringLiteral("commission_rate")}),
            formatPercentText(snapshot, {QStringLiteral("slippageRate"), QStringLiteral("slippage_rate")})
        )},
        {QStringLiteral("description"), QStringLiteral("回测成本假设")},
        {QStringLiteral("trend"), QStringLiteral("neutral")},
        {QStringLiteral("color"), QStringLiteral("#A78BFA")}
    });
    cards.append(QVariantMap{
        {QStringLiteral("title"), QStringLiteral("风险无风险利率")},
        {QStringLiteral("value"), formatPercentText(snapshot, {QStringLiteral("riskFreeRate"), QStringLiteral("risk_free_rate")})},
        {QStringLiteral("description"), QStringLiteral("回测风险假设")},
        {QStringLiteral("trend"), QStringLiteral("neutral")},
        {QStringLiteral("color"), QStringLiteral("#F97316")}
    });
    cards.append(QVariantMap{
        {QStringLiteral("title"), QStringLiteral("风险预算预警")},
        {QStringLiteral("value"), formatPercentText(snapshot, {QStringLiteral("varWarningPercent"), QStringLiteral("var_warning_percent")})},
        {QStringLiteral("description"), QStringLiteral("风险使用率阈值")},
        {QStringLiteral("trend"), QStringLiteral("neutral")},
        {QStringLiteral("color"), QStringLiteral("#22C55E")}
    });
    cards.append(QVariantMap{
        {QStringLiteral("title"), QStringLiteral("熔断阈值")},
        {QStringLiteral("value"), QStringLiteral("%1 / %2 / %3").arg(
            formatText(snapshot, {QStringLiteral("level1Breaker"), QStringLiteral("level_1_breaker")}),
            formatText(snapshot, {QStringLiteral("level2Breaker"), QStringLiteral("level_2_breaker")}),
            formatText(snapshot, {QStringLiteral("level3Breaker"), QStringLiteral("level_3_breaker")})
        )},
        {QStringLiteral("description"), QStringLiteral("一级/二级/三级")},
        {QStringLiteral("trend"), QStringLiteral("neutral")},
        {QStringLiteral("color"), QStringLiteral("#FB7185")}
    });

    return cards;
}

QVariantMap FactorBacktestController::buildSingleFactorRunEntry(const QVariantMap& result,
                                                                const QString& fallbackFactorName) const
{
    if (result.isEmpty()) {
        return {};
    }

    QVariantMap entry = result;
    const QVariantList results = result.value(QStringLiteral("results")).toList();
    if (!results.isEmpty()) {
        entry = results.first().toMap();
    }

    if (entry.isEmpty()) {
        return {};
    }

    const QVariantMap config = entry.value(QStringLiteral("config")).toMap();
    const QVariantMap summary = entry.value(QStringLiteral("summary")).toMap();
    const QVariantMap icir = entry.value(QStringLiteral("icirResult")).toMap();

    const int forwardDays = (std::max)(1, config.value(QStringLiteral("forwardDays")).toInt());
    const int rebalanceDays = (std::max)(1, config.value(QStringLiteral("rebalanceDays")).toInt());

    QString name = config.value(QStringLiteral("factorName")).toString().trimmed();
    if (name.isEmpty()) {
        name = config.value(QStringLiteral("factorId")).toString().trimmed();
    }
    if (name.isEmpty()) {
        name = entry.value(QStringLiteral("factorId")).toString().trimmed();
    }
    if (name.isEmpty()) {
        name = fallbackFactorName.trimmed().isEmpty() ? QStringLiteral("单因子") : fallbackFactorName.trimmed();
    }

    QString factorId = config.value(QStringLiteral("factorId")).toString().trimmed();
    if (factorId.isEmpty()) {
        factorId = entry.value(QStringLiteral("factorId")).toString().trimmed();
    }

    QString runId = entry.value(QStringLiteral("taskId")).toString().trimmed();
    if (runId.isEmpty()) {
        runId = QStringLiteral("%1_%2")
            .arg(QDateTime::currentMSecsSinceEpoch())
            .arg(QString::number(static_cast<qulonglong>(qHash(name + factorId))));
    }

    return QVariantMap{
        {QStringLiteral("runId"), runId},
        {QStringLiteral("factorName"), name},
        {QStringLiteral("factorId"), factorId},
        {QStringLiteral("horizonTag"), QStringLiteral("%1/%2").arg(forwardDays).arg(rebalanceDays)},
        {QStringLiteral("forwardDays"), forwardDays},
        {QStringLiteral("rebalanceDays"), rebalanceDays},
        {QStringLiteral("annualReturn"), summary.value(QStringLiteral("annualReturn")).toDouble()},
        {QStringLiteral("informationRatio"), summary.value(QStringLiteral("informationRatio")).toDouble()},
        {QStringLiteral("sharpeRatio"), summary.value(QStringLiteral("sharpeRatio")).toDouble()},
        {QStringLiteral("maxDrawdown"), summary.value(QStringLiteral("maxDrawdown")).toDouble()},
        {QStringLiteral("turnoverRate"), summary.value(QStringLiteral("turnoverRate")).toDouble()},
        {QStringLiteral("icValue"), icir.value(QStringLiteral("icValue")).toDouble()},
        {QStringLiteral("irValue"), icir.value(QStringLiteral("irValue")).toDouble()},
        {QStringLiteral("timestamp"), QDateTime::currentMSecsSinceEpoch()}
    };
}

QVariantList FactorBacktestController::pushSingleFactorRunHistory(const QVariantList& existingHistory,
                                                                  const QVariantMap& result,
                                                                  int historyLimit,
                                                                  const QString& fallbackFactorName) const
{
    const QVariantMap newEntry = buildSingleFactorRunEntry(
        result,
        fallbackFactorName);
    if (newEntry.isEmpty()) {
        return existingHistory;
    }

    QVariantList history;
    const QString newFactorId = newEntry.value(QStringLiteral("factorId")).toString();
    const QString newHorizonTag = newEntry.value(QStringLiteral("horizonTag")).toString();
    for (const QVariant& existingValue : existingHistory) {
        const QVariantMap existing = existingValue.toMap();
        if (existing.isEmpty()) {
            continue;
        }

        if (existing.value(QStringLiteral("factorId")).toString() == newFactorId
            && existing.value(QStringLiteral("horizonTag")).toString() == newHorizonTag) {
            continue;
        }
        history.append(existing);
    }

    history.prepend(newEntry);

    const int normalizedLimit = (std::max)(1, historyLimit);
    if (history.size() > normalizedLimit) {
        history = history.mid(0, normalizedLimit);
    }

    return history;
}

bool FactorBacktestController::initializeRuntime()
{
    if (m_executor) {
        return true;
    }

    if (m_instanceManager) {
        return true;
    }

    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        qCritical() << "FactorBacktestController: 无法获取数据库连接";
        return false;
    }

    m_database = std::move(database);
    m_threadPool = std::make_shared<foundation::thread::ThreadPoolExecutor>(4);
    m_dataChecker = std::make_shared<factor::DataAvailabilityChecker>(m_database);
    m_cacheManager = std::make_shared<factor::FactorCacheManager>();
    m_instanceManager = std::make_shared<factor::FactorInstanceManager>(m_database, m_dataChecker);

    auto& cacheFacade = AStockQuantEngine::Cache::CacheFacade::getInstance();
    if (!cacheFacade.isEnabled()) {
        AStockQuantEngine::Cache::CacheConfig cacheConfig;
        cacheFacade.initialize(cacheConfig);
    }

    m_cacheManager->setCacheFacade(
        std::shared_ptr<AStockQuantEngine::Cache::CacheFacade>(&cacheFacade, [](AStockQuantEngine::Cache::CacheFacade*) {})
    );

    m_resolvedInstanceIdCache.clear();
    m_instanceInfoCache.clear();

    m_executor = std::make_unique<factor::FactorBacktestExecutor>(m_instanceManager, m_threadPool, m_cacheManager);
    return true;
}

QString FactorBacktestController::resolveInstanceId(const QVariant& factorId) const
{
    if (m_resolveInstanceIdOverrideForTests) {
        return m_resolveInstanceIdOverrideForTests(factorId);
    }

    if (!m_database) {
        return {};
    }

    const QString rawId = factorId.toString().trimmed();
    if (rawId.isEmpty()) {
        return {};
    }

    if (m_resolvedInstanceIdCache.contains(rawId)) {
        return m_resolvedInstanceIdCache.value(rawId);
    }

    const auto candidates = factor::bridge::buildFactorInstanceLookupCandidates(rawId);
    const auto result = m_database->executeQuery(
        QString(
            "SELECT instance_id, factor_id, status FROM factor_instance "
            "WHERE instance_id = :instanceIdPrimary OR factor_id = :factorIdPrimary "
            "OR instance_id = :instanceIdSecondary OR factor_id = :factorIdSecondary "
            "ORDER BY updated_at DESC, created_at DESC"
        ),
        makeNamedParams({
            {"instanceIdPrimary", candidates.primaryId},
            {"factorIdPrimary", candidates.primaryId},
            {"instanceIdSecondary", candidates.secondaryId},
            {"factorIdSecondary", candidates.secondaryId}
        })
    );

    QVector<factor::bridge::FactorInstanceLookupRecord> records;
    records.reserve(static_cast<int>(result.rowCount()));
    for (const auto& row : result.getRows()) {
        factor::bridge::FactorInstanceLookupRecord record;
        record.instanceId = row.getString("instance_id").trimmed();
        record.factorId = row.getString("factor_id").trimmed();
        record.status = row.getString("status").trimmed();
        records.append(record);
    }

    const QString resolvedInstanceId = factor::bridge::resolveFactorInstanceId(rawId, records);
    m_resolvedInstanceIdCache.insert(rawId, resolvedInstanceId);
    return resolvedInstanceId;
}

factor::FactorInstanceInfo FactorBacktestController::getInstanceInfo(const QString& resolvedInstanceId) const
{
    const QString normalizedInstanceId = resolvedInstanceId.trimmed();
    if (normalizedInstanceId.isEmpty()) {
        return {};
    }

    if (m_instanceInfoCache.contains(normalizedInstanceId)) {
        return m_instanceInfoCache.value(normalizedInstanceId);
    }

    factor::FactorInstanceInfo instanceInfo = m_instanceInfoOverrideForTests
        ? m_instanceInfoOverrideForTests(normalizedInstanceId)
        : (m_instanceManager
            ? m_instanceManager->getInstanceInfo(normalizedInstanceId.toStdString())
            : factor::FactorInstanceInfo{});

    m_instanceInfoCache.insert(normalizedInstanceId, instanceInfo);

    return instanceInfo;
}

factor::BacktestConfig FactorBacktestController::buildBacktestConfig(const QString& resolvedInstanceId,
                                                                     const QString& groupText,
                                                                     const QString& startDate,
                                                                     const QString& endDate) const
{
    factor::BacktestConfig config;
    config.instanceId = resolvedInstanceId.toStdString();
    const QStringList overrideStockCodes = normalizeStockPoolSymbols(m_selectedStockPoolSymbols);
    QSet<QString> overrideStockCodeSet;
    for (const QString& stockCode : overrideStockCodes) {
        overrideStockCodeSet.insert(stockCode);
    }

    QString effectiveStartDate = startDate;
    QString effectiveEndDate = endDate;
    const QString dataSourceMode = normalizeDataSourceMode(m_dataSourceMode);
    if (dataSourceMode.isEmpty()) {
        throw std::runtime_error("数据源模式非法，仅允许 cache 或 database");
    }

    if (dataSourceMode == "cache") {
        if (m_selectedDatasetId <= 0) {
            throw std::runtime_error("请选择缓存集后再开始回测");
        }

        const auto datasetInfo = DataServiceCache::getInstance().getDataSetInfo(m_selectedDatasetId);
        if (datasetInfo.id <= 0) {
            throw std::runtime_error("所选缓存集无效，请重新选择");
        }
        if (!isLatestBacktestDataset(datasetInfo)) {
            throw std::runtime_error("所选缓存集当前不可用于因子回测，请重新选择包含可用字段的数据集");
        }

        const QVariantList datasetRows = DataServiceCache::getInstance().getDataSetById(m_selectedDatasetId);
        if (datasetRows.isEmpty()) {
            throw std::runtime_error("所选缓存集为空，无法用于回测");
        }

        if (effectiveStartDate.isEmpty() && datasetInfo.startDate.isValid()) {
            effectiveStartDate = datasetInfo.startDate.toString("yyyy-MM-dd");
        }
        if (effectiveEndDate.isEmpty() && datasetInfo.endDate.isValid()) {
            effectiveEndDate = datasetInfo.endDate.toString("yyyy-MM-dd");
        }
        config.datasetId = m_selectedDatasetId;
        QStringList effectiveStockCodes;
        effectiveStockCodes.reserve(datasetInfo.stockCodes.size());
        for (const QString& stockCode : datasetInfo.stockCodes) {
            const QString normalizedStockCode = stockCode.trimmed().toUpper();
            if (normalizedStockCode.isEmpty()) {
                continue;
            }
            if (!overrideStockCodeSet.isEmpty() && !overrideStockCodeSet.contains(normalizedStockCode)) {
                continue;
            }
            effectiveStockCodes.append(normalizedStockCode);
        }

        if (!overrideStockCodeSet.isEmpty() && effectiveStockCodes.isEmpty()) {
            throw std::runtime_error("覆盖后的股票池不在当前缓存集范围内，请重新选择");
        }

        for (const QString& stockCode : effectiveStockCodes) {
            if (!stockCode.trimmed().isEmpty()) {
                config.allowedStockCodes.push_back(stockCode.trimmed().toStdString());
            }
        }

        struct CacheRowOrderKey {
            int rowIndex = -1;
            QString symbol;
            QString tradeDate;
            bool closeValid = false;
            double close = std::numeric_limits<double>::quiet_NaN();
        };

        std::vector<CacheRowOrderKey> orderedRows;
        orderedRows.reserve(static_cast<size_t>(datasetRows.size()));
        QDate anchorStartDate;
        for (int rowIndex = 0; rowIndex < datasetRows.size(); ++rowIndex) {
            const QVariantMap row = datasetRows.at(rowIndex).toMap();
            const QString symbol = row.value("symbol").toString().trimmed().toUpper();
            QString tradeDate = normalizeTradeDateText(row.value("trade_date").toString());
            if (tradeDate.isEmpty()) {
                tradeDate = normalizeTradeDateText(row.value("date").toString());
            }

            if (!overrideStockCodeSet.isEmpty() && !overrideStockCodeSet.contains(symbol)) {
                continue;
            }
            if (symbol.isEmpty() || tradeDate.isEmpty()) {
                continue;
            }

            bool closeOk = false;
            const double close = row.value("close").toDouble(&closeOk);
            CacheRowOrderKey orderKey;
            orderKey.rowIndex = rowIndex;
            orderKey.symbol = symbol;
            orderKey.tradeDate = tradeDate;
            orderKey.closeValid = closeOk && std::isfinite(close) && close > 0.0;
            orderKey.close = orderKey.closeValid ? close : std::numeric_limits<double>::quiet_NaN();
            orderedRows.push_back(std::move(orderKey));

            const QDate tradeDateValue = QDate::fromString(tradeDate, "yyyy-MM-dd");
            if (tradeDateValue.isValid() && tradeDateValue >= QDate::fromString(effectiveStartDate, "yyyy-MM-dd")) {
                if (!anchorStartDate.isValid() || tradeDateValue < anchorStartDate) {
                    anchorStartDate = tradeDateValue;
                }
            }
        }

        std::sort(orderedRows.begin(), orderedRows.end(), [](const CacheRowOrderKey& lhs, const CacheRowOrderKey& rhs) {
            if (lhs.tradeDate != rhs.tradeDate) {
                return lhs.tradeDate < rhs.tradeDate;
            }
            if (lhs.symbol != rhs.symbol) {
                return lhs.symbol < rhs.symbol;
            }
            return lhs.rowIndex < rhs.rowIndex;
        });

        factor::ArrowMarketData::Builder arrowBuilder;
        const factor::FactorInstanceInfo instanceInfo = getInstanceInfo(resolvedInstanceId);
        const std::shared_ptr<factor::BaseFactor> factorInstance = m_factorInstanceOverrideForTests
            ? m_factorInstanceOverrideForTests(resolvedInstanceId)
            : (m_instanceManager
                ? m_instanceManager->createInstance(resolvedInstanceId.toStdString())
                : nullptr);
        if (!factorInstance) {
            throw std::runtime_error("未能创建因子实例，无法解析缓存预热需求");
        }
        const FactorWarmupRequirement warmupRequirement = loadWarmupRequirement(instanceInfo, factorInstance);
        const size_t warmupRowCount = appendWindowWarmupRows(
            arrowBuilder,
            m_database,
            effectiveStartDate,
            effectiveStockCodes,
            resolvedInstanceId,
            anchorStartDate,
            warmupRequirement);

        size_t appendedCacheRowCount = 0;
        for (const CacheRowOrderKey& orderedRow : orderedRows) {
            const QVariantMap row = datasetRows.at(orderedRow.rowIndex).toMap();
            std::unordered_map<std::string, double> numericFields;
            bool hasNumericField = false;
            for (auto it = row.begin(); it != row.end(); ++it) {
                bool valueOk = false;
                const double numericValue = it.value().toDouble(&valueOk);
                if (!valueOk || !std::isfinite(numericValue)) {
                    continue;
                }
                numericFields[it.key().trimmed().toStdString()] = numericValue;
                hasNumericField = true;
            }
            if (!hasNumericField && !orderedRow.closeValid) {
                continue;
            }
            if (arrowBuilder.appendRow(
                    orderedRow.symbol.toStdString(),
                    orderedRow.tradeDate.toStdString(),
                    orderedRow.close,
                    numericFields)) {
                ++appendedCacheRowCount;
            }
        }

        if (appendedCacheRowCount == 0) {
            throw std::runtime_error("所选缓存集缺少可用于因子计算的 symbol/date/数值字段，暂时无法用于回测");
        }

        config.marketDataCacheKey = QStringLiteral("controller|%1|%2|%3|%4")
            .arg(config.datasetId)
            .arg(QString::fromStdString(config.startDate))
            .arg(QString::fromStdString(config.endDate))
            .arg(static_cast<qulonglong>(arrowBuilder.rowCount()))
            .toStdString();
        config.preparedArrowData = arrowBuilder.finish();
        if (!config.preparedArrowData) {
            throw std::runtime_error("缓存行情 Arrow 数据构建失败，禁止继续回测");
        }

        qDebug() << "FactorBacktestController: 构建缓存回测配置"
                 << "datasetId=" << m_selectedDatasetId
                 << "startDate=" << effectiveStartDate
                 << "endDate=" << effectiveEndDate
                 << "stockCodeCount=" << effectiveStockCodes.size()
                 << "cachedBarCount=" << static_cast<qulonglong>(config.preparedArrowData->rowCount())
                 << "warmupRowCount=" << static_cast<qulonglong>(warmupRowCount);
    } else if (dataSourceMode == "database") {
        config.datasetId = -1;
        for (const QString& stockCode : overrideStockCodes) {
            config.allowedStockCodes.push_back(stockCode.toStdString());
        }
    } else {
        throw std::runtime_error("数据源模式非法，仅允许 cache 或 database");
    }

    effectiveStartDate = effectiveStartDate.trimmed();
    effectiveEndDate = effectiveEndDate.trimmed();
    if (effectiveStartDate.isEmpty()) {
        throw std::runtime_error("回测开始日期缺失，禁止使用默认兜底日期");
    }
    if (effectiveEndDate.isEmpty()) {
        throw std::runtime_error("回测结束日期缺失，禁止使用默认兜底日期");
    }

    config.startDate = effectiveStartDate.toStdString();
    config.endDate = effectiveEndDate.toStdString();
    config.numGroups = parseGroupCount(groupText);

    const QVariantMap appliedRiskConfig = mergeRiskConfigurations(
        loadAppliedRiskConfiguration(),
        loadCurrentRiskConfiguration());
    if (appliedRiskConfig.isEmpty()) {
        throw std::runtime_error("未检测到已应用风控配置，回测禁止使用兜底参数");
    }

    const bridge::config::StrategyStructureResolverSet resolverSet;
    const QVariantMap effectiveRiskConfig = mergeRiskConfigurations(appliedRiskConfig, m_backtestRuntimeParams);
    const bridge::config::StrategyStructureResolution resolvedStructures = resolverSet.resolve(QVariantMap{}, effectiveRiskConfig);

    auto requireConfiguredValue = [](const QVariantMap& source,
                                     const QStringList& aliases,
                                     const QString& fieldName) -> QVariant {
        const QVariant value = firstConfiguredValue(source, aliases);
        if (!value.isValid() || value.isNull()) {
            throw std::runtime_error(QStringLiteral("风控模块缺少必填配置: %1").arg(fieldName).toStdString());
        }
        if (value.typeId() == QMetaType::QString && value.toString().trimmed().isEmpty()) {
            throw std::runtime_error(QStringLiteral("风控模块缺少必填配置: %1").arg(fieldName).toStdString());
        }
        return value;
    };

    const QVariant commissionRawValue = requireConfiguredValue(
        resolvedStructures.backtestAssumptions,
        {QStringLiteral("commissionRate"), QStringLiteral("commission_rate"), QStringLiteral("commission"), QStringLiteral("transactionCost"), QStringLiteral("transaction_cost")},
        QStringLiteral("commissionRate"));

    const QVariant forwardDaysRawValue = requireConfiguredValue(
        resolvedStructures.backtestAssumptions,
        {QStringLiteral("forwardDays"), QStringLiteral("forward_days"), QStringLiteral("holdingPeriod"), QStringLiteral("holding_period")},
        QStringLiteral("forwardDays"));

    const QVariant rebalanceDaysRawValue = requireConfiguredValue(
        resolvedStructures.executionPolicy,
        {QStringLiteral("rebalanceDays"), QStringLiteral("rebalance_days"), QStringLiteral("rebalancePeriod"), QStringLiteral("rebalance_period"), QStringLiteral("rebalancingPeriod"), QStringLiteral("rebalanceFrequency")},
        QStringLiteral("rebalanceDays"));

    config.forwardDays = normalizedPositiveInt(forwardDaysRawValue, 0);
    config.rebalanceDays = normalizedPositiveInt(rebalanceDaysRawValue, 0);
    if (config.forwardDays <= 0 || config.rebalanceDays <= 0) {
        throw std::runtime_error("风控模块字段 forwardDays/rebalanceDays 非法");
    }

    const double commissionRate = normalizeBacktestAssumptionRate(
        commissionRawValue,
        normalizedPercentRate(commissionRawValue, std::numeric_limits<double>::quiet_NaN()),
        std::numeric_limits<double>::quiet_NaN(),
        0.05,
        "commissionRate");
    if (!std::isfinite(commissionRate)) {
        throw std::runtime_error("风控配置字段 commissionRate 非法");
    }

    const QVariant slippageRawValue = requireConfiguredValue(
        resolvedStructures.backtestAssumptions,
        {QStringLiteral("slippageRate"), QStringLiteral("slippage")},
        QStringLiteral("slippageRate"));
    const double slippageRate = normalizeBacktestAssumptionRate(
        slippageRawValue,
        normalizedPercentRate(slippageRawValue, std::numeric_limits<double>::quiet_NaN()),
        std::numeric_limits<double>::quiet_NaN(),
        0.05,
        "slippageRate");
    if (!std::isfinite(slippageRate)) {
        throw std::runtime_error("风控配置字段 slippageRate 非法");
    }

    config.slippageRate = slippageRate;
    config.transactionCost = commissionRate + slippageRate;

    const QVariant riskFreeRawValue = requireConfiguredValue(
        resolvedStructures.backtestAssumptions,
        {QStringLiteral("riskFreeRate"), QStringLiteral("risk_free_rate")},
        QStringLiteral("riskFreeRate"));
    config.riskFreeRate = normalizeBacktestAssumptionRate(
        riskFreeRawValue,
        normalizedPercentRate(riskFreeRawValue, std::numeric_limits<double>::quiet_NaN()),
        std::numeric_limits<double>::quiet_NaN(),
        0.30,
        "riskFreeRate");
    if (!std::isfinite(config.riskFreeRate)) {
        throw std::runtime_error("风控配置字段 riskFreeRate 非法");
    }

    const QString benchmarkSymbol = requireConfiguredValue(
        resolvedStructures.backtestAssumptions,
        {QStringLiteral("benchmarkSymbol")},
        QStringLiteral("benchmarkSymbol")).toString().trimmed().toUpper();
    if (benchmarkSymbol.isEmpty()) {
        throw std::runtime_error("风控配置字段 benchmarkSymbol 非法");
    }
    config.benchmarkSymbol = benchmarkSymbol.toStdString();

    config.stopLossRate = normalizedPercentRate(requireConfiguredValue(
        resolvedStructures.ruleProfile,
        {QStringLiteral("stopLossPercent"), QStringLiteral("stop_loss"), QStringLiteral("stopLoss")},
        QStringLiteral("stopLossPercent")), std::numeric_limits<double>::quiet_NaN());
    config.takeProfitRate = normalizedPercentRate(requireConfiguredValue(
        resolvedStructures.ruleProfile,
        {QStringLiteral("takeProfitPercent"), QStringLiteral("take_profit"), QStringLiteral("takeProfit")},
        QStringLiteral("takeProfitPercent")), std::numeric_limits<double>::quiet_NaN());
    config.maxDrawdownLimit = normalizedPercentRate(requireConfiguredValue(
        resolvedStructures.ruleProfile,
        {QStringLiteral("maxDrawdownLimit"), QStringLiteral("max_drawdown_limit")},
        QStringLiteral("maxDrawdownLimit")), std::numeric_limits<double>::quiet_NaN());
    config.maxDailyLoss = normalizedPercentRate(requireConfiguredValue(
        resolvedStructures.ruleProfile,
        {QStringLiteral("maxDailyLoss"), QStringLiteral("max_daily_loss")},
        QStringLiteral("maxDailyLoss")), std::numeric_limits<double>::quiet_NaN());
    config.maxPositionPercent = normalizedPercentRate(requireConfiguredValue(
        resolvedStructures.ruleProfile,
        {QStringLiteral("maxPositionPercent"), QStringLiteral("max_position_percent"), QStringLiteral("maxPositionSize")},
        QStringLiteral("maxPositionPercent")), std::numeric_limits<double>::quiet_NaN());
    config.maxTotalExposure = normalizedPercentRate(requireConfiguredValue(
        resolvedStructures.ruleProfile,
        {QStringLiteral("maxTotalExposure"), QStringLiteral("max_total_exposure")},
        QStringLiteral("maxTotalExposure")), std::numeric_limits<double>::quiet_NaN());

    if (!std::isfinite(config.stopLossRate)
        || !std::isfinite(config.takeProfitRate)
        || !std::isfinite(config.maxDrawdownLimit)
        || !std::isfinite(config.maxDailyLoss)
        || !std::isfinite(config.maxPositionPercent)
        || !std::isfinite(config.maxTotalExposure)) {
        throw std::runtime_error("风控模块约束配置包含非法数值");
    }

    config.enableDateParallelism = true;

    qDebug() << "FactorBacktestController: 最终回测成本参数"
             << "commissionRate=" << commissionRate
             << "slippageRate=" << config.slippageRate
             << "transactionCost=" << config.transactionCost
             << "riskFreeRate=" << config.riskFreeRate
             << "forwardDays=" << config.forwardDays
             << "rebalanceDays=" << config.rebalanceDays;

    return config;
}

QVariantMap FactorBacktestController::buildResultMap(const QString& requestedFactorId,
                                                     const factor::BacktestResult& result) const
{
    const double annualizationFactor = annualizationFactorForForwardDays(result.config.forwardDays);
    QVariantList groups;
    groups.reserve(static_cast<int>(result.groupResult.groupReturns.size()));
    for (int index = 0; index < static_cast<int>(result.groupResult.groupReturns.size()); ++index) {
        const double groupReturn = result.groupResult.groupReturns[static_cast<size_t>(index)];
        QVariantMap groupMap;
        groupMap["groupId"] = index + 1;
        groupMap["groupName"] = QString("第%1组").arg(index + 1);
        groupMap["return"] = groupReturn;
        groupMap["stockCount"] = index < static_cast<int>(result.groupResult.groupStockCounts.size())
            ? result.groupResult.groupStockCounts[static_cast<size_t>(index)]
            : 0;
        groupMap["minFactorValue"] = index < static_cast<int>(result.groupResult.minFactorValues.size())
            ? result.groupResult.minFactorValues[static_cast<size_t>(index)]
            : 0.0;
        groupMap["maxFactorValue"] = index < static_cast<int>(result.groupResult.maxFactorValues.size())
            ? result.groupResult.maxFactorValues[static_cast<size_t>(index)]
            : 0.0;
        groupMap["annualizedReturn"] = groupReturn * annualizationFactor;
        groupMap["annualReturn"] = groupReturn * annualizationFactor;
        groupMap["volatility"] = QVariant();
        groupMap["sharpeRatio"] = QVariant();
        groupMap["maxDrawdown"] = QVariant();
        groupMap["winRate"] = QVariant();
        groupMap["profitFactor"] = QVariant();
        groupMap["calmarRatio"] = QVariant();
        groupMap["sortinoRatio"] = QVariant();
        groupMap["alpha"] = QVariant();
        groupMap["beta"] = QVariant();
        groupMap["trackingError"] = QVariant();
        groupMap["informationRatio"] = QVariant();
        groups.append(groupMap);
    }

    QVariantMap icirMap;
    icirMap["icValue"] = result.icirResult.icMean;
    icirMap["irValue"] = result.icirResult.ir;
    icirMap["icTStat"] = 0.0;
    icirMap["icPValue"] = 1.0;
    icirMap["icPositiveRate"] = result.icirResult.icPositiveRatio;
    icirMap["isSignificant"] = std::abs(result.icirResult.icMean) > 0.03;
    icirMap["icSeries"] = toVariantList(result.icirResult.icSeries);
    icirMap["irSeries"] = QVariantList();
    icirMap["conclusion"] = QString("IC均值: %1, IR: %2, IC正率: %3%")
        .arg(result.icirResult.icMean, 0, 'f', 4)
        .arg(result.icirResult.ir, 0, 'f', 4)
        .arg(result.icirResult.icPositiveRatio * 100.0, 0, 'f', 1);

    QVariantMap summaryMap;
    summaryMap["topGroupReturn"] = result.groupResult.topGroupReturn;
    summaryMap["bottomGroupReturn"] = result.groupResult.bottomGroupReturn;
    summaryMap["spreadReturn"] = result.groupResult.longShortReturn;
    summaryMap["monotonicity"] = calculateGroupMonotonicity(result);
    summaryMap["discrimination"] = calculateGroupDiscrimination(result);
    summaryMap["winRate"] = normalizedWinRateRatio(result.winRate);
    summaryMap["sharpeRatio"] = result.sharpeRatio;
    summaryMap["maxDrawdown"] = result.maxDrawdown;
    summaryMap["annualReturn"] = result.annualReturn;
    summaryMap["benchmarkAnnualReturn"] = result.benchmarkAnnualReturn;
    summaryMap["excessAnnualReturn"] = result.excessAnnualReturn;
    summaryMap["longShortAnnualReturn"] = result.annualReturn;
    summaryMap["trackingError"] = result.trackingError;
    summaryMap["informationRatio"] = result.informationRatio;
    summaryMap["profitFactor"] = result.profitFactor;
    summaryMap["alpha"] = result.alpha;
    summaryMap["beta"] = result.beta;
    summaryMap["turnoverRate"] = result.turnoverRate;
    summaryMap["dataCoverage"] = result.dataCoverage;

    if (!result.groupResult.groupReturns.empty()) {
        const double firstGroupReturn = result.groupResult.groupReturns.front();
        const double lastGroupReturn = result.groupResult.groupReturns.back();
        const double firstGroupAnnualizedReturn = firstGroupReturn * annualizationFactor;
        const double lastGroupAnnualizedReturn = lastGroupReturn * annualizationFactor;
        const double rawTopBottomSpread = firstGroupReturn - lastGroupReturn;
        const double costAdjustedTopBottomSpread = rawTopBottomSpread - (2.0 * result.config.transactionCost);
        const double riskAdjustedAveragePeriodLongShort = result.groupResult.longShortReturn;

        qDebug().noquote() << QStringLiteral(
            "FactorBacktestController: 因子 %1 分组/多空对账 firstGroup=%2 lastGroup=%3 firstAnnual=%4 lastAnnual=%5 rawTopBottomSpread=%6 costAdjustedTopBottomSpread=%7 riskAdjustedAveragePeriodLongShort=%8 riskAdjustedAnnualLongShort=%9 transactionCost=%10")
                                  .arg(requestedFactorId.isEmpty() ? QStringLiteral("<unknown>") : requestedFactorId)
                                  .arg(QString::number(firstGroupReturn, 'f', 6))
                                  .arg(QString::number(lastGroupReturn, 'f', 6))
                                  .arg(QString::number(firstGroupAnnualizedReturn, 'f', 6))
                                  .arg(QString::number(lastGroupAnnualizedReturn, 'f', 6))
                                  .arg(QString::number(rawTopBottomSpread, 'f', 6))
                                  .arg(QString::number(costAdjustedTopBottomSpread, 'f', 6))
                                  .arg(QString::number(riskAdjustedAveragePeriodLongShort, 'f', 6))
                                  .arg(QString::number(result.annualReturn, 'f', 6))
                                  .arg(QString::number(result.config.transactionCost, 'f', 6));
    }

    // 风控指标
    QVariantMap riskMetrics;
    riskMetrics["volatility"] = result.volatility;
    riskMetrics["downsideDeviation"] = result.downsideDeviation;
    riskMetrics["sortinoRatio"] = result.sortinoRatio;
    riskMetrics["calmarRatio"] = result.calmarRatio;
    riskMetrics["valueAtRisk"] = result.valueAtRisk;
    riskMetrics["conditionalVaR"] = result.conditionalVaR;
    riskMetrics["riskTriggeredCount"] = result.riskTriggeredCount;
    riskMetrics["riskControlSummary"] = QString::fromStdString(result.riskControlSummary);
    summaryMap["riskMetrics"] = riskMetrics;

    QVariantMap configMap;
    configMap["factorId"] = requestedFactorId;
    configMap["instanceId"] = QString::fromStdString(result.instanceId);
    configMap["factorName"] = QString::fromStdString(result.instanceName);
    configMap["startDate"] = QString::fromStdString(result.config.startDate);
    configMap["endDate"] = QString::fromStdString(result.config.endDate);
    configMap["numGroups"] = result.config.numGroups;
    configMap["forwardDays"] = result.config.forwardDays;
    configMap["rebalanceDays"] = result.config.rebalanceDays;
    configMap["transactionCost"] = result.config.transactionCost;
    configMap["slippageRate"] = result.config.slippageRate;
    configMap["riskFreeRate"] = result.config.riskFreeRate;
    configMap["benchmarkSymbol"] = QString::fromStdString(result.config.benchmarkSymbol);
    configMap["stopLossRate"] = result.config.stopLossRate;
    configMap["stopLossPercent"] = result.config.stopLossRate;
    configMap["takeProfitRate"] = result.config.takeProfitRate;
    configMap["takeProfitPercent"] = result.config.takeProfitRate;
    configMap["maxDrawdownLimit"] = result.config.maxDrawdownLimit;
    configMap["maxDailyLoss"] = result.config.maxDailyLoss;
    configMap["maxPositionPercent"] = result.config.maxPositionPercent;
    configMap["maxTotalExposure"] = result.config.maxTotalExposure;
    configMap["datasetId"] = result.config.datasetId;
    configMap["dataSourceMode"] = normalizeDataSourceMode(m_dataSourceMode);
    QVariantList selectedSymbols;
    for (const std::string& stockCode : result.config.allowedStockCodes) {
        selectedSymbols.append(QString::fromStdString(stockCode));
    }
    configMap["symbol_pool"] = selectedSymbols;
    configMap["symbolPool"] = selectedSymbols;
    configMap["selectedSymbols"] = selectedSymbols;
    const QVariantMap appliedRiskConfig = loadAppliedRiskConfiguration();
    const bridge::config::StrategyStructureResolverSet resolverSet;
    const bridge::config::StrategyStructureResolution resolvedStructures = resolverSet.resolve(QVariantMap{}, appliedRiskConfig);
    configMap["ruleProfileSnapshot"] = resolvedStructures.ruleProfile;
    configMap["backtestAssumptionsSnapshot"] = resolvedStructures.backtestAssumptions;

    QVariantMap resultMap;
    resultMap["taskId"] = QString::fromStdString(result.resultId.to_string());
    resultMap["executionTime"] = result.executionTimeMs;
    resultMap["success"] = true;
    resultMap["status"] = QString::fromStdString(result.status);
    resultMap["config"] = configMap;
    resultMap["groups"] = groups;
    resultMap["icirResult"] = icirMap;
    resultMap["summary"] = summaryMap;
    resultMap["turnoverRate"] = result.turnoverRate;
    resultMap["profitFactor"] = result.profitFactor;
    return resultMap;
}

QVariantMap FactorBacktestController::buildAggregatedResultMap() const
{
    if (m_batchResultMaps.empty()) {
        return {};
    }

    QVariantList orderedResults;
    orderedResults.reserve(static_cast<int>(m_batchResultMaps.size()));
    for (const auto& resultMap : m_batchResultMaps) {
        if (!resultMap.isEmpty()) {
            orderedResults.append(resultMap);
        }
    }

    QVariantMap firstResult = orderedResults.isEmpty() ? QVariantMap() : orderedResults.first().toMap();
    if (orderedResults.size() == 1) {
        return firstResult;
    }

    int totalExecutionTime = 0;
    for (const QVariant& item : orderedResults) {
        totalExecutionTime += item.toMap().value("executionTime").toInt();
    }

    QVariantMap configMap = firstResult.value("config").toMap();
    configMap["factorIds"] = m_batchFactorIds;

    QVariantMap aggregate = firstResult;
    aggregate["config"] = configMap;
    aggregate["results"] = orderedResults;
    aggregate["factorIds"] = m_batchFactorIds;
    aggregate["factorCount"] = orderedResults.size();
    aggregate["executionTime"] = totalExecutionTime;
    aggregate["success"] = true;
    aggregate["status"] = QStringLiteral("SUCCESS");
    aggregate.remove("groups");
    aggregate.remove("icirResult");
    aggregate.remove("summary");
    aggregate.remove("turnoverRate");
    return aggregate;
}

void FactorBacktestController::resetResults()
{
    m_backtestResult.clear();
    m_groupResults.clear();
    m_icirResult.clear();
    m_summaryStats.clear();
}

void FactorBacktestController::shutdownBacktestInfrastructure()
{
    if (m_progressTimer) {
        m_progressTimer->stop();
    }

    if (m_executor) {
        for (const auto& pendingTask : m_pendingBacktestTasks) {
            m_executor->cancel(pendingTask.taskId);
        }
    }

    if (m_threadPool) {
        m_threadPool->shutdown(true);
    }

    m_pendingBacktestTasks.clear();
    m_hasActiveTask = false;
    m_isRunning = false;
}

void FactorBacktestController::cancelBacktest()
{
    if (!m_isRunning && m_pendingBacktestTasks.empty()) {
        return;
    }

    qDebug() << "FactorBacktestController: 取消回测请求";
    m_cancelRequested.store(true);

    if (m_progressTimer) {
        m_progressTimer->stop();
    }

    if (m_executor) {
        for (const auto& pendingTask : m_pendingBacktestTasks) {
            m_executor->cancel(pendingTask.taskId);
        }
    }

    finalizeBacktestFailure(QStringLiteral("回测已取消"), true);
}

void FactorBacktestController::resetBatchState()
{
    m_batchFactorIds.clear();
    m_batchResultMaps.clear();
    m_pendingBacktestTasks.clear();
    m_pendingGroupText.clear();
    m_pendingStartDate.clear();
    m_pendingEndDate.clear();
    m_activeFactorIndex = 0;
}

bool FactorBacktestController::saveResultToFile(const QString& filePath) const
{
    if (m_backtestResult.isEmpty()) {
        qWarning() << "FactorBacktestController: 没有可保存的回测结果";
        return false;
    }

    QFile file(filePath);
    QDir dir = QFileInfo(file).dir();
    if (!dir.exists() && !dir.mkpath(".")) {
        qWarning() << "FactorBacktestController: 无法创建结果目录:" << dir.path();
        return false;
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "FactorBacktestController: 无法写入回测结果文件:" << filePath;
        return false;
    }

    const QJsonDocument doc(QJsonObject::fromVariantMap(m_backtestResult));
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool FactorBacktestController::loadResultFromFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.exists()) {
        return false;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "FactorBacktestController: 无法读取回测结果文件:" << filePath;
        return false;
    }

    const QByteArray data = file.readAll();
    file.close();

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "FactorBacktestController: 回测结果文件不是有效JSON:" << filePath;
        return false;
    }

    applyPersistedResult(doc.object().toVariantMap());
    return true;
}

void FactorBacktestController::applyPersistedResult(const QVariantMap& result)
{
    if (result.isEmpty()) {
        return;
    }

    if (!m_lastPreflightFailures.isEmpty()) {
        m_lastPreflightFailures.clear();
        emit lastPreflightFailuresChanged(m_lastPreflightFailures);
    }

    m_backtestResult = result;
    const bool hasAggregatedResults = result.value("results").canConvert<QVariantList>()
        && !result.value("results").toList().isEmpty();
    if (hasAggregatedResults) {
        m_groupResults = QVariantList();
        m_icirResult = QVariantMap();
        m_summaryStats = QVariantMap();
    } else {
        m_groupResults = result.value("groups").toList();
        m_icirResult = result.value("icirResult").toMap();
        m_summaryStats = result.value("summary").toMap();
    }

    emit backtestResultChanged(m_backtestResult);
    emit groupResultsChanged(m_groupResults);
    emit icirResultChanged(m_icirResult);
    emit summaryStatsChanged(m_summaryStats);
}

bool FactorBacktestController::persistLatestResult() const
{
    return saveResultToFile(persistedResultFilePath());
}

bool FactorBacktestController::clearPersistedResult() const
{
    const QString filePath = persistedResultFilePath();
    QFile file(filePath);
    if (!file.exists()) {
        return true;
    }

    if (!file.remove()) {
        qWarning() << "FactorBacktestController: 无法清理历史回测结果文件:" << filePath;
        return false;
    }

    return true;
}

bool FactorBacktestController::clearBacktestCache()
{
    bool clearedAny = clearPersistedResult();

    if (m_cacheManager) {
        m_cacheManager->clearAll();
        clearedAny = true;
    }

    resetResults();
    return clearedAny;
}

QString FactorBacktestController::persistedResultFilePath() const
{
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (baseDir.trimmed().isEmpty()) {
        baseDir = QDir::currentPath() + "/data/runtime";
    }

    QDir dir(baseDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    if (!dir.exists("factor_backtest")) {
        dir.mkpath("factor_backtest");
    }
    return dir.filePath("factor_backtest/latest_result.json");
}

void FactorBacktestController::pollBacktestProgress()
{
    if (m_pendingBacktestTasks.empty()) {
        return;
    }

    if (!m_executor || !m_hasActiveTask) {
        return;
    }

    for (auto it = m_pendingBacktestTasks.begin(); it != m_pendingBacktestTasks.end();) {
        if (!it->future) {
            it = m_pendingBacktestTasks.erase(it);
            continue;
        }

        if (it->future->wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }

        PendingBacktestTask task = std::move(*it);
        it = m_pendingBacktestTasks.erase(it);

        try {
            factor::BacktestResult result = task.future->get();
            if (result.status == "SUCCESS") {
                finalizeBacktestSuccess(task.requestedFactorId, result, task.batchIndex);
                if (m_pendingBacktestTasks.empty()) {
                    return;
                }
                continue;
            }

            for (const auto& pendingTask : m_pendingBacktestTasks) {
                m_executor->cancel(pendingTask.taskId);
            }

            if (result.status == "CANCELLED") {
                finalizeBacktestFailure("回测已取消", true);
            } else {
                finalizeBacktestFailure(QString::fromStdString(result.errorMessage.empty() ? "因子回测执行失败" : result.errorMessage), false);
            }
            return;
        } catch (const std::exception& e) {
            for (const auto& pendingTask : m_pendingBacktestTasks) {
                m_executor->cancel(pendingTask.taskId);
            }
            finalizeBacktestFailure(QString::fromUtf8(e.what()), false);
            return;
        }
    }

    const int totalFactors = (std::max)(1, static_cast<int>(m_batchFactorIds.size()));
    int pendingProgressSum = 0;
    int pendingProgressCount = 0;
    QString statusText = QStringLiteral("正在并行执行回测");
    for (const auto& task : m_pendingBacktestTasks) {
        const auto progressInfo = m_executor->getProgress(task.taskId);
        if (progressInfo.status == "NOT_FOUND") {
            continue;
        }

        pendingProgressSum += progressInfo.progress;
        ++pendingProgressCount;
        const QString stepText = QString::fromStdString(progressInfo.currentStep.empty() ? progressInfo.status : progressInfo.currentStep);
        if (!stepText.trimmed().isEmpty()) {
            statusText = stepText;
        }
    }

    const int averagePendingProgress = pendingProgressCount > 0 ? pendingProgressSum / pendingProgressCount : 0;
    const int progressValue = (m_activeFactorIndex * 100 + averagePendingProgress) / totalFactors;
    const int currentFactorIndex = (pendingProgressCount > 0 ? m_activeFactorIndex + 1 : m_activeFactorIndex);
    if (totalFactors > 1) {
        statusText = QStringLiteral("正在并行执行回测 (批次 %1/%2)：%3")
                         .arg((std::max)(1, currentFactorIndex))
                         .arg(totalFactors)
                         .arg(statusText);
    }

    if (m_progress != progressValue) {
        m_progress = progressValue;
        emit progressChanged(m_progress);
    }

    if (m_status != statusText) {
        m_status = statusText;
        emit statusChanged(m_status);
    }

    if (totalFactors > 1) {
        emit backtestProgressDetailed(m_progress, m_status, (std::max)(1, currentFactorIndex), totalFactors);
    }

    emit backtestProgress(m_progress, m_status);
}

void FactorBacktestController::finalizeBacktestSuccess(const QString& requestedFactorId,
                                                       const factor::BacktestResult& result,
                                                       size_t batchIndex)
{
    const QVariantMap resultMap = buildResultMap(requestedFactorId, result);
    syncBacktestMetricsToFactor(requestedFactorId, result);
    if (batchIndex >= m_batchResultMaps.size()) {
        m_batchResultMaps.resize(batchIndex + 1);
    }
    m_batchResultMaps[batchIndex] = resultMap;
    ++m_activeFactorIndex;

    const int totalFactors = (std::max)(1, static_cast<int>(m_batchFactorIds.size()));
    const int progressValue = (m_activeFactorIndex * 100) / totalFactors;
    const QString statusText = QStringLiteral("正在并行执行回测 (%1/%2)：当前因子已完成，等待结果汇总")
                                   .arg(m_activeFactorIndex)
                                   .arg(totalFactors);
    if (m_progress != progressValue) {
        m_progress = progressValue;
        emit progressChanged(m_progress);
    }
    if (m_status != statusText) {
        m_status = statusText;
        emit statusChanged(m_status);
    }
    emit backtestProgress(m_progress, m_status);

    if (m_activeFactorIndex < m_batchFactorIds.size()) {
        return;
    }

    if (m_progressTimer) {
        m_progressTimer->stop();
    }

    const QVariantMap finalResultMap = buildAggregatedResultMap();
    m_backtestResult = finalResultMap;
    if (m_batchResultMaps.size() > 1) {
        m_groupResults = QVariantList();
        m_icirResult = QVariantMap();
        m_summaryStats = QVariantMap();
    } else {
        m_groupResults = finalResultMap.value("groups").toList();
        m_icirResult = finalResultMap.value("icirResult").toMap();
        m_summaryStats = finalResultMap.value("summary").toMap();
    }
    m_isRunning = false;
    m_progress = 100;
    m_status = "回测完成";
    m_hasActiveTask = false;
    m_activeRequestedFactorId.clear();
    m_cancelRequested.store(false);

    emit isRunningChanged(m_isRunning);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    emit backtestResultChanged(m_backtestResult);
    emit groupResultsChanged(m_groupResults);
    emit icirResultChanged(m_icirResult);
    emit summaryStatsChanged(m_summaryStats);
    emit backtestProgressDetailed(100, m_status, m_groupResults.size(), m_groupResults.size());
    emit backtestCompleted(finalResultMap);

    resetBatchState();

    if (!persistLatestResult()) {
        qWarning() << "FactorBacktestController: 自动保存最新回测结果失败:" << persistedResultFilePath();
    }
}

void FactorBacktestController::syncBacktestMetricsToFactor(const QString& requestedFactorId,
                                                           const factor::BacktestResult& result)
{
    const QString factorId = requestedFactorId.trimmed();
    if (factorId.isEmpty()) {
        return;
    }

    const QVariantMap appliedRiskConfig = m_loadAppliedRiskConfigOverrideForTests
        ? m_loadAppliedRiskConfigOverrideForTests()
        : loadAppliedRiskConfiguration();
    const MetricPersistenceQualificationThresholds thresholds = resolveMetricPersistenceThresholds(appliedRiskConfig);

    if (!isQualifiedForMetricPersistence(result, thresholds)) {
        qDebug() << "FactorBacktestController: 回测结果未达合格阈值，跳过指标写库:" << factorId
                 << "icMean=" << result.icirResult.icMean
                 << "ir=" << result.icirResult.ir
                 << "coverage=" << result.dataCoverage
                 << "maxDD=" << result.maxDrawdown
                 << "turnover=" << result.turnoverRate;
        return;
    }

    FactorService* factorService = FactorService::instance();
    if (!factorService) {
        qWarning() << "FactorBacktestController: 因子服务不可用，无法同步回测指标:" << factorId;
        return;
    }

    QVariantMap factorData = factorService->getFactorById(factorId);
    if (factorData.isEmpty()) {
        qWarning() << "FactorBacktestController: 未找到因子定义，无法同步回测指标:" << factorId;
        return;
    }

    factorData["icValue"] = result.icirResult.icMean;
    factorData["irValue"] = result.icirResult.ir;
    factorData["turnoverRate"] = result.turnoverRate;

    if (!factorService->updateFactor(factorId, factorData)) {
        qWarning() << "FactorBacktestController: 回测后同步因子指标失败:" << factorId;
    }
}

void FactorBacktestController::finalizeBacktestFailure(const QString& errorMessage,
                                                       bool cancelled)
{
    if (m_progressTimer) {
        m_progressTimer->stop();
    }
    resetResults();
    clearPersistedResult();
    m_isRunning = false;
    m_progress = cancelled ? 0 : m_progress;
    m_status = cancelled ? "已取消" : "回测失败";
    m_hasActiveTask = false;
    m_activeRequestedFactorId.clear();
    resetBatchState();
    m_cancelRequested.store(false);

    emit isRunningChanged(m_isRunning);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    emit backtestResultChanged(m_backtestResult);
    emit groupResultsChanged(m_groupResults);
    emit icirResultChanged(m_icirResult);
    emit summaryStatsChanged(m_summaryStats);

    if (cancelled) {
        emit backtestCancelled();
    } else {
        emit backtestFailed(errorMessage);
        qCritical() << "回测失败:" << errorMessage;
    }
}
