#include "FactorBacktestController.h"
#include "AppStoragePaths.h"
#include "DataServiceCache.h"
#include "FactorBacktestWarmupUtils.h"
#include "FactorBacktestResultContract.h"
#include "FactorBacktestPreflightUtils.h"
#include "FactorDetectionService.h"
#include "FactorService.h"
#include "DataFetchFieldContractUtils.h"
#include "DatabaseConnectionManager.h"
#include "RiskConfigService.h"
#include "../../../domain/factor/include/CustomFactor.h"
#include "../../../domain/factor/include/DividendFactor.h"
#include "../../../domain/factor/include/GrowthFactor.h"
#include "../../../domain/factor/include/IndustryFactor.h"
#include "../../../domain/factor/include/LiquidityFactor.h"
#include "../../../domain/factor/include/LowVolFactor.h"
#include "../../../domain/factor/include/MacroFactor.h"
#include "../../../domain/factor/include/MomentumFactor.h"
#include "../../../domain/factor/include/QualityFactor.h"
#include "../../../domain/factor/include/SentimentFactor.h"
#include "../../../domain/factor/include/SizeFactor.h"
#include "../../../domain/factor/include/TechnicalFactor.h"
#include "../../../domain/factor/include/ValueFactor.h"
#include "../../../domain/factor/include/ArrowMarketData.h"
#include "FactorBacktestCachedBarUtils.h"

#include <QDate>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <thread>
namespace {

QString removedReason()
{
    return QStringLiteral("因子引擎侧业务代码已删除");
}

QString idleStatusText()
{
    return QStringLiteral("就绪");
}

QString cancelledBacktestReason()
{
    return QStringLiteral("已取消回测");
}

QString defaultBacktestFailureReason()
{
    return QStringLiteral("因子回测执行失败");
}

QString unsupportedBacktestReason()
{
    return QStringLiteral("当前因子未通过回测检查");
}

QString pendingPreflightReason()
{
    return QStringLiteral("当前因子尚未完成回测检查");
}

int backtestExecutionLagTradingDays(factor::MarketEnvironmentProfile profile)
{
    return profile == factor::MarketEnvironmentProfile::CN_A_SHARE ? 1 : 0;
}

QString backtestSignalDateSemantics()
{
    return QStringLiteral("factor_observation_date");
}

QString backtestExecutionDateSemantics(factor::MarketEnvironmentProfile profile)
{
    return backtestExecutionLagTradingDays(profile) > 0
        ? QStringLiteral("next_trading_day_after_signal")
        : QStringLiteral("same_trading_day_as_signal");
}

QVariantMap removedSupportInfo(const QString& factorId)
{
    QVariantMap info;
    info[QStringLiteral("factorId")] = factorId.trimmed();
    info[QStringLiteral("supported")] = false;
    info[QStringLiteral("category")] = QStringLiteral("engine-factor-removed");
    info[QStringLiteral("reason")] = removedReason();
    info[QStringLiteral("requiredFields")] = QVariantList{};
    info[QStringLiteral("missingFields")] = QVariantList{};
    return info;
}

QVariantMap removedFailure(const QString& factorId)
{
    QVariantMap failure;
    failure[QStringLiteral("factorId")] = factorId.trimmed();
    failure[QStringLiteral("instanceId")] = QString();
    failure[QStringLiteral("reason")] = removedReason();
    failure[QStringLiteral("category")] = QStringLiteral("engine-factor-removed");
    return failure;
}

QVariantList dedupeFactorIds(const QVariantList& factorIds)
{
    QVariantList normalized;
    QSet<QString> seen;
    for (const QVariant& factorIdValue : factorIds) {
        const QString factorId = factorIdValue.toString().trimmed();
        if (factorId.isEmpty() || seen.contains(factorId)) {
            continue;
        }
        seen.insert(factorId);
        normalized.append(factorId);
    }
    return normalized;
}

QString normalizedDataSourceMode(const QString& dataSourceMode)
{
    const QString normalized = dataSourceMode.trimmed().toLower();
    return normalized.isEmpty() ? QStringLiteral("cache") : normalized;
}

QString resolveBacktestWindowDate(const QString& requestedDate,
                                  const QVariantMap& cacheSnapshot,
                                  const QString& key,
                                  int datasetId)
{
    const QString normalizedRequestedDate = requestedDate.trimmed();
    if (!normalizedRequestedDate.isEmpty()) {
        return normalizedRequestedDate;
    }

    const QString snapshotDate = cacheSnapshot.value(key).toString().trimmed();
    if (!snapshotDate.isEmpty()) {
        return snapshotDate;
    }

    if (datasetId <= 0) {
        return {};
    }

    auto& cache = DataServiceCache::getInstance();
    cache.initializeCache();
    const DataServiceCache::DataSetInfo dataSetInfo = cache.getDataSetInfo(datasetId);
    const QDate dataSetDate = key == QStringLiteral("startDate")
        ? dataSetInfo.startDate
        : dataSetInfo.endDate;
    return dataSetDate.isValid() ? dataSetDate.toString(Qt::ISODate) : QString();
}

QString normalizedBenchmarkSymbolText(const QVariant& value)
{
    return value.toString().trimmed().toUpper();
}

QString resolveBenchmarkSymbolFromMap(const QVariantMap& metadata, bool allowGenericKeys);

QString resolveIndexSymbolFromMap(const QVariantMap& metadata);

QString resolveIndexSymbolFromVariant(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return {};
    }

    const QVariantMap nestedMap = value.toMap();
    if (!nestedMap.isEmpty()) {
        return resolveIndexSymbolFromMap(nestedMap);
    }

    return normalizedBenchmarkSymbolText(value);
}

QString resolveBenchmarkSymbolFromVariant(const QVariant& value, bool allowGenericKeys)
{
    if (!value.isValid() || value.isNull()) {
        return {};
    }

    const QVariantMap nestedMap = value.toMap();
    if (!nestedMap.isEmpty()) {
        return resolveBenchmarkSymbolFromMap(nestedMap, allowGenericKeys);
    }

    return normalizedBenchmarkSymbolText(value);
}

QString resolveBenchmarkSymbolFromMap(const QVariantMap& metadata, bool allowGenericKeys)
{
    static const QStringList directKeys{
        QStringLiteral("benchmarkSymbol"),
        QStringLiteral("benchmark_symbol"),
        QStringLiteral("benchmarkCode"),
        QStringLiteral("benchmark_code"),
        QStringLiteral("indexSymbol"),
        QStringLiteral("index_symbol"),
        QStringLiteral("indexCode"),
        QStringLiteral("index_code")
    };
    for (const QString& key : directKeys) {
        const QString symbol = resolveBenchmarkSymbolFromVariant(metadata.value(key), false);
        if (!symbol.isEmpty()) {
            return symbol;
        }
    }

    static const QStringList nestedKeys{
        QStringLiteral("benchmark"),
        QStringLiteral("benchmarkInfo"),
        QStringLiteral("benchmarkMetadata"),
        QStringLiteral("index"),
        QStringLiteral("indexInfo"),
        QStringLiteral("indexMetadata")
    };
    for (const QString& key : nestedKeys) {
        const QString symbol = resolveBenchmarkSymbolFromVariant(metadata.value(key), true);
        if (!symbol.isEmpty()) {
            return symbol;
        }
    }

    if (allowGenericKeys) {
        static const QStringList genericKeys{
            QStringLiteral("symbol"),
            QStringLiteral("code")
        };
        for (const QString& key : genericKeys) {
            const QString symbol = resolveBenchmarkSymbolFromVariant(metadata.value(key), false);
            if (!symbol.isEmpty()) {
                return symbol;
            }
        }
    }

    return {};
}

QString resolveIndexSymbolFromMap(const QVariantMap& metadata)
{
    static const QStringList directKeys{
        QStringLiteral("indexSymbol"),
        QStringLiteral("index_symbol"),
        QStringLiteral("indexCode"),
        QStringLiteral("index_code")
    };

    for (const QString& key : directKeys) {
        const QString symbol = resolveIndexSymbolFromVariant(metadata.value(key));
        if (!symbol.isEmpty()) {
            return symbol;
        }
    }

    static const QStringList nestedKeys{
        QStringLiteral("index"),
        QStringLiteral("indexInfo"),
        QStringLiteral("indexMetadata")
    };
    for (const QString& key : nestedKeys) {
        const QString symbol = resolveIndexSymbolFromVariant(metadata.value(key));
        if (!symbol.isEmpty()) {
            return symbol;
        }
    }

    return {};
}

struct HistoricalIndexConstituentRange {
    QString symbol;
    QDate startDate;
    QDate endDate;
    bool openEnded = false;
};

std::vector<HistoricalIndexConstituentRange> queryHistoricalIndexConstituentRanges(
    const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
    const QString& indexSymbol,
    const QDate& startDate,
    const QDate& endDate)
{
    std::vector<HistoricalIndexConstituentRange> ranges;
    if (!database || indexSymbol.trimmed().isEmpty() || !startDate.isValid() || !endDate.isValid() || startDate > endDate) {
        return ranges;
    }

    const auto result = database->executeQuery(
        QStringLiteral(
            "SELECT constituent_symbol, start_date, end_date "
            "FROM index_constituents "
            "WHERE index_symbol = :index_symbol "
            "  AND start_date <= :end_date "
            "  AND (end_date IS NULL OR end_date >= :start_date) "
            "ORDER BY constituent_symbol ASC, start_date ASC"),
        {{QStringLiteral(":index_symbol"), indexSymbol.trimmed()},
         {QStringLiteral(":start_date"), startDate.toString(QStringLiteral("yyyy-MM-dd"))},
         {QStringLiteral(":end_date"), endDate.toString(QStringLiteral("yyyy-MM-dd"))}});

    ranges.reserve(result.rowCount());
    for (size_t rowIndex = 0; rowIndex < result.rowCount(); ++rowIndex) {
        const auto row = result.getRow(rowIndex);
        HistoricalIndexConstituentRange range;
        range.symbol = row.getString(QStringLiteral("constituent_symbol")).trimmed().toUpper();
        range.startDate = QDate::fromString(row.getString(QStringLiteral("start_date")).trimmed(), Qt::ISODate);
        const QString endDateText = row.getString(QStringLiteral("end_date")).trimmed();
        range.openEnded = endDateText.isEmpty();
        range.endDate = range.openEnded ? QDate() : QDate::fromString(endDateText, Qt::ISODate);
        if (range.symbol.isEmpty() || !range.startDate.isValid()) {
            continue;
        }
        ranges.push_back(std::move(range));
    }

    return ranges;
}

QStringList collectHistoricalIndexConstituentSymbols(const std::vector<HistoricalIndexConstituentRange>& ranges)
{
    QStringList symbols;
    QSet<QString> seen;
    for (const auto& range : ranges) {
        if (range.symbol.isEmpty() || seen.contains(range.symbol)) {
            continue;
        }
        seen.insert(range.symbol);
        symbols.append(range.symbol);
    }
    symbols.sort();
    return symbols;
}

std::unordered_map<std::string, std::vector<std::string>> buildAllowedStockCodesByDate(
    const std::vector<HistoricalIndexConstituentRange>& ranges,
    const QDate& startDate,
    const QDate& endDate)
{
    std::unordered_map<std::string, std::vector<std::string>> allowedStockCodesByDate;
    if (!startDate.isValid() || !endDate.isValid() || startDate > endDate || ranges.empty()) {
        return allowedStockCodesByDate;
    }

    for (QDate currentDate = startDate; currentDate <= endDate; currentDate = currentDate.addDays(1)) {
        std::vector<std::string> symbols;
        symbols.reserve(ranges.size());
        for (const auto& range : ranges) {
            if (range.startDate > currentDate) {
                continue;
            }
            if (!range.openEnded && range.endDate.isValid() && range.endDate < currentDate) {
                continue;
            }
            symbols.push_back(range.symbol.toStdString());
        }
        if (symbols.empty()) {
            continue;
        }
        std::sort(symbols.begin(), symbols.end());
        symbols.erase(std::unique(symbols.begin(), symbols.end()), symbols.end());
        allowedStockCodesByDate.emplace(currentDate.toString(QStringLiteral("yyyy-MM-dd")).toStdString(),
                                        std::move(symbols));
    }

    return allowedStockCodesByDate;
}

QString resolveConfiguredBenchmarkSymbol(const QVariantMap& runtimeParams,
                                        const QVariantMap& datasetBenchmarkMetadata)
{
    const QString defaultBenchmarkSymbol = QStringLiteral("000300.SH");
    const QString datasetBenchmarkSymbol = resolveBenchmarkSymbolFromMap(datasetBenchmarkMetadata, false);
    QString configuredSymbol = risk::config::benchmarkSymbol(
        runtimeParams,
        datasetBenchmarkSymbol.isEmpty() ? defaultBenchmarkSymbol : datasetBenchmarkSymbol).trimmed().toUpper();

    if (configuredSymbol.isEmpty()) {
        return datasetBenchmarkSymbol.isEmpty() ? defaultBenchmarkSymbol : datasetBenchmarkSymbol;
    }

    if (configuredSymbol == defaultBenchmarkSymbol && !datasetBenchmarkSymbol.isEmpty()) {
        return datasetBenchmarkSymbol;
    }

    return configuredSymbol;
}

bool shouldEnableDateParallelism(int batchFactorCount, int workerCount)
{
    Q_UNUSED(batchFactorCount)
    return workerCount > 1;
}

QVariantList toVariantList(const QStringList& values)
{
    QVariantList result;
    result.reserve(values.size());
    for (const QString& value : values) {
        result.append(value);
    }
    return result;
}

QStringList dedupeStringList(const QStringList& values)
{
    QStringList result;
    QSet<QString> seen;
    for (const QString& value : values) {
        const QString normalized = value.trimmed();
        if (normalized.isEmpty() || seen.contains(normalized)) {
            continue;
        }
        seen.insert(normalized);
        result.append(normalized);
    }
    return result;
}

QString resolveInstanceIdFromFactorValue(
    const QVariant& factorId,
    const std::function<QString(const QVariant&)>& overrideForTests)
{
    if (overrideForTests) {
        return overrideForTests(factorId).trimmed();
    }

    const QString directId = factorId.toString().trimmed();
    if (!directId.isEmpty()) {
        return directId;
    }

    const QVariantMap factorMap = factorId.toMap();
    return factorMap.value(QStringLiteral("instanceId"), factorMap.value(QStringLiteral("factorId"))).toString().trimmed();
}

factor::FactorType resolveRuntimeType(const factor::FactorInstanceInfo& info,
                                      const std::shared_ptr<factor::BaseFactor>& factorInstance);

bool configHasCustomExpression(const factor::FactorInstanceInfo& info);

QStringList normalizedRequiredFields(factor::FactorType runtimeType,
                                     const factor::DataRequirements& requirements);

bool configNeutralizationEnabled(const factor::FactorInstanceInfo& info);

QStringList declaredRequiredFieldsFromConfig(const factor::FactorInstanceInfo& info);

struct FactorRuntimeSnapshot {
    std::shared_ptr<astock::database::QtMySQLDatabase> database;
    std::shared_ptr<factor::DataAvailabilityChecker> dataChecker;
    std::shared_ptr<factor::FactorInstanceManager> instanceManager;
    QString errorMessage;
};

FactorRuntimeSnapshot resolveFactorRuntimeSnapshot(
    const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
    const std::shared_ptr<factor::DataAvailabilityChecker>& dataChecker,
    const std::shared_ptr<factor::FactorInstanceManager>& instanceManager,
    bool skipInstanceRefreshForTests)
{
    FactorRuntimeSnapshot snapshot;
    snapshot.database = database;
    snapshot.dataChecker = dataChecker;
    snapshot.instanceManager = instanceManager;

    if (snapshot.instanceManager || skipInstanceRefreshForTests) {
        return snapshot;
    }

    auto& dbManager = astock::database::DatabaseConnectionManager::instance();
    if (!dbManager.initialize()) {
        snapshot.errorMessage = QStringLiteral("因子检查运行时初始化失败：数据库连接初始化失败");
        return snapshot;
    }

    snapshot.database = dbManager.getDatabase();
    if (!snapshot.database) {
        snapshot.errorMessage = QStringLiteral("因子检查运行时初始化失败：数据库实例不可用");
        return snapshot;
    }

    if (!snapshot.dataChecker) {
        snapshot.dataChecker = std::make_shared<factor::DataAvailabilityChecker>(snapshot.database);
    }
    snapshot.instanceManager = std::make_shared<factor::FactorInstanceManager>(snapshot.database, snapshot.dataChecker);
    return snapshot;
}

factor::FactorType resolveRuntimeType(const factor::FactorInstanceInfo& info,
                                      const std::shared_ptr<factor::BaseFactor>& factorInstance)
{
    if (factorInstance) {
        const factor::FactorType fromInstance = factorInstance->getFactorType();
        if (fromInstance != factor::FactorType::UNKNOWN) {
            return fromInstance;
        }
    }

    if (info.factorType != factor::FactorType::UNKNOWN) {
        return info.factorType;
    }

    return factor::FactorType::UNKNOWN;
}

bool configHasCustomExpression(const factor::FactorInstanceInfo& info)
{
    if (!info.config.has("calculation")) {
        return false;
    }
    const auto calculation = info.config.get("calculation");
    if (!calculation.has("expression")) {
        return false;
    }
    const auto expression = calculation.get("expression");
    return expression.isString() && !QString::fromStdString(expression.asString()).trimmed().isEmpty();
}

QStringList normalizedRequiredFields(factor::FactorType runtimeType,
                                     const factor::DataRequirements& requirements)
{
    QStringList fields;
    for (const std::string& field : requirements.requiredFields) {
        const QString normalized = QString::fromStdString(field).trimmed();
        if (!normalized.isEmpty()) {
            fields.append(normalized);
        }
    }

    if (runtimeType == factor::FactorType::DIVIDEND) {
        const QStringList orderedSpecialFields{
            factor::bridge::MarketBarFieldKeys::PRE_ADJ_FACTOR,
            factor::bridge::MarketBarFieldKeys::POST_ADJ_FACTOR,
            factor::bridge::MarketBarFieldKeys::INDUSTRY_CODE,
            factor::bridge::MarketBarFieldKeys::MARKET_CAP};
        QStringList reordered;
        for (const QString& field : fields) {
            if (!orderedSpecialFields.contains(field)) {
                reordered.append(field);
            }
        }
        for (const QString& field : orderedSpecialFields) {
            if (fields.contains(field)) {
                reordered.append(field);
            }
        }
        fields = reordered;
    }

    return dedupeStringList(fields);
}

QVariantList normalizedStockPoolSymbols(const QVariantList& stockPoolSymbols)
{
    QVariantList normalized;
    QSet<QString> seen;
    for (const QVariant& value : stockPoolSymbols) {
        const QString symbol = value.toString().trimmed().toUpper();
        if (symbol.isEmpty() || seen.contains(symbol)) {
            continue;
        }
        seen.insert(symbol);
        normalized.append(symbol);
    }
    return normalized;
}

QVariantMap buildMetricSectionsMap(const factor::BacktestResult& result)
{
    return FactorBacktestResultContract::buildMetrics(result);
}

QVariantMap buildConfigMap(const QString& requestedFactorId,
                          const factor::BacktestResult& result)
{
    QVariantMap config;
    const factor::MarketEnvironmentProfile marketEnvironmentProfile = result.config.marketEnvironmentProfile;
    config[QStringLiteral("factorId")] = requestedFactorId.trimmed();
    config[QStringLiteral("factorName")] = QString::fromStdString(result.instanceName);
    config[QStringLiteral("instanceId")] = QString::fromStdString(result.instanceId);
    config[QStringLiteral("startDate")] = QString::fromStdString(result.config.startDate);
    config[QStringLiteral("endDate")] = QString::fromStdString(result.config.endDate);
    config[QStringLiteral("actualStartDate")] = QString::fromStdString(result.actualStartDate);
    config[QStringLiteral("warmupTrimmedTradingDays")] = result.warmupTrimmedTradingDays;
    config[QStringLiteral("numGroups")] = result.config.numGroups;
    risk::config::setMarketEnvironmentProfile(
        config,
        factor::marketEnvironmentProfileIndex(marketEnvironmentProfile));
    config[QStringLiteral("executionLagTradingDays")] = backtestExecutionLagTradingDays(marketEnvironmentProfile);
    config[QStringLiteral("signalDateSemantics")] = backtestSignalDateSemantics();
    config[QStringLiteral("executionDateSemantics")] = backtestExecutionDateSemantics(marketEnvironmentProfile);
    risk::config::setForwardDays(config, result.config.forwardDays);
    risk::config::setRebalanceDays(config, result.config.rebalanceDays);
    risk::config::setCommissionRate(config, result.config.transactionCost);
    risk::config::setSlippageRate(config, result.config.slippageRate);
    risk::config::setRiskFreeRate(config, result.config.riskFreeRate);
    risk::config::setBenchmarkSymbol(config, QString::fromStdString(result.config.benchmarkSymbol));
    config[QStringLiteral("datasetId")] = result.config.datasetId;
    return config;
}

QString persistedResultFilePathForController()
{
    return bridge::storage::factorBacktestResultFilePath();
}

QVariant normalizeQueryValue(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return QVariant();
    }

    if (value.canConvert<QDateTime>()) {
        const QDateTime dateTime = value.toDateTime();
        if (dateTime.isValid()) {
            return dateTime.toString("yyyy-MM-dd HH:mm:ss");
        }
    }

    if (value.canConvert<QDate>()) {
        const QDate date = value.toDate();
        if (date.isValid()) {
            return date.toString("yyyy-MM-dd");
        }
    }

    return value;
}

QVariantMap convertQueryRowToVariantMap(const astock::database::QueryResultRow& row)
{
    QVariantMap record;
    for (const auto& entry : row.getValues()) {
        record[entry.first] = normalizeQueryValue(entry.second);
    }
    return record;
}

QVariantList convertQueryResultToVariantList(const astock::database::QueryResult& result)
{
    QVariantList rows;
    rows.reserve(static_cast<int>(result.rowCount()));
    for (const auto& row : result.getRows()) {
        rows.append(convertQueryRowToVariantMap(row));
    }
    return rows;
}

QString escapeSqlLiteral(QString value)
{
    value.replace(QStringLiteral("'"), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(value);
}

QString buildSymbolInClause(const QStringList& symbols)
{
    QStringList escapedSymbols;
    escapedSymbols.reserve(symbols.size());
    for (const QString& symbol : symbols) {
        escapedSymbols.append(escapeSqlLiteral(symbol));
    }
    return escapedSymbols.join(QStringLiteral(", "));
}

bool tableHasColumn(const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                    const QString& tableName,
                    const QString& columnName)
{
    if (!database || tableName.trimmed().isEmpty() || columnName.trimmed().isEmpty()) {
        return false;
    }

    const auto result = database->executeQuery(
        QStringLiteral(
            "SELECT COUNT(*) AS count "
            "FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = :table_name AND COLUMN_NAME = :column_name"),
        {{QStringLiteral(":table_name"), tableName.trimmed()},
         {QStringLiteral(":column_name"), columnName.trimmed()}});
    return !result.isEmpty() && result.getRow(0).getInt(QStringLiteral("count")) > 0;
}

bool isDailyBarWarmupField(const QString& rawField)
{
    const QString field = rawField.trimmed().toLower();
    if (field.isEmpty()) {
        return false;
    }

    return field == QString(factor::bridge::CommonFieldKeys::SYMBOL)
        || field == QString(factor::bridge::CommonFieldKeys::TRADE_DATE)
        || field == QString(factor::bridge::CommonFieldKeys::DATA_SOURCE)
        || factor::bridge::marketBarBacktestReadyFields().contains(field);
}

bool canUseDailyBarWarmup(const QStringList& requiredFields)
{
    if (requiredFields.isEmpty()) {
        return false;
    }

    for (const QString& field : requiredFields) {
        if (!isDailyBarWarmupField(field)) {
            return false;
        }
    }

    return true;
}

std::shared_ptr<factor::BaseFactor> createRuntimeRequirementsFactorInstance(
    const factor::FactorInstanceInfo& info,
    const std::shared_ptr<factor::DataAvailabilityChecker>& dataChecker)
{
    try {
        switch (info.factorType) {
        case factor::FactorType::MOMENTUM:
            return factor::MomentumFactor::create(info, dataChecker);
        case factor::FactorType::VALUE:
            return factor::ValueFactor::create(info, dataChecker);
        case factor::FactorType::QUALITY:
            return factor::QualityFactor::create(info, dataChecker);
        case factor::FactorType::SIZE:
            return factor::SizeFactor::create(info, dataChecker);
        case factor::FactorType::LOW_VOLATILITY:
            return factor::LowVolFactor::create(info, dataChecker);
        case factor::FactorType::GROWTH:
            return factor::GrowthFactor::create(info, dataChecker);
        case factor::FactorType::DIVIDEND:
            return factor::DividendFactor::create(info, dataChecker);
        case factor::FactorType::TECHNICAL:
            return factor::TechnicalFactor::create(info, dataChecker);
        case factor::FactorType::LIQUIDITY:
            return factor::LiquidityFactor::create(info, dataChecker);
        case factor::FactorType::MACRO:
            return factor::MacroFactor::create(info, dataChecker);
        case factor::FactorType::INDUSTRY:
            return factor::IndustryFactor::create(info, dataChecker);
        case factor::FactorType::SENTIMENT:
            return factor::SentimentFactor::create(info, dataChecker);
        case factor::FactorType::CUSTOM:
            return factor::CustomFactor::create(info, dataChecker);
        case factor::FactorType::UNKNOWN:
        default:
            break;
        }
    } catch (const std::exception& e) {
        qWarning() << "FactorBacktestController: failed to create fallback runtime requirements instance"
                   << QString::fromStdString(info.instanceId)
                   << "factorType=" << static_cast<int>(info.factorType)
                   << "error=" << e.what();
    } catch (...) {
        qWarning() << "FactorBacktestController: failed to create fallback runtime requirements instance"
                   << QString::fromStdString(info.instanceId)
                   << "factorType=" << static_cast<int>(info.factorType)
                   << "error=unknown";
    }

    return nullptr;
}

struct BacktestRuntimeRequirements {
    QStringList requiredFields;
    int warmupTradingDays = 1;
    bool useDailyBarWarmup = false;
};

BacktestRuntimeRequirements resolveBacktestRuntimeRequirements(
    const QString& resolvedInstanceId,
    const factor::FactorInstanceInfo& info,
    const std::shared_ptr<factor::BaseFactor>& factorInstance,
    const QHash<QString, int>& requiredWarmupTradingDaysOverrideForTests)
{
    BacktestRuntimeRequirements runtimeRequirements;
    if (!factorInstance) {
        return runtimeRequirements;
    }

    factor::DataRequirements requirements = factorInstance->getDataRequirements();
    const factor::BoundaryRules boundaryRules = factorInstance->getBoundaryRules();
    const factor::FactorType runtimeType = resolveRuntimeType(info, factorInstance);
    const QStringList explicitConfigRequiredFields = declaredRequiredFieldsFromConfig(info);
    if (requirements.requiredFields.empty() && !explicitConfigRequiredFields.isEmpty()) {
        requirements.requiredFields.clear();
        for (const QString& field : explicitConfigRequiredFields) {
            requirements.requiredFields.push_back(field.toStdString());
        }
    } else if (runtimeType == factor::FactorType::DIVIDEND && !explicitConfigRequiredFields.isEmpty()) {
        requirements.requiredFields.clear();
        for (const QString& field : explicitConfigRequiredFields) {
            requirements.requiredFields.push_back(field.toStdString());
        }
    }

    if (runtimeType == factor::FactorType::DIVIDEND && explicitConfigRequiredFields.isEmpty()) {
        const auto appendRequirementField = [&requirements](const QString& field) {
            const std::string normalized = field.toStdString();
            if (std::find(requirements.requiredFields.begin(), requirements.requiredFields.end(), normalized)
                == requirements.requiredFields.end()) {
                requirements.requiredFields.push_back(normalized);
            }
        };
        appendRequirementField(factor::bridge::MarketBarFieldKeys::PRE_ADJ_FACTOR);
        appendRequirementField(factor::bridge::MarketBarFieldKeys::POST_ADJ_FACTOR);
    }

    if (configNeutralizationEnabled(info)) {
        const auto appendRequirementField = [&requirements](const QString& field) {
            const std::string normalized = field.toStdString();
            if (std::find(requirements.requiredFields.begin(), requirements.requiredFields.end(), normalized)
                == requirements.requiredFields.end()) {
                requirements.requiredFields.push_back(normalized);
            }
        };
        appendRequirementField(factor::bridge::MarketBarFieldKeys::INDUSTRY_CODE);
        appendRequirementField(factor::bridge::MarketBarFieldKeys::MARKET_CAP);
    }

    runtimeRequirements.requiredFields = normalizedRequiredFields(runtimeType, requirements);
    runtimeRequirements.warmupTradingDays = requiredWarmupTradingDaysOverrideForTests.contains(resolvedInstanceId)
        ? (std::max)(1, requiredWarmupTradingDaysOverrideForTests.value(resolvedInstanceId))
        : (std::max)(1, boundaryRules.minDataPoints);
    runtimeRequirements.useDailyBarWarmup = runtimeRequirements.warmupTradingDays > 1
        && canUseDailyBarWarmup(runtimeRequirements.requiredFields);
    return runtimeRequirements;
}

QStringList warmupQuerySymbols(const DataServiceCache::DataSetInfo& dataSetInfo,
                               const QVariantList& selectedStockPoolSymbols)
{
    const QVariantList normalizedSelectedSymbols = normalizedStockPoolSymbols(selectedStockPoolSymbols);
    if (!normalizedSelectedSymbols.isEmpty()) {
        QStringList symbols;
        symbols.reserve(normalizedSelectedSymbols.size());
        for (const QVariant& symbolValue : normalizedSelectedSymbols) {
            symbols.append(symbolValue.toString().trimmed().toUpper());
        }
        return dedupeStringList(symbols);
    }

    QStringList symbols;
    symbols.reserve(dataSetInfo.stockCodes.size());
    for (const QString& symbol : dataSetInfo.stockCodes) {
        const QString normalized = symbol.trimmed().toUpper();
        if (!normalized.isEmpty()) {
            symbols.append(normalized);
        }
    }
    return dedupeStringList(symbols);
}

QDate resolveWarmupHistoryStartDateFromDatabase(
    const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
    const QDate& anchorStartDate,
    int warmupTradingDays)
{
    if (!database || !anchorStartDate.isValid() || warmupTradingDays <= 1) {
        return {};
    }

    const auto result = database->executeQuery(
        QStringLiteral(
            "SELECT DISTINCT trade_date "
            "FROM daily_bar "
            "WHERE trade_date < :anchor_start_date "
            "ORDER BY trade_date ASC"),
        {{QStringLiteral(":anchor_start_date"), anchorStartDate.toString("yyyy-MM-dd")}});

    QStringList ascendingTradeDates;
    ascendingTradeDates.reserve(static_cast<int>(result.rowCount()));
    for (const auto& row : result.getRows()) {
        const QString tradeDate = row.getString(QStringLiteral("trade_date")).trimmed();
        if (!tradeDate.isEmpty()) {
            ascendingTradeDates.append(tradeDate);
        }
    }

    return factor::warmup::resolveWarmupHistoryStartDate(
        anchorStartDate,
        ascendingTradeDates,
        warmupTradingDays);
}

QVariantList queryDailyBarWarmupRows(
    const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
    const QStringList& symbols,
    const QDate& historyStartDate,
    const QDate& anchorStartDate)
{
    if (!database || !historyStartDate.isValid() || !anchorStartDate.isValid() || historyStartDate >= anchorStartDate) {
        return {};
    }

    const bool hasIndustryCodeColumn = tableHasColumn(
        database,
        QStringLiteral("symbol_info"),
        QString(factor::bridge::MarketBarFieldKeys::INDUSTRY_CODE));
    QString sql = hasIndustryCodeColumn
        ? QStringLiteral(
              "SELECT d.*, TRIM(COALESCE(s.industry_code, '')) AS industry_code "
              "FROM daily_bar d "
              "LEFT JOIN symbol_info s ON s.symbol = d.symbol "
              "WHERE d.trade_date BETWEEN :start_date AND :end_date")
        : QStringLiteral(
              "SELECT d.* "
              "FROM daily_bar d "
              "WHERE d.trade_date BETWEEN :start_date AND :end_date");
    if (!symbols.isEmpty()) {
        sql += QStringLiteral(" AND d.symbol IN (%1)").arg(buildSymbolInClause(symbols));
    }
    sql += QStringLiteral(" ORDER BY d.symbol, d.trade_date");

    const auto result = database->executeQuery(
        sql,
        {{QStringLiteral(":start_date"), historyStartDate.toString("yyyy-MM-dd")},
         {QStringLiteral(":end_date"), anchorStartDate.addDays(-1).toString("yyyy-MM-dd")}});
    return convertQueryResultToVariantList(result);
}

bool configNeutralizationEnabled(const factor::FactorInstanceInfo& info)
{
    if (!info.config.has("calculation")) {
        return false;
    }
    const auto calculation = info.config.get("calculation");
    return calculation.has("neutralizationEnabled") && calculation.get("neutralizationEnabled").asBool();
}

void appendConfigStringField(QStringList& fields, const foundation::json::JsonFacade& value)
{
    if (!value.isString()) {
        return;
    }

    const QString normalized = QString::fromStdString(value.asString()).trimmed();
    if (!normalized.isEmpty()) {
        fields.append(normalized);
    }
}

QStringList declaredRequiredFieldsFromConfig(const factor::FactorInstanceInfo& info)
{
    QStringList fields;

    if (!info.config.has("dataRequirements")) {
        return fields;
    }

    const auto dataRequirements = info.config.get("dataRequirements");
    if (!dataRequirements.has("required") || !dataRequirements.get("required").isArray()) {
        return fields;
    }

    const auto required = dataRequirements.get("required");
    for (size_t index = 0; index < required.size(); ++index) {
        appendConfigStringField(fields, required.at(index));
    }

    return dedupeStringList(fields);
}

bool isCleanedBacktestDataset(const QVariantMap& dataset)
{
    const QStringList tags = dataset.value(QStringLiteral("tags")).toStringList();
    if (tags.contains(QStringLiteral("cleaned"))
        || tags.contains(QStringLiteral("清洗后"))
        || tags.contains(QStringLiteral("data_cleaned"))
        || tags.contains(QStringLiteral("cleaning_result"))) {
        return true;
    }

    const QString description = dataset.value(QStringLiteral("description")).toString().trimmed();
    if (description.contains(QStringLiteral("清洗"), Qt::CaseInsensitive)) {
        return true;
    }

    const QString sourceType = dataset.value(QStringLiteral("sourceType")).toString().trimmed();
    return sourceType.contains(QStringLiteral("cleaning"), Qt::CaseInsensitive);
}

QVariantMap categoryMetaTemplate(const QString& key,
                                 const QString& statusText,
                                 const QString& shortText,
                                 const QString& detail,
                                 const QString& accentColor,
                                 const QString& chipBackground,
                                 const QString& chipBorder,
                                 const QString& chipText)
{
    QVariantMap meta;
    meta[QStringLiteral("key")] = key;
    meta[QStringLiteral("statusText")] = statusText;
    meta[QStringLiteral("shortText")] = shortText;
    meta[QStringLiteral("detail")] = detail;
    meta[QStringLiteral("accentColor")] = accentColor;
    meta[QStringLiteral("chipBackground")] = chipBackground;
    meta[QStringLiteral("chipBorder")] = chipBorder;
    meta[QStringLiteral("chipText")] = chipText;
    return meta;
}

} // namespace

FactorBacktestController::FactorBacktestController(QObject *parent)
    : QObject(parent)
    , m_status(idleStatusText())
{
    m_factorDetectionService = std::make_shared<FactorDetectionService>();
    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(120);
    connect(m_progressTimer, &QTimer::timeout, this, &FactorBacktestController::pollBacktestProgress);
    refreshBacktestRuntimeParamsFromRiskConfiguration();
}

FactorBacktestController::~FactorBacktestController()
{
    shutdownBacktestInfrastructure();
}

bool FactorBacktestController::ensureInstanceRuntime()
{
    if (m_instanceManager) {
        return true;
    }

    if (!m_database) {
        auto& dbManager = astock::database::DatabaseConnectionManager::instance();
        if (!dbManager.initialize()) {
            qWarning() << "FactorBacktestController: 数据库连接初始化失败";
            return false;
        }
        m_database = dbManager.getDatabase();
    }

    if (!m_database) {
        qWarning() << "FactorBacktestController: 数据库实例不可用";
        return false;
    }

    if (!m_dataChecker) {
        m_dataChecker = std::make_shared<factor::DataAvailabilityChecker>(m_database);
    }
    if (!m_instanceManager) {
        m_instanceManager = std::make_shared<factor::FactorInstanceManager>(m_database, m_dataChecker);
    }

    return static_cast<bool>(m_instanceManager);
}

void FactorBacktestController::setSelectedFactorIds(const QVariantList& factorIds)
{
    const QVariantList normalized = dedupeFactorIds(factorIds);
    if (m_selectedFactorIds == normalized) {
        return;
    }

    m_selectedFactorIds = normalized;
    emit selectedFactorIdsChanged(m_selectedFactorIds);

    invalidateSupportMapState(true);
}

void FactorBacktestController::setSelectedDatasetId(int datasetId)
{
    if (m_selectedDatasetId == datasetId) {
        return;
    }
    m_selectedDatasetId = datasetId;
    emit selectedDatasetIdChanged(m_selectedDatasetId);
    invalidateSupportMapState(true);
}

void FactorBacktestController::setSelectedDatasetBenchmarkMetadata(const QVariantMap& metadata)
{
    if (m_selectedDatasetBenchmarkMetadata == metadata) {
        return;
    }
    m_selectedDatasetBenchmarkMetadata = metadata;
    emit selectedDatasetBenchmarkMetadataChanged(m_selectedDatasetBenchmarkMetadata);
}

void FactorBacktestController::setDataSourceMode(const QString& dataSourceMode)
{
    const QString normalizedMode = dataSourceMode.trimmed();
    if (m_dataSourceMode == normalizedMode) {
        return;
    }
    m_dataSourceMode = normalizedMode;
    emit dataSourceModeChanged(m_dataSourceMode);
    invalidateSupportMapState(true);
}

void FactorBacktestController::setSelectedStockPoolSymbols(const QVariantList& stockPoolSymbols)
{
    const QVariantList normalized = normalizedStockPoolSymbols(stockPoolSymbols);
    if (m_selectedStockPoolSymbols == normalized) {
        return;
    }
    m_selectedStockPoolSymbols = normalized;
    emit selectedStockPoolSymbolsChanged(m_selectedStockPoolSymbols);
}

void FactorBacktestController::setBacktestRuntimeParams(const QVariantMap& backtestRuntimeParams)
{
    if (m_backtestRuntimeParams == backtestRuntimeParams) {
        return;
    }
    m_backtestRuntimeParams = backtestRuntimeParams;
    emit backtestRuntimeParamsChanged(m_backtestRuntimeParams);
}

void FactorBacktestController::refreshBacktestRuntimeParamsFromRiskConfiguration()
{
    QVariantMap params = m_loadAppliedRiskConfigOverrideForTests
        ? m_loadAppliedRiskConfigOverrideForTests()
        : m_backtestRuntimeParams;
    setBacktestRuntimeParams(params);
}

void FactorBacktestController::invalidateSupportMapState(bool clearPreflightFailures)
{
    ++m_supportMapRequestSeq;
    m_pendingFilterAfterSupportMap = false;

    if (!m_factorSupportMapCache.isEmpty()) {
        m_factorSupportMapCache.clear();
        emit factorSupportMapCacheChanged(m_factorSupportMapCache);
    }

    if (clearPreflightFailures && !m_lastPreflightFailures.isEmpty()) {
        m_lastPreflightFailures.clear();
        emit lastPreflightFailuresChanged(m_lastPreflightFailures);
    }

    if (m_supportMapRequestInFlight) {
        m_supportMapRequestInFlight = false;
        emit supportMapRequestInFlightChanged(false);
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
    const QVariantList normalizedFactorIds = dedupeFactorIds(factorIds);
    const QString effectiveDataSourceMode = normalizedDataSourceMode(m_dataSourceMode);
    const QString effectiveStartDate = effectiveDataSourceMode == QStringLiteral("cache")
        ? resolveBacktestWindowDate(startDate, cacheSnapshot, QStringLiteral("startDate"), m_selectedDatasetId)
        : startDate.trimmed();
    const QString effectiveEndDate = effectiveDataSourceMode == QStringLiteral("cache")
        ? resolveBacktestWindowDate(endDate, cacheSnapshot, QStringLiteral("endDate"), m_selectedDatasetId)
        : endDate.trimmed();
    setSelectedFactorIds(normalizedFactorIds);
    if (normalizedFactorIds.isEmpty()) {
        finalizeBacktestFailure(QStringLiteral("未选择可执行回测的因子"), false);
        return;
    }

    if (!m_lastPreflightFailures.isEmpty()) {
        m_lastPreflightFailures.clear();
        emit lastPreflightFailuresChanged(m_lastPreflightFailures);
    }

    if (!m_threadPool) {
        const unsigned int hardwareThreads = std::thread::hardware_concurrency();
        const size_t workerCount = hardwareThreads > 0
            ? (std::min)(static_cast<size_t>(hardwareThreads), size_t{4})
            : size_t{4};
        m_threadPool = std::make_shared<foundation::thread::ThreadPoolExecutor>((std::max)(size_t{2}, workerCount));
    }
    if (!m_cacheManager) {
        m_cacheManager = std::make_shared<factor::FactorCacheManager>();
    }

    resetBatchState();
    resetResults();
    m_cancelRequested.store(false);
    m_batchFactorIds = normalizedFactorIds;
    m_batchResultMaps.resize(static_cast<size_t>(normalizedFactorIds.size()));
    m_activeFactorIndex = 0;
    m_pendingGroupText = groupText;
    m_pendingStartDate = effectiveStartDate;
    m_pendingEndDate = effectiveEndDate;
    m_isRunning = true;
    m_progress = 0;
    m_status = QStringLiteral("正在提交回测任务");
    emit isRunningChanged(m_isRunning);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    emit backtestStarted(normalizedFactorIds.first().toString().trimmed());

    try {
        m_pendingDataSourceMode = m_dataSourceMode;
        m_pendingDatasetId = m_selectedDatasetId;
        m_pendingDatasetBenchmarkMetadata = m_selectedDatasetBenchmarkMetadata;
        m_pendingStockPoolSymbols = m_selectedStockPoolSymbols;
        m_pendingRuntimeParams = m_backtestRuntimeParams;
        m_pendingBatchFactorCount = normalizedFactorIds.size();
        m_pendingWorkerCount = static_cast<int>(m_threadPool ? m_threadPool->getWorkerCount() : 0);

        launchNextBacktestTask();
        if (!m_isRunning) {
            return;
        }

        m_progressTimer->start();
        pollBacktestProgress();
    } catch (const std::exception& e) {
        finalizeBacktestFailure(QString::fromUtf8(e.what()), false);
    }
}

QVariantMap FactorBacktestController::buildFactorSupportMap(const QVariantList& factorIds,
                                                           const QString& startDate,
                                                           const QString& endDate,
                                                           const QVariantMap& cacheSnapshot)
{
    if (!m_factorDetectionService) {
        m_factorDetectionService = std::make_shared<FactorDetectionService>();
    }

    const FactorRuntimeSnapshot runtimeSnapshot = resolveFactorRuntimeSnapshot(
        m_database,
        m_dataChecker,
        m_instanceManager,
        m_skipInstanceRefreshForTests);

    FactorDetectionService::Request request;
    request.factorIds = factorIds;
    request.startDate = startDate;
    request.endDate = endDate;
    request.cacheSnapshot = cacheSnapshot;
    request.dataSourceMode = m_dataSourceMode;
    request.selectedDatasetId = m_selectedDatasetId;

    FactorDetectionService::Overrides overrides;
    overrides.requiredWarmupTradingDaysOverrideForTests = m_requiredWarmupTradingDaysOverrideForTests;
    overrides.resolveInstanceIdOverrideForTests = m_resolveInstanceIdOverrideForTests;
    overrides.instanceInfoOverrideForTests = m_instanceInfoOverrideForTests;
    overrides.factorInstanceOverrideForTests = m_factorInstanceOverrideForTests;
    overrides.cacheFilePathOverrideForTests = m_supportMapPassCacheFilePathOverrideForTests;

    const FactorDetectionService::RuntimeContext detectionRuntimeContext = m_factorDetectionService->resolveRuntimeContext(
        runtimeSnapshot.database,
        runtimeSnapshot.dataChecker,
        runtimeSnapshot.instanceManager,
        m_skipInstanceRefreshForTests);
    const FactorDetectionService::DetectionResult detectionResult = m_factorDetectionService->buildSupportMap(
        request,
        detectionRuntimeContext,
        overrides);
    return detectionResult.supportMap;
}

void FactorBacktestController::requestFactorSupportMapAsync(const QVariantList& factorIds,
                                                            const QString& startDate,
                                                            const QString& endDate,
                                                            const QVariantMap& cacheSnapshot,
                                                            quint64 requestId)
{
    if (!m_factorDetectionService) {
        m_factorDetectionService = std::make_shared<FactorDetectionService>();
    }

    if (!m_threadPool) {
        const unsigned int hardwareThreads = std::thread::hardware_concurrency();
        const size_t workerCount = hardwareThreads > 0
            ? (std::min)(static_cast<size_t>(hardwareThreads), size_t{4})
            : size_t{4};
        m_threadPool = std::make_shared<foundation::thread::ThreadPoolExecutor>((std::max)(size_t{2}, workerCount));
    }

    if (!m_supportMapRequestInFlight) {
        m_supportMapRequestInFlight = true;
        emit supportMapRequestInFlightChanged(true);
    }

    const std::shared_ptr<astock::database::QtMySQLDatabase> databaseSnapshot = m_database;
    const std::shared_ptr<factor::DataAvailabilityChecker> dataCheckerSnapshot = m_dataChecker;
    const QString dataSourceModeSnapshot = m_dataSourceMode;
    const int selectedDatasetIdSnapshot = m_selectedDatasetId;
    const std::shared_ptr<factor::FactorInstanceManager> instanceManagerSnapshot = m_instanceManager;
    const std::shared_ptr<FactorDetectionService> factorDetectionServiceSnapshot = m_factorDetectionService;
    const QHash<QString, int> warmupSnapshot = m_requiredWarmupTradingDaysOverrideForTests;
    const auto resolveInstanceIdOverrideSnapshot = m_resolveInstanceIdOverrideForTests;
    const auto instanceInfoOverrideSnapshot = m_instanceInfoOverrideForTests;
    const auto factorInstanceOverrideSnapshot = m_factorInstanceOverrideForTests;
    const bool skipInstanceRefreshForTestsSnapshot = m_skipInstanceRefreshForTests;
    const auto supportMapPassCacheFilePathOverrideSnapshot = m_supportMapPassCacheFilePathOverrideForTests;
    QPointer<FactorBacktestController> safeController(this);

    m_threadPool->submit([safeController,
                          factorIds,
                          startDate,
                          endDate,
                          cacheSnapshot,
                          requestId,
                          databaseSnapshot,
                          dataCheckerSnapshot,
                          dataSourceModeSnapshot,
                          selectedDatasetIdSnapshot,
                          instanceManagerSnapshot,
                          factorDetectionServiceSnapshot,
                          warmupSnapshot,
                          resolveInstanceIdOverrideSnapshot,
                          instanceInfoOverrideSnapshot,
                          factorInstanceOverrideSnapshot,
                          supportMapPassCacheFilePathOverrideSnapshot,
                          skipInstanceRefreshForTestsSnapshot]() {
        FactorDetectionService::Request request;
        request.factorIds = factorIds;
        request.startDate = startDate;
        request.endDate = endDate;
        request.cacheSnapshot = cacheSnapshot;
        request.dataSourceMode = dataSourceModeSnapshot;
        request.selectedDatasetId = selectedDatasetIdSnapshot;

        FactorDetectionService::Overrides overrides;
        overrides.requiredWarmupTradingDaysOverrideForTests = warmupSnapshot;
        overrides.resolveInstanceIdOverrideForTests = resolveInstanceIdOverrideSnapshot;
        overrides.instanceInfoOverrideForTests = instanceInfoOverrideSnapshot;
        overrides.factorInstanceOverrideForTests = factorInstanceOverrideSnapshot;
        if (supportMapPassCacheFilePathOverrideSnapshot) {
            overrides.cacheFilePathOverrideForTests = supportMapPassCacheFilePathOverrideSnapshot;
        }

        const FactorDetectionService::RuntimeContext runtimeContext = factorDetectionServiceSnapshot->resolveRuntimeContext(
            databaseSnapshot,
            dataCheckerSnapshot,
            instanceManagerSnapshot,
            skipInstanceRefreshForTestsSnapshot);
        const FactorDetectionService::DetectionResult detectionResult = factorDetectionServiceSnapshot->buildSupportMap(
            request,
            runtimeContext,
            overrides);

        if (!safeController) {
            return;
        }

        QMetaObject::invokeMethod(
            safeController,
            [safeController, requestId, detectionResult]() {
                if (!safeController) {
                    return;
                }

                emit safeController->factorSupportMapReady(requestId, detectionResult.supportMap);
            },
            Qt::QueuedConnection);
    });
}

QVariantMap FactorBacktestController::preflightCategoryMeta(const QString& category) const
{
    const QString key = category.trimmed().isEmpty() ? QStringLiteral("unsupported") : category.trimmed();
    if (key == QStringLiteral("supported")) {
        return categoryMetaTemplate(
            key,
            QStringLiteral("可回测"),
            QStringLiteral("通过"),
            QStringLiteral("字段与历史窗口检查通过"),
            QStringLiteral("#15803d"),
            QStringLiteral("#ecfdf5"),
            QStringLiteral("#86efac"),
            QStringLiteral("#166534"));
    }
    if (key == QStringLiteral("dataset-missing")) {
        return categoryMetaTemplate(
            key,
            QStringLiteral("待选缓存集"),
            QStringLiteral("待选择"),
            QStringLiteral("需要先选择可用于因子检查的缓存集"),
            QStringLiteral("#1d4ed8"),
            QStringLiteral("#eff6ff"),
            QStringLiteral("#bfdbfe"),
            QStringLiteral("#1d4ed8"));
    }
    if (key == QStringLiteral("missing-field")) {
        return categoryMetaTemplate(
            key,
            QStringLiteral("字段缺失"),
            QStringLiteral("缺字段"),
            QStringLiteral("缓存集缺少因子所需字段"),
            QStringLiteral("#b45309"),
            QStringLiteral("#fff7ed"),
            QStringLiteral("#fed7aa"),
            QStringLiteral("#9a3412"));
    }
    if (key == QStringLiteral("insufficient-history")) {
        return categoryMetaTemplate(
            key,
            QStringLiteral("历史不足"),
            QStringLiteral("历史不足"),
            QStringLiteral("缓存集历史长度不足以支撑该因子"),
            QStringLiteral("#b91c1c"),
            QStringLiteral("#fef2f2"),
            QStringLiteral("#fecaca"),
            QStringLiteral("#991b1b"));
    }
    if (key == QStringLiteral("invalid-backtest-window")) {
        return categoryMetaTemplate(
            key,
            QStringLiteral("窗口无效"),
            QStringLiteral("窗口无效"),
            QStringLiteral("回测开始/结束日期必须同时提供，禁止使用默认兜底日期"),
            QStringLiteral("#b91c1c"),
            QStringLiteral("#fef2f2"),
            QStringLiteral("#fecaca"),
            QStringLiteral("#991b1b"));
    }
    if (key == QStringLiteral("unsupported-metric")) {
        return categoryMetaTemplate(
            key,
            QStringLiteral("指标不支持"),
            QStringLiteral("不支持"),
            QStringLiteral("当前因子配置不支持进入回测检查"),
            QStringLiteral("#7c2d12"),
            QStringLiteral("#fff7ed"),
            QStringLiteral("#fdba74"),
            QStringLiteral("#9a3412"));
    }
    if (key == QStringLiteral("dataset-empty") || key == QStringLiteral("dataset-invalid")) {
        return categoryMetaTemplate(
            key,
            QStringLiteral("缓存集无效"),
            QStringLiteral("缓存集无效"),
            QStringLiteral("所选缓存集无法用于因子检查"),
            QStringLiteral("#b91c1c"),
            QStringLiteral("#fef2f2"),
            QStringLiteral("#fecaca"),
            QStringLiteral("#991b1b"));
    }
    if (key == QStringLiteral("instance-missing") || key == QStringLiteral("instance-create-failed")) {
        return categoryMetaTemplate(
            key,
            QStringLiteral("实例异常"),
            QStringLiteral("实例异常"),
            QStringLiteral("因子实例不可用，无法执行检查"),
            QStringLiteral("#b91c1c"),
            QStringLiteral("#fef2f2"),
            QStringLiteral("#fecaca"),
            QStringLiteral("#991b1b"));
    }

    return categoryMetaTemplate(
        key,
        QStringLiteral("不可回测"),
        QStringLiteral("不可回测"),
        unsupportedBacktestReason(),
        QStringLiteral("#6b7280"),
        QStringLiteral("#f3f4f6"),
        QStringLiteral("#d1d5db"),
        QStringLiteral("#374151"));
}

QString FactorBacktestController::preflightFailureDetailText(const QVariantMap& failure,
                                                             const QString& factorDisplayName) const
{
    const QString reason = failure.value(QStringLiteral("reason")).toString().trimmed();
    if (!reason.isEmpty()) {
        return reason;
    }
    const QString category = failure.value(QStringLiteral("category")).toString().trimmed();
    const QVariantMap meta = preflightCategoryMeta(category);
    if (!factorDisplayName.trimmed().isEmpty()) {
        return QStringLiteral("%1: %2").arg(factorDisplayName.trimmed(), meta.value(QStringLiteral("detail")).toString());
    }
    return meta.value(QStringLiteral("detail")).toString();
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
    QVariantMap state;
    state[QStringLiteral("factorId")] = factorId.trimmed();

    const QString normalizedFactorId = factorId.trimmed();
    const bool isSelected = selectedFactorIds.contains(normalizedFactorId);
    if (!hasFactorDefinition) {
        state[QStringLiteral("supported")] = false;
        state[QStringLiteral("category")] = QStringLiteral("instance-missing");
        state[QStringLiteral("reason")] = QStringLiteral("未找到因子定义或实例配置");
    } else if (!isSelected) {
        state[QStringLiteral("supported")] = false;
        state[QStringLiteral("category")] = QStringLiteral("unselected");
        state[QStringLiteral("reason")] = QStringLiteral("当前因子未加入回测检查列表");
    } else if (normalizedDataSourceMode(dataSourceMode) == QStringLiteral("cache") && !hasAvailableCacheDataset && selectedDatasetId <= 0) {
        state[QStringLiteral("supported")] = false;
        state[QStringLiteral("category")] = QStringLiteral("dataset-missing");
        state[QStringLiteral("reason")] = QStringLiteral("请先选择可用于因子回测检查的缓存集");
    } else if (!supportInfo.isEmpty()) {
        state[QStringLiteral("supported")] = supportInfo.value(QStringLiteral("supported")).toBool();
        state[QStringLiteral("category")] = supportInfo.value(QStringLiteral("category")).toString();
        state[QStringLiteral("reason")] = supportInfo.value(QStringLiteral("reason")).toString();
    } else if (!preflightFailures.isEmpty()) {
        const QVariantMap failure = preflightFailures.first().toMap();
        state[QStringLiteral("supported")] = false;
        state[QStringLiteral("category")] = failure.value(QStringLiteral("category")).toString();
        state[QStringLiteral("reason")] = preflightFailureDetailText(failure, factorDisplayName);
    } else if (!lastBacktestError.trimmed().isEmpty()) {
        state[QStringLiteral("supported")] = false;
        state[QStringLiteral("category")] = QStringLiteral("runtime-init-failed");
        state[QStringLiteral("reason")] = lastBacktestError.trimmed();
    } else if (!backtestResult.isEmpty()) {
        state[QStringLiteral("supported")] = true;
        state[QStringLiteral("category")] = QStringLiteral("supported");
        state[QStringLiteral("reason")] = QStringLiteral("该因子已有回测结果，可继续复用当前检查结论");
    } else {
        state[QStringLiteral("supported")] = false;
        state[QStringLiteral("category")] = QStringLiteral("unsupported");
        state[QStringLiteral("reason")] = pendingPreflightReason();
    }

    const QVariantMap meta = preflightCategoryMeta(state.value(QStringLiteral("category")).toString());
    state[QStringLiteral("statusText")] = meta.value(QStringLiteral("statusText")).toString();
    state[QStringLiteral("shortText")] = meta.value(QStringLiteral("shortText")).toString();
    state[QStringLiteral("detail")] = meta.value(QStringLiteral("detail")).toString();
    state[QStringLiteral("accentColor")] = meta.value(QStringLiteral("accentColor")).toString();
    state[QStringLiteral("chipBackground")] = meta.value(QStringLiteral("chipBackground")).toString();
    state[QStringLiteral("chipBorder")] = meta.value(QStringLiteral("chipBorder")).toString();
    state[QStringLiteral("chipText")] = meta.value(QStringLiteral("chipText")).toString();
    state[QStringLiteral("displayName")] = factorDisplayName.trimmed();
    return state;
}

bool FactorBacktestController::datasetSelectableForBacktest(const QVariantMap& dataset) const
{
    const int datasetId = dataset.value(QStringLiteral("id"), dataset.value(QStringLiteral("value"))).toInt();
    if (datasetId <= 0) {
        return false;
    }

    if (!isCleanedBacktestDataset(dataset)) {
        return false;
    }

    const bool isBacktestReady = dataset.value(QStringLiteral("isBacktestReady")).toBool();
    const QStringList tags = dataset.value(QStringLiteral("tags")).toStringList();
    if (!isBacktestReady && !tags.contains(QStringLiteral("factor_backtest_ready"))) {
        return false;
    }

    const QStringList availableFields = dataset.value(QStringLiteral("availableFields")).toStringList();
    return !availableFields.isEmpty();
}

QVariantList FactorBacktestController::buildBacktestDatasetOptions(const QVariantList& datasetList) const
{
    QVariantList options;
    options.append(QVariantMap{
        {QStringLiteral("text"), QStringLiteral("请选择缓存集")},
        {QStringLiteral("value"), -1},
        {QStringLiteral("raw"), QVariantMap{}}
    });

    QList<QVariantMap> selectableDatasets;
    selectableDatasets.reserve(datasetList.size());
    for (const QVariant& datasetValue : datasetList) {
        const QVariantMap dataset = datasetValue.toMap();
        if (datasetSelectableForBacktest(dataset)) {
            selectableDatasets.append(dataset);
        }
    }

    std::sort(selectableDatasets.begin(), selectableDatasets.end(), [](const QVariantMap& left, const QVariantMap& right) {
        return left.value(QStringLiteral("id")).toInt() > right.value(QStringLiteral("id")).toInt();
    });

    for (const QVariantMap& dataset : selectableDatasets) {
        const int datasetId = dataset.value(QStringLiteral("id")).toInt();
        const QString displayName = dataset.value(QStringLiteral("displayName")).toString().trimmed();
        const QString startDate = dataset.value(QStringLiteral("startDate")).toString().trimmed();
        const QString endDate = dataset.value(QStringLiteral("endDate")).toString().trimmed();
        QString text = displayName.isEmpty()
            ? QStringLiteral("缓存集 #%1").arg(datasetId)
            : displayName;
        if (!startDate.isEmpty() && !endDate.isEmpty()) {
            text += QStringLiteral(" (%1 ~ %2)").arg(startDate, endDate);
        }
        options.append(QVariantMap{
            {QStringLiteral("text"), text},
            {QStringLiteral("value"), datasetId},
            {QStringLiteral("raw"), dataset}
        });
    }

    return options;
}

QVariantList FactorBacktestController::normalizeFactorIds(const QVariantList& factorIds) const
{
    return dedupeFactorIds(factorIds);
}

QVariantMap FactorBacktestController::filterFactorIdsBySupport(const QVariantList& factorIds,
                                                               const QVariantMap& supportMap) const
{
    QVariantMap result;
    QVariantList supportedFactorIds;
    QVariantList unsupportedFactorIds;

    const QVariantList normalized = dedupeFactorIds(factorIds);
    for (const QVariant& factorIdValue : normalized) {
        const QString factorId = factorIdValue.toString().trimmed();
        const QVariantMap supportInfo = supportMap.value(factorId).toMap();
        if (!supportInfo.isEmpty() && supportInfo.value(QStringLiteral("supported")).toBool()) {
            supportedFactorIds.append(factorId);
        } else {
            unsupportedFactorIds.append(factorId);
        }
    }

    result[QStringLiteral("supportedFactorIds")] = supportedFactorIds;
    result[QStringLiteral("unsupportedFactorIds")] = unsupportedFactorIds;
    result[QStringLiteral("filteredFactorIds")] = supportedFactorIds;
    return result;
}

int FactorBacktestController::beginFactorSupportMapRefresh(const QVariantList& factorIds,
                                                           const QString& startDate,
                                                           const QString& endDate,
                                                           const QVariantMap& cacheSnapshot)
{
    const int requestId = ++m_supportMapRequestSeq;
    requestFactorSupportMapAsync(factorIds, startDate, endDate, cacheSnapshot, static_cast<quint64>(requestId));
    return requestId;
}

bool FactorBacktestController::handleFactorSupportMapReady(int requestId,
                                                           const QVariantMap& supportMap)
{
    if (requestId != m_supportMapRequestSeq || requestId <= m_supportMapAppliedSeq) {
        return false;
    }

    m_supportMapAppliedSeq = requestId;
    m_factorSupportMapCache = supportMap;
    emit factorSupportMapCacheChanged(m_factorSupportMapCache);
    m_supportMapRequestInFlight = false;
    emit supportMapRequestInFlightChanged(false);
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
    Q_UNUSED(previousBacktestReport)
    Q_UNUSED(currentDatasetInfo)
    return {};
}

QString FactorBacktestController::stockPoolComparisonText(const QVariantList& selectedFactorIds,
                                                          const QVariantMap& comparison) const
{
    Q_UNUSED(selectedFactorIds)
    Q_UNUSED(comparison)
    return removedReason();
}

QVariantList FactorBacktestController::displayedBacktestResults(const QVariantMap& backtestResult) const
{
    return backtestResult.value(QStringLiteral("results")).toList();
}

QString FactorBacktestController::displayedBacktestResultName(const QVariantMap& entry) const
{
    const QString displayName = entry.value(QStringLiteral("displayName")).toString().trimmed();
    if (!displayName.isEmpty()) {
        return displayName;
    }
    return entry.value(QStringLiteral("factorName")).toString().trimmed();
}

QVariantMap FactorBacktestController::resolveDisplayedBacktestState(const QVariantMap& backtestResult,
                                                                    int selectedResultIndex) const
{
    Q_UNUSED(selectedResultIndex)
    return backtestResult;
}

QVariantMap FactorBacktestController::resolveRiskConfigurationSnapshot(const QVariantMap& displayedBacktestResult,
                                                                      const QVariantMap& appliedConfiguration,
                                                                      const QVariantMap& currentConfiguration) const
{
    Q_UNUSED(displayedBacktestResult)
    Q_UNUSED(appliedConfiguration)
    Q_UNUSED(currentConfiguration)
    return {};
}

QString FactorBacktestController::riskConfigBenchmarkSymbol(const QVariantMap& snapshot,
                                                            const QString& fallbackSymbol) const
{
    const QString symbol = risk::config::benchmarkSymbol(snapshot, fallbackSymbol);
    return symbol.isEmpty() ? fallbackSymbol : symbol;
}

QVariantList FactorBacktestController::riskConfigMetricCards(const QVariantMap& snapshot) const
{
    Q_UNUSED(snapshot)
    return {};
}

QVariantMap FactorBacktestController::buildSingleFactorRunEntry(const QVariantMap& result,
                                                                const QString& fallbackFactorName) const
{
    QVariantMap entry = result;
    if (entry.value(QStringLiteral("factorName")).toString().trimmed().isEmpty()) {
        entry[QStringLiteral("factorName")] = fallbackFactorName;
    }

    // 单因子历史卡片复用完整回测结果合同，保持 metrics / config / results 结构不变。
    // 禁止在这里扁平化 execution / ic / factorQuality 字段，避免历史卡片与主结果页再次漂移。
    return entry;
}

QVariantList FactorBacktestController::pushSingleFactorRunHistory(const QVariantList& existingHistory,
                                                                  const QVariantMap& result,
                                                                  int historyLimit,
                                                                  const QString& fallbackFactorName) const
{
    QVariantList history = existingHistory;
    history.prepend(buildSingleFactorRunEntry(result, fallbackFactorName));
    while (history.size() > historyLimit && historyLimit >= 0) {
        history.removeLast();
    }
    return history;
}

int FactorBacktestController::parseGroupCount(const QString& groupText) const
{
    QString digits;
    for (const QChar ch : groupText) {
        if (ch.isDigit()) {
            digits.append(ch);
        }
    }
    bool ok = false;
    const int parsed = digits.toInt(&ok);
    return ok && parsed > 0 ? parsed : 10;
}

QString FactorBacktestController::resolveInstanceId(const QVariant& factorId) const
{
    if (m_resolveInstanceIdOverrideForTests) {
        return m_resolveInstanceIdOverrideForTests(factorId).trimmed();
    }

    const QString directId = factorId.toString().trimmed();
    if (!directId.isEmpty()) {
        return directId;
    }

    const QVariantMap factorMap = factorId.toMap();
    return factorMap.value(QStringLiteral("instanceId"), factorMap.value(QStringLiteral("factorId"))).toString().trimmed();
}

factor::FactorInstanceInfo FactorBacktestController::getInstanceInfo(const QString& resolvedInstanceId) const
{
    if (m_instanceInfoOverrideForTests) {
        return m_instanceInfoOverrideForTests(resolvedInstanceId);
    }
    auto* self = const_cast<FactorBacktestController*>(this);
    if (self->ensureInstanceRuntime() && m_instanceManager) {
        return m_instanceManager->getInstanceInfo(resolvedInstanceId.toStdString());
    }

    factor::FactorInstanceInfo info;
    info.instanceId = resolvedInstanceId.toStdString();
    return info;
}

factor::BacktestConfig FactorBacktestController::buildBacktestConfig(const QString& resolvedInstanceId,
                                                                     const QString& groupText,
                                                                     const QString& startDate,
                                                                     const QString& endDate,
                                                                     const QString& dataSourceMode,
                                                                     int datasetId,
                                                                     const QVariantMap& datasetBenchmarkMetadata,
                                                                     const QVariantList& selectedStockPoolSymbols,
                                                                     const QVariantMap& backtestRuntimeParams,
                                                                     int batchFactorCount,
                                                                     int workerCount) const
{
    const QString trimmedStartDate = startDate.trimmed();
    const QString trimmedEndDate = endDate.trimmed();
    Q_UNUSED(dataSourceMode);
    if (trimmedStartDate.isEmpty()) {
        throw std::runtime_error(QStringLiteral("回测开始日期缺失，禁止使用默认兜底日期").toUtf8().constData());
    }
    if (trimmedEndDate.isEmpty()) {
        throw std::runtime_error(QStringLiteral("回测结束日期缺失，禁止使用默认兜底日期").toUtf8().constData());
    }

    factor::BacktestConfig config;
    config.instanceId = resolvedInstanceId.toStdString();
    config.datasetId = datasetId;
    config.startDate = trimmedStartDate.toStdString();
    config.endDate = trimmedEndDate.toStdString();
    config.numGroups = parseGroupCount(groupText);

    const QVariantMap runtimeParams = backtestRuntimeParams;
    config.marketEnvironmentProfile = factor::marketEnvironmentProfileFromIndex(
        risk::config::marketEnvironmentProfile(
            runtimeParams,
            risk::config::kDefaultMarketEnvironmentProfile));
    config.forwardDays = risk::config::forwardDays(runtimeParams, risk::config::kDefaultForwardDays);
    config.rebalanceDays = risk::config::rebalanceDays(runtimeParams, risk::config::kDefaultRebalanceDays);
    config.transactionCost = risk::config::commissionRate(runtimeParams, risk::config::kDefaultCommissionRate);
    config.slippageRate = risk::config::slippageRate(runtimeParams, risk::config::kDefaultSlippageRate);
    config.riskFreeRate = risk::config::riskFreeRate(runtimeParams, risk::config::kDefaultRiskFreeRate);
    config.benchmarkSymbol = resolveConfiguredBenchmarkSymbol(runtimeParams, datasetBenchmarkMetadata).toStdString();
    config.stopLossRate = risk::config::stopLossPercent(runtimeParams, risk::config::kDefaultStopLossPercent);
    config.takeProfitRate = risk::config::takeProfitPercent(runtimeParams, risk::config::kDefaultTakeProfitPercent);
    config.maxDrawdownLimit = risk::config::maxDrawdownLimit(runtimeParams, risk::config::kDefaultMaxDrawdownLimit);
    config.maxDailyLoss = risk::config::maxDailyLoss(runtimeParams, risk::config::kDefaultMaxDailyLoss);
    config.maxPositionPercent = risk::config::maxPositionPercent(runtimeParams, risk::config::kDefaultMaxPositionPercent);
    config.maxTotalExposure = risk::config::maxTotalExposure(runtimeParams, risk::config::kDefaultMaxTotalExposure);
    config.enableRiskControls = risk::config::enableRiskControls(runtimeParams, risk::config::kDefaultEnableRiskControls);
    config.enableDateParallelism = shouldEnableDateParallelism(batchFactorCount, workerCount);

    qDebug() << "FactorBacktestController: 构建回测配置"
             << "instanceId=" << resolvedInstanceId
             << "marketEnvironmentProfile="
             << factor::marketEnvironmentProfileIndex(config.marketEnvironmentProfile)
             << "datasetId=" << datasetId
             << "batchFactorCount=" << batchFactorCount
             << "threadPoolReady=" << (workerCount > 0)
             << "workerCount=" << workerCount
             << "enableDateParallelism=" << config.enableDateParallelism
             << "startDate=" << trimmedStartDate
             << "endDate=" << trimmedEndDate;

    for (const QVariant& symbolValue : normalizedStockPoolSymbols(selectedStockPoolSymbols)) {
        config.allowedStockCodes.push_back(symbolValue.toString().toStdString());
    }

    const QString dynamicIndexSymbol = resolveIndexSymbolFromMap(datasetBenchmarkMetadata);
    std::vector<HistoricalIndexConstituentRange> dynamicIndexRanges;
    QStringList dynamicIndexUnionSymbols;
    QDate dynamicIndexRangeStartDate;
    QDate dynamicIndexRangeEndDate = QDate::fromString(trimmedEndDate, Qt::ISODate);
    const QDate anchorStartDate = QDate::fromString(trimmedStartDate, Qt::ISODate);
    const bool requiresDynamicIndexHistory = !dynamicIndexSymbol.isEmpty()
        && dynamicIndexSymbol != QStringLiteral("BIG_CAP")
        && dynamicIndexSymbol != QStringLiteral("SMALL_CAP")
        && anchorStartDate.isValid()
        && dynamicIndexRangeEndDate.isValid();

    BacktestRuntimeRequirements runtimeRequirements;
    if (datasetId > 0) {
        const factor::FactorInstanceInfo info = getInstanceInfo(resolvedInstanceId);
        auto* self = const_cast<FactorBacktestController*>(this);
        std::shared_ptr<factor::BaseFactor> factorInstance = m_factorInstanceOverrideForTests
            ? m_factorInstanceOverrideForTests(resolvedInstanceId)
            : ((self->ensureInstanceRuntime() && m_instanceManager)
                   ? m_instanceManager->createIsolatedInstance(resolvedInstanceId.toStdString())
                   : nullptr);
        if (!factorInstance) {
            factorInstance = createRuntimeRequirementsFactorInstance(info, m_dataChecker);
        }
        runtimeRequirements = resolveBacktestRuntimeRequirements(
            resolvedInstanceId,
            info,
            factorInstance,
            m_requiredWarmupTradingDaysOverrideForTests);
    }

    std::shared_ptr<astock::database::QtMySQLDatabase> database = m_database;
    if (datasetId > 0
        && (runtimeRequirements.useDailyBarWarmup || requiresDynamicIndexHistory)
        && !database) {
        auto& dbManager = astock::database::DatabaseConnectionManager::instance();
        if (dbManager.initialize()) {
            auto* self = const_cast<FactorBacktestController*>(this);
            self->m_database = dbManager.getDatabase();
            database = self->m_database;
        }
    }

    if (runtimeRequirements.useDailyBarWarmup && !database) {
        throw std::runtime_error(
            QStringLiteral("因子回测需要 %1 个交易日的 warmup 历史，但数据库连接未就绪，禁止仅使用缓存集继续回测")
                .arg(runtimeRequirements.warmupTradingDays)
                .toUtf8()
                .constData());
    }

    if (requiresDynamicIndexHistory && !database) {
        throw std::runtime_error(
            QStringLiteral("动态指数股票池需要数据库中的历史成分股数据，数据库连接未就绪")
                .toUtf8()
                .constData());
    }

    if (datasetId > 0 && database) {
        if (runtimeRequirements.useDailyBarWarmup && !anchorStartDate.isValid()) {
            throw std::runtime_error(QStringLiteral("回测开始日期非法，无法补齐 warmup 历史数据").toUtf8().constData());
        }

        QDate historyStartDate;
        if (runtimeRequirements.useDailyBarWarmup) {
            historyStartDate = resolveWarmupHistoryStartDateFromDatabase(
                database,
                anchorStartDate,
                runtimeRequirements.warmupTradingDays);
            if (!historyStartDate.isValid()) {
                throw std::runtime_error(
                    QStringLiteral("因子回测需要 %1 个交易日的 warmup 历史，但数据库未返回足够早的交易日，禁止仅使用缓存集继续回测")
                        .arg(runtimeRequirements.warmupTradingDays)
                        .toUtf8()
                        .constData());
            }
        }

        if (requiresDynamicIndexHistory) {
            dynamicIndexRangeStartDate = historyStartDate.isValid()
                ? historyStartDate
                : anchorStartDate;
            dynamicIndexRanges = queryHistoricalIndexConstituentRanges(
                database,
                dynamicIndexSymbol,
                dynamicIndexRangeStartDate,
                dynamicIndexRangeEndDate);
            if (dynamicIndexRanges.empty()) {
                throw std::runtime_error(
                    QStringLiteral("指数历史成分股为空，禁止使用静态股票池回测: %1")
                        .arg(dynamicIndexSymbol)
                        .toUtf8()
                        .constData());
            }
            dynamicIndexUnionSymbols = collectHistoricalIndexConstituentSymbols(dynamicIndexRanges);
            config.allowedStockCodesByDate = buildAllowedStockCodesByDate(
                dynamicIndexRanges,
                QDate::fromString(trimmedStartDate, Qt::ISODate),
                dynamicIndexRangeEndDate);
            config.allowedStockCodes.clear();
        }

        if (runtimeRequirements.useDailyBarWarmup) {
            auto& cache = DataServiceCache::getInstance();
            cache.initializeCache();
            const QVariantList baseRows = cache.getDataSetById(datasetId);
            if (baseRows.isEmpty()) {
                throw std::runtime_error(QStringLiteral("所选缓存集为空，无法合并 warmup 历史数据").toUtf8().constData());
            }

            const DataServiceCache::DataSetInfo dataSetInfo = cache.getDataSetInfo(datasetId);
            const QStringList symbols = dynamicIndexUnionSymbols.isEmpty()
                ? warmupQuerySymbols(dataSetInfo, selectedStockPoolSymbols)
                : dynamicIndexUnionSymbols;
            const QVariantList warmupRows = queryDailyBarWarmupRows(
                database,
                symbols,
                historyStartDate,
                anchorStartDate);
            if (warmupRows.isEmpty()) {
                throw std::runtime_error(
                    QStringLiteral("因子回测需要 %1 个交易日的 warmup 历史，但数据库未返回 %2 之前的补充行情，禁止仅使用缓存集继续回测")
                        .arg(runtimeRequirements.warmupTradingDays)
                        .arg(trimmedStartDate)
                        .toUtf8()
                        .constData());
            }

            QVariantList mergedRows = warmupRows;
            mergedRows += baseRows;
            std::vector<factor::CachedMarketBar> cachedBars = factor::cached_bars::buildCachedBarsFromRows(mergedRows);
            if (cachedBars.empty()) {
                throw std::runtime_error(QStringLiteral("warmup 历史数据转换失败：未生成有效缓存行").toUtf8().constData());
            }

            config.preparedArrowData = factor::ArrowMarketData::fromCachedBars(cachedBars);
            if (!config.preparedArrowData || config.preparedArrowData->rowCount() <= 0) {
                throw std::runtime_error(QStringLiteral("warmup 历史数据转换失败：Arrow 市场数据为空").toUtf8().constData());
            }

            const QVariantList normalizedSymbols = normalizedStockPoolSymbols(selectedStockPoolSymbols);
            QStringList symbolList;
            if (!dynamicIndexUnionSymbols.isEmpty()) {
                symbolList = dynamicIndexUnionSymbols;
            } else {
                symbolList.reserve(normalizedSymbols.size());
                for (const QVariant& symbolValue : normalizedSymbols) {
                    symbolList.append(symbolValue.toString().trimmed().toUpper());
                }
            }
            const QString symbolScope = symbolList.isEmpty()
                ? QStringLiteral("ALL")
                : QString::number(qHash(symbolList.join(QStringLiteral("|"))));
            config.marketDataCacheKey = QStringLiteral("warmup|ds=%1|hist=%2|start=%3|end=%4|env=%5|fwd=%6|warm=%7|pool=%8")
                .arg(datasetId)
                .arg(historyStartDate.toString(QStringLiteral("yyyy-MM-dd")))
                .arg(trimmedStartDate)
                .arg(trimmedEndDate)
                .arg(factor::marketEnvironmentProfileIndex(config.marketEnvironmentProfile))
                .arg(config.forwardDays)
                .arg(runtimeRequirements.warmupTradingDays)
                .arg(symbolScope)
                .toStdString();

            qDebug() << "FactorBacktestController: 已从数据库补充 warmup 历史"
                     << "instanceId=" << resolvedInstanceId
                     << "datasetId=" << datasetId
                     << "dynamicIndexSymbol=" << dynamicIndexSymbol
                     << "warmupTradingDays=" << runtimeRequirements.warmupTradingDays
                     << "historyStartDate=" << historyStartDate.toString("yyyy-MM-dd")
                     << "warmupRowCount=" << warmupRows.size()
                     << "mergedRowCount=" << mergedRows.size()
                     << "preparedArrowRowCount=" << config.preparedArrowData->rowCount();
        }
    }

    return config;
}

QVariantMap FactorBacktestController::buildResultMap(const QString& requestedFactorId,
                                                     const factor::BacktestResult& result) const
{
    QVariantMap map;
    map[QStringLiteral("factorId")] = requestedFactorId.trimmed();
    map[QStringLiteral("taskId")] = QString::fromStdString(result.resultId.to_string());
    map[QStringLiteral("executionTime")] = result.executionTimeMs;
    map[QStringLiteral("success")] = (result.status == "SUCCESS");
    map[QStringLiteral("status")] = QString::fromStdString(result.status);
    map[QStringLiteral("config")] = buildConfigMap(requestedFactorId, result);
    map[QStringLiteral("metrics")] = buildMetricSectionsMap(result);
    map[QStringLiteral("results")] = QVariantList{};
    map[QStringLiteral("factorIds")] = QVariantList{};
    return map;
}

QVariantMap FactorBacktestController::buildAggregatedResultMap() const
{
    QVariantList completedResults;
    completedResults.reserve(static_cast<int>(m_batchResultMaps.size()));
    int executionTime = 0;
    for (const QVariantMap& resultMap : m_batchResultMaps) {
        if (resultMap.isEmpty()) {
            continue;
        }
        completedResults.append(resultMap);
        executionTime += resultMap.value(QStringLiteral("executionTime")).toInt();
    }

    if (completedResults.isEmpty()) {
        return {};
    }

    QVariantMap aggregate = completedResults.first().toMap();
    aggregate[QStringLiteral("results")] = completedResults;
    aggregate[QStringLiteral("factorIds")] = dedupeFactorIds(m_batchFactorIds);
    aggregate[QStringLiteral("factorCount")] = completedResults.size();
    aggregate[QStringLiteral("executionTime")] = executionTime;
    return aggregate;
}

std::shared_ptr<factor::FactorBacktestExecutor> FactorBacktestController::ensureBatchExecutor(
    const std::shared_ptr<factor::FactorInstanceManager>& instanceManager)
{
    if (m_batchExecutor) {
        return m_batchExecutor;
    }

    if (!instanceManager || !m_threadPool || !m_cacheManager) {
        return nullptr;
    }

    m_batchExecutor = m_createExecutorOverrideForTests
        ? m_createExecutorOverrideForTests(instanceManager, m_threadPool, m_cacheManager)
        : std::make_shared<factor::FactorBacktestExecutor>(instanceManager, m_threadPool, m_cacheManager);
    return m_batchExecutor;
}

void FactorBacktestController::launchNextBacktestTask()
{
    if (!m_isRunning || m_cancelRequested.load() || m_batchFactorIds.isEmpty()) {
        return;
    }

    if (m_pendingBatchLaunchFuture || m_pendingBatchFuture) {
        return;
    }

    if (!m_threadPool || !m_cacheManager) {
        finalizeBacktestFailure(QStringLiteral("因子回测运行时未初始化"), false);
        return;
    }

    const FactorRuntimeSnapshot runtimeSnapshot = resolveFactorRuntimeSnapshot(
        m_database,
        m_dataChecker,
        m_instanceManager,
        m_skipInstanceRefreshForTests);
    if (!runtimeSnapshot.errorMessage.isEmpty()) {
        finalizeBacktestFailure(runtimeSnapshot.errorMessage, false);
        return;
    }
    if (!m_database && runtimeSnapshot.database) {
        m_database = runtimeSnapshot.database;
    }
    if (!m_dataChecker && runtimeSnapshot.dataChecker) {
        m_dataChecker = runtimeSnapshot.dataChecker;
    }
    if (!m_instanceManager && runtimeSnapshot.instanceManager) {
        m_instanceManager = runtimeSnapshot.instanceManager;
    }

    const QVariantList batchFactorIdsSnapshot = m_batchFactorIds;
    const QString groupText = m_pendingGroupText;
    const QString startDate = m_pendingStartDate;
    const QString endDate = m_pendingEndDate;
    const QString sourceModeSnapshot = m_pendingDataSourceMode;
    const int datasetIdSnapshot = m_pendingDatasetId;
    const QVariantMap datasetBenchmarkMetadataSnapshot = m_pendingDatasetBenchmarkMetadata;
    const QVariantList stockPoolSymbolsSnapshot = m_pendingStockPoolSymbols;
    const QVariantMap runtimeParamsSnapshot = m_pendingRuntimeParams;
    const int batchFactorCountSnapshot = m_pendingBatchFactorCount;
    const int workerCountSnapshot = m_pendingWorkerCount;
    const std::shared_ptr<astock::database::QtMySQLDatabase> databaseSnapshot = m_database;
    const std::shared_ptr<factor::DataAvailabilityChecker> dataCheckerSnapshot = m_dataChecker;
    const std::shared_ptr<factor::FactorInstanceManager> instanceManagerSnapshot = m_instanceManager;
    const std::shared_ptr<foundation::thread::ThreadPoolExecutor> threadPoolSnapshot = m_threadPool;
    const std::shared_ptr<factor::FactorCacheManager> cacheManagerSnapshot = m_cacheManager;
    const bool skipInstanceRefreshForTestsSnapshot = m_skipInstanceRefreshForTests;
    const auto resolveInstanceIdOverrideSnapshot = m_resolveInstanceIdOverrideForTests;
    std::shared_ptr<factor::FactorBacktestExecutor> batchExecutorSnapshot = m_batchExecutor;
    if (!batchExecutorSnapshot) {
        if (!runtimeSnapshot.errorMessage.isEmpty() || !runtimeSnapshot.instanceManager) {
            finalizeBacktestFailure(runtimeSnapshot.errorMessage.isEmpty()
                                        ? QStringLiteral("因子回测运行时未初始化")
                                        : runtimeSnapshot.errorMessage,
                                    false);
            return;
        }

        batchExecutorSnapshot = ensureBatchExecutor(runtimeSnapshot.instanceManager);
        if (!batchExecutorSnapshot) {
            finalizeBacktestFailure(QStringLiteral("因子回测执行器初始化失败"), false);
            return;
        }
    }
    if (!m_pendingBatchLaunchProgressState) {
        m_pendingBatchLaunchProgressState = std::make_shared<PendingBacktestLaunchProgressState>();
    }
    m_pendingBatchLaunchProgressState->update(0, QStringLiteral("正在提交整批回测任务"));
    const std::shared_ptr<PendingBacktestLaunchProgressState> launchProgressStateSnapshot = m_pendingBatchLaunchProgressState;

    m_pendingBatchLaunchFuture = std::make_shared<std::future<PendingBacktestBatchLaunchResult>>(
        m_threadPool->submit([this,
                              batchFactorIdsSnapshot,
                              groupText,
                              startDate,
                              endDate,
                              databaseSnapshot,
                              dataCheckerSnapshot,
                              instanceManagerSnapshot,
                              sourceModeSnapshot,
                              datasetIdSnapshot,
                              datasetBenchmarkMetadataSnapshot,
                              stockPoolSymbolsSnapshot,
                              runtimeParamsSnapshot,
                              threadPoolSnapshot,
                              cacheManagerSnapshot,
                              batchFactorCountSnapshot,
                              workerCountSnapshot,
                              resolveInstanceIdOverrideSnapshot,
                              batchExecutorSnapshot,
                              launchProgressStateSnapshot]() -> PendingBacktestBatchLaunchResult {
            PendingBacktestBatchLaunchResult launchResult;

            try {
                if (m_cancelRequested.load()) {
                    launchResult.errorMessage = cancelledBacktestReason();
                    return launchResult;
                }

                std::vector<factor::BacktestConfig> configs;
                configs.reserve(static_cast<size_t>(batchFactorIdsSnapshot.size()));

                for (int index = 0; index < batchFactorIdsSnapshot.size(); ++index) {
                    if (m_cancelRequested.load()) {
                        launchResult.errorMessage = cancelledBacktestReason();
                        return launchResult;
                    }

                    launchProgressStateSnapshot->update(
                        20 + static_cast<int>((40.0 * static_cast<double>(index))
                                              / static_cast<double>((std::max)(size_t{1}, static_cast<size_t>(batchFactorIdsSnapshot.size())))),
                        QStringLiteral("正在构建批量回测配置"));
                    const QString resolvedInstanceId = resolveInstanceIdFromFactorValue(
                        batchFactorIdsSnapshot.at(index),
                        resolveInstanceIdOverrideSnapshot);
                    if (resolvedInstanceId.isEmpty()) {
                        throw std::runtime_error(QStringLiteral("未找到可执行回测的因子实例").toUtf8().constData());
                    }

                    configs.push_back(buildBacktestConfig(resolvedInstanceId,
                                                          groupText,
                                                          startDate,
                                                          endDate,
                                                          sourceModeSnapshot,
                                                          datasetIdSnapshot,
                                                          datasetBenchmarkMetadataSnapshot,
                                                          stockPoolSymbolsSnapshot,
                                                          runtimeParamsSnapshot,
                                                          batchFactorCountSnapshot,
                                                          workerCountSnapshot));
                }

                qDebug() << "FactorBacktestController: 已切换到后台线程提交回测"
                         << "factorCount=" << batchFactorIdsSnapshot.size()
                         << "threadId=" << reinterpret_cast<quintptr>(QThread::currentThreadId());

                if (m_cancelRequested.load()) {
                    launchResult.errorMessage = cancelledBacktestReason();
                    return launchResult;
                }

                launchProgressStateSnapshot->update(90, QStringLiteral("正在启动整批回测执行器"));
                launchResult.executor = batchExecutorSnapshot;
                factor::FactorBacktestExecutor::BatchExecutionHandle handle = launchResult.executor->executeBatchTrackedAsync(configs);
                launchResult.taskId = handle.taskId;
                launchResult.future = std::make_shared<std::future<std::vector<factor::BacktestResult>>>(std::move(handle.future));
                launchProgressStateSnapshot->update(100, QStringLiteral("整批回测任务已提交，等待执行"));
            } catch (const std::exception& e) {
                launchResult.errorMessage = QString::fromUtf8(e.what()).trimmed();
            }

            return launchResult;
        }));
}

void FactorBacktestController::detachPendingBacktestTasks()
{
    if (m_pendingBatchLaunchFuture || m_pendingBatchFuture) {
        DetachedPendingBacktestBatchState detachedBatch;
        detachedBatch.launchFuture = std::move(m_pendingBatchLaunchFuture);
        detachedBatch.future = std::move(m_pendingBatchFuture);
        m_detachedPendingBacktestBatches.push_back(std::move(detachedBatch));
        m_pendingBatchLaunchProgressState.reset();
    }

    if (m_pendingBacktestTasks.empty()) {
        return;
    }

    m_detachedBacktestTasks.reserve(m_detachedBacktestTasks.size() + m_pendingBacktestTasks.size());
    for (PendingBacktestTask& task : m_pendingBacktestTasks) {
        m_detachedBacktestTasks.push_back(std::move(task));
    }
    m_pendingBacktestTasks.clear();
}

void FactorBacktestController::cleanupDetachedBacktestTasks(bool waitForCompletion)
{
    if (!m_detachedPendingBacktestBatches.empty()) {
        auto batchCompleted = [waitForCompletion](DetachedPendingBacktestBatchState& batchState) {
            if (batchState.launchFuture) {
                if (waitForCompletion) {
                    batchState.launchFuture->wait();
                } else if (batchState.launchFuture->wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                    return false;
                }
            }

            if (batchState.future) {
                if (waitForCompletion) {
                    batchState.future->wait();
                } else if (batchState.future->wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                    return false;
                }
            }

            return true;
        };

        if (waitForCompletion) {
            for (DetachedPendingBacktestBatchState& batchState : m_detachedPendingBacktestBatches) {
                batchCompleted(batchState);
            }
            m_detachedPendingBacktestBatches.clear();
        } else {
            auto completedBegin = std::remove_if(m_detachedPendingBacktestBatches.begin(),
                                                 m_detachedPendingBacktestBatches.end(),
                                                 [&batchCompleted](DetachedPendingBacktestBatchState& batchState) {
                                                     return batchCompleted(batchState);
                                                 });
            m_detachedPendingBacktestBatches.erase(completedBegin, m_detachedPendingBacktestBatches.end());
        }
    }

    if (m_detachedBacktestTasks.empty()) {
        return;
    }

    auto taskCompleted = [waitForCompletion](PendingBacktestTask& task) {
        if (task.launchFuture) {
            if (waitForCompletion) {
                task.launchFuture->wait();
            } else if (task.launchFuture->wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                return false;
            }
        }

        if (task.future) {
            if (waitForCompletion) {
                task.future->wait();
            } else if (task.future->wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                return false;
            }
        }

        return true;
    };

    if (waitForCompletion) {
        for (PendingBacktestTask& task : m_detachedBacktestTasks) {
            taskCompleted(task);
        }
        m_detachedBacktestTasks.clear();
        return;
    }

    auto completedBegin = std::remove_if(m_detachedBacktestTasks.begin(),
                                         m_detachedBacktestTasks.end(),
                                         [&taskCompleted](PendingBacktestTask& task) {
                                             return taskCompleted(task);
                                         });
    m_detachedBacktestTasks.erase(completedBegin, m_detachedBacktestTasks.end());
}

void FactorBacktestController::resetBatchState()
{
    m_cancelRequested.store(true);
    if (m_pendingBatchFuture && m_batchExecutor) {
        m_batchExecutor->cancel(m_pendingBatchTaskId);
    }
    for (const PendingBacktestTask& task : m_pendingBacktestTasks) {
        if (task.future && task.executor) {
            task.executor->cancel(task.taskId);
        }
    }
    if (m_progressTimer) {
        m_progressTimer->stop();
    }
    detachPendingBacktestTasks();
    cleanupDetachedBacktestTasks(false);
    m_pendingBatchLaunchProgressState.reset();
    m_batchFactorIds.clear();
    m_batchResultMaps.clear();
    m_pendingGroupText.clear();
    m_pendingStartDate.clear();
    m_pendingEndDate.clear();
    m_pendingDataSourceMode.clear();
    m_pendingDatasetId = -1;
    m_pendingDatasetBenchmarkMetadata.clear();
    m_pendingStockPoolSymbols.clear();
    m_pendingRuntimeParams.clear();
    m_pendingBatchFactorCount = 0;
    m_pendingWorkerCount = 0;
    m_activeFactorIndex = 0;
    m_batchExecutor.reset();
}

void FactorBacktestController::pollBacktestProgress()
{
    cleanupDetachedBacktestTasks(false);

    if (!m_isRunning || (!m_pendingBatchLaunchFuture && !m_pendingBatchFuture && m_batchFactorIds.isEmpty())) {
        if (m_progressTimer) {
            m_progressTimer->stop();
        }
        return;
    }

    if (!m_pendingBatchLaunchFuture && !m_pendingBatchFuture) {
        launchNextBacktestTask();
    }

    if (m_pendingBatchLaunchFuture
        && m_pendingBatchLaunchFuture->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        PendingBacktestBatchLaunchResult launchResult = m_pendingBatchLaunchFuture->get();
        m_pendingBatchLaunchFuture.reset();
        if (!launchResult.errorMessage.isEmpty() || !launchResult.future) {
            finalizeBacktestFailure(launchResult.errorMessage.isEmpty()
                                        ? QStringLiteral("整批因子回测任务提交失败")
                                        : launchResult.errorMessage,
                                    false);
            return;
        }

        m_pendingBatchTaskId = launchResult.taskId;
        if (!m_batchExecutor && launchResult.executor) {
            m_batchExecutor = launchResult.executor;
        }
        m_pendingBatchFuture = std::move(launchResult.future);
        m_status = QStringLiteral("正在回测");
        emit statusChanged(m_status);
    }

    if (m_pendingBatchFuture
        && m_pendingBatchFuture->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        const std::vector<factor::BacktestResult> batchResults = m_pendingBatchFuture->get();
        m_pendingBatchFuture.reset();
        if (batchResults.size() != static_cast<size_t>(m_batchFactorIds.size())) {
            finalizeBacktestFailure(QStringLiteral("批量回测返回结果数量异常"), false);
            return;
        }

        for (size_t index = 0; index < batchResults.size(); ++index) {
            const factor::BacktestResult& result = batchResults[index];
            if (result.status != "SUCCESS") {
                const QString errorText = QString::fromStdString(result.errorMessage).trimmed();
                finalizeBacktestFailure(errorText.isEmpty() ? QStringLiteral("因子回测执行失败") : errorText, false);
                return;
            }

            finalizeBacktestSuccess(m_batchFactorIds.at(static_cast<int>(index)).toString().trimmed(),
                                    result,
                                    index);
        }

        if (!m_isRunning) {
            return;
        }
    }

    const int totalFactors = (std::max)(1, static_cast<int>(m_batchFactorIds.size()));
    int percent = m_progress;
    int currentFactorNumber = (std::max)(1, m_activeFactorIndex);
    QString activeStatus = m_status.trimmed().isEmpty() ? QStringLiteral("正在回测") : m_status.trimmed();

    if (m_pendingBatchFuture && m_batchExecutor) {
        const auto progress = m_batchExecutor->getProgress(m_pendingBatchTaskId);
        if (progress.status != "NOT_FOUND") {
            percent = (std::max)(0, (std::min)(100, progress.progress));
            const QString progressStep = QString::fromStdString(progress.currentStep).trimmed();
            if (!progressStep.isEmpty()) {
                activeStatus = progressStep;
            }
            currentFactorNumber = (std::min)(
                totalFactors,
                (std::max)(1,
                           static_cast<int>(std::ceil((static_cast<double>(percent) / 100.0)
                                                      * static_cast<double>(totalFactors)))));
        }
    } else if (m_pendingBatchLaunchProgressState) {
        percent = m_pendingBatchLaunchProgressState->value();
        const QString progressStep = m_pendingBatchLaunchProgressState->stepText().trimmed();
        if (!progressStep.isEmpty()) {
            activeStatus = progressStep;
        }
        currentFactorNumber = 1;
    }

    if (m_progress != percent) {
        m_progress = percent;
        emit progressChanged(m_progress);
    }
    if (m_status != activeStatus) {
        m_status = activeStatus;
        emit statusChanged(m_status);
    }
    emit backtestProgress(m_progress, m_status);
    emit backtestProgressDetailed(m_progress,
                                  m_status,
                                  (std::min)(totalFactors, currentFactorNumber),
                                  totalFactors);

    if (!m_pendingBatchLaunchFuture && !m_pendingBatchFuture && m_progressTimer) {
        m_progressTimer->stop();
    }

    cleanupDetachedBacktestTasks(false);
}

void FactorBacktestController::finalizeBacktestSuccess(const QString& requestedFactorId,
                                                       const factor::BacktestResult& result,
                                                       size_t batchIndex)
{
    const QVariantMap resultMap = buildResultMap(requestedFactorId, result);
    if (batchIndex < m_batchResultMaps.size()) {
        m_batchResultMaps[batchIndex] = resultMap;
    }

    syncBacktestMetricsToFactor(requestedFactorId, result);

    m_activeFactorIndex = static_cast<int>(std::count_if(
        m_batchResultMaps.cbegin(),
        m_batchResultMaps.cend(),
        [](const QVariantMap& item) {
            return !item.isEmpty();
        }));
    const int totalFactors = (std::max)(1, static_cast<int>(m_batchFactorIds.size()));
    m_progress = static_cast<int>((100.0 * static_cast<double>(m_activeFactorIndex)) / static_cast<double>(totalFactors));
    emit progressChanged(m_progress);
    emit backtestProgress(m_progress, QStringLiteral("正在回测"));
    emit backtestProgressDetailed(m_progress, QStringLiteral("正在回测"), m_activeFactorIndex, totalFactors);

    const bool completedAll = !m_batchResultMaps.empty()
        && std::all_of(m_batchResultMaps.cbegin(), m_batchResultMaps.cend(), [](const QVariantMap& item) {
            return !item.isEmpty();
        });
    if (!completedAll) {
        return;
    }

    const QVariantMap aggregate = buildAggregatedResultMap();
    m_isRunning = false;
    m_progress = 100;
    m_status = QStringLiteral("回测完成");
    m_backtestResult = aggregate;
    m_resultMetrics = aggregate.value(QStringLiteral("metrics")).toMap();
    emit isRunningChanged(m_isRunning);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    emit backtestResultChanged(m_backtestResult);
    emit resultMetricsChanged(m_resultMetrics);
    m_pendingBatchLaunchProgressState.reset();
    m_pendingBatchLaunchFuture.reset();
    m_pendingBatchFuture.reset();
    m_pendingBacktestTasks.clear();
    m_batchExecutor.reset();
    cleanupDetachedBacktestTasks(false);
    if (m_progressTimer) {
        m_progressTimer->stop();
    }
    persistLatestResult();
    emit backtestCompleted(aggregate);
}

void FactorBacktestController::finalizeBacktestFailure(const QString& errorMessage,
                                                       bool cancelled)
{
    m_cancelRequested.store(true);
    if (m_pendingBatchFuture && m_batchExecutor) {
        m_batchExecutor->cancel(m_pendingBatchTaskId);
    }
    for (const PendingBacktestTask& task : m_pendingBacktestTasks) {
        if (task.future && task.executor) {
            task.executor->cancel(task.taskId);
        }
    }
    detachPendingBacktestTasks();
    m_batchExecutor.reset();
    cleanupDetachedBacktestTasks(false);
    if (m_progressTimer) {
        m_progressTimer->stop();
    }
    m_isRunning = false;
    m_progress = 0;
    m_status = errorMessage.trimmed().isEmpty()
        ? (cancelled ? cancelledBacktestReason() : defaultBacktestFailureReason())
        : errorMessage.trimmed();
    emit isRunningChanged(m_isRunning);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    if (cancelled) {
        emit backtestCancelled();
    } else {
        emit backtestFailed(m_status);
    }
}

void FactorBacktestController::syncBacktestMetricsToFactor(const QString& requestedFactorId,
                                                           const factor::BacktestResult& result)
{
    if (requestedFactorId.trimmed().isEmpty() || result.status != "SUCCESS") {
        return;
    }

    if (result.factorMetrics.coreRating < factor::FactorBacktestMetrics::Rating::PASS) {
        return;
    }

    FactorService* service = FactorService::instance();
    if (!service) {
        return;
    }

    QVariantMap factorData = service->getFactorById(requestedFactorId);
    if (factorData.isEmpty()) {
        factorData = service->getFactorByIdFromRepository(requestedFactorId);
    }
    factorData[QStringLiteral("factorId")] = requestedFactorId.trimmed();
    factorData[QStringLiteral("icValue")] = result.icirResult.icMean;
    factorData[QStringLiteral("irValue")] = result.icirResult.ir;
    factorData[QStringLiteral("coreRating")] = static_cast<int>(result.factorMetrics.coreRating);
    factorData[QStringLiteral("turnoverRate")] = result.turnoverRate * 100.0;

    service->updateFactor(requestedFactorId, factorData);
}

void FactorBacktestController::applyPersistedResult(const QVariantMap& result)
{
    m_backtestResult = result;
    m_resultMetrics = result.value(QStringLiteral("metrics")).toMap();
    emit backtestResultChanged(m_backtestResult);
    emit resultMetricsChanged(m_resultMetrics);
}

bool FactorBacktestController::persistLatestResult() const
{
    if (m_backtestResult.isEmpty()) {
        return clearPersistedResult();
    }
    QSaveFile file(persistedResultFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromVariant(m_backtestResult);
    if (file.write(document.toJson(QJsonDocument::Indented)) < 0) {
        return false;
    }
    return file.commit();
}

bool FactorBacktestController::clearPersistedResult() const
{
    return !QFile::exists(persistedResultFilePath()) || QFile::remove(persistedResultFilePath());
}

QString FactorBacktestController::persistedResultFilePath() const
{
    return persistedResultFilePathForController();
}

void FactorBacktestController::shutdownBacktestInfrastructure()
{
    if (m_progressTimer) {
        m_progressTimer->stop();
    }
    resetBatchState();
    cleanupDetachedBacktestTasks(true);
    m_threadPool.reset();
}

void FactorBacktestController::resetResults()
{
    m_backtestResult.clear();
    m_resultMetrics.clear();
    emit backtestResultChanged(m_backtestResult);
    emit resultMetricsChanged(m_resultMetrics);
}

void FactorBacktestController::cancelBacktest()
{
    m_cancelRequested.store(true);
    if (m_pendingBatchFuture && m_batchExecutor) {
        m_batchExecutor->cancel(m_pendingBatchTaskId);
    }
    for (const PendingBacktestTask& task : m_pendingBacktestTasks) {
        if (task.future && task.executor) {
            task.executor->cancel(task.taskId);
        }
    }
    finalizeBacktestFailure(cancelledBacktestReason(), true);
}

bool FactorBacktestController::clearBacktestCache()
{
    m_factorSupportMapCache.clear();
    emit factorSupportMapCacheChanged(m_factorSupportMapCache);
    return true;
}

bool FactorBacktestController::saveResultToFile(const QString& filePath) const
{
    if (m_backtestResult.isEmpty()) {
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromVariant(m_backtestResult);
    if (file.write(document.toJson(QJsonDocument::Indented)) < 0) {
        return false;
    }
    return file.commit();
}

bool FactorBacktestController::loadResultFromFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    applyPersistedResult(document.object().toVariantMap());
    return true;
}
