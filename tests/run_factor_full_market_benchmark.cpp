#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <chrono>
#include <future>
#include <limits>
#include <memory>

#include "cache/include/cache_facade.h"
#include "DatabaseConnectionManager.h"
#include "foundation.h"

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include "domain/factor/include/ArrowMarketData.h"
#include "domain/factor/include/DataAvailabilityChecker.h"
#include "domain/factor/include/FactorBacktestExecutor.h"
#include "domain/factor/include/FactorCacheManager.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include "FactorBacktestWarmupUtils.h"

namespace {

struct BenchmarkOptions {
    QString instanceId;
    int years = 5;
    int daysLimit = 0;
    int forwardDays = 1;
    int rebalanceDays = 1;
    int numGroups = 10;
    int threads = 8;
    int symbolLimit = 0;
    double transactionCost = 0.001;
    double slippageRate = 0.0;
    double riskFreeRate = 0.0;
    double maxHours = 0.5;
    QString benchmarkSymbol = QStringLiteral("000300.SH");
    QString outputPath;
    bool disableDateParallelism = false;
    bool forceColdRun = false;
};

QString detectRepoRoot()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 8; ++depth) {
        if (dir.exists(QStringLiteral("CMakeLists.txt")) && dir.exists(QStringLiteral("src"))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QDir::currentPath();
}

QStringList fetchActiveStockSymbols(const std::shared_ptr<astock::database::QtMySQLDatabase>& database)
{
    QStringList symbols;
    if (!database) {
        return symbols;
    }

    const auto result = database->executeQuery(
        QStringLiteral("SELECT symbol FROM symbol_info WHERE asset_class = 'STOCK' AND status = 'ACTIVE' ORDER BY symbol"),
        {});

    symbols.reserve(static_cast<int>(result.rowCount()));
    for (size_t index = 0; index < result.rowCount(); ++index) {
        const QString symbol = result.getRow(index).getString(QStringLiteral("symbol")).trimmed().toUpper();
        if (!symbol.isEmpty()) {
            symbols.push_back(symbol);
        }
    }
    return symbols;
}

QStringList fetchCoveredStockSymbols(const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                                    const QString& startDate,
                                    const QString& endDate,
                                    int limit)
{
    QStringList symbols;
    if (!database || startDate.trimmed().isEmpty() || endDate.trimmed().isEmpty()) {
        return symbols;
    }

    QString sql = QStringLiteral(
        "SELECT db.symbol AS symbol, COUNT(*) AS row_count "
        "FROM daily_bar db "
        "JOIN symbol_info si ON si.symbol = db.symbol "
        "WHERE si.asset_class = 'STOCK' AND si.status = 'ACTIVE' "
        "AND db.trade_date BETWEEN :start_date AND :end_date "
        "AND db.close > 0 "
        "GROUP BY db.symbol "
        "HAVING COUNT(*) >= ("
        "    SELECT COUNT(DISTINCT trade_date) "
        "    FROM daily_bar "
        "    WHERE trade_date BETWEEN :start_date AND :end_date"
        ") "
        "ORDER BY row_count DESC, db.symbol ASC");
    if (limit > 0) {
        sql += QStringLiteral(" LIMIT %1").arg(limit);
    }

    const auto result = database->executeQuery(
        sql,
        {{QStringLiteral(":start_date"), startDate},
         {QStringLiteral(":end_date"), endDate}});

    symbols.reserve(static_cast<int>(result.rowCount()));
    for (size_t index = 0; index < result.rowCount(); ++index) {
        const QString symbol = result.getRow(index).getString(QStringLiteral("symbol")).trimmed().toUpper();
        if (!symbol.isEmpty()) {
            symbols.push_back(symbol);
        }
    }
    return symbols;
}

QString fetchLatestTradeDate(const std::shared_ptr<astock::database::QtMySQLDatabase>& database)
{
    if (!database) {
        return {};
    }

    const auto result = database->executeQuery(
        QStringLiteral("SELECT MAX(trade_date) AS trade_date FROM daily_bar WHERE trade_date IS NOT NULL"),
        {});
    if (result.isEmpty()) {
        return {};
    }

    return result.getRow(0).getString(QStringLiteral("trade_date")).trimmed();
}

QString fetchBenchmarkInstanceId(const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                                 const std::shared_ptr<factor::DataAvailabilityChecker>& dataChecker,
                                 const QString& requestedInstanceId,
                                 const QString& startDate,
                                 const QString& endDate)
{
    if (!requestedInstanceId.trimmed().isEmpty()) {
        return requestedInstanceId.trimmed();
    }

    (void)dataChecker;
    (void)startDate;
    (void)endDate;

    if (!database) {
        return {};
    }

    const auto result = database->executeQuery(
        QStringLiteral(
            "SELECT fi.instance_id AS instance_id, "
            "COALESCE(JSON_UNQUOTE(JSON_EXTRACT(fi.full_config, '$.factorType')), '') AS factorTypeIndex, "
            "COALESCE(JSON_UNQUOTE(JSON_EXTRACT(fi.full_config, '$.displayName')), "
            "         JSON_UNQUOTE(JSON_EXTRACT(fi.full_config, '$.factorName')), '') AS display_name "
            "FROM factor_instance fi "
            "WHERE fi.status = 'ACTIVE' "
            "ORDER BY fi.created_at DESC "
            "LIMIT 100"),
        {});

    if (result.isEmpty()) {
        return {};
    }

    QString fallbackInstanceId;
    const std::vector<factor::FactorType> preferredTypes = {
        factor::FactorType::TECHNICAL,
        factor::FactorType::SIZE,
        factor::FactorType::VALUE,
        factor::FactorType::LIQUIDITY,
        factor::FactorType::QUALITY,
        factor::FactorType::MOMENTUM,
        factor::FactorType::DIVIDEND,
        factor::FactorType::LOW_VOLATILITY,
        factor::FactorType::INDUSTRY,
        factor::FactorType::GROWTH,
        factor::FactorType::MACRO,
        factor::FactorType::SENTIMENT,
        factor::FactorType::CUSTOM
    };

    for (const factor::FactorType preferredType : preferredTypes) {
        for (size_t index = 0; index < result.rowCount(); ++index) {
            const auto row = result.getRow(index);
            const QString instanceId = row.getString(QStringLiteral("instance_id")).trimmed();
            bool parseOk = false;
            const int factorTypeIndex = row.getString(QStringLiteral("factorTypeIndex")).trimmed().toInt(&parseOk);
            const factor::FactorType factorType = parseOk
                ? factor::factorTypeFromIndex(factorTypeIndex)
                : factor::FactorType::UNKNOWN;
            if (instanceId.isEmpty() || factorType == factor::FactorType::UNKNOWN) {
                continue;
            }

            if (fallbackInstanceId.isEmpty()) {
                fallbackInstanceId = instanceId;
            }

            if (factorType != preferredType) {
                continue;
            }

            return instanceId;
        }
    }

    return fallbackInstanceId;
}

QStringList loadHistoricalTradeDates(const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                                     const QString& anchorStartDate,
                                     const QStringList& stockCodes,
                                     const QString& benchmarkSymbol)
{
    QStringList tradeDates;
    if (!database || anchorStartDate.trimmed().isEmpty()) {
        return tradeDates;
    }

    QStringList symbolPlaceholders;
    std::map<QString, QVariant> params{{QStringLiteral(":anchorStartDate"), anchorStartDate}};
    QString sql = QStringLiteral(
        "SELECT DISTINCT trade_date FROM daily_bar "
        "WHERE trade_date < :anchorStartDate AND close > 0 "
        );
    if (!stockCodes.isEmpty()) {
        for (int index = 0; index < stockCodes.size(); ++index) {
            const QString placeholder = QStringLiteral(":tradeDateSymbol%1").arg(index);
            symbolPlaceholders.append(placeholder);
            params.emplace(placeholder, stockCodes.at(index).trimmed().toUpper());
        }
        sql += QStringLiteral("AND symbol IN (%1) ").arg(symbolPlaceholders.join(QStringLiteral(", ")));
    } else if (!benchmarkSymbol.trimmed().isEmpty()) {
        params.emplace(QStringLiteral(":tradeDateBenchmarkSymbol"), benchmarkSymbol.trimmed().toUpper());
        sql += QStringLiteral("AND symbol = :tradeDateBenchmarkSymbol ");
    }
    sql += QStringLiteral("ORDER BY trade_date ASC");

    const auto result = database->executeQuery(sql, params);
    tradeDates.reserve(static_cast<int>(result.rowCount()));
    for (size_t rowIndex = 0; rowIndex < result.rowCount(); ++rowIndex) {
        const QString tradeDate = result.getRow(rowIndex).getString(QStringLiteral("trade_date")).trimmed();
        if (!tradeDate.isEmpty()) {
            tradeDates.append(tradeDate);
        }
    }
    tradeDates.removeDuplicates();
    return tradeDates;
}

struct WarmupRequirement {
    QStringList requiredFields;
    QStringList optionalFields;
    int minDataPoints = 0;
    int skipRecent = 0;
};

struct BenchmarkFactorConfigInfo {
    factor::FactorType factorType = factor::FactorType::UNKNOWN;
    QString factorName;
    QStringList dailyBarFields;
};

QString factorTypeLabel(factor::FactorType factorType)
{
    switch (factorType) {
    case factor::FactorType::VALUE: return QStringLiteral("value");
    case factor::FactorType::MOMENTUM: return QStringLiteral("momentum");
    case factor::FactorType::SIZE: return QStringLiteral("size");
    case factor::FactorType::QUALITY: return QStringLiteral("quality");
    case factor::FactorType::GROWTH: return QStringLiteral("growth");
    case factor::FactorType::DIVIDEND: return QStringLiteral("dividend");
    case factor::FactorType::TECHNICAL: return QStringLiteral("technical");
    case factor::FactorType::LIQUIDITY: return QStringLiteral("liquidity");
    case factor::FactorType::MACRO: return QStringLiteral("macro");
    case factor::FactorType::INDUSTRY: return QStringLiteral("industry");
    case factor::FactorType::SENTIMENT: return QStringLiteral("sentiment");
    case factor::FactorType::CUSTOM: return QStringLiteral("custom");
    case factor::FactorType::LOW_VOLATILITY: return QStringLiteral("low_volatility");
    default: return QStringLiteral("unknown");
    }
}

QList<int> jsonArrayToIntList(const QJsonValue& value)
{
    QList<int> values;
    const QJsonArray array = value.toArray();
    values.reserve(array.size());
    for (const QJsonValue& item : array) {
        if (!item.isDouble()) {
            continue;
        }
        values.append(item.toInt());
    }
    return values;
}

QStringList jsonArrayToStringList(const QJsonValue& value)
{
    QStringList values;
    const QJsonArray array = value.toArray();
    values.reserve(array.size());
    for (const QJsonValue& item : array) {
        const QString text = item.toString().trimmed().toLower();
        if (!text.isEmpty()) {
            values.append(text);
        }
    }
    values.removeDuplicates();
    return values;
}

QString normalizeWarmupFieldName(const QString& rawField)
{
    const QString field = rawField.trimmed().toLower();
    if (field == QStringLiteral("adj_factor")) {
        return {};
    }
    if (field == QStringLiteral("revenue_growth")) {
        return QStringLiteral("total_revenue");
    }
    return field;
}

QStringList normalizeWarmupFields(const QStringList& fields)
{
    QStringList normalized;
    normalized.reserve(fields.size());
    for (const QString& rawField : fields) {
        const QString field = normalizeWarmupFieldName(rawField);
        if (!field.isEmpty() && !normalized.contains(field)) {
            normalized.append(field);
        }
    }
    return normalized;
}

int resolveConfiguredWarmupWindow(factor::FactorType factorType, const QJsonObject& calculation)
{
    const bool isTechnicalFactor = factorType == factor::FactorType::TECHNICAL;
    const auto configuredAliasOrDefault = [&calculation](const std::initializer_list<QString>& keys, int defaultValue) {
        for (const QString& key : keys) {
            if (calculation.contains(key)) {
                return calculation.value(key).toInt(defaultValue);
            }
        }
        return defaultValue;
    };

    int resolvedWindow = configuredAliasOrDefault(
        {QStringLiteral("window"),
         QStringLiteral("lookbackWindow")},
        isTechnicalFactor ? 20 : 0);

    const std::vector<std::pair<std::initializer_list<QString>, int>> windowKeys = {
        {{QStringLiteral("rsiWindow"), QStringLiteral("rsi_window")}, isTechnicalFactor ? 14 : 0},
        {{QStringLiteral("obvWindow"), QStringLiteral("obv_window")}, isTechnicalFactor ? 20 : 0},
        {{QStringLiteral("turnoverStabilityWindow"), QStringLiteral("turnover_stability_window")}, isTechnicalFactor ? 60 : 0},
        {{QStringLiteral("maWindow"), QStringLiteral("ma_window")}, isTechnicalFactor ? 20 : 0},
        {{QStringLiteral("emaWindow"), QStringLiteral("ema_window")}, isTechnicalFactor ? 20 : 0},
        {{QStringLiteral("bollWindow"), QStringLiteral("boll_window")}, isTechnicalFactor ? 20 : 0},
        {{QStringLiteral("kdjWindow"), QStringLiteral("kdj_window")}, isTechnicalFactor ? 9 : 0},
        {{QStringLiteral("atrWindow"), QStringLiteral("atr_window")}, isTechnicalFactor ? 14 : 0},
        {{QStringLiteral("vwapWindow"), QStringLiteral("vwap_window")}, isTechnicalFactor ? 20 : 0},
        {{QStringLiteral("volumeRatioWindow"), QStringLiteral("volume_ratio_window")}, isTechnicalFactor ? 20 : 0},
        {{QStringLiteral("macroWindow"), QStringLiteral("macro_window")}, factorType == factor::FactorType::MACRO ? 12 : 0}
    };
    for (const auto& [keys, defaultValue] : windowKeys) {
        resolvedWindow = (std::max)(resolvedWindow, configuredAliasOrDefault(keys, defaultValue));
    }

    const int macdSlow = configuredAliasOrDefault(
        {QStringLiteral("macdSlowPeriod"), QStringLiteral("macd_slow_period")},
        isTechnicalFactor ? 26 : 0);
    const int macdSignal = configuredAliasOrDefault(
        {QStringLiteral("macdSignalPeriod"), QStringLiteral("macd_signal_period")},
        isTechnicalFactor ? 9 : 0);
    resolvedWindow = (std::max)(resolvedWindow, macdSlow > 0 ? macdSlow + (std::max)(0, macdSignal - 1) : 0);
    return resolvedWindow;
}

QStringList buildWarmupFieldList(const WarmupRequirement& requirement)
{
    QStringList fields = requirement.requiredFields;
    for (const QString& field : requirement.optionalFields) {
        if (!fields.contains(field)) {
            fields.append(field);
        }
    }
    fields.removeDuplicates();
    return fields;
}

QStringList loadHistoricalTradeDates(const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                                     const QDate& anchorStartDate,
                                     const QStringList& stockCodes,
                                     const QString& benchmarkSymbol)
{
    QStringList tradeDates;
    if (!database || !anchorStartDate.isValid()) {
        return tradeDates;
    }

    QStringList symbolPlaceholders;
    std::map<QString, QVariant> params{{QStringLiteral(":anchorStartDate"), anchorStartDate.toString(QStringLiteral("yyyy-MM-dd"))}};
    QString sql = QStringLiteral(
        "SELECT DISTINCT trade_date FROM daily_bar "
        "WHERE trade_date < :anchorStartDate AND close > 0 ");
    if (!stockCodes.isEmpty()) {
        for (int index = 0; index < stockCodes.size(); ++index) {
            const QString placeholder = QStringLiteral(":tradeDateSymbol%1").arg(index);
            symbolPlaceholders.append(placeholder);
            params.emplace(placeholder, stockCodes.at(index).trimmed().toUpper());
        }
        sql += QStringLiteral("AND symbol IN (%1) ").arg(symbolPlaceholders.join(QStringLiteral(", ")));
    } else if (!benchmarkSymbol.trimmed().isEmpty()) {
        params.emplace(QStringLiteral(":tradeDateBenchmarkSymbol"), benchmarkSymbol.trimmed().toUpper());
        sql += QStringLiteral("AND symbol = :tradeDateBenchmarkSymbol ");
    }
    sql += QStringLiteral("ORDER BY trade_date ASC");

    const auto result = database->executeQuery(sql, params);
    tradeDates.reserve(static_cast<int>(result.rowCount()));
    for (size_t rowIndex = 0; rowIndex < result.rowCount(); ++rowIndex) {
        const QString tradeDate = result.getRow(rowIndex).getString(QStringLiteral("trade_date")).trimmed();
        if (!tradeDate.isEmpty()) {
            tradeDates.append(tradeDate);
        }
    }
    tradeDates.removeDuplicates();
    return tradeDates;
}

QStringList fetchDailyBarColumns(const std::shared_ptr<astock::database::QtMySQLDatabase>& database)
{
    QStringList columns;
    if (!database) {
        return columns;
    }

    const auto result = database->executeQuery(
        QStringLiteral("SHOW COLUMNS FROM daily_bar"),
        {});
    columns.reserve(static_cast<int>(result.rowCount()));
    for (size_t rowIndex = 0; rowIndex < result.rowCount(); ++rowIndex) {
        const QString field = result.getRow(rowIndex).getString(QStringLiteral("Field")).trimmed().toLower();
        if (!field.isEmpty()) {
            columns.push_back(field);
        }
    }
    columns.removeDuplicates();
    return columns;
}

QJsonObject firstPresentObject(const QJsonObject& object, std::initializer_list<QString> keys)
{
    for (const QString& key : keys) {
        if (object.contains(key) && object.value(key).isObject()) {
            return object.value(key).toObject();
        }
    }
    return {};
}

QString firstPresentString(const QJsonObject& object, std::initializer_list<QString> keys)
{
    for (const QString& key : keys) {
        if (object.contains(key)) {
            return object.value(key).toString().trimmed();
        }
    }
    return {};
}

int firstPresentInt(const QJsonObject& object, std::initializer_list<QString> keys, int defaultValue = 0)
{
    for (const QString& key : keys) {
        if (object.contains(key)) {
            return object.value(key).toInt(defaultValue);
        }
    }
    return defaultValue;
}

QJsonArray firstPresentArray(const QJsonObject& object, std::initializer_list<QString> keys)
{
    for (const QString& key : keys) {
        if (object.contains(key) && object.value(key).isArray()) {
            return object.value(key).toArray();
        }
    }
    return {};
}

BenchmarkFactorConfigInfo loadBenchmarkFactorConfigInfo(const QString& fullConfig,
                                                        const QStringList& availableDailyBarColumns)
{
    BenchmarkFactorConfigInfo info;
    QStringList requestedFields = {QStringLiteral("close")};
    if (fullConfig.trimmed().isEmpty()) {
        info.dailyBarFields = requestedFields;
        return info;
    }

    const QJsonDocument document = QJsonDocument::fromJson(fullConfig.toUtf8());
    if (!document.isObject()) {
        info.dailyBarFields = requestedFields;
        return info;
    }

    const QJsonObject config = document.object();
    info.factorType = factor::factorTypeFromIndex(firstPresentInt(config, {QStringLiteral("factorType")}, -1));
    info.factorName = firstPresentString(config, {QStringLiteral("displayName"), QStringLiteral("display_name")});
    if (info.factorName.isEmpty()) {
        info.factorName = firstPresentString(config, {QStringLiteral("factorName"), QStringLiteral("factor_name")});
    }

    const QJsonObject calculation = config.value(QStringLiteral("calculation")).toObject();
    const auto appendRequestedField = [&requestedFields](const QString& field) {
        const QString normalized = field.trimmed().toLower();
        if (!normalized.isEmpty()) {
            requestedFields.push_back(normalized);
        }
    };
    const auto normalizeTechnicalIndicatorType = [](const QString& rawType) {
        const QString normalized = rawType.trimmed().toLower();
        if (normalized == QStringLiteral("rsi")) {
            return QStringLiteral("rsi");
        }
        if (normalized == QStringLiteral("macd")) {
            return QStringLiteral("macd");
        }
        if (normalized == QStringLiteral("ma")) {
            return QStringLiteral("ma");
        }
        if (normalized == QStringLiteral("ema")) {
            return QStringLiteral("ema");
        }
        if (normalized == QStringLiteral("boll")) {
            return QStringLiteral("boll");
        }
        if (normalized == QStringLiteral("kdj")) {
            return QStringLiteral("kdj");
        }
        if (normalized == QStringLiteral("atr")) {
            return QStringLiteral("atr");
        }
        if (normalized == QStringLiteral("obv")) {
            return QStringLiteral("obv");
        }
        if (normalized == QStringLiteral("vwap")) {
            return QStringLiteral("vwap");
        }
        if (normalized == QStringLiteral("volume_ratio")) {
            return QStringLiteral("volume_ratio");
        }
        if (normalized == QStringLiteral("turnover_stability")) {
            return QStringLiteral("turnover_stability");
        }
        return QString();
    };
    const auto resolvePriceField = [](factor::TechnicalPriceType priceType) {
        switch (priceType) {
        case factor::TechnicalPriceType::CLOSE: return QStringLiteral("close");
        case factor::TechnicalPriceType::OPEN: return QStringLiteral("open");
        case factor::TechnicalPriceType::HIGH: return QStringLiteral("high");
        case factor::TechnicalPriceType::LOW: return QStringLiteral("low");
        default: return QString();
        }
    };
    if (info.factorType == factor::FactorType::TECHNICAL) {
        const factor::TechnicalPriceType technicalPriceType = static_cast<factor::TechnicalPriceType>(
            firstPresentInt(calculation, {QStringLiteral("technicalPriceType")}, static_cast<int>(factor::TechnicalPriceType::CLOSE)));
        const QString resolvedPriceField = resolvePriceField(technicalPriceType);
        appendRequestedField(resolvedPriceField);

        QList<factor::TechnicalIndicator> indicators;
        const auto appendIndicator = [&indicators](int indicatorIndex) {
            const factor::TechnicalIndicator indicator = static_cast<factor::TechnicalIndicator>(indicatorIndex);
            if (indicator != factor::TechnicalIndicator::UNKNOWN && !indicators.contains(indicator)) {
                indicators.push_back(indicator);
            }
        };
        const QList<int> technicalIndicators = jsonArrayToIntList(
            calculation.value(QStringLiteral("technicalIndicators")));
        for (const int indicatorIndex : technicalIndicators) {
            appendIndicator(indicatorIndex);
        }

        const bool needHighLowSeries = indicators.contains(factor::TechnicalIndicator::KDJ)
            || indicators.contains(factor::TechnicalIndicator::ATR);
        const bool needVolumeSeries = indicators.contains(factor::TechnicalIndicator::OBV)
            || indicators.contains(factor::TechnicalIndicator::VWAP)
            || indicators.contains(factor::TechnicalIndicator::VOLUME_RATIO)
            || indicators.contains(factor::TechnicalIndicator::TURNOVER_STABILITY);
        const bool needTurnoverSeries = indicators.contains(factor::TechnicalIndicator::TURNOVER_STABILITY);
        if (needHighLowSeries) {
            appendRequestedField(QStringLiteral("high"));
            appendRequestedField(QStringLiteral("low"));
        }
        if (needVolumeSeries) {
            appendRequestedField(QStringLiteral("volume"));
        }
        if (needTurnoverSeries) {
            const factor::LiquidityMetric turnoverMetric = static_cast<factor::LiquidityMetric>(
                firstPresentInt(calculation, {QStringLiteral("turnoverStabilityMetric")}, static_cast<int>(factor::LiquidityMetric::TURNOVER_RATE)));
            appendRequestedField(turnoverMetric == factor::LiquidityMetric::VOLUME
                ? QStringLiteral("volume")
                : QStringLiteral("turnover_rate"));
        }
    }

    const QJsonObject dataRequirements = firstPresentObject(
        config,
        {QStringLiteral("dataRequirements"), QStringLiteral("data_requirements")});
    const auto appendFields = [&requestedFields](const QJsonArray& fields) {
        for (const auto& value : fields) {
            const QString field = value.toString().trimmed().toLower();
            if (!field.isEmpty()) {
                requestedFields.push_back(field);
            }
        }
    };
    appendFields(firstPresentArray(dataRequirements, {QStringLiteral("required"), QStringLiteral("required_fields")}));
    appendFields(firstPresentArray(dataRequirements, {QStringLiteral("optional"), QStringLiteral("optional_fields")}));

    for (const QString& field : requestedFields) {
        if (availableDailyBarColumns.contains(field)) {
            info.dailyBarFields.push_back(field);
        }
    }
    info.dailyBarFields.removeDuplicates();
    if (!info.dailyBarFields.contains(QStringLiteral("close"))) {
        info.dailyBarFields.push_back(QStringLiteral("close"));
    }
    return info;
}

QStringList normalizeBenchmarkFields(const QStringList& selectedFields)
{
    QStringList effectiveFields = selectedFields;
    if (!effectiveFields.contains(QStringLiteral("close"))) {
        effectiveFields.push_back(QStringLiteral("close"));
    }
    effectiveFields.removeDuplicates();
    return effectiveFields;
}

QString buildBenchmarkBarSelectSql(const QStringList& effectiveFields)
{
    QStringList selectColumns = {
        QStringLiteral("db.symbol AS symbol"),
        QStringLiteral("db.trade_date AS trade_date")
    };
    for (const QString& field : effectiveFields) {
        selectColumns.push_back(QStringLiteral("db.%1 AS %1").arg(field));
    }
    return selectColumns.join(QStringLiteral(", "));
}

QString buildBenchmarkBarUniverseClause(const QStringList& benchmarkSymbols,
                                        const QString& benchmarkSymbol,
                                        std::map<QString, QVariant>& params)
{
    params.emplace(QStringLiteral(":benchmark_symbol"), benchmarkSymbol.trimmed().toUpper());
    if (benchmarkSymbols.isEmpty()) {
        return QStringLiteral(
            "AND ((si.asset_class = 'STOCK' AND si.status = 'ACTIVE') OR db.symbol = :benchmark_symbol) ");
    }

    QStringList symbolPlaceholders;
    symbolPlaceholders.reserve(benchmarkSymbols.size());
    for (int index = 0; index < benchmarkSymbols.size(); ++index) {
        const QString placeholder = QStringLiteral(":symbol_%1").arg(index);
        symbolPlaceholders.push_back(placeholder);
        params.emplace(placeholder, benchmarkSymbols.at(index).trimmed().toUpper());
    }
    return QStringLiteral("AND (db.symbol IN (%1) OR db.symbol = :benchmark_symbol) ")
        .arg(symbolPlaceholders.join(QStringLiteral(", ")));
}

size_t appendBenchmarkBarsToBuilder(
    factor::ArrowMarketData::Builder& builder,
    const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
    const QString& startDate,
    const QString& endDate,
    const QStringList& benchmarkSymbols,
    const QString& benchmarkSymbol,
    const QStringList& selectedFields)
{
    if (!database || startDate.trimmed().isEmpty() || endDate.trimmed().isEmpty()) {
        return 0;
    }

    const QStringList effectiveFields = normalizeBenchmarkFields(selectedFields);
    std::map<QString, QVariant> params{
        {QStringLiteral(":start_date"), startDate},
        {QStringLiteral(":end_date"), endDate}
    };

    QString sql = QStringLiteral(
        "SELECT %1 "
        "FROM daily_bar db "
        "LEFT JOIN symbol_info si ON si.symbol = db.symbol "
        "WHERE db.trade_date BETWEEN :start_date AND :end_date "
        "AND db.close > 0 ")
        .arg(buildBenchmarkBarSelectSql(effectiveFields));
    sql += buildBenchmarkBarUniverseClause(benchmarkSymbols, benchmarkSymbol, params);
    sql += QStringLiteral("ORDER BY db.trade_date ASC, db.symbol ASC");

    const size_t extraNumericFieldCount = effectiveFields.size() > 1
        ? static_cast<size_t>(effectiveFields.size() - 1)
        : 0U;
    size_t appendedCount = 0;
    database->visitQuery(sql, params, [&builder, &effectiveFields, extraNumericFieldCount, &appendedCount](const astock::database::QueryResultRow& row) {
        const QString symbol = row.getString(QStringLiteral("symbol")).trimmed().toUpper();
        const QString tradeDate = row.getString(QStringLiteral("trade_date")).trimmed();
        const double close = row.getDouble(QStringLiteral("close"));
        if (symbol.isEmpty() || tradeDate.isEmpty() || !std::isfinite(close) || close <= 0.0) {
            return true;
        }

        std::unordered_map<std::string, double> numericFields;
        if (extraNumericFieldCount > 0) {
            numericFields.reserve(extraNumericFieldCount);
        }
        for (const QString& field : effectiveFields) {
            if (field == QStringLiteral("close")) {
                continue;
            }
            const double numericValue = row.getDouble(field, std::numeric_limits<double>::quiet_NaN());
            if (std::isfinite(numericValue)) {
                numericFields.emplace(field.toStdString(), numericValue);
            }
        }

        if (builder.appendRow(symbol.toStdString(), tradeDate.toStdString(), close, numericFields)) {
            ++appendedCount;
        }
        return true;
    });

    return appendedCount;
}

WarmupRequirement loadWarmupRequirementFromConfigText(const QString& configText)
{
    WarmupRequirement requirement;
    if (configText.trimmed().isEmpty()) {
        return requirement;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(configText.toUtf8());
    if (!doc.isObject()) {
        return requirement;
    }

    const QJsonObject config = doc.object();
    const factor::FactorType factorType = factor::factorTypeFromIndex(
        firstPresentInt(config, {QStringLiteral("factorType")}, -1));
    const QJsonObject calculation = config.value(QStringLiteral("calculation")).toObject();
    const QJsonObject boundaryRules = firstPresentObject(
        config,
        {QStringLiteral("boundaryRules"), QStringLiteral("boundary_rules")});
    const QJsonObject dataRequirements = firstPresentObject(
        config,
        {QStringLiteral("dataRequirements"), QStringLiteral("data_requirements")});

    requirement.requiredFields = normalizeWarmupFields(jsonArrayToStringList(firstPresentArray(
        dataRequirements,
        {QStringLiteral("required"), QStringLiteral("required_fields")})));
    requirement.optionalFields = normalizeWarmupFields(jsonArrayToStringList(firstPresentArray(
        dataRequirements,
        {QStringLiteral("optional"), QStringLiteral("optional_fields")})));

    if (!requirement.requiredFields.contains(QStringLiteral("close"))) {
        requirement.requiredFields.append(QStringLiteral("close"));
    }
    requirement.optionalFields.removeAll(QStringLiteral("close"));

    requirement.minDataPoints = (std::max)(
        firstPresentInt(boundaryRules, {QStringLiteral("minDataPoints"), QStringLiteral("min_data_points")}),
        resolveConfiguredWarmupWindow(factorType, calculation));
    requirement.skipRecent = firstPresentInt(
        calculation,
        {QStringLiteral("skipRecent"), QStringLiteral("skip_recent")},
        0);
    return requirement;
}

QDate resolveBenchmarkAnchorStartDate(const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                                      const QString& configuredStartDateText,
                                      const QString& configuredEndDateText,
                                      const QStringList& datasetStockCodes,
                                      const QString& benchmarkSymbol)
{
    const QDate configuredStartDate = QDate::fromString(configuredStartDateText, QStringLiteral("yyyy-MM-dd"));
    if (!database || !configuredStartDate.isValid() || configuredEndDateText.trimmed().isEmpty()) {
        return configuredStartDate;
    }

    std::map<QString, QVariant> params{
        {QStringLiteral(":start_date"), configuredStartDateText},
        {QStringLiteral(":end_date"), configuredEndDateText}
    };
    QString sql = QStringLiteral(
        "SELECT MIN(db.trade_date) AS first_trade_date "
        "FROM daily_bar db "
        "LEFT JOIN symbol_info si ON si.symbol = db.symbol "
        "WHERE db.trade_date BETWEEN :start_date AND :end_date "
        "AND db.close > 0 ");
    sql += buildBenchmarkBarUniverseClause(datasetStockCodes, benchmarkSymbol, params);

    const auto result = database->executeQuery(sql, params);
    if (result.isEmpty()) {
        return configuredStartDate;
    }

    const QDate resolvedStartDate = QDate::fromString(
        result.getRow(0).getString(QStringLiteral("first_trade_date")).trimmed(),
        QStringLiteral("yyyy-MM-dd"));
    return resolvedStartDate.isValid() ? resolvedStartDate : configuredStartDate;
}

size_t appendBenchmarkWarmupRows(factor::ArrowMarketData::Builder& builder,
                                 const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                                 const QString& configuredStartDateText,
                                 const QString& configuredEndDateText,
                                 const QStringList& datasetStockCodes,
                                 const QString& benchmarkSymbol,
                                 const QStringList& selectedFields,
                                 const WarmupRequirement& requirement)
{
    if (!database || configuredStartDateText.trimmed().isEmpty()) {
        return 0;
    }

    if (requirement.minDataPoints <= 1 && requirement.skipRecent <= 0) {
        return 0;
    }

    const int requiredTradingDays = factor::warmup::requiredWarmupTradingDays(requirement.minDataPoints, requirement.skipRecent);
    if (requiredTradingDays <= 0) {
        return 0;
    }

    QStringList stockCodes = datasetStockCodes;
    stockCodes.removeDuplicates();

    QStringList warmupFields = buildWarmupFieldList(requirement);
    for (const QString& field : selectedFields) {
        const QString normalizedField = normalizeWarmupFieldName(field);
        if (!normalizedField.isEmpty() && !warmupFields.contains(normalizedField)) {
            warmupFields.push_back(normalizedField);
        }
    }
    warmupFields.removeDuplicates();
    if (warmupFields.isEmpty()) {
        return 0;
    }

    const QDate configuredStartDate = QDate::fromString(configuredStartDateText, QStringLiteral("yyyy-MM-dd"));
    if (!configuredStartDate.isValid()) {
        return 0;
    }

    const QDate anchorStartDate = resolveBenchmarkAnchorStartDate(
        database,
        configuredStartDateText,
        configuredEndDateText,
        datasetStockCodes,
        benchmarkSymbol);

    const QStringList historicalTradeDates = loadHistoricalTradeDates(
        database,
        anchorStartDate.toString(QStringLiteral("yyyy-MM-dd")),
        stockCodes,
        benchmarkSymbol);
    const QDate preciseHistoryStartDate = factor::warmup::resolveWarmupHistoryStartDate(
        anchorStartDate,
        historicalTradeDates,
        requiredTradingDays);
    const QString historyStartDate = preciseHistoryStartDate.isValid()
        ? preciseHistoryStartDate.toString(QStringLiteral("yyyy-MM-dd"))
        : anchorStartDate.addDays(-factor::warmup::fallbackWarmupCalendarLookbackDays(requiredTradingDays))
            .toString(QStringLiteral("yyyy-MM-dd"));
    const QString historyEndDate = anchorStartDate.addDays(-1).toString(QStringLiteral("yyyy-MM-dd"));
    if (historyEndDate < historyStartDate) {
        return 0;
    }

    std::map<QString, QVariant> params{
        {QStringLiteral(":historyStartDate"), historyStartDate},
        {QStringLiteral(":historyEndDate"), historyEndDate}
    };
    const QString sql = QStringLiteral(
        "SELECT %1 FROM daily_bar db "
        "LEFT JOIN symbol_info si ON si.symbol = db.symbol "
        "WHERE db.trade_date BETWEEN :historyStartDate AND :historyEndDate "
        "AND db.close > 0 ")
        .arg(buildBenchmarkBarSelectSql(normalizeBenchmarkFields(warmupFields)));
    QString warmupSql = sql;
    warmupSql += buildBenchmarkBarUniverseClause(stockCodes, benchmarkSymbol, params);
    warmupSql += QStringLiteral(
        "ORDER BY db.trade_date ASC, db.symbol ASC")
        ;

    const size_t extraNumericFieldCount = warmupFields.size() > 1
        ? static_cast<size_t>(warmupFields.size() - 1)
        : 0U;
    size_t appendedCount = 0;
    database->visitQuery(warmupSql, params, [&builder, &warmupFields, extraNumericFieldCount, &appendedCount](const astock::database::QueryResultRow& row) {
        const QString symbol = row.getString(QStringLiteral("symbol")).trimmed().toUpper();
        const QString tradeDate = row.getString(QStringLiteral("trade_date")).trimmed();
        const double close = row.getDouble(QStringLiteral("close"));
        if (symbol.isEmpty() || tradeDate.isEmpty() || !std::isfinite(close) || close <= 0.0) {
            return true;
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
            if (std::isfinite(numericValue)) {
                numericFields.emplace(field.toStdString(), numericValue);
            }
        }
        if (builder.appendRow(symbol.toStdString(), tradeDate.toStdString(), close, numericFields)) {
            ++appendedCount;
        }
        return true;
    });

    return appendedCount;
}

int countTradeDates(const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                    const QString& startDate,
                    const QString& endDate)
{
    if (!database) {
        return 0;
    }

    const auto result = database->executeQuery(
        QStringLiteral("SELECT COUNT(DISTINCT trade_date) AS trade_date_count FROM daily_bar WHERE trade_date BETWEEN :start_date AND :end_date"),
        {{QStringLiteral(":start_date"), startDate},
         {QStringLiteral(":end_date"), endDate}});

    if (result.isEmpty()) {
        return 0;
    }

    return result.getRow(0).getInt(QStringLiteral("trade_date_count"));
}

QString defaultOutputPath(const QString& repoRoot, const QString& instanceId)
{
    const QString outputDir = QDir(repoRoot).filePath(QStringLiteral("build/tests/full_market_benchmark"));
    QDir().mkpath(outputDir);
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString fileName = QStringLiteral("%1_%2.json").arg(instanceId, timestamp);
    return QDir(outputDir).filePath(fileName);
}

bool writeJsonFile(const QString& filePath, const QVariantMap& payload)
{
    const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
    QDir().mkpath(QFileInfo(absolutePath).absolutePath());

    QFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromVariant(payload);
    file.write(document.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool readJsonFile(const QString& filePath, QVariantMap& payload)
{
    QFile file(filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject()) {
        return false;
    }

    payload = document.object().toVariantMap();
    return true;
}

QString buildBenchmarkCacheSignature(const QString& instanceId,
                                     const QString& startDate,
                                     const QString& endDate,
                                     int forwardDays,
                                     int rebalanceDays,
                                     int numGroups,
                                     double transactionCost,
                                     double slippageRate,
                                     double riskFreeRate,
                                     const QString& benchmarkSymbol,
                                     bool dateParallelism,
                                     int tradeDateCount,
                                     const QStringList& activeSymbols)
{
    const QByteArray activeSymbolBytes = activeSymbols.join(QStringLiteral("|")).toUtf8();
    const QString activeSymbolFingerprint = QString::fromUtf8(
        QCryptographicHash::hash(activeSymbolBytes, QCryptographicHash::Sha256).toHex());

    const QStringList parts = {
        instanceId,
        startDate,
        endDate,
        QString::number(forwardDays),
        QString::number(rebalanceDays),
        QString::number(numGroups),
        QString::number(transactionCost, 'f', 8),
        QString::number(slippageRate, 'f', 8),
        QString::number(riskFreeRate, 'f', 8),
        benchmarkSymbol,
        dateParallelism ? QStringLiteral("1") : QStringLiteral("0"),
        QString::number(tradeDateCount),
        activeSymbolFingerprint
    };

    return QString::fromUtf8(QCryptographicHash::hash(parts.join(QStringLiteral("|")).toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString benchmarkCachePath(const QString& repoRoot, const QString& cacheSignature)
{
    const QString cacheDir = QDir(repoRoot).filePath(QStringLiteral("build/tests/full_market_benchmark/cache"));
    QDir().mkpath(cacheDir);
    return QDir(cacheDir).filePath(cacheSignature + QStringLiteral(".json"));
}

QString sanitizeCacheComponent(const QString& value)
{
    QString sanitized;
    sanitized.reserve(value.size());
    for (const QChar ch : value.trimmed()) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-')) {
            sanitized.append(ch);
        } else {
            sanitized.append(QLatin1Char('_'));
        }
    }
    if (sanitized.isEmpty()) {
        return QStringLiteral("unknown");
    }
    return sanitized;
}

QString benchmarkStableCachePath(const QString& repoRoot,
                                 const QString& instanceId,
                                 const QString& startDate,
                                 const QString& endDate,
                                 int forwardDays,
                                 int rebalanceDays,
                                 int numGroups,
                                 const QString& benchmarkSymbol,
                                 bool dateParallelism)
{
    const QString cacheDir = QDir(repoRoot).filePath(QStringLiteral("build/tests/full_market_benchmark/cache"));
    QDir().mkpath(cacheDir);

    const QString fileName = QStringLiteral("stable__%1__%2__%3__f%4__r%5__g%6__%7__dp%8.json")
        .arg(sanitizeCacheComponent(instanceId),
             sanitizeCacheComponent(startDate),
             sanitizeCacheComponent(endDate),
             QString::number(forwardDays),
             QString::number(rebalanceDays),
             QString::number(numGroups),
             sanitizeCacheComponent(benchmarkSymbol),
             dateParallelism ? QStringLiteral("1") : QStringLiteral("0"));

    return QDir(cacheDir).filePath(fileName);
}

void printLine(const QString& line)
{
    QTextStream(stdout) << line << Qt::endl;
}

void printKeyValue(const QString& key, const QVariant& value)
{
    printLine(QStringLiteral("[benchmark] %1=%2").arg(key, value.toString()));
}

BenchmarkOptions parseOptions(QCoreApplication& app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AStock factor full-market benchmark"));
    parser.addHelpOption();

    QCommandLineOption instanceOption({QStringLiteral("i"), QStringLiteral("instance-id")},
                                      QStringLiteral("Factor instance id to benchmark"),
                                      QStringLiteral("instanceId"));
    QCommandLineOption yearsOption({QStringLiteral("y"), QStringLiteral("years")},
                                   QStringLiteral("Benchmark window in years"),
                                   QStringLiteral("years"),
                                   QStringLiteral("5"));
    QCommandLineOption daysLimitOption({QStringLiteral("days-limit")},
                                       QStringLiteral("Limit the benchmark window to the last N days"),
                                       QStringLiteral("days"),
                                       QStringLiteral("0"));
    QCommandLineOption forwardDaysOption({QStringLiteral("f"), QStringLiteral("forward-days")},
                                         QStringLiteral("Forward return days"),
                                         QStringLiteral("days"),
                                         QStringLiteral("1"));
    QCommandLineOption rebalanceDaysOption({QStringLiteral("r"), QStringLiteral("rebalance-days")},
                                           QStringLiteral("Rebalance interval in trading days"),
                                           QStringLiteral("days"),
                                           QStringLiteral("1"));
    QCommandLineOption groupsOption({QStringLiteral("g"), QStringLiteral("groups")},
                                    QStringLiteral("Number of groups"),
                                    QStringLiteral("count"),
                                    QStringLiteral("10"));
    QCommandLineOption threadsOption({QStringLiteral("t"), QStringLiteral("threads")},
                                     QStringLiteral("Thread pool size"),
                                     QStringLiteral("count"),
                                     QStringLiteral("8"));
    QCommandLineOption symbolLimitOption({QStringLiteral("symbol-limit")},
                                         QStringLiteral("Limit active stock symbols for a shorter benchmark slice"),
                                         QStringLiteral("count"),
                                         QStringLiteral("0"));
    QCommandLineOption transactionCostOption({QStringLiteral("transaction-cost")},
                                             QStringLiteral("Transaction cost"),
                                             QStringLiteral("ratio"),
                                             QStringLiteral("0.001"));
    QCommandLineOption slippageOption({QStringLiteral("slippage")},
                                      QStringLiteral("Slippage rate"),
                                      QStringLiteral("ratio"),
                                      QStringLiteral("0.0"));
    QCommandLineOption benchmarkSymbolOption({QStringLiteral("benchmark-symbol")},
                                             QStringLiteral("Benchmark symbol"),
                                             QStringLiteral("symbol"),
                                             QStringLiteral("000300.SH"));
    QCommandLineOption maxHoursOption({QStringLiteral("max-hours")},
                                      QStringLiteral("Maximum allowed wall-clock hours (default 0.5, i.e. 30 minutes)"),
                                      QStringLiteral("hours"),
                                      QStringLiteral("0.5"));
    QCommandLineOption outputOption({QStringLiteral("o"), QStringLiteral("output")},
                                    QStringLiteral("Output JSON file"),
                                    QStringLiteral("path"));
    QCommandLineOption disableDateParallelismOption(QStringLiteral("disable-date-parallelism"),
                                                    QStringLiteral("Disable date-level parallelism"));
    QCommandLineOption forceColdRunOption(QStringLiteral("cold-run"),
                                          QStringLiteral("Bypass benchmark summary cache and disable factor cache backend"));

    parser.addOption(instanceOption);
    parser.addOption(yearsOption);
    parser.addOption(daysLimitOption);
    parser.addOption(forwardDaysOption);
    parser.addOption(rebalanceDaysOption);
    parser.addOption(groupsOption);
    parser.addOption(threadsOption);
    parser.addOption(symbolLimitOption);
    parser.addOption(transactionCostOption);
    parser.addOption(slippageOption);
    parser.addOption(benchmarkSymbolOption);
    parser.addOption(maxHoursOption);
    parser.addOption(outputOption);
    parser.addOption(disableDateParallelismOption);
    parser.addOption(forceColdRunOption);
    parser.process(app);

    BenchmarkOptions options;
    options.instanceId = parser.value(instanceOption).trimmed();
    options.years = qMax(1, parser.value(yearsOption).toInt());
    options.daysLimit = qMax(0, parser.value(daysLimitOption).toInt());
    options.forwardDays = qMax(1, parser.value(forwardDaysOption).toInt());
    options.rebalanceDays = qMax(1, parser.value(rebalanceDaysOption).toInt());
    options.numGroups = qMax(2, parser.value(groupsOption).toInt());
    options.threads = qMax(1, parser.value(threadsOption).toInt());
    options.symbolLimit = qMax(0, parser.value(symbolLimitOption).toInt());
    options.transactionCost = parser.value(transactionCostOption).toDouble();
    options.slippageRate = parser.value(slippageOption).toDouble();
    options.benchmarkSymbol = parser.value(benchmarkSymbolOption).trimmed().isEmpty()
        ? QStringLiteral("000300.SH")
        : parser.value(benchmarkSymbolOption).trimmed().toUpper();
    options.maxHours = qMax(0.1, parser.value(maxHoursOption).toDouble());
    options.outputPath = parser.value(outputOption).trimmed();
    options.disableDateParallelism = parser.isSet(disableDateParallelismOption);
    options.forceColdRun = parser.isSet(forceColdRunOption);
    return options;
}

void populateActiveStocks(factor::BacktestConfig& config, const QStringList& symbols)
{
    config.allowedStockCodes.reserve(static_cast<size_t>(symbols.size()));
    for (const QString& symbol : symbols) {
        const QString normalized = symbol.trimmed().toUpper();
        if (!normalized.isEmpty()) {
            config.allowedStockCodes.push_back(normalized.toStdString());
        }
    }
}

[[noreturn]] void terminateBenchmarkSuccess()
{
    QTextStream(stdout).flush();
    std::quick_exit(0);
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("astockquant_factor_full_market_benchmark"));

    const BenchmarkOptions options = parseOptions(app);
    const QString repoRoot = detectRepoRoot();
    QDir::setCurrent(repoRoot);

    printKeyValue(QStringLiteral("repoRoot"), repoRoot);

    foundation::Config foundationConfig;
    foundationConfig.profile = "development";
    foundationConfig.config_dir = QDir(repoRoot).filePath(QStringLiteral("config")).toStdString();
    foundationConfig.enable_console_log = true;
    foundationConfig.enable_file_log = false;
    foundationConfig.thread_pool_size = static_cast<size_t>(options.threads);
    if (!foundation::Foundation::instance().initialize(foundationConfig)) {
        printLine(QStringLiteral("[benchmark] foundation_initialize_failed"));
        return 1;
    }

    if (!astock::database::DatabaseConnectionManager::instance().initialize()) {
        printLine(QStringLiteral("[benchmark] database_initialize_failed"));
        return 2;
    }

    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        printLine(QStringLiteral("[benchmark] database_unavailable"));
        return 3;
    }

    auto dataChecker = std::make_shared<factor::DataAvailabilityChecker>(database);

    printLine(QStringLiteral("[benchmark] step=fetchLatestTradeDate begin"));
    const QString latestTradeDate = fetchLatestTradeDate(database);
    printLine(QStringLiteral("[benchmark] step=fetchLatestTradeDate end"));
    if (latestTradeDate.isEmpty()) {
        printLine(QStringLiteral("[benchmark] latest_trade_date_not_found"));
        return 4;
    }

    const QDate endDate = QDate::fromString(latestTradeDate, QStringLiteral("yyyy-MM-dd"));
    if (!endDate.isValid()) {
        printLine(QStringLiteral("[benchmark] latest_trade_date_invalid=") + latestTradeDate);
        return 5;
    }

    const QDate requestedStartDate = options.daysLimit > 0 ? endDate.addDays(-options.daysLimit) : endDate.addYears(-options.years);
    QDate startDate = requestedStartDate;
    if (!startDate.isValid()) {
        printLine(QStringLiteral("[benchmark] start_date_invalid"));
        return 6;
    }

    printLine(QStringLiteral("[benchmark] step=fetchActiveStockSymbols begin"));
    const QStringList activeSymbols = fetchActiveStockSymbols(database);
    printLine(QStringLiteral("[benchmark] step=fetchActiveStockSymbols end"));
    if (activeSymbols.isEmpty()) {
        printLine(QStringLiteral("[benchmark] active_stock_universe_empty"));
        return 7;
    }

    QStringList benchmarkSymbols = activeSymbols;

    printLine(QStringLiteral("[benchmark] step=fetchBenchmarkInstanceId begin"));
    const QString instanceId = fetchBenchmarkInstanceId(
        database,
        dataChecker,
        options.instanceId,
        startDate.toString(QStringLiteral("yyyy-MM-dd")),
        endDate.toString(QStringLiteral("yyyy-MM-dd")));
    printLine(QStringLiteral("[benchmark] step=fetchBenchmarkInstanceId end"));
    if (instanceId.isEmpty()) {
        printLine(QStringLiteral("[benchmark] benchmark_instance_not_found"));
        return 8;
    }
    printLine(QStringLiteral("[benchmark] step=resolveBenchmarkStartDate replaced_by_warmup_history"));

    benchmarkSymbols = activeSymbols;
    if (options.symbolLimit > 0 && benchmarkSymbols.size() > options.symbolLimit) {
        benchmarkSymbols = benchmarkSymbols.mid(0, options.symbolLimit);
    }

    printLine(QStringLiteral("[benchmark] step=countTradeDates begin"));
    const int tradeDateCount = countTradeDates(
        database,
        startDate.toString(QStringLiteral("yyyy-MM-dd")),
        endDate.toString(QStringLiteral("yyyy-MM-dd")));
    printLine(QStringLiteral("[benchmark] step=countTradeDates end"));

    BenchmarkFactorConfigInfo benchmarkFactorConfig;
    WarmupRequirement warmupRequirement;
    {
        printLine(QStringLiteral("[benchmark] step=loadBenchmarkFactorConfig begin"));
        const auto instanceInfo = database->executeQuery(
            QStringLiteral("SELECT CAST(full_config AS CHAR) AS full_config FROM factor_instance WHERE instance_id = :instance_id LIMIT 1"),
            {{QStringLiteral(":instance_id"), instanceId}});
        printLine(QStringLiteral("[benchmark] step=loadBenchmarkFactorConfig end"));
        if (!instanceInfo.isEmpty()) {
            const QString fullConfig = instanceInfo.getRow(0).getString(QStringLiteral("full_config")).trimmed();
            benchmarkFactorConfig = loadBenchmarkFactorConfigInfo(fullConfig, fetchDailyBarColumns(database));
            warmupRequirement = loadWarmupRequirementFromConfigText(fullConfig);
            const QStringList warmupFields = buildWarmupFieldList(warmupRequirement);
            for (const QString& field : warmupFields) {
                if (!benchmarkFactorConfig.dailyBarFields.contains(field)) {
                    benchmarkFactorConfig.dailyBarFields.push_back(field);
                }
            }
            benchmarkFactorConfig.dailyBarFields.removeDuplicates();
        }
    }

    factor::BacktestConfig config;
    config.instanceId = instanceId.toStdString();
    config.startDate = startDate.toString(QStringLiteral("yyyy-MM-dd")).toStdString();
    config.endDate = endDate.toString(QStringLiteral("yyyy-MM-dd")).toStdString();
    config.forwardDays = options.forwardDays;
    config.rebalanceDays = options.rebalanceDays;
    config.numGroups = options.numGroups;
    config.transactionCost = options.transactionCost;
    config.slippageRate = options.slippageRate;
    config.riskFreeRate = 0.0;
    config.benchmarkSymbol = options.benchmarkSymbol.toStdString();
    config.enableDateParallelism = !options.disableDateParallelism;
    config.datasetId = -1;
    populateActiveStocks(config, benchmarkSymbols);
    printLine(QStringLiteral("[benchmark] step=loadBenchmarkCachedBars begin"));
    factor::ArrowMarketData::Builder arrowBuilder;
    const size_t appendedWarmupBarCount = appendBenchmarkWarmupRows(
        arrowBuilder,
        database,
        startDate.toString(QStringLiteral("yyyy-MM-dd")),
        endDate.toString(QStringLiteral("yyyy-MM-dd")),
        options.symbolLimit > 0 ? benchmarkSymbols : QStringList(),
        options.benchmarkSymbol,
        benchmarkFactorConfig.dailyBarFields,
        warmupRequirement);
    const size_t appendedMainBarCount = appendBenchmarkBarsToBuilder(
        arrowBuilder,
        database,
        startDate.toString(QStringLiteral("yyyy-MM-dd")),
        endDate.toString(QStringLiteral("yyyy-MM-dd")),
        options.symbolLimit > 0 ? benchmarkSymbols : QStringList(),
        options.benchmarkSymbol,
        benchmarkFactorConfig.dailyBarFields);
    printLine(QStringLiteral("[benchmark] step=loadBenchmarkCachedBars end"));
    if (appendedMainBarCount == 0) {
        printLine(QStringLiteral("[benchmark] benchmark_cached_bars_empty"));
        return 9;
    }

    config.marketDataCacheKey = QStringLiteral("benchmark|%1|%2|%3|%4")
        .arg(QString::fromStdString(config.startDate))
        .arg(QString::fromStdString(config.endDate))
        .arg(static_cast<qulonglong>(arrowBuilder.rowCount()))
        .arg(benchmarkFactorConfig.dailyBarFields.join(QStringLiteral(",")))
        .toStdString();
    printLine(QStringLiteral("[benchmark] step=buildArrowMarketData begin"));
    config.preparedArrowData = arrowBuilder.finish();
    printLine(QStringLiteral("[benchmark] step=buildArrowMarketData end"));
    if (!config.preparedArrowData) {
        printLine(QStringLiteral("[benchmark] benchmark_arrow_market_data_empty"));
        return 10;
    }

    printKeyValue(QStringLiteral("instanceId"), instanceId);
    if (benchmarkFactorConfig.factorType != factor::FactorType::UNKNOWN) {
        printKeyValue(QStringLiteral("benchmarkFactorType"), factor::factorTypeIndex(benchmarkFactorConfig.factorType));
        printKeyValue(QStringLiteral("benchmarkFactorTypeLabel"), factorTypeLabel(benchmarkFactorConfig.factorType));
    }
    if (!benchmarkFactorConfig.factorName.isEmpty()) {
        printKeyValue(QStringLiteral("benchmarkFactorName"), benchmarkFactorConfig.factorName);
    }
    printKeyValue(QStringLiteral("cachedBarCount"), static_cast<qlonglong>(config.preparedArrowData->rowCount()));
    printKeyValue(QStringLiteral("warmupCachedBarCount"), static_cast<qlonglong>(appendedWarmupBarCount));
    printKeyValue(QStringLiteral("cachedFieldCount"), benchmarkFactorConfig.dailyBarFields.size());
    printKeyValue(QStringLiteral("startDate"), config.startDate.c_str());
    printKeyValue(QStringLiteral("requestedStartDate"), requestedStartDate.toString(QStringLiteral("yyyy-MM-dd")));
    printKeyValue(QStringLiteral("endDate"), config.endDate.c_str());
    printKeyValue(QStringLiteral("daysLimit"), options.daysLimit);
    printKeyValue(QStringLiteral("activeSymbolCount"), activeSymbols.size());
    printKeyValue(QStringLiteral("benchmarkSymbolCount"), benchmarkSymbols.size());
    printKeyValue(QStringLiteral("benchmarkSymbol"), options.benchmarkSymbol);
    printKeyValue(QStringLiteral("symbolLimit"), options.symbolLimit);
    printKeyValue(QStringLiteral("dateParallelism"), config.enableDateParallelism ? QStringLiteral("enabled") : QStringLiteral("disabled"));
    printKeyValue(QStringLiteral("coldRun"), options.forceColdRun ? QStringLiteral("true") : QStringLiteral("false"));

    const QString cacheSignature = buildBenchmarkCacheSignature(
        instanceId,
        QString::fromStdString(config.startDate),
        QString::fromStdString(config.endDate),
        config.forwardDays,
        config.rebalanceDays,
        config.numGroups,
        config.transactionCost,
        config.slippageRate,
        config.riskFreeRate,
        options.benchmarkSymbol,
        config.enableDateParallelism,
        tradeDateCount,
        benchmarkSymbols);
    const QString benchmarkCacheFile = benchmarkCachePath(repoRoot, cacheSignature);
    const QString stableBenchmarkCacheFile = benchmarkStableCachePath(
        repoRoot,
        instanceId,
        QString::fromStdString(config.startDate),
        QString::fromStdString(config.endDate),
        config.forwardDays,
        config.rebalanceDays,
        config.numGroups,
        options.benchmarkSymbol,
        config.enableDateParallelism);

    const QString outputPath = options.outputPath.isEmpty()
        ? defaultOutputPath(repoRoot, instanceId)
        : (QDir::isAbsolutePath(options.outputPath) ? options.outputPath : QDir(repoRoot).filePath(options.outputPath));

    printKeyValue(QStringLiteral("cacheSignature"), cacheSignature);
    printKeyValue(QStringLiteral("benchmarkCacheFile"), QDir::toNativeSeparators(benchmarkCacheFile));
    printKeyValue(QStringLiteral("stableBenchmarkCacheFile"), QDir::toNativeSeparators(stableBenchmarkCacheFile));
    printKeyValue(QStringLiteral("benchmarkCacheExists"), QFileInfo::exists(benchmarkCacheFile) ? QStringLiteral("true") : QStringLiteral("false"));

    QVariantMap cachedSummary;
    const bool cacheLoaded = !options.forceColdRun && readJsonFile(benchmarkCacheFile, cachedSummary);
    const bool stableCacheLoaded = !options.forceColdRun && !cacheLoaded && stableBenchmarkCacheFile != benchmarkCacheFile
        ? readJsonFile(stableBenchmarkCacheFile, cachedSummary)
        : false;
    const QString loadedCacheFile = cacheLoaded
        ? benchmarkCacheFile
        : (stableCacheLoaded ? stableBenchmarkCacheFile : QString());
    printKeyValue(QStringLiteral("benchmarkCacheLoaded"), (cacheLoaded || stableCacheLoaded) ? QStringLiteral("true") : QStringLiteral("false"));
    printKeyValue(QStringLiteral("benchmarkCacheBypassed"), options.forceColdRun ? QStringLiteral("true") : QStringLiteral("false"));
    if (cacheLoaded || stableCacheLoaded) {
        printKeyValue(QStringLiteral("benchmarkCacheSource"), QDir::toNativeSeparators(loadedCacheFile));
    }
    if ((cacheLoaded || stableCacheLoaded)
        && cachedSummary.value(QStringLiteral("status")).toString() == QStringLiteral("SUCCESS")
        && cachedSummary.value(QStringLiteral("instanceId")).toString() == instanceId
        && cachedSummary.value(QStringLiteral("startDate")).toString() == QString::fromStdString(config.startDate)
        && cachedSummary.value(QStringLiteral("endDate")).toString() == QString::fromStdString(config.endDate)
        && cachedSummary.value(QStringLiteral("forwardDays")).toInt() == config.forwardDays
        && cachedSummary.value(QStringLiteral("rebalanceDays")).toInt() == config.rebalanceDays
        && cachedSummary.value(QStringLiteral("numGroups")).toInt() == config.numGroups
        && cachedSummary.value(QStringLiteral("benchmarkSymbol")).toString() == options.benchmarkSymbol)
    {
        cachedSummary.insert(QStringLiteral("activeSymbolCount"), activeSymbols.size());
        cachedSummary.insert(QStringLiteral("tradeDateCount"), tradeDateCount);
        cachedSummary.insert(QStringLiteral("cacheHit"), true);
        cachedSummary.insert(QStringLiteral("cachePath"), QDir::toNativeSeparators(loadedCacheFile));

        if (!writeJsonFile(outputPath, cachedSummary)) {
            printKeyValue(QStringLiteral("outputWriteFailed"), outputPath);
        } else {
            printKeyValue(QStringLiteral("outputPath"), QDir::toNativeSeparators(outputPath));
        }

        if (loadedCacheFile == benchmarkCacheFile && stableBenchmarkCacheFile != benchmarkCacheFile) {
            if (writeJsonFile(stableBenchmarkCacheFile, cachedSummary)) {
                printKeyValue(QStringLiteral("stableCachePath"), QDir::toNativeSeparators(stableBenchmarkCacheFile));
            } else {
                printKeyValue(QStringLiteral("stableCacheWriteFailed"), stableBenchmarkCacheFile);
            }
        }

        if (loadedCacheFile == stableBenchmarkCacheFile && stableBenchmarkCacheFile != benchmarkCacheFile) {
            if (writeJsonFile(benchmarkCacheFile, cachedSummary)) {
                printKeyValue(QStringLiteral("cachePath"), QDir::toNativeSeparators(benchmarkCacheFile));
            } else {
                printKeyValue(QStringLiteral("cacheWriteFailed"), benchmarkCacheFile);
            }
        }

        printKeyValue(QStringLiteral("cacheHit"), QStringLiteral("true"));
        printKeyValue(QStringLiteral("cachePath"), QDir::toNativeSeparators(loadedCacheFile));
        printKeyValue(QStringLiteral("resultStatus"), cachedSummary.value(QStringLiteral("status")).toString());
        printKeyValue(QStringLiteral("wallClockMs"), cachedSummary.value(QStringLiteral("wallClockMs")));
        printKeyValue(QStringLiteral("executorExecutionMs"), cachedSummary.value(QStringLiteral("executorExecutionMs")));
        printKeyValue(QStringLiteral("withinThreshold"), cachedSummary.value(QStringLiteral("withinThreshold")).toBool() ? QStringLiteral("true") : QStringLiteral("false"));
        printKeyValue(QStringLiteral("annualReturn"), QString::number(cachedSummary.value(QStringLiteral("annualReturn")).toDouble(), 'f', 6));
        printKeyValue(QStringLiteral("benchmarkAnnualReturn"), QString::number(cachedSummary.value(QStringLiteral("benchmarkAnnualReturn")).toDouble(), 'f', 6));
        printKeyValue(QStringLiteral("excessAnnualReturn"), QString::number(cachedSummary.value(QStringLiteral("excessAnnualReturn")).toDouble(), 'f', 6));
        printKeyValue(QStringLiteral("sharpeRatio"), QString::number(cachedSummary.value(QStringLiteral("sharpeRatio")).toDouble(), 'f', 6));
        printKeyValue(QStringLiteral("maxDrawdown"), QString::number(cachedSummary.value(QStringLiteral("maxDrawdown")).toDouble(), 'f', 6));
        printKeyValue(QStringLiteral("winRate"), QString::number(cachedSummary.value(QStringLiteral("winRate")).toDouble(), 'f', 6));
        printKeyValue(QStringLiteral("profitFactor"), QString::number(cachedSummary.value(QStringLiteral("profitFactor")).toDouble(), 'f', 6));
        printKeyValue(QStringLiteral("turnoverRate"), QString::number(cachedSummary.value(QStringLiteral("turnoverRate")).toDouble(), 'f', 6));
        printKeyValue(QStringLiteral("dataCoverage"), QString::number(cachedSummary.value(QStringLiteral("dataCoverage")).toDouble(), 'f', 6));
        terminateBenchmarkSuccess();
    }

    auto instanceManager = std::make_shared<factor::FactorInstanceManager>(database, dataChecker);
    auto threadPool = std::make_shared<foundation::thread::ThreadPoolExecutor>(static_cast<size_t>(options.threads));

    auto& cacheFacade = AStockQuantEngine::Cache::CacheFacade::getInstance();
    if (!cacheFacade.isEnabled()) {
        AStockQuantEngine::Cache::CacheConfig cacheConfig;
        cacheConfig.enabled = true;
        cacheConfig.localCache.enabled = true;
        cacheConfig.redisCache.enabled = true;
        cacheFacade.initialize(cacheConfig);
    }

    auto cacheManager = std::make_shared<factor::FactorCacheManager>();
    cacheManager->setCacheFacade(
        std::shared_ptr<AStockQuantEngine::Cache::CacheFacade>(&cacheFacade, [](AStockQuantEngine::Cache::CacheFacade*) {})
    );

    factor::FactorBacktestExecutor executor(instanceManager, threadPool, cacheManager);
    QElapsedTimer wallClock;
    wallClock.start();
    const qint64 maxAllowedMs = static_cast<qint64>(options.maxHours * 3600.0 * 1000.0);
    factor::FactorBacktestExecutor::ExecutionHandle executionHandle = executor.executeTrackedAsync(config);
    factor::BacktestResult result;
    bool timedOut = false;

    while (true) {
        if (executionHandle.future.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready) {
            result = executionHandle.future.get();
            break;
        }

        const qint64 elapsedMs = wallClock.elapsed();
        if (elapsedMs > maxAllowedMs) {
            timedOut = true;
            executor.cancel(executionHandle.taskId);
            result = executionHandle.future.get();
            break;
        }
    }

    const qint64 wallClockMs = wallClock.elapsed();
    const bool withinThreshold = wallClockMs <= maxAllowedMs;

    QVariantMap summary;
    summary.insert(QStringLiteral("repoRoot"), repoRoot);
    summary.insert(QStringLiteral("instanceId"), instanceId);
    summary.insert(QStringLiteral("startDate"), QString::fromStdString(result.config.startDate));
    summary.insert(QStringLiteral("requestedStartDate"), requestedStartDate.toString(QStringLiteral("yyyy-MM-dd")));
    summary.insert(QStringLiteral("endDate"), QString::fromStdString(result.config.endDate));
    summary.insert(QStringLiteral("tradeDateCount"), tradeDateCount);
    summary.insert(QStringLiteral("activeSymbolCount"), activeSymbols.size());
    summary.insert(QStringLiteral("wallClockMs"), static_cast<qlonglong>(wallClockMs));
    summary.insert(QStringLiteral("executorExecutionMs"), result.executionTimeMs);
    summary.insert(QStringLiteral("maxAllowedMs"), static_cast<qlonglong>(maxAllowedMs));
    summary.insert(QStringLiteral("withinThreshold"), withinThreshold);
    summary.insert(QStringLiteral("timedOut"), timedOut);
    summary.insert(QStringLiteral("status"), QString::fromStdString(result.status));
    summary.insert(QStringLiteral("errorMessage"), QString::fromStdString(result.errorMessage));
    summary.insert(QStringLiteral("annualReturn"), result.annualReturn);
    summary.insert(QStringLiteral("benchmarkAnnualReturn"), result.benchmarkAnnualReturn);
    summary.insert(QStringLiteral("excessAnnualReturn"), result.excessAnnualReturn);
    summary.insert(QStringLiteral("sharpeRatio"), result.sharpeRatio);
    summary.insert(QStringLiteral("maxDrawdown"), result.maxDrawdown);
    summary.insert(QStringLiteral("winRate"), result.winRate);
    summary.insert(QStringLiteral("profitFactor"), result.profitFactor);
    summary.insert(QStringLiteral("turnoverRate"), result.turnoverRate);
    summary.insert(QStringLiteral("dataCoverage"), result.dataCoverage);
    summary.insert(QStringLiteral("benchmarkSymbol"), QString::fromStdString(result.config.benchmarkSymbol));
    summary.insert(QStringLiteral("forwardDays"), result.config.forwardDays);
    summary.insert(QStringLiteral("rebalanceDays"), result.config.rebalanceDays);
    summary.insert(QStringLiteral("numGroups"), result.config.numGroups);
    summary.insert(QStringLiteral("dateParallelism"), result.config.enableDateParallelism);
    summary.insert(QStringLiteral("cacheHit"), false);
    summary.insert(QStringLiteral("cachePath"), QDir::toNativeSeparators(benchmarkCacheFile));

    if (!writeJsonFile(outputPath, summary)) {
        printKeyValue(QStringLiteral("outputWriteFailed"), outputPath);
    } else {
        printKeyValue(QStringLiteral("outputPath"), QDir::toNativeSeparators(outputPath));
    }

    if (result.status == "SUCCESS") {
        if (writeJsonFile(benchmarkCacheFile, summary)) {
            printKeyValue(QStringLiteral("cachePath"), QDir::toNativeSeparators(benchmarkCacheFile));
        } else {
            printKeyValue(QStringLiteral("cacheWriteFailed"), benchmarkCacheFile);
        }
        if (stableBenchmarkCacheFile != benchmarkCacheFile) {
            if (writeJsonFile(stableBenchmarkCacheFile, summary)) {
                printKeyValue(QStringLiteral("stableCachePath"), QDir::toNativeSeparators(stableBenchmarkCacheFile));
            } else {
                printKeyValue(QStringLiteral("stableCacheWriteFailed"), stableBenchmarkCacheFile);
            }
        }
    }

    printKeyValue(QStringLiteral("resultStatus"), QString::fromStdString(result.status));
    printKeyValue(QStringLiteral("wallClockMs"), wallClockMs);
    printKeyValue(QStringLiteral("executorExecutionMs"), result.executionTimeMs);
    printKeyValue(QStringLiteral("withinThreshold"), withinThreshold ? QStringLiteral("true") : QStringLiteral("false"));
    printKeyValue(QStringLiteral("timedOut"), timedOut ? QStringLiteral("true") : QStringLiteral("false"));
    printKeyValue(QStringLiteral("annualReturn"), QString::number(result.annualReturn, 'f', 6));
    printKeyValue(QStringLiteral("benchmarkAnnualReturn"), QString::number(result.benchmarkAnnualReturn, 'f', 6));
    printKeyValue(QStringLiteral("excessAnnualReturn"), QString::number(result.excessAnnualReturn, 'f', 6));
    printKeyValue(QStringLiteral("sharpeRatio"), QString::number(result.sharpeRatio, 'f', 6));
    printKeyValue(QStringLiteral("maxDrawdown"), QString::number(result.maxDrawdown, 'f', 6));
    printKeyValue(QStringLiteral("winRate"), QString::number(result.winRate, 'f', 6));
    printKeyValue(QStringLiteral("profitFactor"), QString::number(result.profitFactor, 'f', 6));
    printKeyValue(QStringLiteral("turnoverRate"), QString::number(result.turnoverRate, 'f', 6));
    printKeyValue(QStringLiteral("dataCoverage"), QString::number(result.dataCoverage, 'f', 6));

    if (result.status != "SUCCESS") {
        printKeyValue(QStringLiteral("errorMessage"), QString::fromStdString(result.errorMessage));
        return 9;
    }

    if (!withinThreshold) {
        printLine(QStringLiteral("[benchmark] threshold_exceeded"));
        return 10;
    }

    printKeyValue(QStringLiteral("exitMode"), QStringLiteral("quick_exit"));
    terminateBenchmarkSuccess();
}
