#include "FactorBacktestController.h"

#include "../include/DataServiceCache.h"
#include "../include/DatabaseConnectionManager.h"
#include "../../../cache/include/cache_facade.h"
#include "../../../domain/factor/include/DataAvailabilityChecker.h"
#include "../../../domain/factor/include/FactorBacktestExecutor.h"
#include "../../../domain/factor/include/FactorCacheManager.h"
#include "../../../domain/factor/include/FactorInstanceManager.h"
#include "foundation/thread/ThreadPoolExecutor.h"

#include <QDate>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QStandardPaths>
#include <QVariantMap>
#include <QTimer>

#include <chrono>
#include <cmath>
#include <map>
#include <stdexcept>

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
    for (const auto& [key, value] : values) {
        params.emplace(key, value);
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

QString normalizeDataSourceMode(const QString& rawMode)
{
    const QString mode = rawMode.trimmed().toLower();
    if (mode == "database") {
        return "database";
    }
    return "cache";
}

bool isLatestBacktestDataset(const DataServiceCache::DataSetInfo& info)
{
    return info.id > 0
        && info.schemaVersion >= 2
        && info.isBacktestReady
        && info.tags.contains("daily_bar_full_v2")
        && info.tags.contains("factor_backtest_ready");
}

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

    loadResultFromFile(persistedResultFilePath());
}

FactorBacktestController::~FactorBacktestController()
{
    m_cancelRequested.store(true);
    qDebug() << "FactorBacktestController 销毁";
}

void FactorBacktestController::setSelectedFactorIds(const QVariantList& factorIds)
{
    if (m_selectedFactorIds != factorIds) {
        m_selectedFactorIds = factorIds;
        emit selectedFactorIdsChanged(m_selectedFactorIds);
        qDebug() << "FactorBacktestController: 更新选择的因子ID，数量:" << m_selectedFactorIds.size();
    }
}

void FactorBacktestController::setSelectedDatasetId(int datasetId)
{
    if (m_selectedDatasetId != datasetId) {
        m_selectedDatasetId = datasetId;
        emit selectedDatasetIdChanged(m_selectedDatasetId);
        qDebug() << "FactorBacktestController: 更新选择的数据集ID:" << m_selectedDatasetId;
    }
}

void FactorBacktestController::setDataSourceMode(const QString& dataSourceMode)
{
    const QString normalizedMode = normalizeDataSourceMode(dataSourceMode);
    if (m_dataSourceMode != normalizedMode) {
        m_dataSourceMode = normalizedMode;
        emit dataSourceModeChanged(m_dataSourceMode);
        qDebug() << "FactorBacktestController: 更新数据源模式:" << m_dataSourceMode;
    }
}

void FactorBacktestController::startBacktest(const QString& groupText,
                                             const QString& startDate,
                                             const QString& endDate)
{
    startBacktestWithFactors(m_selectedFactorIds, groupText, startDate, endDate);
}

void FactorBacktestController::startBacktestWithFactors(const QVariantList& factorIds,
                                                        const QString& groupText,
                                                        const QString& startDate,
                                                        const QString& endDate)
{
    qDebug() << "开始回测，因子数量:" << factorIds.size() << "分组:" << groupText;

    auto failFast = [this](const QString& errorMessage) {
        m_isRunning = false;
        m_hasActiveTask = false;
        m_pendingBacktestResult.reset();
        if (m_progressTimer) {
            m_progressTimer->stop();
        }
        m_status = errorMessage;
        emit isRunningChanged(m_isRunning);
        emit statusChanged(m_status);
        emit backtestFailed(errorMessage);
    };

    if (!m_isRunning) {
        m_hasActiveTask = false;
        m_pendingBacktestResult.reset();
        m_activeRequestedFactorId.clear();
        if (m_progressTimer) {
            m_progressTimer->stop();
        }
    }

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

    if (m_instanceManager) {
        m_instanceManager->refreshCache();
    }

    const QString requestedFactorId = factorIds.first().toString();
    const QString resolvedInstanceId = resolveInstanceId(factorIds.first());
    if (resolvedInstanceId.isEmpty()) {
        failFast(QString("未找到可用的因子实例: %1").arg(requestedFactorId));
        return;
    }

    resetResults();
    m_cancelRequested.store(false);
    m_isRunning = true;
    m_progress = 5;
    m_status = "正在准备回测";

    emit isRunningChanged(m_isRunning);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    emit backtestStarted(requestedFactorId);

    try {
        const factor::BacktestConfig config = buildBacktestConfig(
            resolvedInstanceId,
            groupText,
            startDate,
            endDate
        );

        auto handle = m_executor->executeTrackedAsync(config);
        m_activeTaskId = handle.taskId;
        m_hasActiveTask = true;
        m_activeRequestedFactorId = requestedFactorId;
        m_pendingBacktestResult = std::make_unique<std::future<factor::BacktestResult>>(std::move(handle.future));

        m_progress = 15;
        m_status = "正在执行回测";
        emit progressChanged(m_progress);
        emit statusChanged(m_status);
        emit backtestProgress(m_progress, m_status);

        if (m_progressTimer && !m_progressTimer->isActive()) {
            m_progressTimer->start();
        }
    } catch (const std::exception& e) {
        finalizeBacktestFailure(QString::fromUtf8(e.what()), false);
    }
}

void FactorBacktestController::cancelBacktest()
{
    qDebug() << "取消回测";

    if (!m_isRunning) {
        return;
    }

    m_cancelRequested.store(true);
    if (m_executor && m_hasActiveTask) {
        m_executor->cancel(m_activeTaskId);
    }
    m_status = "正在取消...";
    emit statusChanged(m_status);
}

int FactorBacktestController::parseGroupCount(const QString& groupText) const
{
    if (groupText.contains("20")) return 20;
    if (groupText.contains("10")) return 10;
    if (groupText.contains("5")) return 5;
    return 10;
}

bool FactorBacktestController::initializeRuntime()
{
    if (m_executor) {
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

    m_executor = std::make_unique<factor::FactorBacktestExecutor>(m_instanceManager, m_threadPool, m_cacheManager);
    return true;
}

QString FactorBacktestController::resolveInstanceId(const QVariant& factorId) const
{
    if (!m_database) {
        return {};
    }

    const QString rawId = factorId.toString().trimmed();
    if (rawId.isEmpty()) {
        return {};
    }

    QStringList candidateIds;
    candidateIds.append(rawId);
    if (rawId.endsWith("_instance")) {
        const QString baseId = rawId.left(rawId.size() - QString("_instance").size()).trimmed();
        if (!baseId.isEmpty() && !candidateIds.contains(baseId)) {
            candidateIds.append(baseId);
        }
    }

    const QString primaryId = candidateIds.value(0);
    const QString secondaryId = candidateIds.value(1, primaryId);

    auto resolveByPriority = [&](bool onlyActive) -> QString {
        const QString sql = onlyActive
            ? QString(
                "SELECT instance_id FROM factor_instance "
                "WHERE (instance_id = :instanceIdPrimary OR factor_id = :factorIdPrimary "
                "OR instance_id = :instanceIdSecondary OR factor_id = :factorIdSecondary) "
                "AND status = 'ACTIVE' "
                "ORDER BY CASE "
                "WHEN instance_id = :priorityInstancePrimary THEN 0 "
                "WHEN instance_id = :priorityInstanceSecondary THEN 1 "
                "WHEN factor_id = :priorityFactorPrimary THEN 2 "
                "WHEN factor_id = :priorityFactorSecondary THEN 3 "
                "ELSE 4 END, "
                "updated_at DESC, created_at DESC LIMIT 1")
            : QString(
                "SELECT instance_id FROM factor_instance "
                "WHERE instance_id = :instanceIdPrimary OR factor_id = :factorIdPrimary "
                "OR instance_id = :instanceIdSecondary OR factor_id = :factorIdSecondary "
                "ORDER BY CASE "
                "WHEN instance_id = :priorityInstancePrimary THEN 0 "
                "WHEN instance_id = :priorityInstanceSecondary THEN 1 "
                "WHEN factor_id = :priorityFactorPrimary THEN 2 "
                "WHEN factor_id = :priorityFactorSecondary THEN 3 "
                "ELSE 4 END, "
                "updated_at DESC, created_at DESC LIMIT 1");

        auto result = m_database->executeQuery(
            sql,
            makeNamedParams({
                {"instanceIdPrimary", primaryId},
                {"factorIdPrimary", primaryId},
                {"instanceIdSecondary", secondaryId},
                {"factorIdSecondary", secondaryId},
                {"priorityInstancePrimary", primaryId},
                {"priorityInstanceSecondary", secondaryId},
                {"priorityFactorPrimary", primaryId},
                {"priorityFactorSecondary", secondaryId}
            })
        );

        if (result.isEmpty()) {
            return {};
        }

        return result.getRow(0).getString("instance_id");
    };

    const QString activeMatch = resolveByPriority(true);
    if (!activeMatch.isEmpty()) {
        return activeMatch;
    }

    return resolveByPriority(false);
}

factor::BacktestConfig FactorBacktestController::buildBacktestConfig(const QString& resolvedInstanceId,
                                                                     const QString& groupText,
                                                                     const QString& startDate,
                                                                     const QString& endDate) const
{
    factor::BacktestConfig config;
    config.instanceId = resolvedInstanceId.toStdString();

    QString effectiveStartDate = startDate;
    QString effectiveEndDate = endDate;
    const QString dataSourceMode = normalizeDataSourceMode(m_dataSourceMode);
    if (dataSourceMode == "cache") {
        if (m_selectedDatasetId <= 0) {
            throw std::runtime_error("请选择缓存集后再开始回测");
        }

        const auto datasetInfo = DataServiceCache::getInstance().getDataSetInfo(m_selectedDatasetId);
        if (datasetInfo.id <= 0) {
            throw std::runtime_error("所选缓存集无效，请重新选择");
        }
        if (!isLatestBacktestDataset(datasetInfo)) {
            throw std::runtime_error("所选缓存集不是最新完整日线数据，请重新生成并选择最新缓存集");
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
        for (const QString& stockCode : datasetInfo.stockCodes) {
            if (!stockCode.trimmed().isEmpty()) {
                config.allowedStockCodes.push_back(stockCode.trimmed().toStdString());
            }
        }

        config.cachedBars.reserve(static_cast<size_t>(datasetRows.size()));
        for (const QVariant& rowVariant : datasetRows) {
            const QVariantMap row = rowVariant.toMap();
            const QString symbol = row.value("symbol").toString().trimmed();
            QString tradeDate = row.value("trade_date").toString().trimmed();
            if (tradeDate.isEmpty()) {
                tradeDate = row.value("date").toString().trimmed();
            }

            bool closeOk = false;
            const double close = row.value("close").toDouble(&closeOk);
            if (symbol.isEmpty() || tradeDate.isEmpty() || !closeOk || !std::isfinite(close) || close <= 0.0) {
                continue;
            }

            factor::CachedMarketBar bar;
            bar.symbol = symbol.toStdString();
            bar.tradeDate = tradeDate.toStdString();
            bar.close = close;
            for (auto it = row.begin(); it != row.end(); ++it) {
                bool valueOk = false;
                const double numericValue = it.value().toDouble(&valueOk);
                if (!valueOk || !std::isfinite(numericValue)) {
                    continue;
                }
                bar.numericFields[it.key().trimmed().toStdString()] = numericValue;
            }
            bar.numericFields["close"] = close;
            config.cachedBars.push_back(std::move(bar));
        }

        if (config.cachedBars.empty()) {
            throw std::runtime_error("所选缓存集缺少 symbol/date/close 数据，暂时无法用于回测");
        }
    } else {
        config.datasetId = -1;
    }

    config.startDate = effectiveStartDate.isEmpty() ? "2020-01-01" : effectiveStartDate.toStdString();
    config.endDate = effectiveEndDate.isEmpty() ? QDate::currentDate().toString("yyyy-MM-dd").toStdString() : effectiveEndDate.toStdString();
    config.numGroups = parseGroupCount(groupText);
    config.forwardDays = 1;
    config.transactionCost = 0.001;

    return config;
}

QVariantMap FactorBacktestController::buildResultMap(const QString& requestedFactorId,
                                                     const factor::BacktestResult& result) const
{
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
        groupMap["annualizedReturn"] = groupReturn * 252.0;
        groupMap["volatility"] = std::abs(groupReturn) * std::sqrt(252.0);
        groupMap["sharpeRatio"] = result.sharpeRatio;
        groupMap["maxDrawdown"] = result.maxDrawdown;
        groupMap["winRate"] = result.winRate;
        groupMap["profitFactor"] = 0.0;
        groupMap["calmarRatio"] = 0.0;
        groupMap["sortinoRatio"] = 0.0;
        groupMap["alpha"] = 0.0;
        groupMap["beta"] = 0.0;
        groupMap["trackingError"] = 0.0;
        groupMap["informationRatio"] = result.icirResult.ir;
        groups.append(groupMap);
    }

    QVariantMap icirMap;
    icirMap["icValue"] = result.icirResult.icMean;
    icirMap["irValue"] = result.icirResult.ir;
    icirMap["icTStat"] = 0.0;
    icirMap["icPValue"] = 1.0;
    icirMap["icPositiveRate"] = result.icirResult.icPositiveRatio;
    icirMap["isSignificant"] = std::abs(result.icirResult.icMean) >= 0.02;
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
    summaryMap["monotonicity"] = result.icirResult.icMean;
    summaryMap["discrimination"] = result.icirResult.ir;
    summaryMap["winRate"] = result.winRate;
    summaryMap["sharpeRatio"] = result.sharpeRatio;
    summaryMap["maxDrawdown"] = result.maxDrawdown;
    summaryMap["annualReturn"] = result.annualReturn;
    summaryMap["dataCoverage"] = result.dataCoverage;

    QVariantMap configMap;
    configMap["factorId"] = requestedFactorId;
    configMap["instanceId"] = QString::fromStdString(result.instanceId);
    configMap["factorName"] = QString::fromStdString(result.instanceName);
    configMap["startDate"] = QString::fromStdString(result.config.startDate);
    configMap["endDate"] = QString::fromStdString(result.config.endDate);
    configMap["numGroups"] = result.config.numGroups;
    configMap["forwardDays"] = result.config.forwardDays;
    configMap["transactionCost"] = result.config.transactionCost;
    configMap["datasetId"] = result.config.datasetId;
    configMap["dataSourceMode"] = normalizeDataSourceMode(m_dataSourceMode);

    QVariantMap resultMap;
    resultMap["taskId"] = QString::fromStdString(result.resultId.to_string());
    resultMap["executionTime"] = result.executionTimeMs;
    resultMap["success"] = true;
    resultMap["status"] = QString::fromStdString(result.status);
    resultMap["config"] = configMap;
    resultMap["groups"] = groups;
    resultMap["icirResult"] = icirMap;
    resultMap["summary"] = summaryMap;
    return resultMap;
}

void FactorBacktestController::resetResults()
{
    m_backtestResult.clear();
    m_groupResults.clear();
    m_icirResult.clear();
    m_summaryStats.clear();
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

    m_backtestResult = result;
    m_groupResults = result.value("groups").toList();
    m_icirResult = result.value("icirResult").toMap();
    m_summaryStats = result.value("summary").toMap();

    emit backtestResultChanged(m_backtestResult);
    emit groupResultsChanged(m_groupResults);
    emit icirResultChanged(m_icirResult);
    emit summaryStatsChanged(m_summaryStats);
}

bool FactorBacktestController::persistLatestResult() const
{
    return saveResultToFile(persistedResultFilePath());
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
    if (m_pendingBacktestResult &&
        m_pendingBacktestResult->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        try {
            factor::BacktestResult result = m_pendingBacktestResult->get();
            m_pendingBacktestResult.reset();
            if (m_progressTimer) {
                m_progressTimer->stop();
            }

            if (result.status == "SUCCESS") {
                finalizeBacktestSuccess(m_activeRequestedFactorId, result);
            } else if (result.status == "CANCELLED") {
                finalizeBacktestFailure("回测已取消", true);
            } else {
                finalizeBacktestFailure(QString::fromStdString(result.errorMessage.empty() ? "因子回测执行失败" : result.errorMessage), false);
            }
        } catch (const std::exception& e) {
            m_pendingBacktestResult.reset();
            if (m_progressTimer) {
                m_progressTimer->stop();
            }
            finalizeBacktestFailure(QString::fromUtf8(e.what()), false);
        }
        return;
    }

    if (!m_executor || !m_hasActiveTask) {
        return;
    }

    const auto progressInfo = m_executor->getProgress(m_activeTaskId);
    if (progressInfo.status == "NOT_FOUND") {
        return;
    }

    const int progressValue = progressInfo.progress;
    const QString statusText = QString::fromStdString(progressInfo.currentStep.empty() ? progressInfo.status : progressInfo.currentStep);

    if (m_progress != progressValue) {
        m_progress = progressValue;
        emit progressChanged(m_progress);
    }

    if (m_status != statusText) {
        m_status = statusText;
        emit statusChanged(m_status);
    }

    emit backtestProgress(m_progress, m_status);
}

void FactorBacktestController::finalizeBacktestSuccess(const QString& requestedFactorId,
                                                       const factor::BacktestResult& result)
{
    m_pendingBacktestResult.reset();
    if (m_progressTimer) {
        m_progressTimer->stop();
    }
    const QVariantMap resultMap = buildResultMap(requestedFactorId, result);
    m_backtestResult = resultMap;
    m_groupResults = resultMap.value("groups").toList();
    m_icirResult = resultMap.value("icirResult").toMap();
    m_summaryStats = resultMap.value("summary").toMap();
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
    emit backtestCompleted(resultMap);

    if (!persistLatestResult()) {
        qWarning() << "FactorBacktestController: 自动保存最新回测结果失败:" << persistedResultFilePath();
    }
}

void FactorBacktestController::finalizeBacktestFailure(const QString& errorMessage,
                                                       bool cancelled)
{
    m_pendingBacktestResult.reset();
    if (m_progressTimer) {
        m_progressTimer->stop();
    }
    m_isRunning = false;
    m_progress = cancelled ? 0 : m_progress;
    m_status = cancelled ? "已取消" : "回测失败";
    m_hasActiveTask = false;
    m_activeRequestedFactorId.clear();
    m_cancelRequested.store(false);

    emit isRunningChanged(m_isRunning);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);

    if (cancelled) {
        emit backtestCancelled();
    } else {
        emit backtestFailed(errorMessage);
        qCritical() << "回测失败:" << errorMessage;
    }
}
