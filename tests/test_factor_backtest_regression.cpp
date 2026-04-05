#include <gtest/gtest.h>

#include "FactorBacktestController.h"
#include "FactorBacktestPreflightUtils.h"
#include "FactorDomainSyncRetryUtils.h"
#include "FactorDomainSyncUtils.h"
#include "FactorInstanceResolutionUtils.h"
#include "infrastructure/include/database/FactorRepository.h"
#include "cache/include/cache_facade.h"
#include "domain/factor/include/FactorCacheManager.h"
#include "domain/factor/include/FactorBacktestCachedBarUtils.h"
#include "domain/factor/include/FactorBacktestExecutor.h"
#include "domain/factor/include/FactorBacktestGroupingUtils.h"
#include "domain/factor/include/FactorBacktestIcUtils.h"
#include "domain/factor/include/MomentumFactor.h"
#include "FactorService.h"
#include "foundation/thread/ThreadPoolExecutor.h"
#include "FactorBacktestWarmupUtils.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QVariantList>

#include <memory>
#include <optional>
#include <future>
#include <unordered_map>
#include <utility>
#include <vector>

class FactorServiceTestAccess
{
public:
    static FactorService* create()
    {
        return new FactorService();
    }

    static void destroy(FactorService* service)
    {
        delete service;
    }

    static void configureForRepositoryRegression(
        FactorService& service,
        const std::shared_ptr<astock::database::IFactorRepository>& repository)
    {
        service.m_repository = repository;
        service.m_database.reset();
        service.m_dataChecker.reset();
        service.m_factorCacheManager.reset();
        service.m_factorInstanceManager.reset();
        service.m_memoryCache.clear();
        service.m_initialized.store(true);
        service.m_isLoading.store(false);
        service.m_cacheLoaded.store(false);
        service.m_syncFactorDefinitionOverrideForTests = {};
        service.m_removeFactorDefinitionOverrideForTests = {};
    }

    static void setDomainSyncOverride(
        FactorService& service,
        std::function<bool(const QVariantMap&)> overrideFn)
    {
        service.m_syncFactorDefinitionOverrideForTests = std::move(overrideFn);
    }

    static void setDomainDeleteOverride(
        FactorService& service,
        std::function<bool(const QString&)> overrideFn)
    {
        service.m_removeFactorDefinitionOverrideForTests = std::move(overrideFn);
    }

    static const QMap<QString, QVariantMap>& memoryCache(const FactorService& service)
    {
        return service.m_memoryCache;
    }

    static QVariantList recentOperationReports(const FactorService& service)
    {
        return service.m_recentOperationReports;
    }

    static void setMemoryCache(FactorService& service,
                               const QString& factorId,
                               const QVariantMap& factorData)
    {
        service.m_memoryCache[factorId] = factorData;
    }
};

namespace {

using factor::CalculationContext;
using factor::CalculationResult;
using factor::FactorBacktestExecutor;
using factor::FactorCacheManager;
using factor::BacktestConfig;
using factor::BacktestResult;
using factor::MomentumFactor;
using factor::bridge::BacktestPreflightFailure;
using factor::bridge::FactorDomainExistingRecord;
using factor::bridge::FactorDomainSyncWritePlan;
using factor::bridge::FactorInstanceLookupCandidates;
using factor::bridge::FactorInstanceLookupRecord;

class InMemoryFactorRepository : public astock::database::IFactorRepository
{
public:
    QVariantMap findById(const QString& factorId) override
    {
        return records.value(factorId);
    }

    std::vector<QVariantMap> findAll() override
    {
        std::vector<QVariantMap> result;
        result.reserve(static_cast<size_t>(records.size()));
        for (auto it = records.cbegin(); it != records.cend(); ++it) {
            result.push_back(it.value());
        }
        return result;
    }

    std::vector<QVariantMap> findByType(const QString&) override
    {
        return findAll();
    }

    std::vector<QVariantMap> findByCategory(const QString&) override
    {
        return findAll();
    }

    std::vector<QVariantMap> findByTags(const QStringList&) override
    {
        return findAll();
    }

    std::vector<QVariantMap> search(const QString&) override
    {
        return findAll();
    }

    bool save(const QVariantMap& factor) override
    {
        const QString factorId = factor.value("factorId").toString().trimmed();
        if (factorId.isEmpty()) {
            return false;
        }

        ++saveCalls;
        records[factorId] = factor;
        return true;
    }

    size_t saveBatch(const std::vector<QVariantMap>& factors) override
    {
        size_t saved = 0;
        for (const QVariantMap& factor : factors) {
            if (save(factor)) {
                ++saved;
            }
        }
        return saved;
    }

    bool update(const QString& factorId, const QVariantMap& factor) override
    {
        ++updateCalls;
        updateHistory.append(factor);

        if (!records.contains(factorId)) {
            return false;
        }

        QVariantMap persisted = factor;
        persisted["factorId"] = factorId;
        records[factorId] = persisted;
        return true;
    }

    bool remove(const QString& factorId) override
    {
        ++removeCalls;
        removedIds.append(factorId);
        return records.remove(factorId) > 0;
    }

    size_t count() override
    {
        return static_cast<size_t>(records.size());
    }

    bool exists(const QString& factorId) override
    {
        return records.contains(factorId);
    }

    bool initialize() override
    {
        initialized = true;
        return true;
    }

    bool clearAll() override
    {
        records.clear();
        return true;
    }

    QMap<QString, QVariantMap> records;
    QVector<QVariantMap> updateHistory;
    QStringList removedIds;
    int saveCalls = 0;
    int updateCalls = 0;
    int removeCalls = 0;
    bool initialized = false;
};

std::unique_ptr<FactorService, void(*)(FactorService*)> makeTestFactorService()
{
    return {FactorServiceTestAccess::create(), &FactorServiceTestAccess::destroy};
}

QVariantMap makeValidFactorRecord(const QString& factorId,
                                  const QString& factorName,
                                  const QString& displayName)
{
    return {
        {"factorId", factorId},
        {"factorName", factorName},
        {"displayName", displayName},
        {"majorCategory", QString::fromUtf8("质量因子")},
        {"description", QString::fromUtf8("测试用因子")},
        {"status", "ACTIVE"}
    };
}

QVariantMap makeGroupResult(int groupId, double groupReturn)
{
    QVariantMap group;
    group["groupId"] = groupId;
    group["groupName"] = QString("第%1组").arg(groupId);
    group["return"] = groupReturn;
    group["stockCount"] = 12;
    group["minFactorValue"] = -0.5;
    group["maxFactorValue"] = 1.5;
    return group;
}

QVariantMap makeSingleFactorResult(const QString& factorId,
                                   const QString& factorName,
                                   double icValue,
                                   double spreadReturn,
                                   int executionTime)
{
    QVariantMap config;
    config["factorId"] = factorId;
    config["factorName"] = factorName;
    config["instanceId"] = factorId + "_instance";
    config["startDate"] = "2024-01-01";
    config["endDate"] = "2024-03-31";
    config["numGroups"] = 10;
    config["forwardDays"] = 1;
    config["transactionCost"] = 0.001;

    QVariantMap result;
    result["taskId"] = factorId + "_task";
    result["executionTime"] = executionTime;
    result["success"] = true;
    result["status"] = "SUCCESS";
    result["config"] = config;
    result["groups"] = QVariantList{makeGroupResult(1, 0.031), makeGroupResult(2, 0.014)};
    result["icirResult"] = QVariantMap{{"icValue", icValue}, {"irValue", 0.45}, {"icPositiveRate", 0.63}};
    result["summary"] = QVariantMap{{"spreadReturn", spreadReturn}, {"dataCoverage", 0.94}, {"sharpeRatio", 1.12}};
    return result;
}

QVariantMap makeBatchResult()
{
    const QVariantMap first = makeSingleFactorResult("factor_quality", "质量因子", 0.072, 0.028, 120);
    const QVariantMap second = makeSingleFactorResult("factor_momentum", "动量因子", 0.055, 0.019, 180);

    QVariantMap aggregate = first;
    aggregate["results"] = QVariantList{first, second};
    aggregate["factorIds"] = QVariantList{QStringLiteral("factor_quality"), QStringLiteral("factor_momentum")};
    aggregate["factorCount"] = 2;
    aggregate["executionTime"] = 300;
    return aggregate;
}

QString writeResultFile(const QVariantMap& result)
{
    QTemporaryDir dir;
    EXPECT_TRUE(dir.isValid());
    const QString filePath = dir.filePath("factor_backtest_result.json");

    QFile file(filePath);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QJsonDocument document(QJsonObject::fromVariantMap(result));
    file.write(document.toJson(QJsonDocument::Indented));
    file.close();

    // Keep the file alive after this helper returns by disabling auto-remove on the directory.
    dir.setAutoRemove(false);
    return filePath;
}

QString writeRawFile(const QByteArray& content)
{
    QTemporaryDir dir;
    EXPECT_TRUE(dir.isValid());
    const QString filePath = dir.filePath("factor_backtest_result.json");

    QFile file(filePath);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content);
    file.close();

    dir.setAutoRemove(false);
    return filePath;
}

class StubFactorDataProvider : public FactorDataProvider
{
public:
    explicit StubFactorDataProvider(std::unordered_map<std::string, std::vector<FactorDataPoint>> seriesBySymbol)
        : seriesBySymbol_(std::move(seriesBySymbol))
    {
    }

    bool hasField(const std::string& field) const override
    {
        return field == "close";
    }

    std::optional<double> getValue(const std::string& symbol,
                                   const std::string& date,
                                   const std::string& field) const override
    {
        if (field != "close") {
            return std::nullopt;
        }

        const auto it = seriesBySymbol_.find(symbol);
        if (it == seriesBySymbol_.end()) {
            return std::nullopt;
        }

        for (const auto& point : it->second) {
            if (point.date == date) {
                return point.value;
            }
        }
        return std::nullopt;
    }

    std::vector<FactorDataPoint> getSeries(const std::string& symbol,
                                           const std::string& startDate,
                                           const std::string& endDate,
                                           const std::string& field) const override
    {
        lastRequestedSymbol = symbol;
        lastRequestedStartDate = startDate;
        lastRequestedEndDate = endDate;
        lastRequestedField = field;

        std::vector<FactorDataPoint> filtered;
        if (field != "close") {
            return filtered;
        }

        const auto it = seriesBySymbol_.find(symbol);
        if (it == seriesBySymbol_.end()) {
            return filtered;
        }

        for (const auto& point : it->second) {
            if ((!startDate.empty() && point.date < startDate) ||
                (!endDate.empty() && point.date > endDate)) {
                continue;
            }
            filtered.push_back(point);
        }
        return filtered;
    }

    std::vector<std::string> getAvailableSymbols(const std::string& date) const override
    {
        std::vector<std::string> symbols;
        for (const auto& [symbol, series] : seriesBySymbol_) {
            for (const auto& point : series) {
                if (point.date == date) {
                    symbols.push_back(symbol);
                    break;
                }
            }
        }
        return symbols;
    }

    std::unordered_map<std::string, double> getCrossSection(const std::string& date,
                                                            const std::string& field,
                                                            const std::vector<std::string>& symbols = {}) const override
    {
        std::unordered_map<std::string, double> values;
        if (field != "close") {
            return values;
        }

        if (!symbols.empty()) {
            for (const auto& symbol : symbols) {
                const auto value = getValue(symbol, date, field);
                if (value.has_value()) {
                    values.emplace(symbol, *value);
                }
            }
            return values;
        }

        for (const auto& [symbol, series] : seriesBySymbol_) {
            for (const auto& point : series) {
                if (point.date == date) {
                    values.emplace(symbol, point.value);
                    break;
                }
            }
        }
        return values;
    }

    mutable std::string lastRequestedSymbol;
    mutable std::string lastRequestedStartDate;
    mutable std::string lastRequestedEndDate;
    mutable std::string lastRequestedField;

private:
    std::unordered_map<std::string, std::vector<FactorDataPoint>> seriesBySymbol_;
};

std::shared_ptr<StubFactorDataProvider> makeCloseSeriesProvider(
    std::initializer_list<std::pair<const char*, double>> points)
{
    std::vector<FactorDataPoint> series;
    series.reserve(points.size());
    for (const auto& [date, value] : points) {
        series.push_back(FactorDataPoint{date, value});
    }
    return std::make_shared<StubFactorDataProvider>(
        std::unordered_map<std::string, std::vector<FactorDataPoint>>{{"AAA", std::move(series)}});
}

CalculationResult calculateMomentum(const MomentumFactor::Params& params,
                                    const std::string& date,
                                    const std::shared_ptr<StubFactorDataProvider>& provider)
{
    MomentumFactor factor;
    factor.setParams(params);

    CalculationContext context;
    context.date = date;
    context.symbols = {"AAA"};
    context.dataProvider = provider;
    return factor.calculate(context);
}

AStockQuantEngine::Cache::CacheFacade& configureLocalOnlyCacheFacade()
{
    auto& cacheFacade = AStockQuantEngine::Cache::CacheFacade::getInstance();
    AStockQuantEngine::Cache::CacheConfig cacheConfig;
    cacheConfig.enabled = true;
    cacheConfig.localCache.enabled = true;
    cacheConfig.localCache.maxSize = 256;
    cacheConfig.redisCache.enabled = false;
    cacheFacade.initialize(cacheConfig);
    cacheFacade.clear();
    return cacheFacade;
}

std::shared_ptr<AStockQuantEngine::Cache::CacheFacade> makeSharedCacheFacade()
{
    auto& cacheFacade = configureLocalOnlyCacheFacade();
    return std::shared_ptr<AStockQuantEngine::Cache::CacheFacade>(&cacheFacade, [](AStockQuantEngine::Cache::CacheFacade*) {});
}

BacktestResult makeCachedExecutorResult(const std::string& instanceId,
                                        double annualReturn,
                                        int executionTimeMs)
{
    BacktestResult result;
    result.resultId = foundation::utils::Uuid::generate_v4();
    result.instanceId = instanceId;
    result.instanceName = instanceId + "_name";
    result.config.instanceId = instanceId;
    result.config.startDate = "2024-01-01";
    result.config.endDate = "2024-01-31";
    result.config.forwardDays = 3;
    result.config.numGroups = 5;
    result.dataCoverage = 1.0;
    result.icirResult.icMean = 0.12;
    result.icirResult.icStd = 0.04;
    result.icirResult.ir = 3.0;
    result.icirResult.icPositiveRatio = 0.75;
    result.groupResult.groupReturns = {0.05, 0.03, 0.01, -0.01, -0.02};
    result.groupResult.groupStockCounts = {10, 10, 10, 10, 10};
    result.groupResult.minFactorValues = {-1.0, -0.5, 0.0, 0.5, 1.0};
    result.groupResult.maxFactorValues = {-0.6, -0.1, 0.4, 0.9, 1.4};
    result.groupResult.topGroupReturn = 0.05;
    result.groupResult.bottomGroupReturn = -0.02;
    result.groupResult.longShortReturn = 0.068;
    result.annualReturn = annualReturn;
    result.sharpeRatio = 1.5;
    result.maxDrawdown = 0.08;
    result.winRate = 0.75;
    result.executionTimeMs = executionTimeMs;
    result.status = "SUCCESS";
    return result;
}

BacktestConfig makeCachedBacktestConfig(const std::string& instanceId)
{
    BacktestConfig config;
    config.instanceId = instanceId;
    config.startDate = "2024-01-01";
    config.endDate = "2024-01-31";
    config.forwardDays = 3;
    config.numGroups = 5;
    config.transactionCost = 0.001;
    return config;
}

void seedBacktestResultCache(const std::shared_ptr<FactorCacheManager>& cacheManager,
                             const BacktestConfig& config,
                             const BacktestResult& result)
{
    cacheManager->setBacktestResult(
        config.instanceId,
        config.startDate,
        config.endDate,
        config.forwardDays,
        config.numGroups,
        result.toJson());
}

CalculationResult makeCalculationResult(const std::string& date,
                                        std::initializer_list<std::pair<const char*, double>> values)
{
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = date;
    result.dataStatus.availability = factor::DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.metadata = foundation::json::JsonFacade::createObject();
    for (const auto& [symbol, value] : values) {
        result.values.emplace(symbol, value);
    }
    return result;
}

} // namespace

TEST(FactorBacktestRegressionTest, DataSourceModeNormalizationKeepsSupportedModes)
{
    FactorBacktestController controller;

    EXPECT_EQ(controller.dataSourceMode().toStdString(), std::string("cache"));

    controller.setDataSourceMode("database");
    EXPECT_EQ(controller.dataSourceMode().toStdString(), std::string("database"));

    controller.setDataSourceMode("  DATABASE  ");
    EXPECT_EQ(controller.dataSourceMode().toStdString(), std::string("database"));

    controller.setDataSourceMode("unexpected-mode");
    EXPECT_EQ(controller.dataSourceMode().toStdString(), std::string("cache"));
}

TEST(FactorBacktestRegressionTest, LoadResultFromFileRestoresBatchBacktestState)
{
    const QVariantMap batchResult = makeBatchResult();
    const QString filePath = writeResultFile(batchResult);

    FactorBacktestController controller;
    ASSERT_TRUE(controller.loadResultFromFile(filePath));

    const QVariantMap restored = controller.backtestResult();
    ASSERT_EQ(restored.value("factorCount").toInt(), 2);
    ASSERT_EQ(restored.value("results").toList().size(), 2);
    ASSERT_EQ(restored.value("factorIds").toList().size(), 2);

    ASSERT_EQ(controller.groupResults().size(), 2);
    EXPECT_DOUBLE_EQ(controller.icirResult().value("icValue").toDouble(), 0.072);
    EXPECT_DOUBLE_EQ(controller.summaryStats().value("spreadReturn").toDouble(), 0.028);
}

TEST(FactorBacktestRegressionTest, SaveResultToFileReturnsFalseWhenNoResultLoaded)
{
    FactorBacktestController controller;

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    EXPECT_FALSE(controller.saveResultToFile(dir.filePath("result.json")));
}

TEST(FactorBacktestRegressionTest, SaveResultToFileWritesRestoredBatchState)
{
    const QVariantMap batchResult = makeBatchResult();
    const QString sourceFilePath = writeResultFile(batchResult);

    FactorBacktestController controller;
    ASSERT_TRUE(controller.loadResultFromFile(sourceFilePath));

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString targetFilePath = dir.filePath("nested/output/factor_backtest_result.json");

    ASSERT_TRUE(controller.saveResultToFile(targetFilePath));

    QFile savedFile(targetFilePath);
    ASSERT_TRUE(savedFile.open(QIODevice::ReadOnly));
    const QJsonDocument savedDoc = QJsonDocument::fromJson(savedFile.readAll());
    ASSERT_TRUE(savedDoc.isObject());

    const QVariantMap savedResult = savedDoc.object().toVariantMap();
    EXPECT_EQ(savedResult.value("factorCount").toInt(), 2);
    EXPECT_EQ(savedResult.value("results").toList().size(), 2);
    EXPECT_EQ(savedResult.value("groups").toList().size(), 2);
    EXPECT_DOUBLE_EQ(savedResult.value("summary").toMap().value("spreadReturn").toDouble(), 0.028);
}

TEST(FactorBacktestRegressionTest, LoadMissingResultFileKeepsExistingRestoredState)
{
    const QString filePath = writeResultFile(makeBatchResult());

    FactorBacktestController controller;
    ASSERT_TRUE(controller.loadResultFromFile(filePath));

    const QVariantMap previousResult = controller.backtestResult();
    const QVariantList previousGroups = controller.groupResults();
    const QVariantMap previousIcir = controller.icirResult();
    const QVariantMap previousSummary = controller.summaryStats();

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    EXPECT_FALSE(controller.loadResultFromFile(dir.filePath("missing.json")));
    EXPECT_EQ(controller.backtestResult(), previousResult);
    EXPECT_EQ(controller.groupResults(), previousGroups);
    EXPECT_EQ(controller.icirResult(), previousIcir);
    EXPECT_EQ(controller.summaryStats(), previousSummary);
}

TEST(FactorBacktestRegressionTest, LoadInvalidJsonKeepsExistingRestoredState)
{
    const QString filePath = writeResultFile(makeBatchResult());
    const QString invalidFilePath = writeRawFile("{ invalid json");

    FactorBacktestController controller;
    ASSERT_TRUE(controller.loadResultFromFile(filePath));

    const QVariantMap previousResult = controller.backtestResult();
    const QVariantList previousGroups = controller.groupResults();
    const QVariantMap previousIcir = controller.icirResult();
    const QVariantMap previousSummary = controller.summaryStats();

    EXPECT_FALSE(controller.loadResultFromFile(invalidFilePath));
    EXPECT_EQ(controller.backtestResult(), previousResult);
    EXPECT_EQ(controller.groupResults(), previousGroups);
    EXPECT_EQ(controller.icirResult(), previousIcir);
    EXPECT_EQ(controller.summaryStats(), previousSummary);
}

TEST(FactorBacktestRegressionTest, LoadSingleFactorResultReplacesPriorBatchMetadata)
{
    const QString batchFilePath = writeResultFile(makeBatchResult());
    const QString singleFilePath = writeResultFile(
        makeSingleFactorResult("factor_value", "价值因子", 0.031, 0.012, 95));

    FactorBacktestController controller;
    ASSERT_TRUE(controller.loadResultFromFile(batchFilePath));
    ASSERT_EQ(controller.backtestResult().value("factorCount").toInt(), 2);

    ASSERT_TRUE(controller.loadResultFromFile(singleFilePath));

    const QVariantMap restored = controller.backtestResult();
    EXPECT_FALSE(restored.contains("factorCount"));
    EXPECT_TRUE(restored.value("results").toList().isEmpty());
    EXPECT_TRUE(restored.value("factorIds").toList().isEmpty());
    EXPECT_EQ(restored.value("taskId").toString(), QString("factor_value_task"));
    EXPECT_EQ(controller.groupResults().size(), 2);
    EXPECT_DOUBLE_EQ(controller.icirResult().value("icValue").toDouble(), 0.031);
    EXPECT_DOUBLE_EQ(controller.summaryStats().value("spreadReturn").toDouble(), 0.012);
}

TEST(FactorBacktestRegressionTest, MomentumFactorSkipRecentUsesTradingDayOffsetAcrossWeekend)
{
    const auto provider = makeCloseSeriesProvider({
        {"2024-01-02", 100.0},
        {"2024-01-03", 110.0},
        {"2024-01-04", 120.0},
        {"2024-01-05", 150.0},
        {"2024-01-08", 210.0},
    });

    MomentumFactor::Params params;
    params.window = 1;
    params.skipRecent = 3;

    const CalculationResult result = calculateMomentum(params, "2024-01-08", provider);

    ASSERT_EQ(result.values.size(), 1U);
    ASSERT_TRUE(result.values.find("AAA") != result.values.end());
    EXPECT_EQ(provider->lastRequestedEndDate, std::string("2024-01-08"));
    EXPECT_NEAR(result.values.at("AAA"), 0.1, 1e-9);
}

TEST(FactorBacktestRegressionTest, MomentumFactorSkipRecentUsesTradingDayOffsetAcrossLongHoliday)
{
    const auto provider = makeCloseSeriesProvider({
        {"2024-09-24", 100.0},
        {"2024-09-25", 110.0},
        {"2024-09-26", 120.0},
        {"2024-09-27", 130.0},
        {"2024-09-30", 140.0},
        {"2024-10-08", 200.0},
    });

    MomentumFactor::Params params;
    params.window = 1;
    params.skipRecent = 3;

    const CalculationResult result = calculateMomentum(params, "2024-10-08", provider);

    ASSERT_EQ(result.values.size(), 1U);
    ASSERT_TRUE(result.values.find("AAA") != result.values.end());
    EXPECT_EQ(provider->lastRequestedEndDate, std::string("2024-10-08"));
    EXPECT_NEAR(result.values.at("AAA"), (120.0 - 110.0) / 110.0, 1e-9);
}

TEST(FactorBacktestRegressionTest, MomentumFactorReportsEmptyReasonWhenHistoryIsInsufficient)
{
    const auto provider = makeCloseSeriesProvider({
        {"2024-01-04", 100.0},
        {"2024-01-05", 101.0},
        {"2024-01-08", 102.0},
    });

    MomentumFactor::Params params;
    params.window = 3;
    params.skipRecent = 1;

    const CalculationResult result = calculateMomentum(params, "2024-01-08", provider);

    EXPECT_TRUE(result.values.empty());
    ASSERT_TRUE(result.metadata.has("empty_reason"));
    EXPECT_NE(result.metadata.get("empty_reason").asString().find("至少 5 个交易日样本"), std::string::npos);
    ASSERT_TRUE(result.metadata.has("skip_recent"));
    EXPECT_EQ(result.metadata.get("skip_recent").asInt(), 1);
}

TEST(FactorBacktestRegressionTest, ExecutorExecuteReturnsCachedBacktestResultWithoutInstanceManager)
{
    const BacktestConfig config = makeCachedBacktestConfig("cached_factor_sync");
    const BacktestResult cachedResult = makeCachedExecutorResult(config.instanceId, 0.21, 7);
    const auto cacheManager = std::make_shared<FactorCacheManager>();
    cacheManager->setCacheFacade(makeSharedCacheFacade());
    seedBacktestResultCache(cacheManager, config, cachedResult);

    FactorBacktestExecutor executor(nullptr, nullptr, cacheManager);
    const BacktestResult result = executor.execute(config);

    EXPECT_EQ(result.status, std::string("SUCCESS"));
    EXPECT_EQ(result.instanceId, config.instanceId);
    EXPECT_DOUBLE_EQ(result.annualReturn, 0.21);
    EXPECT_EQ(result.executionTimeMs, 7);
    EXPECT_EQ(result.groupResult.groupReturns.size(), 5U);
}

TEST(FactorBacktestRegressionTest, ExecutorExecuteAsyncReturnsCachedBacktestResult)
{
    const BacktestConfig config = makeCachedBacktestConfig("cached_factor_async");
    const BacktestResult cachedResult = makeCachedExecutorResult(config.instanceId, 0.18, 5);
    const auto cacheManager = std::make_shared<FactorCacheManager>();
    cacheManager->setCacheFacade(makeSharedCacheFacade());
    seedBacktestResultCache(cacheManager, config, cachedResult);

    FactorBacktestExecutor executor(nullptr, nullptr, cacheManager);
    auto future = executor.executeAsync(config);
    const BacktestResult result = future.get();

    EXPECT_EQ(result.status, std::string("SUCCESS"));
    EXPECT_EQ(result.instanceId, config.instanceId);
    EXPECT_DOUBLE_EQ(result.annualReturn, 0.18);
    EXPECT_EQ(result.executionTimeMs, 5);
}

TEST(FactorBacktestRegressionTest, ExecutorTrackedAsyncReturnsCachedResultAndFinalizesTask)
{
    const BacktestConfig config = makeCachedBacktestConfig("cached_factor_tracked");
    const BacktestResult cachedResult = makeCachedExecutorResult(config.instanceId, 0.16, 9);
    const auto cacheManager = std::make_shared<FactorCacheManager>();
    cacheManager->setCacheFacade(makeSharedCacheFacade());
    seedBacktestResultCache(cacheManager, config, cachedResult);

    auto threadPool = std::make_shared<foundation::thread::ThreadPoolExecutor>(1);
    FactorBacktestExecutor executor(nullptr, threadPool, cacheManager);
    auto handle = executor.executeTrackedAsync(config);
    const BacktestResult result = handle.future.get();
    const auto progress = executor.getProgress(handle.taskId);

    EXPECT_FALSE(handle.taskId.to_string().empty());
    EXPECT_EQ(result.status, std::string("SUCCESS"));
    EXPECT_DOUBLE_EQ(result.annualReturn, 0.16);
    EXPECT_EQ(progress.status, std::string("NOT_FOUND"));

    threadPool->shutdown();
    AStockQuantEngine::Cache::CacheFacade::getInstance().shutdown();
}

TEST(FactorBacktestRegressionTest, ExecutorBatchReturnsCachedResultsForEachConfig)
{
    const BacktestConfig firstConfig = makeCachedBacktestConfig("cached_factor_batch_1");
    const BacktestConfig secondConfig = makeCachedBacktestConfig("cached_factor_batch_2");
    const auto cacheManager = std::make_shared<FactorCacheManager>();
    cacheManager->setCacheFacade(makeSharedCacheFacade());
    seedBacktestResultCache(cacheManager, firstConfig, makeCachedExecutorResult(firstConfig.instanceId, 0.11, 4));
    seedBacktestResultCache(cacheManager, secondConfig, makeCachedExecutorResult(secondConfig.instanceId, 0.13, 6));

    FactorBacktestExecutor executor(nullptr, nullptr, cacheManager);
    const auto results = executor.executeBatch({firstConfig, secondConfig});

    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].instanceId, firstConfig.instanceId);
    EXPECT_EQ(results[1].instanceId, secondConfig.instanceId);
    EXPECT_DOUBLE_EQ(results[0].annualReturn, 0.11);
    EXPECT_DOUBLE_EQ(results[1].annualReturn, 0.13);

    AStockQuantEngine::Cache::CacheFacade::getInstance().shutdown();
}

TEST(FactorBacktestRegressionTest, WarmupResolveHistoryStartDateUsesTradingDaysAcrossLongHoliday)
{
    const QStringList ascendingTradeDates = {
        "2024-09-23",
        "2024-09-24",
        "2024-09-25",
        "2024-09-26",
        "2024-09-27",
        "2024-09-30"
    };

    const QDate historyStartDate = factor::warmup::resolveWarmupHistoryStartDate(
        QDate::fromString("2024-10-08", "yyyy-MM-dd"),
        ascendingTradeDates,
        3);

    ASSERT_TRUE(historyStartDate.isValid());
    EXPECT_EQ(historyStartDate.toString("yyyy-MM-dd"), QString("2024-09-26"));
}

TEST(FactorBacktestRegressionTest, WarmupResolveHistoryStartDateReturnsEarliestAvailableTradeDate)
{
    const QStringList ascendingTradeDates = {
        "2024-01-02",
        "2024-01-03"
    };

    const QDate historyStartDate = factor::warmup::resolveWarmupHistoryStartDate(
        QDate::fromString("2024-01-08", "yyyy-MM-dd"),
        ascendingTradeDates,
        5);

    ASSERT_TRUE(historyStartDate.isValid());
    EXPECT_EQ(historyStartDate.toString("yyyy-MM-dd"), QString("2024-01-02"));
}

TEST(FactorBacktestRegressionTest, WarmupFallbackCalendarLookbackKeepsLegacySafetyMargin)
{
    EXPECT_EQ(factor::warmup::fallbackWarmupCalendarLookbackDays(5), 365);
    EXPECT_EQ(factor::warmup::fallbackWarmupCalendarLookbackDays(300), 620);
    EXPECT_EQ(factor::warmup::requiredWarmupTradingDays(21, 5), 25);
}

TEST(FactorBacktestRegressionTest, CachedBarsExtractTradeDatesNormalizesAndDeduplicatesFormats)
{
    const std::vector<factor::CachedMarketBar> bars = {
        {"AAA", "2024-01-08 15:00:00", 101.0, {}},
        {"BBB", "2024/01/08", 102.0, {}},
        {"AAA", "2024-01-09T15:00:00", 103.0, {}},
        {"AAA", "2024-01-10", 104.0, {}},
    };

    const auto tradeDates = factor::cached_bars::extractTradeDates(bars, "2024-01-08", "2024-01-09");

    ASSERT_EQ(tradeDates.size(), 2U);
    EXPECT_EQ(tradeDates[0], std::string("2024-01-08"));
    EXPECT_EQ(tradeDates[1], std::string("2024-01-09"));
}

TEST(FactorBacktestRegressionTest, CachedBarsExtractSymbolsMatchesNormalizedTradeDateAndFilter)
{
    const std::vector<factor::CachedMarketBar> bars = {
        {"AAA", "2024-01-08 15:00:00", 101.0, {}},
        {"BBB", "2024/01/08", 102.0, {}},
        {"CCC", "2024-01-09", 103.0, {}},
    };

    const std::unordered_set<std::string> allowedSymbols = {"BBB", "CCC"};
    const auto symbols = factor::cached_bars::extractSymbols(bars, "2024-01-08", allowedSymbols);

    ASSERT_EQ(symbols.size(), 1U);
    EXPECT_EQ(symbols.front(), std::string("BBB"));
}

TEST(FactorBacktestRegressionTest, CachedBarsFutureReturnUsesNormalizedTradeDateOrdering)
{
    const std::vector<factor::CachedMarketBar> bars = {
        {"AAA", "2024-01-05 15:00:00", 100.0, {}},
        {"AAA", "2024/01/08 15:00:00", 110.0, {}},
        {"AAA", "2024-01-09T15:00:00", 121.0, {}},
        {"AAA", "2024-01-10", 133.1, {}},
    };

    const double futureReturn = factor::cached_bars::calculateFutureReturn(bars, "AAA", "2024-01-08", 2);

    EXPECT_NEAR(futureReturn, 0.21, 1e-9);
}

TEST(FactorBacktestRegressionTest, GroupBacktestAggregateBuildsDeterministicTwoGroupResult)
{
    const std::vector<CalculationResult> factorResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 4.0}, {"BBB", 3.0}, {"CCC", 2.0}, {"DDD", 1.0}}),
        makeCalculationResult("2024-01-09", {{"AAA", 8.0}, {"BBB", 6.0}, {"CCC", 4.0}, {"DDD", 2.0}}),
    };
    const std::vector<CalculationResult> returnResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 0.10}, {"BBB", 0.05}, {"CCC", -0.02}, {"DDD", -0.04}}),
        makeCalculationResult("2024-01-09", {{"AAA", 0.08}, {"BBB", 0.04}, {"CCC", -0.01}, {"DDD", -0.03}}),
    };

    const auto summary = factor::group_backtest::aggregate(factorResults, returnResults, 2, 0.001);

    ASSERT_TRUE(summary.hasValidGroup);
    ASSERT_EQ(summary.groupResult.groupReturns.size(), 2U);
    EXPECT_NEAR(summary.groupResult.groupReturns[0], 0.0675, 1e-9);
    EXPECT_NEAR(summary.groupResult.groupReturns[1], -0.025, 1e-9);
    EXPECT_EQ(summary.groupResult.groupStockCounts[0], 2);
    EXPECT_EQ(summary.groupResult.groupStockCounts[1], 2);
    EXPECT_DOUBLE_EQ(summary.groupResult.minFactorValues[0], 3.0);
    EXPECT_DOUBLE_EQ(summary.groupResult.maxFactorValues[0], 8.0);
    EXPECT_DOUBLE_EQ(summary.groupResult.minFactorValues[1], 1.0);
    EXPECT_DOUBLE_EQ(summary.groupResult.maxFactorValues[1], 4.0);
    EXPECT_NEAR(summary.groupResult.longShortReturn, 0.0905, 1e-9);
    EXPECT_EQ(summary.overlapDateCount, 2);
    EXPECT_EQ(summary.groupedDateCount, 2);
}

TEST(FactorBacktestRegressionTest, GroupBacktestAggregateLetsLastGroupAbsorbRemainder)
{
    const std::vector<CalculationResult> factorResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 5.0}, {"BBB", 4.0}, {"CCC", 3.0}, {"DDD", 2.0}, {"EEE", 1.0}}),
    };
    const std::vector<CalculationResult> returnResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 0.10}, {"BBB", 0.08}, {"CCC", 0.06}, {"DDD", 0.04}, {"EEE", 0.02}}),
    };

    const auto summary = factor::group_backtest::aggregate(factorResults, returnResults, 3, 0.0);

    ASSERT_TRUE(summary.hasValidGroup);
    ASSERT_EQ(summary.groupResult.groupReturns.size(), 3U);
    EXPECT_EQ(summary.groupResult.groupStockCounts[0], 1);
    EXPECT_EQ(summary.groupResult.groupStockCounts[1], 1);
    EXPECT_EQ(summary.groupResult.groupStockCounts[2], 3);
    EXPECT_NEAR(summary.groupResult.groupReturns[2], 0.04, 1e-9);
}

TEST(FactorBacktestRegressionTest, GroupBacktestAggregateReportsInsufficientMatchedStocks)
{
    const std::vector<CalculationResult> factorResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 5.0}}),
    };
    const std::vector<CalculationResult> returnResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 0.10}}),
    };

    const auto summary = factor::group_backtest::aggregate(factorResults, returnResults, 3, 0.001);

    EXPECT_FALSE(summary.hasValidGroup);
    EXPECT_EQ(summary.maxMatchedStocks, 1U);
    EXPECT_EQ(summary.overlapDateCount, 1);
    EXPECT_EQ(summary.groupedDateCount, 1);
    EXPECT_TRUE(summary.groupResult.groupReturns.size() < 2);
}

TEST(FactorBacktestRegressionTest, IcIrAggregateBuildsPositiveAndNegativeSeries)
{
    const std::vector<CalculationResult> factorResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 1.0}, {"BBB", 2.0}, {"CCC", 3.0}}),
        makeCalculationResult("2024-01-09", {{"AAA", 1.0}, {"BBB", 2.0}, {"CCC", 3.0}}),
    };
    const std::vector<CalculationResult> returnResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 1.0}, {"BBB", 2.0}, {"CCC", 3.0}}),
        makeCalculationResult("2024-01-09", {{"AAA", 3.0}, {"BBB", 2.0}, {"CCC", 1.0}}),
    };

    const auto summary = factor::icir::aggregate(factorResults, returnResults);

    ASSERT_TRUE(summary.hasValidSeries);
    ASSERT_EQ(summary.result.icSeries.size(), 2U);
    EXPECT_NEAR(summary.result.icSeries[0], 1.0, 1e-9);
    EXPECT_NEAR(summary.result.icSeries[1], -1.0, 1e-9);
    EXPECT_NEAR(summary.result.icMean, 0.0, 1e-9);
    EXPECT_NEAR(summary.result.icStd, 1.0, 1e-9);
    EXPECT_NEAR(summary.result.ir, 0.0, 1e-9);
    EXPECT_NEAR(summary.result.icPositiveRatio, 0.5, 1e-9);
}

TEST(FactorBacktestRegressionTest, IcIrAggregateReturnsInvalidWhenOverlapIsInsufficient)
{
    const std::vector<CalculationResult> factorResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 1.0}}),
        makeCalculationResult("2024-01-09", {{"BBB", 2.0}}),
    };
    const std::vector<CalculationResult> returnResults = {
        makeCalculationResult("2024-01-08", {{"AAA", 0.3}}),
        makeCalculationResult("2024-01-09", {{"CCC", 0.5}}),
    };

    const auto summary = factor::icir::aggregate(factorResults, returnResults);

    EXPECT_FALSE(summary.hasValidSeries);
    EXPECT_TRUE(summary.result.icSeries.empty());
    EXPECT_DOUBLE_EQ(summary.result.icMean, 0.0);
    EXPECT_DOUBLE_EQ(summary.result.icStd, 0.0);
    EXPECT_DOUBLE_EQ(summary.result.ir, 0.0);
    EXPECT_DOUBLE_EQ(summary.result.icPositiveRatio, 0.0);
}

TEST(FactorBacktestRegressionTest, InstanceLookupCandidatesIncludeBaseFactorId)
{
    const FactorInstanceLookupCandidates candidates =
        factor::bridge::buildFactorInstanceLookupCandidates("quality_factor_instance");

    EXPECT_EQ(candidates.primaryId, "quality_factor_instance");
    EXPECT_EQ(candidates.secondaryId, "quality_factor");
}

TEST(FactorBacktestRegressionTest, InstanceLookupPrefersActiveFactorIdFallbackOverInactiveExactMatch)
{
    const QVector<FactorInstanceLookupRecord> records = {
        {"quality_factor_instance", "quality_factor", "INACTIVE"},
        {"quality_factor_v2", "quality_factor", "ACTIVE"}
    };

    EXPECT_EQ(factor::bridge::resolveFactorInstanceId("quality_factor_instance", records),
              "quality_factor_v2");
}

TEST(FactorBacktestRegressionTest, InstanceLookupPrefersExactActiveInstanceOverFactorIdMatch)
{
    const QVector<FactorInstanceLookupRecord> records = {
        {"quality_factor_v2", "quality_factor", "ACTIVE"},
        {"quality_factor_instance", "quality_factor", "ACTIVE"}
    };

    EXPECT_EQ(factor::bridge::resolveFactorInstanceId("quality_factor_instance", records),
              "quality_factor_instance");
}

TEST(FactorBacktestRegressionTest, DomainSyncWritePlanReusesExactInstanceAndDeletesFactorDuplicates)
{
    const QVector<FactorDomainExistingRecord> records = {
        {"quality_factor_instance", "quality_factor"},
        {"quality_factor_legacy", "quality_factor"},
        {"other_factor_instance", "other_factor"}
    };

    const FactorDomainSyncWritePlan plan =
        factor::bridge::planFactorDomainSyncWrite("quality_factor_instance", "quality_factor", records, false);

    EXPECT_TRUE(plan.updateExisting);
    EXPECT_EQ(plan.persistedInstanceId, "quality_factor_instance");
    EXPECT_EQ(plan.duplicateInstanceIds, QStringList{"quality_factor_legacy"});
}

TEST(FactorBacktestRegressionTest, DomainSyncWritePlanFallsBackToFactorMatchWhenRequestedInstanceMissing)
{
    const QVector<FactorDomainExistingRecord> records = {
        {"quality_factor_v2", "quality_factor"}
    };

    const FactorDomainSyncWritePlan plan =
        factor::bridge::planFactorDomainSyncWrite("quality_factor_instance", "quality_factor", records, false);

    EXPECT_TRUE(plan.updateExisting);
    EXPECT_EQ(plan.persistedInstanceId, "quality_factor_v2");
    EXPECT_TRUE(plan.duplicateInstanceIds.isEmpty());
}

TEST(FactorBacktestRegressionTest, DomainSyncWritePlanRetryForcesRequestedInstanceId)
{
    const QVector<FactorDomainExistingRecord> records = {
        {"quality_factor_v2", "quality_factor"}
    };

    const FactorDomainSyncWritePlan plan =
        factor::bridge::planFactorDomainSyncWrite("quality_factor_instance", "quality_factor", records, true);

    EXPECT_FALSE(plan.updateExisting);
    EXPECT_EQ(plan.persistedInstanceId, "quality_factor_instance");
    EXPECT_EQ(plan.duplicateInstanceIds, QStringList{"quality_factor_v2"});
}

TEST(FactorBacktestRegressionTest, DomainSyncRetryDeletesAndRewritesWhenInitialVerificationFails)
{
    QVector<bool> forceRequestedFlags;
    QVector<QString> verifyCalls;
    QString deletedFactorId;
    QString deletedInstanceId;

    QString finalInstanceId;
    const bool success = factor::bridge::executeDomainSyncWithRetry(
        "quality_factor_instance",
        "quality_factor",
        [&forceRequestedFlags](QString* persistedInstanceId, bool forceRequestedInstanceId) {
            forceRequestedFlags.append(forceRequestedInstanceId);
            if (persistedInstanceId) {
                *persistedInstanceId = forceRequestedInstanceId
                    ? QStringLiteral("quality_factor_instance")
                    : QStringLiteral("quality_factor_v2");
            }
            return true;
        },
        [&verifyCalls](const QString& instanceId, QString* errorMessage) {
            verifyCalls.append(instanceId);
            if (verifyCalls.size() == 1) {
                if (errorMessage) {
                    *errorMessage = QString::fromUtf8("首次验证失败");
                }
                return false;
            }
            return true;
        },
        [&deletedFactorId, &deletedInstanceId](const QString& factorId, const QString& instanceId) {
            deletedFactorId = factorId;
            deletedInstanceId = instanceId;
        },
        &finalInstanceId
    );

    EXPECT_TRUE(success);
    EXPECT_EQ(forceRequestedFlags, QVector<bool>({false, true}));
    EXPECT_EQ(verifyCalls, QVector<QString>({QStringLiteral("quality_factor_v2"), QStringLiteral("quality_factor_instance")}));
    EXPECT_EQ(deletedFactorId, QStringLiteral("quality_factor"));
    EXPECT_EQ(deletedInstanceId, QStringLiteral("quality_factor_v2"));
    EXPECT_EQ(finalInstanceId, QStringLiteral("quality_factor_instance"));
}

TEST(FactorBacktestRegressionTest, DomainSyncRetryFailsWhenRebuiltInstanceStillCannotVerify)
{
    int writeCount = 0;
    int verifyCount = 0;
    int deleteCount = 0;

    const bool success = factor::bridge::executeDomainSyncWithRetry(
        "quality_factor_instance",
        "quality_factor",
        [&writeCount](QString* persistedInstanceId, bool forceRequestedInstanceId) {
            ++writeCount;
            if (persistedInstanceId) {
                *persistedInstanceId = forceRequestedInstanceId
                    ? QStringLiteral("quality_factor_instance")
                    : QStringLiteral("quality_factor_v2");
            }
            return true;
        },
        [&verifyCount](const QString&, QString* errorMessage) {
            ++verifyCount;
            if (errorMessage) {
                *errorMessage = QString::fromUtf8("仍然无法验证");
            }
            return false;
        },
        [&deleteCount](const QString&, const QString&) {
            ++deleteCount;
        }
    );

    EXPECT_FALSE(success);
    EXPECT_EQ(writeCount, 2);
    EXPECT_EQ(verifyCount, 2);
    EXPECT_EQ(deleteCount, 1);
}

TEST(FactorBacktestRegressionTest, PreflightFailureSummaryIncludesResolvedInstanceId)
{
    const BacktestPreflightFailure failure{"quality_factor", "quality_factor_instance", "实例创建失败"};

    EXPECT_EQ(factor::bridge::summarizeBacktestPreflightFailure(failure),
              "quality_factor (instanceId=quality_factor_instance, 实例创建失败)");
}

TEST(FactorBacktestRegressionTest, PreflightFailureSummaryOmitsMissingInstanceId)
{
    const BacktestPreflightFailure failure{"quality_factor", QString(), "未解析到实例ID"};

    EXPECT_EQ(factor::bridge::summarizeBacktestPreflightFailure(failure),
              "quality_factor (未解析到实例ID)");
}

TEST(FactorBacktestRegressionTest, PreflightFailureVariantListPreservesStructuredFields)
{
    const QList<BacktestPreflightFailure> failures = {
        {"quality_factor", "quality_factor_instance", "实例创建失败"},
        {"value_factor", QString(), "未解析到实例ID"}
    };

    const QVariantList result = factor::bridge::toVariantList(failures);
    ASSERT_EQ(result.size(), 2);

    const QVariantMap first = result.at(0).toMap();
    EXPECT_EQ(first.value("factorId").toString(), "quality_factor");
    EXPECT_EQ(first.value("instanceId").toString(), "quality_factor_instance");
    EXPECT_EQ(first.value("reason").toString(), "实例创建失败");

    const QVariantMap second = result.at(1).toMap();
    EXPECT_EQ(second.value("factorId").toString(), "value_factor");
    EXPECT_TRUE(second.value("instanceId").toString().isEmpty());
    EXPECT_EQ(second.value("reason").toString(), "未解析到实例ID");
}

TEST(FactorBacktestRegressionTest, AddFactorRollsBackRepositoryWriteWhenDomainSyncFails)
{
    auto service = makeTestFactorService();
    auto repository = std::make_shared<InMemoryFactorRepository>();
    FactorServiceTestAccess::configureForRepositoryRegression(*service, repository);
    FactorServiceTestAccess::setDomainSyncOverride(*service, [](const QVariantMap&) {
        return false;
    });

    const QVariantMap factor = makeValidFactorRecord(
        QStringLiteral("factor_quality"),
        QString::fromUtf8("质量因子"),
        QString::fromUtf8("质量因子展示"));

    EXPECT_TRUE(service->addFactor(factor).isEmpty());
    EXPECT_EQ(repository->saveCalls, 1);
    EXPECT_EQ(repository->removeCalls, 1);
    EXPECT_EQ(repository->removedIds.value(0), QStringLiteral("factor_quality"));
    EXPECT_FALSE(repository->exists(QStringLiteral("factor_quality")));
    EXPECT_TRUE(FactorServiceTestAccess::memoryCache(*service).isEmpty());

    const QVariantMap report = service->lastOperationReport();
    EXPECT_EQ(report.value("operation").toString(), QStringLiteral("addFactor"));
    EXPECT_EQ(report.value("factorId").toString(), QStringLiteral("factor_quality"));
    EXPECT_FALSE(report.value("success").toBool());
    EXPECT_EQ(report.value("stage").toString(), QStringLiteral("sync_domain_failed_rolled_back"));
    EXPECT_TRUE(report.value("message").toString().contains(QString::fromUtf8("已回滚 factors 表")));
    EXPECT_FALSE(service->mutationInProgress());

    const QVariantList history = service->recentOperationReports();
    ASSERT_EQ(history.size(), 1);
    EXPECT_EQ(history.at(0).toMap().value("stage").toString(), QStringLiteral("sync_domain_failed_rolled_back"));
}

TEST(FactorBacktestRegressionTest, UpdateFactorRollsBackRepositoryWriteWhenDomainSyncFails)
{
    auto service = makeTestFactorService();
    auto repository = std::make_shared<InMemoryFactorRepository>();
    FactorServiceTestAccess::configureForRepositoryRegression(*service, repository);
    FactorServiceTestAccess::setDomainSyncOverride(*service, [](const QVariantMap&) {
        return false;
    });

    const QVariantMap previousFactor = makeValidFactorRecord(
        QStringLiteral("factor_quality"),
        QString::fromUtf8("质量因子"),
        QString::fromUtf8("旧显示名"));
    repository->records.insert(QStringLiteral("factor_quality"), previousFactor);

    QVariantMap updatedFactor = previousFactor;
    updatedFactor["displayName"] = QString::fromUtf8("新显示名");
    updatedFactor["description"] = QString::fromUtf8("更新后的描述");

    EXPECT_FALSE(service->updateFactor(QStringLiteral("factor_quality"), updatedFactor));
    EXPECT_EQ(repository->updateCalls, 2);
    ASSERT_EQ(repository->updateHistory.size(), 2);
    EXPECT_EQ(repository->updateHistory.at(0).value("displayName").toString(), QString::fromUtf8("新显示名"));
    EXPECT_EQ(repository->updateHistory.at(1).value("displayName").toString(), QString::fromUtf8("旧显示名"));
    EXPECT_EQ(repository->findById(QStringLiteral("factor_quality")).value("displayName").toString(),
              QString::fromUtf8("旧显示名"));
    EXPECT_TRUE(FactorServiceTestAccess::memoryCache(*service).isEmpty());
}

TEST(FactorBacktestRegressionTest, DeleteFactorKeepsRepositoryAndCacheWhenDomainDeleteFails)
{
    auto service = makeTestFactorService();
    auto repository = std::make_shared<InMemoryFactorRepository>();
    FactorServiceTestAccess::configureForRepositoryRegression(*service, repository);
    FactorServiceTestAccess::setDomainDeleteOverride(*service, [](const QString&) {
        return false;
    });

    const QVariantMap persistedFactor = makeValidFactorRecord(
        QStringLiteral("factor_quality"),
        QString::fromUtf8("质量因子"),
        QString::fromUtf8("缓存中的显示名"));
    repository->records.insert(QStringLiteral("factor_quality"), persistedFactor);
    FactorServiceTestAccess::setMemoryCache(*service, QStringLiteral("factor_quality"), persistedFactor);

    EXPECT_FALSE(service->deleteFactor(QStringLiteral("factor_quality")));
    EXPECT_EQ(repository->removeCalls, 0);
    EXPECT_TRUE(repository->exists(QStringLiteral("factor_quality")));
    ASSERT_TRUE(FactorServiceTestAccess::memoryCache(*service).contains(QStringLiteral("factor_quality")));
    EXPECT_EQ(FactorServiceTestAccess::memoryCache(*service)
                  .value(QStringLiteral("factor_quality"))
                  .value("displayName")
                  .toString(),
              QString::fromUtf8("缓存中的显示名"));

    const QVariantMap report = service->lastOperationReport();
    EXPECT_EQ(report.value("operation").toString(), QStringLiteral("deleteFactor"));
    EXPECT_EQ(report.value("factorId").toString(), QStringLiteral("factor_quality"));
    EXPECT_FALSE(report.value("success").toBool());
    EXPECT_EQ(report.value("stage").toString(), QStringLiteral("delete_domain_failed"));
    EXPECT_TRUE(report.value("message").toString().contains(QString::fromUtf8("factor_instance 删除失败")));
    EXPECT_FALSE(service->mutationInProgress());
}

TEST(FactorBacktestRegressionTest, ConcurrentAddFactorMutationsAreSerialized)
{
    using namespace std::chrono_literals;

    auto service = makeTestFactorService();
    auto repository = std::make_shared<InMemoryFactorRepository>();
    FactorServiceTestAccess::configureForRepositoryRegression(*service, repository);

    std::promise<void> firstEnteredPromise;
    std::shared_future<void> firstEnteredFuture(firstEnteredPromise.get_future());
    std::promise<void> allowFirstFinishPromise;
    std::shared_future<void> allowFirstFinishFuture(allowFirstFinishPromise.get_future());
    std::promise<void> secondEnteredPromise;
    std::shared_future<void> secondEnteredFuture(secondEnteredPromise.get_future());
    std::atomic<int> callCount{0};

    FactorServiceTestAccess::setDomainSyncOverride(*service, [&](const QVariantMap&) {
        const int currentCall = ++callCount;
        if (currentCall == 1) {
            firstEnteredPromise.set_value();
            allowFirstFinishFuture.wait();
            return true;
        }

        secondEnteredPromise.set_value();
        return true;
    });

    const QVariantMap firstFactor = makeValidFactorRecord(
        QStringLiteral("factor_quality_1"),
        QString::fromUtf8("质量因子一"),
        QString::fromUtf8("质量因子一"));
    const QVariantMap secondFactor = makeValidFactorRecord(
        QStringLiteral("factor_quality_2"),
        QString::fromUtf8("质量因子二"),
        QString::fromUtf8("质量因子二"));

    auto firstTask = std::async(std::launch::async, [&]() {
        return service->addFactor(firstFactor);
    });

    ASSERT_EQ(firstEnteredFuture.wait_for(1s), std::future_status::ready);
    EXPECT_TRUE(service->mutationInProgress());

    auto secondTask = std::async(std::launch::async, [&]() {
        return service->addFactor(secondFactor);
    });

    EXPECT_EQ(secondEnteredFuture.wait_for(50ms), std::future_status::timeout);

    allowFirstFinishPromise.set_value();

    EXPECT_EQ(firstTask.get(), QStringLiteral("factor_quality_1"));
    ASSERT_EQ(secondEnteredFuture.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(secondTask.get(), QStringLiteral("factor_quality_2"));
    EXPECT_FALSE(service->mutationInProgress());
    EXPECT_EQ(repository->saveCalls, 2);

    const QVariantList history = service->recentOperationReports();
    ASSERT_EQ(history.size(), 2);
    EXPECT_EQ(history.at(0).toMap().value("factorId").toString(), QStringLiteral("factor_quality_2"));
    EXPECT_EQ(history.at(1).toMap().value("factorId").toString(), QStringLiteral("factor_quality_1"));
}