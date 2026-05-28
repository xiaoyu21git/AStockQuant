// FactorService.cpp
// 因子桥接服务实现。

#include "FactorService.h"
#include "FactorViewModel.h"
#include "DataFetchFieldContractUtils.h"
#include "DatabaseConnectionManager.h"
#include "FactorBacktestWarmupUtils.h"
#include "FactorRequirementInferenceUtils.h"
#include "../../../infrastructure/include/database/FactorRepository.h"
#include "../../../infrastructure/include/database/QtMySQLDatabase.h"
#include "../../../domain/factor/include/ArrowMarketData.h"
#include "../../../domain/factor/include/DataAvailabilityChecker.h"
#include "../../../domain/factor/include/FactorConfigAccess.h"
#include "../../../domain/factor/include/FactorInstanceManager.h"
#include "../../../domain/factor/include/HistoricalView.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QMutexLocker>
#include <QReadLocker>
#include <QSet>
#include <QWriteLocker>

#include <cmath>
#include <optional>

namespace {

constexpr int kMaxRecentFactorOperationReports = 8;
constexpr int kMaxQueryWindowCacheEntries = 16;
constexpr const char* kEarliestHistoricalDate = "1900-01-01";
constexpr const char* kActiveInstanceStatus = "ACTIVE";

int requiredHistoricalTradingDays(const std::shared_ptr<factor::BaseFactor>& factorInstance)
{
    if (!factorInstance) {
        return 1;
    }

    return (std::max)(1, factorInstance->getBoundaryRules().minDataPoints);
}

QDate resolveHistoricalQueryStartDate(
    const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
    const QDate& anchorDate,
    const int requiredTradingDays)
{
    if (!database || !anchorDate.isValid()) {
        return {};
    }

    const int normalizedTradingDays = (std::max)(1, requiredTradingDays);
    if (normalizedTradingDays == 1) {
        return anchorDate;
    }

    const auto result = database->executeQuery(
        QStringLiteral(
            "SELECT DISTINCT trade_date "
            "FROM daily_bar "
            "WHERE trade_date <= :anchor_date "
            "ORDER BY trade_date ASC"),
        {{QStringLiteral(":anchor_date"), anchorDate.toString("yyyy-MM-dd")}});

    QStringList ascendingTradeDates;
    ascendingTradeDates.reserve(static_cast<int>(result.rowCount()));
    for (const auto& row : result.getRows()) {
        const QString tradeDate = row.getString(QStringLiteral("trade_date")).trimmed();
        if (!tradeDate.isEmpty()) {
            ascendingTradeDates.append(tradeDate);
        }
    }

    return factor::warmup::resolveWarmupHistoryStartDate(
        anchorDate.addDays(1),
        ascendingTradeDates,
        normalizedTradingDays);
}

QString removedReason()
{
    return QStringLiteral("因子引擎侧业务代码已删除");
}

std::vector<std::string> normalizeRequestedSymbols(const QStringList& rawSymbols)
{
    std::vector<std::string> symbols;
    QSet<QString> seenSymbols;
    for (const QString& rawSymbol : rawSymbols) {
        const QString normalizedSymbol = rawSymbol.trimmed().toUpper();
        if (normalizedSymbol.isEmpty() || seenSymbols.contains(normalizedSymbol)) {
            continue;
        }

        seenSymbols.insert(normalizedSymbol);
        symbols.push_back(normalizedSymbol.toStdString());
    }

    return symbols;
}

QStringList normalizeRequestedSymbolList(const QStringList& rawSymbols)
{
    QStringList symbols;
    QSet<QString> seenSymbols;
    for (const QString& rawSymbol : rawSymbols) {
        const QString normalizedSymbol = rawSymbol.trimmed().toUpper();
        if (normalizedSymbol.isEmpty() || seenSymbols.contains(normalizedSymbol)) {
            continue;
        }

        seenSymbols.insert(normalizedSymbol);
        symbols.append(normalizedSymbol);
    }

    return symbols;
}

QStringList normalizeRequestedFactorIdList(const QStringList& rawFactorIds)
{
    QStringList factorIds;
    QSet<QString> seenFactorIds;
    for (const QString& rawFactorId : rawFactorIds) {
        const QString normalizedFactorId = rawFactorId.trimmed();
        if (normalizedFactorId.isEmpty() || seenFactorIds.contains(normalizedFactorId)) {
            continue;
        }

        seenFactorIds.insert(normalizedFactorId);
        factorIds.append(normalizedFactorId);
    }

    return factorIds;
}

QString buildQueryWindowCacheKey(const QString& minDate,
                                 const QString& maxDate,
                                 const QStringList& normalizedSymbols)
{
    return minDate.trimmed()
        + QStringLiteral("|")
        + maxDate.trimmed()
        + QStringLiteral("|")
        + normalizedSymbols.join(QStringLiteral(","));
}

QString positionalParamKey(int index)
{
    return QStringLiteral("__pos_%1").arg(index, 6, 10, QLatin1Char('0'));
}

std::map<QString, QVariant> makePositionalParams(std::initializer_list<QVariant> values)
{
    std::map<QString, QVariant> params;
    int index = 0;
    for (const QVariant& value : values) {
        params.emplace(positionalParamKey(index++), value);
    }
    return params;
}

QString symbolInfoIndustrySelect(const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                                 const QString& alias)
{
    Q_UNUSED(database)
    const QString normalizedAlias = alias.trimmed().isEmpty() ? QStringLiteral("s") : alias.trimmed();
    return QStringLiteral("TRIM(COALESCE(%1.industry_code, '')) AS industry_code ").arg(normalizedAlias);
}

std::optional<factor::FactorType> factorTypeFromVariant(const QVariant& rawValue)
{
    if (!rawValue.isValid() || rawValue.isNull()) {
        return std::nullopt;
    }

    bool ok = false;
    const int typeIndex = rawValue.toInt(&ok);
    if (ok) {
        const factor::FactorType factorType = factor::factorTypeFromIndex(typeIndex);
        if (factorType != factor::FactorType::UNKNOWN) {
            return factorType;
        }
    }

    return std::nullopt;
}

std::optional<factor::FactorType> factorTypeFromJsonValue(const QJsonValue& rawValue)
{
    if (rawValue.isDouble()) {
        const factor::FactorType factorType = factor::factorTypeFromIndex(rawValue.toInt());
        if (factorType != factor::FactorType::UNKNOWN) {
            return factorType;
        }
        return std::nullopt;
    }

    return std::nullopt;
}

QJsonObject parseJsonObject(const QString& rawJson)
{
    if (rawJson.trimmed().isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(rawJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }

    return document.object();
}

QJsonArray toJsonArray(const QStringList& values)
{
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return array;
}

QJsonObject buildDataRequirementsObject(const factor::bridge::FactorRequirementProfile& profile)
{
    QJsonObject object;
    object.insert(QStringLiteral("required"), toJsonArray(profile.requiredFields.orderedValues()));
    object.insert(QStringLiteral("optional"), toJsonArray(profile.optionalFields.orderedValues()));
    if (profile.sourceTable != factor::SourceTable::UNKNOWN) {
        object.insert(QStringLiteral("sourceTable"), static_cast<int>(profile.sourceTable));
    }
    return object;
}

bool isJsonInteger(const QJsonValue& value)
{
    if (!value.isDouble()) {
        return false;
    }

    const double rawValue = value.toDouble();
    return std::isfinite(rawValue) && std::floor(rawValue) == rawValue;
}

bool requireNumericEnumField(const QJsonObject& calculation,
                             const QString& fieldName,
                             QString* errorMessage,
                             const QString& factorId)
{
    if (!calculation.contains(fieldName)) {
        return true;
    }

    const QJsonValue rawValue = calculation.value(fieldName);
    if (!isJsonInteger(rawValue)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("因子 %1 的 calculation.%2 必须是数值枚举，禁止字符串")
                                .arg(factorId, fieldName);
        }
        return false;
    }

    return true;
}

bool requireNumericEnumArrayField(const QJsonObject& calculation,
                                  const QString& fieldName,
                                  QString* errorMessage,
                                  const QString& factorId)
{
    if (!calculation.contains(fieldName)) {
        return true;
    }

    const QJsonValue rawValue = calculation.value(fieldName);
    if (!rawValue.isArray()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("因子 %1 的 calculation.%2 必须是数值枚举数组")
                                .arg(factorId, fieldName);
        }
        return false;
    }

    const QJsonArray values = rawValue.toArray();
    for (const QJsonValue& entry : values) {
        if (!isJsonInteger(entry)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("因子 %1 的 calculation.%2 必须是数值枚举数组，禁止字符串")
                                    .arg(factorId, fieldName);
            }
            return false;
        }
    }

    return true;
}

bool validateCalculationEnumContract(const factor::FactorType factorType,
                                     const QJsonObject& calculation,
                                     QString* errorMessage,
                                     const QString& factorId)
{
    const auto requireField = [&](const char* fieldName) {
        return requireNumericEnumField(calculation, QString::fromUtf8(fieldName), errorMessage, factorId);
    };
    const auto requireArrayField = [&](const char* fieldName) {
        return requireNumericEnumArrayField(calculation, QString::fromUtf8(fieldName), errorMessage, factorId);
    };

    switch (factorType) {
    case factor::FactorType::VALUE:
        return requireArrayField("valuationMetrics")
            && requireField("frequency")
            && requireField("standardization");
    case factor::FactorType::MOMENTUM:
        return requireField("frequency")
            && requireField("standardization")
            && requireField("type")
            && requireField("adjustPriceType");
    case factor::FactorType::SIZE:
        return requireField("sizeMetric")
            && requireField("frequency")
            && requireField("standardization");
    case factor::FactorType::QUALITY:
        return requireField("metric")
            && requireField("frequency")
            && requireField("standardization");
    case factor::FactorType::GROWTH:
        return requireField("frequency")
            && requireField("standardization")
            && requireArrayField("growthMetrics");
    case factor::FactorType::DIVIDEND:
        return requireField("frequency")
            && requireField("standardization")
            && requireField("metric")
            && requireArrayField("dividendMetrics");
    case factor::FactorType::TECHNICAL:
        return requireField("frequency")
            && requireField("standardization")
            && requireArrayField("technicalIndicators")
            && requireField("technicalCombinationMode")
            && requireField("turnoverStabilityMetric")
            && requireField("technicalPriceType");
    case factor::FactorType::LIQUIDITY:
        return requireField("frequency")
            && requireField("standardization")
            && requireField("metric");
    case factor::FactorType::MACRO:
        return requireField("frequency")
            && requireField("standardization")
            && requireArrayField("macroDimensions")
            && requireArrayField("macroIndicators")
            && requireField("macroFrequency")
            && requireField("priceType");
    case factor::FactorType::INDUSTRY:
        return requireField("frequency")
            && requireField("standardization")
            && requireField("industryMetric")
            && requireField("sectorType");
    case factor::FactorType::SENTIMENT:
        return requireField("frequency")
            && requireField("standardization")
            && requireField("metric")
            && requireField("sentimentSource");
    case factor::FactorType::LOW_VOLATILITY:
        return requireField("frequency")
            && requireField("standardization")
            && requireArrayField("components");
    case factor::FactorType::CUSTOM:
        return requireField("frequency")
            && requireField("standardization");
    default:
        return true;
    }
}

QJsonObject loadExistingInstanceConfig(astock::database::QtMySQLDatabase* database,
                                      const QString& instanceId,
                                      const QString& factorId)
{
    if (!database) {
        return {};
    }

    const auto loadByQuery = [database](const QString& sql,
                                        const std::map<QString, QVariant>& params) -> QJsonObject {
        const astock::database::QueryResult result = database->executeQuery(sql, params);
        if (result.isEmpty()) {
            return {};
        }
        return parseJsonObject(result.getRow(0).getString(QStringLiteral("full_config")));
    };

    if (!instanceId.trimmed().isEmpty()) {
        const QJsonObject byInstanceId = loadByQuery(
            QStringLiteral(
                "SELECT CAST(full_config AS CHAR) AS full_config "
                "FROM factor_instance WHERE instance_id = ? ORDER BY updated_at DESC LIMIT 1"),
            makePositionalParams({instanceId.trimmed()}));
        if (!byInstanceId.isEmpty()) {
            return byInstanceId;
        }
    }

    if (!factorId.trimmed().isEmpty()) {
        return loadByQuery(
            QStringLiteral(
                "SELECT CAST(full_config AS CHAR) AS full_config "
                "FROM factor_instance WHERE factor_id = ? ORDER BY updated_at DESC LIMIT 1"),
            makePositionalParams({factorId.trimmed()}));
    }

    return {};
}

std::optional<QJsonObject> buildCanonicalInstanceConfig(const QVariantMap& factorData,
                                                        const QJsonObject& existingConfig,
                                                        QString* errorMessage)
{
    const QString factorId = factorData.value(QStringLiteral("factorId")).toString().trimmed();
    const QVariantMap providedParameters = factorData.value(QStringLiteral("parameters")).toMap();
    const QVariantMap providedCalculation = factorData.value(QStringLiteral("calculation")).toMap();

    std::optional<factor::FactorType> factorType = factorTypeFromVariant(factorData.value(QStringLiteral("factorType")));
    if (!factorType.has_value()) {
        factorType = factorTypeFromJsonValue(existingConfig.value(QStringLiteral("factorType")));
    }
    if (!factorType.has_value()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("因子 %1 缺少合法的 factorType").arg(factorId);
        }
        return std::nullopt;
    }

    QJsonObject config = existingConfig;
    config.insert(QStringLiteral("factorType"), factor::factorTypeIndex(*factorType));

    const auto applyStringField = [&factorData, &config](const QString& key) {
        if (factorData.contains(key)) {
            config.insert(key, factorData.value(key).toString().trimmed());
        }
    };

    applyStringField(QStringLiteral("factorName"));
    applyStringField(QStringLiteral("displayName"));
    applyStringField(QStringLiteral("description"));
    applyStringField(QStringLiteral("majorCategory"));
    applyStringField(QStringLiteral("subCategory"));

    if (factorData.contains(QStringLiteral("tags"))) {
        config.insert(QStringLiteral("tags"), QJsonArray::fromStringList(factorData.value(QStringLiteral("tags")).toStringList()));
    }

    QVariantMap calculation = providedParameters;
    if (calculation.isEmpty()) {
        calculation = providedCalculation;
    }

    if (!calculation.isEmpty()) {
        const factor::bridge::FactorRequirementProfile profile =
            factor::bridge::resolveFactorRequirementProfile(*factorType, calculation);
        if (!profile.supported && !profile.allowEmptyRequiredFields) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("因子 %1 的参数无法推导有效 dataRequirements").arg(factorId);
            }
            return std::nullopt;
        }

        QJsonObject calculationObject = QJsonObject::fromVariantMap(calculation);
        if (!validateCalculationEnumContract(*factorType, calculationObject, errorMessage, factorId)) {
            return std::nullopt;
        }
        if (profile.metric.isValid()) {
            calculationObject.insert(QStringLiteral("metric"), QJsonValue::fromVariant(profile.metric));
        }

        config.insert(QStringLiteral("calculation"), calculationObject);
        config.insert(QStringLiteral("dataRequirements"), buildDataRequirementsObject(profile));
    }

    return config;
}

class FactorServiceHistoricalView final : public factor::HistoricalView
{
public:
    explicit FactorServiceHistoricalView(std::shared_ptr<factor::ArrowMarketData> data)
        : data_(std::move(data))
    {
        if (!data_) {
            throw std::runtime_error("FactorService: failed to initialize ArrowMarketData");
        }
    }

    bool hasField(const std::string& field) const override {
        return data_->getColumn(field) != nullptr;
    }

    std::optional<double> getValue(const std::string& symbol,
                                   const std::string& date,
                                   const std::string& field) const override {
        const double value = data_->getValue(symbol, date, field);
        if (!std::isfinite(value)) {
            return std::nullopt;
        }
        return value;
    }

    std::vector<factor::HistoricalDataPoint> getSeries(const std::string& symbol,
                                                       const std::string& startDate,
                                                       const std::string& endDate,
                                                       const std::string& field) const override {
        return data_->getSeries(symbol, startDate, endDate, field);
    }

    std::vector<std::string> getAvailableSymbols(const std::string& date) const override {
        return data_->getAvailableSymbols(date);
    }

    std::unordered_map<std::string, double> getCrossSection(const std::string& date,
                                                            const std::string& field,
                                                            const std::vector<std::string>& symbols = {}) const override {
        return data_->getCrossSection(date, field, symbols);
    }

    std::unordered_map<std::string, std::unordered_map<std::string, double>> getBatchCrossSections(
        const std::string& date,
        const std::vector<std::string>& symbols,
        const std::vector<std::string>& fields) const override
    {
        return data_->getBatchCrossSections(date, symbols, fields);
    }

    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> getBatchTimeSeries(
        const std::vector<std::string>& symbols,
        const std::string& startDate,
        const std::string& endDate,
        const std::vector<std::string>& fields) const override
    {
        return data_->getBatchTimeSeries(symbols, startDate, endDate, fields);
    }

    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> getBatchTimeSeries(
        const std::vector<std::string>& symbols,
        const std::string& anchorDate,
        int window,
        const std::vector<std::string>& fields) const override
    {
        std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> batchSeries;
        if (!data_ || window <= 0) {
            return batchSeries;
        }

        for (const auto& field : fields) {
            const auto seriesBySymbol = data_->getBatchTimeSeries(symbols, field, window, anchorDate);
            auto& fieldSeries = batchSeries[field];
            for (size_t index = 0; index < symbols.size() && index < seriesBySymbol.size(); ++index) {
                fieldSeries.emplace(symbols[index], seriesBySymbol[index]);
            }
        }

        return batchSeries;
    }

private:
    std::shared_ptr<factor::ArrowMarketData> data_;
};

QVariant normalizeQueryValue(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return QVariant();
    }

    if (value.canConvert<QDateTime>()) {
        const QDateTime dateTime = value.toDateTime();
        if (dateTime.isValid()) {
            return dateTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        }
    }

    if (value.canConvert<QDate>()) {
        const QDate date = value.toDate();
        if (date.isValid()) {
            return date.toString(QStringLiteral("yyyy-MM-dd"));
        }
    }

    return value;
}

QVariantMap convertRowToVariantMap(const astock::database::QueryResultRow& row)
{
    QVariantMap record;
    for (const auto& [key, value] : row.getValues()) {
        record.insert(key, normalizeQueryValue(value));
    }
    return record;
}

QStringList normalizeRequestedDates(const QStringList& dates)
{
    QStringList normalized;
    QSet<QString> seen;
    for (const QString& rawDate : dates) {
        const QString date = rawDate.trimmed();
        if (date.isEmpty() || seen.contains(date)) {
            continue;
        }
        seen.insert(date);
        normalized.append(date);
    }
    std::sort(normalized.begin(), normalized.end());
    return normalized;
}

std::vector<factor::CachedMarketBar> buildCachedBarsFromRows(const QVariantList& rows)
{
    std::vector<factor::CachedMarketBar> cachedBars;
    cachedBars.reserve(static_cast<size_t>(rows.size()));

    const QString symbolField = factor::bridge::CommonFieldKeys::SYMBOL.toQString();
    const QString tradeDateField = factor::bridge::CommonFieldKeys::TRADE_DATE.toQString();
    const QString legacyDateField = factor::bridge::LegacyCleaningFieldKeys::DATE.toQString();
    const QString closeField = factor::bridge::MarketBarFieldKeys::CLOSE.toQString();
    const QString preAdjFactorField = factor::bridge::MarketBarFieldKeys::PRE_ADJ_FACTOR.toQString();
    const QString postAdjFactorField = factor::bridge::MarketBarFieldKeys::POST_ADJ_FACTOR.toQString();
    const QString adjFactorField = factor::bridge::LegacyCleaningFieldKeys::ADJ_FACTOR.toQString();

    for (const QVariant& rowValue : rows) {
        const QVariantMap row = rowValue.toMap();
        const QString symbol = row.value(symbolField).toString().trimmed().toUpper();
        const QString tradeDate = row.value(tradeDateField, row.value(legacyDateField)).toString().trimmed();
        bool closeOk = false;
        const double close = row.value(closeField).toDouble(&closeOk);
        if (symbol.isEmpty() || tradeDate.isEmpty() || !closeOk || !std::isfinite(close)) {
            continue;
        }

        factor::CachedMarketBar bar;
        bar.symbol = symbol.toStdString();
        bar.tradeDate = tradeDate.toStdString();
        bar.close = close;
        bar.numericFields[closeField.toStdString()] = close;

        for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
            const QString key = factor::bridge::runtimeContractFieldName(it.key());
            if (key.isEmpty()
                || key == symbolField
                || key == tradeDateField
                || key == legacyDateField) {
                continue;
            }

            bool ok = false;
            const double numericValue = it.value().toDouble(&ok);
            if (!ok || !std::isfinite(numericValue)) {
                continue;
            }

            bar.numericFields[key.toStdString()] = numericValue;
            if (key == postAdjFactorField || key == preAdjFactorField) {
                bar.numericFields[adjFactorField.toStdString()] = numericValue;
            }
        }

        cachedBars.push_back(std::move(bar));
    }

    return cachedBars;
}

QString calculationMessage(const factor::CalculationResult& result)
{
    if (result.metadata.has("empty_reason")) {
        return QString::fromStdString(result.metadata.get("empty_reason").asString()).trimmed();
    }
    return QString::fromStdString(result.dataStatus.message).trimmed();
}

QVariantMap buildFactorValueErrorResult(const QString& factorId,
                                        const QString& instanceId,
                                        const QString& date,
                                        const QString& errorMessage)
{
    QVariantMap result;
    result[QStringLiteral("status")] = QStringLiteral("error");
    result[QStringLiteral("factorId")] = factorId.trimmed();
    result[QStringLiteral("instanceId")] = instanceId.trimmed();
    result[QStringLiteral("date")] = date.trimmed();
    result[QStringLiteral("error")] = errorMessage.trimmed();
    result[QStringLiteral("message")] = errorMessage.trimmed();
    return result;
}

QString normalizedFactorIdFromData(const QVariantMap& factorData)
{
    return factorData.value(QStringLiteral("factorId")).toString().trimmed();
}

QVariantList toVariantList(const std::vector<QVariantMap>& factors)
{
    QVariantList list;
    list.reserve(static_cast<int>(factors.size()));
    for (const QVariantMap& factor : factors) {
        list.append(factor);
    }
    return list;
}

QMap<QString, QVariantMap> toMemoryCacheMap(const std::vector<QVariantMap>& factors)
{
    QMap<QString, QVariantMap> cache;
    for (const QVariantMap& factor : factors) {
        const QString factorId = normalizedFactorIdFromData(factor);
        if (!factorId.isEmpty()) {
            cache.insert(factorId, factor);
        }
    }
    return cache;
}

bool factorMatchesKeyword(const QVariantMap& factor, const QString& keyword)
{
    const QString normalizedKeyword = keyword.trimmed().toLower();
    if (normalizedKeyword.isEmpty()) {
        return true;
    }

    const auto containsKeyword = [&normalizedKeyword](const QVariant& value) {
        return value.toString().trimmed().toLower().contains(normalizedKeyword);
    };

    if (containsKeyword(factor.value(QStringLiteral("factorId")))
        || containsKeyword(factor.value(QStringLiteral("factorName")))
        || containsKeyword(factor.value(QStringLiteral("displayName")))
        || containsKeyword(factor.value(QStringLiteral("description")))
        || containsKeyword(factor.value(QStringLiteral("majorCategory")))
        || containsKeyword(factor.value(QStringLiteral("category")))) {
        return true;
    }

    const QStringList tags = factor.value(QStringLiteral("tags")).toStringList();
    for (const QString& tag : tags) {
        if (tag.trimmed().toLower().contains(normalizedKeyword)) {
            return true;
        }
    }
    return false;
}

bool factorMatchesCategory(const QVariantMap& factor, const QString& category)
{
    const QString normalizedCategory = category.trimmed();
    if (normalizedCategory.isEmpty()) {
        return true;
    }
    return factor.value(QStringLiteral("category")).toString().trimmed() == normalizedCategory
        || factor.value(QStringLiteral("majorCategory")).toString().trimmed() == normalizedCategory;
}

bool factorMatchesTags(const QVariantMap& factor, const QStringList& tags)
{
    if (tags.isEmpty()) {
        return true;
    }

    const QSet<QString> requestedTags = QSet<QString>(tags.begin(), tags.end());
    const QStringList factorTags = factor.value(QStringLiteral("tags")).toStringList();
    for (const QString& tag : factorTags) {
        if (requestedTags.contains(tag)) {
            return true;
        }
    }
    return false;
}

QVariantList filterFactorList(const QVariantList& factors,
                              const std::function<bool(const QVariantMap&)>& predicate)
{
    QVariantList filtered;
    for (const QVariant& factorValue : factors) {
        const QVariantMap factor = factorValue.toMap();
        if (factor.isEmpty()) {
            continue;
        }
        if (predicate(factor)) {
            filtered.append(factor);
        }
    }
    return filtered;
}

QVariantMap buildOperationReport(const QString& operation,
                                 const QString& factorId,
                                 bool success,
                                 const QString& stage,
                                 const QString& message)
{
    QVariantMap report;
    report[QStringLiteral("operation")] = operation.trimmed();
    report[QStringLiteral("factorId")] = factorId.trimmed();
    report[QStringLiteral("success")] = success;
    report[QStringLiteral("stage")] = stage.trimmed();
    report[QStringLiteral("message")] = message.trimmed();
    report[QStringLiteral("timestamp")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    return report;
}

} // namespace

FactorService* FactorService::m_instance = nullptr;
QMutex FactorService::m_instanceMutex;

FactorService* FactorService::instance()
{
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        m_instance = new FactorService();
        m_instance->initialize();
    }
    return m_instance;
}

FactorService::FactorService(QObject* parent)
    : QObject(parent)
    , m_repository(nullptr)
    , m_database(nullptr)
    , m_dataChecker(nullptr)
    , m_factorCacheManager(nullptr)
    , m_factorInstanceManager(nullptr)
    , m_initialized(false)
    , m_isLoading(false)
    , m_cacheLoaded(false)
    , m_autoInitialize(false)
    , m_viewModel(new FactorViewModel(this))
    , m_mutationInProgress(false)
{
    connect(this, &FactorService::factorsLoaded, this, [this](const QVariantList& factors) {
        if (m_viewModel) {
            m_viewModel->updateData(factors);
        }
    });
}

FactorService::~FactorService() = default;

void FactorService::initialize()
{
    QMutexLocker locker(&m_initMutex);
    if (m_initialized.load()) {
        return;
    }

    m_isLoading = true;
    emit isLoadingChanged();

    try {
        initializeRepository();
        m_initialized = true;
        m_isLoading = false;

        emit initializedChanged();
        emit isLoadingChanged();
    } catch (const std::exception& e) {
        {
            QWriteLocker cacheLocker(&m_rwLock);
            m_memoryCache.clear();
        }

        m_cacheLoaded = false;
        m_isLoading = false;

        if (m_viewModel) {
            m_viewModel->updateData({});
        }

        qWarning() << "FactorService::initialize: 初始化失败 -" << e.what();
        emit factorsLoaded({});
        emit isLoadingChanged();
        emit cacheLoadedChanged();
    }
}

void FactorService::initializeRepository()
{
    auto& dbManager = astock::database::DatabaseConnectionManager::instance();
    if (!dbManager.initialize()) {
        throw std::runtime_error("数据库连接初始化失败");
    }

    m_database = dbManager.getDatabase();
    if (!m_database) {
        throw std::runtime_error("数据库实例不可用");
    }

    if (!m_repository) {
        m_repository = std::make_shared<astock::database::FactorRepository>();
    }

    if (!m_repository->initialize()) {
        throw std::runtime_error("因子仓储初始化失败");
    }
}

QVariantList FactorService::loadFactorsFromDatabase()
{
    if (!m_repository) {
        return {};
    }

    const std::vector<QVariantMap> repositoryFactors = m_repository->findAll();
    const QVariantList factors = toVariantList(repositoryFactors);

    {
        QWriteLocker locker(&m_rwLock);
        m_memoryCache = toMemoryCacheMap(repositoryFactors);
    }

    return factors;
}

bool FactorService::initializeFactorDomainRuntime()
{
    if (m_factorInstanceManager) {
        return true;
    }

    if (!m_database) {
        m_database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    }
    if (!m_database) {
        return false;
    }

    if (!m_dataChecker) {
        m_dataChecker = std::make_shared<factor::DataAvailabilityChecker>(m_database);
    }
    if (!m_factorInstanceManager) {
        m_factorInstanceManager = std::make_shared<factor::FactorInstanceManager>(m_database, m_dataChecker);
    }

    return static_cast<bool>(m_factorInstanceManager);
}

bool FactorService::mutationInProgress() const
{
    QMutexLocker locker(&m_observabilityMutex);
    return m_mutationInProgress;
}

QVariantMap FactorService::lastOperationReport() const
{
    QMutexLocker locker(&m_observabilityMutex);
    return m_lastOperationReport;
}

QVariantList FactorService::recentOperationReports() const
{
    QMutexLocker locker(&m_observabilityMutex);
    return m_recentOperationReports;
}

void FactorService::setMutationInProgress(bool inProgress)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_observabilityMutex);
        if (m_mutationInProgress != inProgress) {
            m_mutationInProgress = inProgress;
            changed = true;
        }
    }

    if (changed) {
        emit mutationInProgressChanged();
    }
}

void FactorService::publishOperationReport(const QString& operation,
                                           const QString& factorId,
                                           bool success,
                                           const QString& stage,
                                           const QString& message)
{
    const QVariantMap report = buildOperationReport(operation, factorId, success, stage, message);

    {
        QMutexLocker locker(&m_observabilityMutex);
        m_lastOperationReport = report;
        m_recentOperationReports.prepend(report);
        while (m_recentOperationReports.size() > kMaxRecentFactorOperationReports) {
            m_recentOperationReports.removeLast();
        }
    }

    emit lastOperationReportChanged();
    emit recentOperationReportsChanged();
}

QString FactorService::addFactor(const QVariantMap& factorData)
{
    QMutexLocker mutationLocker(&m_mutationMutex);
    setMutationInProgress(true);
    QVariantMap normalizedFactorData = factorData;
    const QString factorId = normalizedFactorIdFromData(normalizedFactorData);
    if (factorId.isEmpty()) {
        publishOperationReport(QStringLiteral("addFactor"), QString(), false, QStringLiteral("invalid_input"), QStringLiteral("缺少 factorId"));
        setMutationInProgress(false);
        return {};
    }

    normalizedFactorData[QStringLiteral("factorId")] = factorId;
    if (!m_repository) {
        publishOperationReport(QStringLiteral("addFactor"), factorId, false, QStringLiteral("repository_unavailable"), QStringLiteral("因子仓储不可用"));
        setMutationInProgress(false);
        return {};
    }

    if (!m_repository->save(normalizedFactorData)) {
        publishOperationReport(QStringLiteral("addFactor"), factorId, false, QStringLiteral("repository_save_failed"), QStringLiteral("因子仓储写入失败"));
        setMutationInProgress(false);
        return {};
    }

    bool domainSyncOk = true;
    QString domainSyncErrorMessage;
    if (m_syncFactorDefinitionOverrideForTests) {
        domainSyncOk = m_syncFactorDefinitionOverrideForTests(normalizedFactorData);
    } else {
        domainSyncOk = syncFactorDefinitionToDomain(normalizedFactorData, &domainSyncErrorMessage);
    }
    if (!domainSyncOk) {
        m_repository->remove(factorId);
        publishOperationReport(
            QStringLiteral("addFactor"),
            factorId,
            false,
            QStringLiteral("sync_domain_failed_rolled_back"),
            domainSyncErrorMessage.trimmed().isEmpty()
                ? QStringLiteral("因子定义同步失败，已回滚 factors 表记录")
                : QStringLiteral("%1；已回滚 factors 表记录").arg(domainSyncErrorMessage.trimmed()));
        setMutationInProgress(false);
        return {};
    }

    {
        QWriteLocker locker(&m_rwLock);
        m_memoryCache[factorId] = normalizedFactorData;
    }

    emit factorAdded(factorId, normalizedFactorData);
    emit dataChanged();
    publishOperationReport(QStringLiteral("addFactor"), factorId, true, QStringLiteral("completed"), QStringLiteral("因子已创建"));
    setMutationInProgress(false);
    return factorId;
}

bool FactorService::updateFactor(const QString& factorId, const QVariantMap& factorData)
{
    QMutexLocker mutationLocker(&m_mutationMutex);
    setMutationInProgress(true);
    const QString normalizedFactorId = factorId.trimmed();
    if (normalizedFactorId.isEmpty()) {
        publishOperationReport(QStringLiteral("updateFactor"), QString(), false, QStringLiteral("invalid_input"), QStringLiteral("缺少 factorId"));
        setMutationInProgress(false);
        return false;
    }
    if (!m_repository) {
        publishOperationReport(QStringLiteral("updateFactor"), normalizedFactorId, false, QStringLiteral("repository_unavailable"), QStringLiteral("因子仓储不可用"));
        setMutationInProgress(false);
        return false;
    }

    QVariantMap mergedFactorData = factorData;
    mergedFactorData[QStringLiteral("factorId")] = normalizedFactorId;

    QVariantMap existingFactor = m_repository->findById(normalizedFactorId);
    if (existingFactor.isEmpty()) {
        QReadLocker locker(&m_rwLock);
        existingFactor = m_memoryCache.value(normalizedFactorId);
    }
    if (existingFactor.isEmpty()) {
        publishOperationReport(QStringLiteral("updateFactor"), normalizedFactorId, false, QStringLiteral("not_found"), QStringLiteral("未找到待更新因子"));
        setMutationInProgress(false);
        return false;
    }
    for (auto it = existingFactor.cbegin(); it != existingFactor.cend(); ++it) {
        if (!mergedFactorData.contains(it.key())) {
            mergedFactorData.insert(it.key(), it.value());
        }
    }

    const bool repositoryUpdated = m_repository->update(normalizedFactorId, mergedFactorData);
    if (!repositoryUpdated) {
        publishOperationReport(QStringLiteral("updateFactor"), normalizedFactorId, false, QStringLiteral("repository_update_failed"), QStringLiteral("因子仓储更新失败"));
        setMutationInProgress(false);
        return false;
    }

    bool domainSyncOk = true;
    QString domainSyncErrorMessage;
    if (m_syncFactorDefinitionOverrideForTests) {
        domainSyncOk = m_syncFactorDefinitionOverrideForTests(mergedFactorData);
    } else {
        domainSyncOk = syncFactorDefinitionToDomain(mergedFactorData, &domainSyncErrorMessage);
    }
    if (!domainSyncOk) {
        m_repository->update(normalizedFactorId, existingFactor);
        publishOperationReport(
            QStringLiteral("updateFactor"),
            normalizedFactorId,
            false,
            QStringLiteral("sync_domain_failed_rolled_back"),
            domainSyncErrorMessage.trimmed().isEmpty()
                ? QStringLiteral("因子定义同步失败，已回滚 factors 表记录")
                : QStringLiteral("%1；已回滚 factors 表记录").arg(domainSyncErrorMessage.trimmed()));
        setMutationInProgress(false);
        return false;
    }

    {
        QWriteLocker locker(&m_rwLock);
        m_memoryCache[normalizedFactorId] = mergedFactorData;
    }

    emit factorUpdated(normalizedFactorId, mergedFactorData);
    emit dataChanged();
    publishOperationReport(QStringLiteral("updateFactor"), normalizedFactorId, true, QStringLiteral("completed"), QStringLiteral("因子已更新"));
    setMutationInProgress(false);
    return true;
}

bool FactorService::deleteFactor(const QString& factorId)
{
    QMutexLocker mutationLocker(&m_mutationMutex);
    setMutationInProgress(true);
    const QString normalizedFactorId = factorId.trimmed();
    if (normalizedFactorId.isEmpty()) {
        publishOperationReport(QStringLiteral("deleteFactor"), QString(), false, QStringLiteral("invalid_input"), QStringLiteral("缺少 factorId"));
        setMutationInProgress(false);
        return false;
    }

    bool domainDeleteOk = true;
    if (m_removeFactorDefinitionOverrideForTests) {
        domainDeleteOk = m_removeFactorDefinitionOverrideForTests(normalizedFactorId);
    } else {
        domainDeleteOk = removeFactorDefinitionFromDomain(normalizedFactorId);
    }
    if (!domainDeleteOk) {
        publishOperationReport(
            QStringLiteral("deleteFactor"),
            normalizedFactorId,
            false,
            QStringLiteral("delete_domain_failed"),
            QStringLiteral("factor_instance 删除失败"));
        setMutationInProgress(false);
        return false;
    }

    if (!m_repository) {
        publishOperationReport(QStringLiteral("deleteFactor"), normalizedFactorId, false, QStringLiteral("repository_unavailable"), QStringLiteral("因子仓储不可用"));
        setMutationInProgress(false);
        return false;
    }

    if (!m_repository->remove(normalizedFactorId)) {
        publishOperationReport(QStringLiteral("deleteFactor"), normalizedFactorId, false, QStringLiteral("repository_delete_failed"), QStringLiteral("因子仓储删除失败"));
        setMutationInProgress(false);
        return false;
    }

    {
        QWriteLocker locker(&m_rwLock);
        m_memoryCache.remove(normalizedFactorId);
    }

    emit factorDeleted(normalizedFactorId);
    emit dataChanged();
    publishOperationReport(QStringLiteral("deleteFactor"), normalizedFactorId, true, QStringLiteral("completed"), QStringLiteral("因子已删除"));
    setMutationInProgress(false);
    return true;
}

QVariantMap FactorService::getFactorById(const QString& factorId)
{
    const QString normalizedFactorId = factorId.trimmed();
    {
        QReadLocker locker(&m_rwLock);
        const auto it = m_memoryCache.constFind(normalizedFactorId);
        if (it != m_memoryCache.constEnd()) {
            return it.value();
        }
    }

    QVariantMap fromRepository = getFactorByIdFromRepository(normalizedFactorId);
    if (!fromRepository.isEmpty()) {
        QWriteLocker locker(&m_rwLock);
        m_memoryCache[normalizedFactorId] = fromRepository;
    }
    return fromRepository;
}

QVariantMap FactorService::getFactorByIdFromRepository(const QString& factorId)
{
    if (!m_repository) {
        return {};
    }
    return m_repository->findById(factorId.trimmed());
}

QString FactorService::resolveDomainInstanceId(const QString& factorId) const
{
    const QString normalizedFactorId = factorId.trimmed();
    if (normalizedFactorId.isEmpty()) {
        return {};
    }

    {
        QReadLocker locker(&m_rwLock);
        const auto it = m_memoryCache.constFind(normalizedFactorId);
        if (it != m_memoryCache.constEnd()) {
            const QString instanceId = it.value().value(QStringLiteral("instanceId")).toString().trimmed();
            if (!instanceId.isEmpty()) {
                return instanceId;
            }
        }
    }

    if (!m_database) {
        return {};
    }

    const auto querySingleInstanceId = [this](const QString& sql,
                                              const std::map<QString, QVariant>& params) -> QString {
        const astock::database::QueryResult result = m_database->executeQuery(sql, params);
        if (result.isEmpty()) {
            return {};
        }
        return result.getRow(0).getString(QStringLiteral("instance_id")).trimmed();
    };

    try {
        const QString directInstanceId = querySingleInstanceId(
            QStringLiteral("SELECT instance_id FROM factor_instance WHERE instance_id = :instance_id LIMIT 1"),
            {{QStringLiteral(":instance_id"), normalizedFactorId}});
        if (!directInstanceId.isEmpty()) {
            return directInstanceId;
        }

        const QString activeInstanceId = querySingleInstanceId(
            QStringLiteral("SELECT instance_id FROM factor_instance WHERE factor_id = :factor_id AND status = :status ORDER BY updated_at DESC LIMIT 1"),
            {
                {QStringLiteral(":factor_id"), normalizedFactorId},
                {QStringLiteral(":status"), QStringLiteral("ACTIVE")}
            });
        if (!activeInstanceId.isEmpty()) {
            return activeInstanceId;
        }

        return querySingleInstanceId(
            QStringLiteral("SELECT instance_id FROM factor_instance WHERE factor_id = :factor_id ORDER BY updated_at DESC LIMIT 1"),
            {{QStringLiteral(":factor_id"), normalizedFactorId}});
    } catch (const std::exception&) {
        return {};
    }
}

QVariantList FactorService::getAllFactors()
{
    if (!m_cacheLoaded.load() && m_repository) {
        const QVariantList factors = loadFactorsFromDatabase();
        emit factorsLoaded(factors);
        return factors;
    }

    QReadLocker locker(&m_rwLock);
    QVariantList factors;
    factors.reserve(m_memoryCache.size());
    for (auto it = m_memoryCache.cbegin(); it != m_memoryCache.cend(); ++it) {
        factors.append(it.value());
    }
    return factors;
}

QVariantList FactorService::searchFactors(const QString& keyword)
{
    const QVariantList factors = getAllFactors();
    return filterFactorList(factors, [&](const QVariantMap& factor) {
        return factorMatchesKeyword(factor, keyword);
    });
}

QVariantList FactorService::filterFactorsByCategory(const QString& category)
{
    const QVariantList factors = getAllFactors();
    return filterFactorList(factors, [&](const QVariantMap& factor) {
        return factorMatchesCategory(factor, category);
    });
}

QVariantList FactorService::filterFactorsByTags(const QStringList& tags)
{
    const QVariantList factors = getAllFactors();
    return filterFactorList(factors, [&](const QVariantMap& factor) {
        return factorMatchesTags(factor, tags);
    });
}

QVariantList FactorService::queryDatabaseData(const QString& minDate, const QString& maxDate)
{
    if (m_queryDatabaseDataOverrideForTests) {
        return m_queryDatabaseDataOverrideForTests(minDate.trimmed(), maxDate.trimmed());
    }
    if (!m_database) {
        return {};
    }

    const QString industrySelect = symbolInfoIndustrySelect(m_database, QStringLiteral("s"));

    QVariantList rows;
    m_database->visitQuery(
        QStringLiteral(
            "SELECT d.*, %1"
            "FROM daily_bar d "
            "LEFT JOIN symbol_info s ON s.symbol = d.symbol "
            "WHERE d.trade_date >= :min_date AND d.trade_date <= :max_date "
            "ORDER BY d.trade_date, d.symbol").arg(industrySelect),
        {
            {QStringLiteral(":min_date"), minDate.trimmed()},
            {QStringLiteral(":max_date"), maxDate.trimmed()}
        },
        [&rows](const astock::database::QueryResultRow& row) {
            rows.append(convertRowToVariantMap(row));
            return true;
        });
    return rows;
}

QVariantList FactorService::queryDatabaseDataForSymbols(const QString& minDate,
                                                        const QString& maxDate,
                                                        const QStringList& requestedSymbols)
{
    const QStringList normalizedSymbols = normalizeRequestedSymbolList(requestedSymbols);
    if (normalizedSymbols.isEmpty()) {
        return queryDatabaseData(minDate, maxDate);
    }

    if (m_queryDatabaseDataOverrideForTests) {
        return m_queryDatabaseDataOverrideForTests(minDate.trimmed(), maxDate.trimmed());
    }
    if (!m_database) {
        return {};
    }

    const QString cacheKey = buildQueryWindowCacheKey(minDate, maxDate, normalizedSymbols);
    {
        QMutexLocker locker(&m_queryWindowCacheMutex);
        const auto iterator = m_queryWindowCache.constFind(cacheKey);
        if (iterator != m_queryWindowCache.cend()) {
            INTERNAL_INFO_STREAM << "FactorService::queryDatabaseDataForSymbols cache hit"
                                 << " symbolCount=" << normalizedSymbols.size()
                                 << " minDate=" << minDate.toStdString()
                                 << " maxDate=" << maxDate.toStdString()
                                 << " rows=" << iterator.value().size();
            return iterator.value();
        }
    }

    const QString industrySelect = symbolInfoIndustrySelect(m_database, QStringLiteral("s"));

    std::map<QString, QVariant> params{
        {QStringLiteral(":min_date"), minDate.trimmed()},
        {QStringLiteral(":max_date"), maxDate.trimmed()}
    };
    QStringList symbolPlaceholders;
    symbolPlaceholders.reserve(normalizedSymbols.size());
    for (int index = 0; index < normalizedSymbols.size(); ++index) {
        const QString placeholder = QStringLiteral(":symbol_%1").arg(index);
        symbolPlaceholders.append(placeholder);
        params.emplace(placeholder, normalizedSymbols.at(index));
    }

    INTERNAL_INFO_STREAM << "FactorService::queryDatabaseDataForSymbols loading rows"
                         << " symbolCount=" << normalizedSymbols.size()
                         << " minDate=" << minDate.toStdString()
                         << " maxDate=" << maxDate.toStdString();

    QVariantList rows;
    m_database->visitQuery(
        QStringLiteral(
            "SELECT d.*, %1"
            "FROM daily_bar d "
            "LEFT JOIN symbol_info s ON s.symbol = d.symbol "
            "WHERE d.trade_date >= :min_date AND d.trade_date <= :max_date "
            "AND d.symbol IN (%2) "
            "ORDER BY d.trade_date, d.symbol /* symbol-filtered */")
            .arg(industrySelect, symbolPlaceholders.join(QStringLiteral(", "))),
        params,
        [&rows](const astock::database::QueryResultRow& row) {
            rows.append(convertRowToVariantMap(row));
            return true;
        });

    INTERNAL_INFO_STREAM << "FactorService::queryDatabaseDataForSymbols loaded rows=" << rows.size();

    {
        QMutexLocker locker(&m_queryWindowCacheMutex);
        if (!m_queryWindowCache.contains(cacheKey)) {
            if (m_queryWindowCacheOrder.size() >= kMaxQueryWindowCacheEntries) {
                const QString evictedKey = m_queryWindowCacheOrder.takeFirst();
                m_queryWindowCache.remove(evictedKey);
            }
            m_queryWindowCache.insert(cacheKey, rows);
            m_queryWindowCacheOrder.append(cacheKey);
        }
    }

    return rows;
}

QVariantMap FactorService::getFactorValuesFromDomain(const QString& factorId,
                                                     const QString& resolvedInstanceId,
                                                     const QString& date,
                                                     const QStringList& requestedSymbols)
{
    if (!m_factorInstanceManager) {
        return buildFactorValueErrorResult(
            factorId,
            resolvedInstanceId,
            date,
            QStringLiteral("因子运行时未初始化"));
    }

    auto factor = m_factorInstanceManager->createInstance(resolvedInstanceId.trimmed().toStdString());
    if (!factor) {
        return buildFactorValueErrorResult(
            factorId,
            resolvedInstanceId,
            date,
            QStringLiteral("未找到对应的因子实例"));
    }

    const QDate anchorDate = QDate::fromString(date.trimmed(), QStringLiteral("yyyy-MM-dd"));
    if (!anchorDate.isValid()) {
        return buildFactorValueErrorResult(
            factorId,
            resolvedInstanceId,
            date,
            QStringLiteral("因子查询日期非法"));
    }

    const QDate historyStartDate = resolveHistoricalQueryStartDate(
        m_database,
        anchorDate,
        requiredHistoricalTradingDays(factor));
    const QVariantList rows = historyStartDate.isValid()
        ? queryDatabaseDataForSymbols(historyStartDate.toString(QStringLiteral("yyyy-MM-dd")),
                                      date.trimmed(),
                                      requestedSymbols)
        : QVariantList{};
    if (rows.isEmpty()) {
        QVariantMap result;
        result[QStringLiteral("status")] = QStringLiteral("success");
        result[QStringLiteral("factorId")] = factorId.trimmed();
        result[QStringLiteral("instanceId")] = resolvedInstanceId.trimmed();
        result[QStringLiteral("date")] = date.trimmed();
        result[QStringLiteral("stockValues")] = QVariantMap{};
        result[QStringLiteral("message")] = QStringLiteral("指定日期前未查询到可用行情数据");
        return result;
    }

    try {
        const std::vector<factor::CachedMarketBar> cachedBars = buildCachedBarsFromRows(rows);
        if (cachedBars.empty()) {
            QVariantMap result;
            result[QStringLiteral("status")] = QStringLiteral("success");
            result[QStringLiteral("factorId")] = factorId.trimmed();
            result[QStringLiteral("instanceId")] = resolvedInstanceId.trimmed();
            result[QStringLiteral("date")] = date.trimmed();
            result[QStringLiteral("stockValues")] = QVariantMap{};
            result[QStringLiteral("message")] = QStringLiteral("可用行情数据为空，无法生成因子横截面");
            return result;
        }

        const auto arrowData = factor::ArrowMarketData::fromCachedBars(cachedBars);
        auto historicalView = std::make_shared<FactorServiceHistoricalView>(arrowData);
        std::vector<std::string> symbols = normalizeRequestedSymbols(requestedSymbols);
        if (symbols.empty()) {
            symbols = historicalView->getAvailableSymbols(date.trimmed().toStdString());
        }

        factor::CalculationContext context;
        context.date = date.trimmed().toStdString();
        context.symbols = symbols;
        context.historicalView = historicalView;

        const factor::CalculationResult calculation = factor->calculate(context);
        if (!calculation.dataStatus.isValid()) {
            return buildFactorValueErrorResult(
                factorId,
                resolvedInstanceId,
                date,
                QString::fromStdString(calculation.dataStatus.message).trimmed().isEmpty()
                    ? QStringLiteral("因子值计算失败")
                    : QString::fromStdString(calculation.dataStatus.message).trimmed());
        }

        QVariantMap stockValues;
        for (const auto& [symbol, value] : calculation.values) {
            if (std::isfinite(value)) {
                stockValues.insert(QString::fromStdString(symbol), value);
            }
        }

        QVariantMap result;
        result[QStringLiteral("status")] = QStringLiteral("success");
        result[QStringLiteral("factorId")] = factorId.trimmed();
        result[QStringLiteral("instanceId")] = resolvedInstanceId.trimmed();
        result[QStringLiteral("date")] = date.trimmed();
        result[QStringLiteral("stockValues")] = stockValues;
        const QString message = calculationMessage(calculation);
        if (!message.isEmpty()) {
            result[QStringLiteral("message")] = message;
        }
        return result;
    } catch (const std::exception& e) {
        return buildFactorValueErrorResult(
            factorId,
            resolvedInstanceId,
            date,
            QString::fromUtf8(e.what()));
    }
}

QVariantMap FactorService::getFactorValuesBatchFromDomain(const QString& factorId,
                                                          const QString& resolvedInstanceId,
                                                          const QStringList& dates)
{
    if (!m_factorInstanceManager) {
        return buildFactorValueErrorResult(
            factorId,
            resolvedInstanceId,
            {},
            QStringLiteral("因子运行时未初始化"));
    }

    const QStringList normalizedDates = normalizeRequestedDates(dates);
    if (normalizedDates.isEmpty()) {
        return buildFactorValueErrorResult(
            factorId,
            resolvedInstanceId,
            {},
            QStringLiteral("缺少有效日期范围"));
    }

    auto factor = m_factorInstanceManager->createInstance(resolvedInstanceId.trimmed().toStdString());
    if (!factor) {
        return buildFactorValueErrorResult(
            factorId,
            resolvedInstanceId,
            normalizedDates.constLast(),
            QStringLiteral("未找到对应的因子实例"));
    }

    INTERNAL_INFO_STREAM << "FactorService::getFactorValuesBatchFromDomain loading rows for factor "
                         << factorId.toStdString()
                         << ", instance=" << resolvedInstanceId.toStdString()
                         << ", requestedDates=" << normalizedDates.size()
                         << ", queryEndDate=" << normalizedDates.constLast().toStdString();

    const QDate anchorDate = QDate::fromString(normalizedDates.constFirst(), QStringLiteral("yyyy-MM-dd"));
    if (!anchorDate.isValid()) {
        return buildFactorValueErrorResult(
            factorId,
            resolvedInstanceId,
            normalizedDates.constFirst(),
            QStringLiteral("因子批量查询起始日期非法"));
    }

    const QDate historyStartDate = resolveHistoricalQueryStartDate(
        m_database,
        anchorDate,
        requiredHistoricalTradingDays(factor));
    const QVariantList rows = historyStartDate.isValid()
        ? queryDatabaseData(historyStartDate.toString(QStringLiteral("yyyy-MM-dd")), normalizedDates.constLast())
        : QVariantList{};
    if (rows.isEmpty()) {
        QVariantMap result;
        result[QStringLiteral("status")] = QStringLiteral("success");
        result[QStringLiteral("factorId")] = factorId.trimmed();
        result[QStringLiteral("instanceId")] = resolvedInstanceId.trimmed();
        result[QStringLiteral("data")] = QVariantMap{};
        result[QStringLiteral("message")] = QStringLiteral("指定日期前未查询到可用行情数据");
        return result;
    }

    try {
        const std::vector<factor::CachedMarketBar> cachedBars = buildCachedBarsFromRows(rows);
        if (cachedBars.empty()) {
            QVariantMap result;
            result[QStringLiteral("status")] = QStringLiteral("success");
            result[QStringLiteral("factorId")] = factorId.trimmed();
            result[QStringLiteral("instanceId")] = resolvedInstanceId.trimmed();
            result[QStringLiteral("data")] = QVariantMap{};
            result[QStringLiteral("message")] = QStringLiteral("可用行情数据为空，无法生成因子横截面");
            return result;
        }

        INTERNAL_INFO_STREAM << "FactorService::getFactorValuesBatchFromDomain queried rows="
                             << rows.size()
                             << " for factor " << factorId.toStdString();

        const auto arrowData = factor::ArrowMarketData::fromCachedBars(cachedBars);
        auto historicalView = std::make_shared<FactorServiceHistoricalView>(arrowData);

        QVariantMap data;
        QString firstMessage;
        INTERNAL_INFO_STREAM << "FactorService::getFactorValuesBatchFromDomain calculating dates="
                             << normalizedDates.size()
                             << " for factor " << factorId.toStdString();
        for (const QString& date : normalizedDates) {
            factor::CalculationContext context;
            context.date = date.toStdString();
            context.symbols = historicalView->getAvailableSymbols(context.date);
            context.historicalView = historicalView;

            const factor::CalculationResult calculation = factor->calculate(context);
            if (!calculation.dataStatus.isValid()) {
                return buildFactorValueErrorResult(
                    factorId,
                    resolvedInstanceId,
                    date,
                    QString::fromStdString(calculation.dataStatus.message).trimmed().isEmpty()
                        ? QStringLiteral("批量因子值计算失败")
                        : QString::fromStdString(calculation.dataStatus.message).trimmed());
            }

            QVariantMap stockValues;
            for (const auto& [symbol, value] : calculation.values) {
                if (std::isfinite(value)) {
                    stockValues.insert(QString::fromStdString(symbol), value);
                }
            }
            if (!stockValues.isEmpty()) {
                data.insert(date, stockValues);
            } else if (firstMessage.isEmpty()) {
                firstMessage = calculationMessage(calculation);
            }
        }

        QVariantMap result;
        result[QStringLiteral("status")] = QStringLiteral("success");
        result[QStringLiteral("factorId")] = factorId.trimmed();
        result[QStringLiteral("instanceId")] = resolvedInstanceId.trimmed();
        result[QStringLiteral("data")] = data;
        if (!firstMessage.isEmpty()) {
            result[QStringLiteral("message")] = firstMessage;
        }
        INTERNAL_INFO_STREAM << "FactorService::getFactorValuesBatchFromDomain finished factor "
                             << factorId.toStdString()
                             << ", producedDates=" << data.size();
        return result;
    } catch (const std::exception& e) {
        return buildFactorValueErrorResult(
            factorId,
            resolvedInstanceId,
            normalizedDates.constLast(),
            QString::fromUtf8(e.what()));
    }
}

QVariantMap FactorService::getFactorValues(const QString& factorId, const QString& date)
{
    const QString normalizedFactorId = factorId.trimmed();
    const QString normalizedDate = date.trimmed();
    if (normalizedFactorId.isEmpty() || normalizedDate.isEmpty()) {
        return buildFactorValueErrorResult(
            normalizedFactorId,
            {},
            normalizedDate,
            QStringLiteral("缺少因子 ID 或日期"));
    }

    if (!initializeFactorDomainRuntime()) {
        return buildFactorValueErrorResult(
            normalizedFactorId,
            {},
            normalizedDate,
            QStringLiteral("因子运行时初始化失败"));
    }

    const QString resolvedInstanceId = resolveDomainInstanceId(normalizedFactorId);
    if (resolvedInstanceId.isEmpty()) {
        return buildFactorValueErrorResult(
            normalizedFactorId,
            {},
            normalizedDate,
            QStringLiteral("未找到对应的因子实例 ID"));
    }

    return getFactorValuesFromDomain(normalizedFactorId, resolvedInstanceId, normalizedDate);
}

QVariantMap FactorService::getFactorValuesForSymbols(const QString& factorId,
                                                     const QString& date,
                                                     const QStringList& symbols)
{
    const QString normalizedFactorId = factorId.trimmed();
    const QString normalizedDate = date.trimmed();
    if (normalizedFactorId.isEmpty() || normalizedDate.isEmpty()) {
        return buildFactorValueErrorResult(
            normalizedFactorId,
            {},
            normalizedDate,
            QStringLiteral("缺少因子 ID 或日期"));
    }

    if (!initializeFactorDomainRuntime()) {
        return buildFactorValueErrorResult(
            normalizedFactorId,
            {},
            normalizedDate,
            QStringLiteral("因子运行时初始化失败"));
    }

    const QString resolvedInstanceId = resolveDomainInstanceId(normalizedFactorId);
    if (resolvedInstanceId.isEmpty()) {
        return buildFactorValueErrorResult(
            normalizedFactorId,
            {},
            normalizedDate,
            QStringLiteral("未找到对应的因子实例 ID"));
    }

    return getFactorValuesFromDomain(normalizedFactorId,
                                     resolvedInstanceId,
                                     normalizedDate,
                                     symbols);
}

QVariantMap FactorService::getFactorValuesForSymbolsBatch(const QStringList& factorIds,
                                                          const QString& date,
                                                          const QStringList& symbols)
{
    const QStringList normalizedFactorIds = normalizeRequestedFactorIdList(factorIds);
    const QString normalizedDate = date.trimmed();
    if (normalizedFactorIds.isEmpty() || normalizedDate.isEmpty()) {
        return buildFactorValueErrorResult(
            normalizedFactorIds.isEmpty() ? QString{} : normalizedFactorIds.constFirst(),
            {},
            normalizedDate,
            QStringLiteral("缺少因子 ID 或日期"));
    }

    if (!initializeFactorDomainRuntime()) {
        return buildFactorValueErrorResult(
            normalizedFactorIds.constFirst(),
            {},
            normalizedDate,
            QStringLiteral("因子运行时初始化失败"));
    }

    const QDate anchorDate = QDate::fromString(normalizedDate, QStringLiteral("yyyy-MM-dd"));
    if (!anchorDate.isValid()) {
        return buildFactorValueErrorResult(
            normalizedFactorIds.constFirst(),
            {},
            normalizedDate,
            QStringLiteral("因子查询日期非法"));
    }

    struct PreparedFactorRequest final {
        QString factorId;
        QString resolvedInstanceId;
        std::shared_ptr<factor::BaseFactor> factor;
    };

    QVector<PreparedFactorRequest> preparedFactors;
    preparedFactors.reserve(normalizedFactorIds.size());

    int maxRequiredTradingDays = 1;
    for (const QString& factorId : normalizedFactorIds) {
        const QString resolvedInstanceId = resolveDomainInstanceId(factorId);
        if (resolvedInstanceId.isEmpty()) {
            return buildFactorValueErrorResult(
                factorId,
                {},
                normalizedDate,
                QStringLiteral("未找到对应的因子实例 ID"));
        }

        auto factor = m_factorInstanceManager->createInstance(resolvedInstanceId.toStdString());
        if (!factor) {
            return buildFactorValueErrorResult(
                factorId,
                resolvedInstanceId,
                normalizedDate,
                QStringLiteral("未找到对应的因子实例"));
        }

        maxRequiredTradingDays = (std::max)(maxRequiredTradingDays, requiredHistoricalTradingDays(factor));
        preparedFactors.push_back(PreparedFactorRequest{factorId, resolvedInstanceId, std::move(factor)});
    }

    INTERNAL_INFO_STREAM << "FactorService::getFactorValuesForSymbolsBatch loading factors="
                         << normalizedFactorIds.size()
                         << " symbolCount=" << symbols.size()
                         << " date=" << normalizedDate.toStdString();

    const QDate historyStartDate = resolveHistoricalQueryStartDate(
        m_database,
        anchorDate,
        maxRequiredTradingDays);
    const QVariantList rows = historyStartDate.isValid()
        ? queryDatabaseDataForSymbols(historyStartDate.toString(QStringLiteral("yyyy-MM-dd")),
                                      normalizedDate,
                                      symbols)
        : QVariantList{};

    QVariantMap factorResults;
    const auto buildEmptySuccessResult = [&normalizedDate](const QString& factorId,
                                                           const QString& resolvedInstanceId,
                                                           const QString& message) {
        QVariantMap result;
        result[QStringLiteral("status")] = QStringLiteral("success");
        result[QStringLiteral("factorId")] = factorId;
        result[QStringLiteral("instanceId")] = resolvedInstanceId;
        result[QStringLiteral("date")] = normalizedDate;
        result[QStringLiteral("stockValues")] = QVariantMap{};
        if (!message.isEmpty()) {
            result[QStringLiteral("message")] = message;
        }
        return result;
    };

    if (rows.isEmpty()) {
        for (const PreparedFactorRequest& preparedFactor : preparedFactors) {
            factorResults.insert(preparedFactor.factorId,
                                 buildEmptySuccessResult(preparedFactor.factorId,
                                                         preparedFactor.resolvedInstanceId,
                                                         QStringLiteral("指定日期前未查询到可用行情数据")));
        }

        QVariantMap result;
        result[QStringLiteral("status")] = QStringLiteral("success");
        result[QStringLiteral("date")] = normalizedDate;
        result[QStringLiteral("factorValues")] = factorResults;
        return result;
    }

    try {
        const std::vector<factor::CachedMarketBar> cachedBars = buildCachedBarsFromRows(rows);
        if (cachedBars.empty()) {
            for (const PreparedFactorRequest& preparedFactor : preparedFactors) {
                factorResults.insert(preparedFactor.factorId,
                                     buildEmptySuccessResult(preparedFactor.factorId,
                                                             preparedFactor.resolvedInstanceId,
                                                             QStringLiteral("可用行情数据为空，无法生成因子横截面")));
            }

            QVariantMap result;
            result[QStringLiteral("status")] = QStringLiteral("success");
            result[QStringLiteral("date")] = normalizedDate;
            result[QStringLiteral("factorValues")] = factorResults;
            return result;
        }

        const auto arrowData = factor::ArrowMarketData::fromCachedBars(cachedBars);
        auto historicalView = std::make_shared<FactorServiceHistoricalView>(arrowData);

        std::vector<std::string> contextSymbols = normalizeRequestedSymbols(symbols);
        if (contextSymbols.empty()) {
            contextSymbols = historicalView->getAvailableSymbols(normalizedDate.toStdString());
        }

        for (const PreparedFactorRequest& preparedFactor : preparedFactors) {
            factor::CalculationContext context;
            context.date = normalizedDate.toStdString();
            context.symbols = contextSymbols;
            context.historicalView = historicalView;

            const factor::CalculationResult calculation = preparedFactor.factor->calculate(context);
            if (!calculation.dataStatus.isValid()) {
                return buildFactorValueErrorResult(
                    preparedFactor.factorId,
                    preparedFactor.resolvedInstanceId,
                    normalizedDate,
                    QString::fromStdString(calculation.dataStatus.message).trimmed().isEmpty()
                        ? QStringLiteral("批量因子值计算失败")
                        : QString::fromStdString(calculation.dataStatus.message).trimmed());
            }

            QVariantMap stockValues;
            for (const auto& [symbol, value] : calculation.values) {
                if (std::isfinite(value)) {
                    stockValues.insert(QString::fromStdString(symbol), value);
                }
            }

            QVariantMap factorResult;
            factorResult[QStringLiteral("status")] = QStringLiteral("success");
            factorResult[QStringLiteral("factorId")] = preparedFactor.factorId;
            factorResult[QStringLiteral("instanceId")] = preparedFactor.resolvedInstanceId;
            factorResult[QStringLiteral("date")] = normalizedDate;
            factorResult[QStringLiteral("stockValues")] = stockValues;
            const QString message = calculationMessage(calculation);
            if (!message.isEmpty()) {
                factorResult[QStringLiteral("message")] = message;
            }

            factorResults.insert(preparedFactor.factorId, factorResult);
        }

        QVariantMap result;
        result[QStringLiteral("status")] = QStringLiteral("success");
        result[QStringLiteral("date")] = normalizedDate;
        result[QStringLiteral("factorValues")] = factorResults;
        return result;
    } catch (const std::exception& e) {
        return buildFactorValueErrorResult(
            normalizedFactorIds.constFirst(),
            {},
            normalizedDate,
            QString::fromUtf8(e.what()));
    }
}

QVariantMap FactorService::getFactorValuesBatch(const QString& factorId, const QStringList& dates)
{
    const QString normalizedFactorId = factorId.trimmed();
    if (normalizedFactorId.isEmpty()) {
        return buildFactorValueErrorResult(
            normalizedFactorId,
            {},
            {},
            QStringLiteral("缺少因子 ID"));
    }

    if (!initializeFactorDomainRuntime()) {
        return buildFactorValueErrorResult(
            normalizedFactorId,
            {},
            {},
            QStringLiteral("因子运行时初始化失败"));
    }

    const QString resolvedInstanceId = resolveDomainInstanceId(normalizedFactorId);
    if (resolvedInstanceId.isEmpty()) {
        return buildFactorValueErrorResult(
            normalizedFactorId,
            {},
            {},
            QStringLiteral("未找到对应的因子实例 ID"));
    }

    return getFactorValuesBatchFromDomain(normalizedFactorId, resolvedInstanceId, dates);
}

QString FactorService::getLatestAvailableTradeDate()
{
    if (!m_database) {
        return {};
    }

    try {
        const astock::database::QueryResult result = m_database->executeQuery(
            QStringLiteral("SELECT MAX(trade_date) AS latest_date FROM daily_bar"));
        if (result.isEmpty()) {
            return {};
        }
        return result.getRow(0).getString(QStringLiteral("latest_date")).trimmed();
    } catch (const std::exception&) {
        return {};
    }
}

QVariantMap FactorService::getUnifiedParameterSchema() const
{
    return QVariantMap{
        {QStringLiteral("removed"), true},
        {QStringLiteral("reason"), removedReason()}
    };
}

QString FactorService::determineDomainInstanceId(const QVariantMap& factorData) const
{
    const QString instanceId = factorData.value(QStringLiteral("instanceId")).toString().trimmed();
    if (!instanceId.isEmpty()) {
        return instanceId;
    }
    return factorData.value(QStringLiteral("factorId")).toString().trimmed();
}

bool FactorService::syncFactorDefinitionToDomain(const QVariantMap& factorData, QString* errorMessage)
{
    if (!m_database) {
        auto& dbManager = astock::database::DatabaseConnectionManager::instance();
        if (!dbManager.initialize()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("数据库连接初始化失败");
            }
            return false;
        }
        m_database = dbManager.getDatabase();
    }

    if (!m_database) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("数据库实例不可用");
        }
        return false;
    }

    const QString factorId = factorData.value(QStringLiteral("factorId")).toString().trimmed();
    const QString instanceId = determineDomainInstanceId(factorData);
    if (factorId.isEmpty() || instanceId.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("缺少 factorId 或 instanceId");
        }
        return false;
    }

    try {
        const QJsonObject existingConfig = loadExistingInstanceConfig(m_database.get(), instanceId, factorId);
        const std::optional<QJsonObject> canonicalConfig = buildCanonicalInstanceConfig(factorData, existingConfig, errorMessage);
        if (!canonicalConfig.has_value()) {
            return false;
        }

        const QString instanceName = factorData.value(QStringLiteral("displayName")).toString().trimmed().isEmpty()
            ? factorData.value(QStringLiteral("factorName")).toString().trimmed()
            : factorData.value(QStringLiteral("displayName")).toString().trimmed();
        const QString description = factorData.value(QStringLiteral("description")).toString().trimmed();
        const QString fullConfigJson = QString::fromUtf8(QJsonDocument(*canonicalConfig).toJson(QJsonDocument::Compact));

        const astock::database::QueryResult existingRow = m_database->executeQuery(
            QStringLiteral(
                "SELECT instance_id FROM factor_instance "
                "WHERE instance_id = ? OR factor_id = ? ORDER BY updated_at DESC LIMIT 1"),
            makePositionalParams({instanceId, factorId}));

        if (existingRow.isEmpty()) {
            m_database->executeUpdate(
                QStringLiteral(
                    "INSERT INTO factor_instance ("
                    "instance_id, factor_id, instance_name, description, full_config, status, created_at, updated_at) "
                    "VALUES (?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)"),
                makePositionalParams({
                    instanceId,
                    factorId,
                    instanceName,
                    description,
                    fullConfigJson,
                    QString::fromUtf8(kActiveInstanceStatus)
                }));
        } else {
            m_database->executeUpdate(
                QStringLiteral(
                    "UPDATE factor_instance SET "
                    "factor_id = ?, instance_name = ?, description = ?, full_config = ?, status = ?, updated_at = CURRENT_TIMESTAMP "
                    "WHERE instance_id = ?"),
                makePositionalParams({
                    factorId,
                    instanceName,
                    description,
                    fullConfigJson,
                    QString::fromUtf8(kActiveInstanceStatus),
                    existingRow.getRow(0).getString(QStringLiteral("instance_id")).trimmed()
                }));
        }

        if (m_factorInstanceManager) {
            m_factorInstanceManager->refreshCache();
        }
        return true;
    } catch (const std::exception& e) {
        if (errorMessage) {
            *errorMessage = QString::fromUtf8(e.what());
        }
        return false;
    }
}

bool FactorService::removeFactorDefinitionFromDomain(const QString& factorId)
{
    if (!m_database) {
        auto& dbManager = astock::database::DatabaseConnectionManager::instance();
        if (!dbManager.initialize()) {
            return false;
        }
        m_database = dbManager.getDatabase();
    }
    if (!m_database) {
        return false;
    }

    try {
        m_database->executeUpdate(
            QStringLiteral("DELETE FROM factor_instance WHERE factor_id = ? OR instance_id = ?"),
            makePositionalParams({factorId.trimmed(), factorId.trimmed()}));
        if (m_factorInstanceManager) {
            m_factorInstanceManager->refreshCache();
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
