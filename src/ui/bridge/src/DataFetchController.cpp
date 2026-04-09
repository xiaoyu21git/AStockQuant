// DataFetchController.cpp - 改进版本，支持模型和数据缓存
#include "DataFetchController.h"
#include "DataService.h"
#include "DataCleaningEngine.h"
#include "DataServiceCache.h"
#include "PreviewDataModel.h"
#include "foundation.h"

#include <QMetaObject>
#include <QPointer>
#include <QDebug>
#include <QDateTime>
#include <QSet>
#include <QVector>
#include <QQmlEngine>
#include <QQmlContext>

#include <functional>
#include <map>

namespace {

template <typename Func>
void invokeOnMainThread(DataFetchController* controller, Func&& func)
{
    QPointer<DataFetchController> safeController(controller);
    QMetaObject::invokeMethod(controller,
                              [safeController, fn = std::forward<Func>(func)]() mutable {
                                  if (safeController) {
                                      fn(safeController.data());
                                  }
                              },
                              Qt::QueuedConnection);
}

template <typename Func>
bool submitToFoundationThreadPool(DataFetchController* controller, Func&& func, QString* errorMessage = nullptr)
{
    QPointer<DataFetchController> safeController(controller);
    try {
        foundation::Foundation::instance().thread_pool().post(
            [safeController, fn = std::forward<Func>(func)]() mutable {
                if (safeController) {
                    fn(safeController.data());
                }
            }
        );
        return true;
    } catch (const std::exception& e) {
        if (errorMessage) {
            *errorMessage = QString::fromUtf8(e.what());
        }
    } catch (...) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未知线程池错误");
        }
    }
    return false;
}

void updateStringProperty(QString& target, const QString& value, const std::function<void()>& notifier)
{
    if (target != value) {
        target = value;
        notifier();
    }
}

void updateBoolProperty(bool& target, bool value, const std::function<void()>& notifier)
{
    if (target != value) {
        target = value;
        notifier();
    }
}

QString resolveTradeDate(const QVariantMap& dataMap)
{
    if (dataMap.contains("trade_date")) {
        return dataMap.value("trade_date").toString().trimmed();
    }
    if (dataMap.contains("date")) {
        return dataMap.value("date").toString().trimmed();
    }
    if (dataMap.contains("Date")) {
        return dataMap.value("Date").toString().trimmed();
    }
    return {};
}

QString resolveSymbol(const QVariantMap& dataMap)
{
    if (dataMap.contains("symbol")) {
        return dataMap.value("symbol").toString().trimmed();
    }
    if (dataMap.contains("code")) {
        return dataMap.value("code").toString().trimmed();
    }
    if (dataMap.contains("stock_code")) {
        return dataMap.value("stock_code").toString().trimmed();
    }
    return {};
}

QString summarizeRuleNames(const QVariantMap& rules)
{
    QStringList enabledRules;
    for (auto it = rules.constBegin(); it != rules.constEnd(); ++it) {
        if (it.value().toBool()) {
            enabledRules.append(it.key());
        }
    }
    return enabledRules.join(",");
}

double extractNumericValue(const QVariantMap& dataMap,
                          const QStringList& keys,
                          bool* ok = nullptr)
{
    for (const QString& key : keys) {
        auto it = dataMap.constFind(key);
        if (it == dataMap.constEnd()) {
            continue;
        }

        bool conversionOk = false;
        const double value = it.value().toDouble(&conversionOk);
        if (conversionOk) {
            if (ok) {
                *ok = true;
            }
            return value;
        }
    }

    if (ok) {
        *ok = false;
    }
    return 0.0;
}

struct StockPreviewSummary {
    QString symbol;
    QString name;
    QString startDate;
    QString endDate;
    int recordCount = 0;
    double firstClose = 0.0;
    bool hasFirstClose = false;
    double latestClose = 0.0;
    bool hasLatestClose = false;
    double totalVolume = 0.0;
};

QVector<QVariantMap> buildStockSummaryPreviewData(const QVariantList& data)
{
    std::map<QString, StockPreviewSummary> summaries;

    for (const QVariant& item : data) {
        if (!item.canConvert<QVariantMap>()) {
            continue;
        }

        const QVariantMap row = item.toMap();
        const QString symbol = resolveSymbol(row);
        if (symbol.isEmpty()) {
            continue;
        }

        auto& summary = summaries[symbol];
        summary.symbol = symbol;
        if (summary.name.isEmpty()) {
            summary.name = row.value(
                "name",
                row.value(
                    "stockName",
                    row.value(
                        "stock_name",
                        row.value(
                            "displayName",
                            row.value(
                                "sec_name",
                                row.value(
                                    "Name",
                                    row.value("名称", ""))))))).toString().trimmed();
        }

        const QString tradeDate = resolveTradeDate(row);
        bool hasClose = false;
        const double closeValue = extractNumericValue(row, {"close", "Close", "price"}, &hasClose);

        if (!tradeDate.isEmpty()) {
            if (summary.startDate.isEmpty() || tradeDate < summary.startDate) {
                summary.startDate = tradeDate;
                if (hasClose && closeValue > 0.0) {
                    summary.firstClose = closeValue;
                    summary.hasFirstClose = true;
                }
            } else if (tradeDate == summary.startDate && !summary.hasFirstClose && hasClose && closeValue > 0.0) {
                summary.firstClose = closeValue;
                summary.hasFirstClose = true;
            }

            if (summary.endDate.isEmpty() || tradeDate > summary.endDate) {
                summary.endDate = tradeDate;
                if (hasClose && closeValue > 0.0) {
                    summary.latestClose = closeValue;
                    summary.hasLatestClose = true;
                }
            } else if (tradeDate == summary.endDate && hasClose && closeValue > 0.0) {
                summary.latestClose = closeValue;
                summary.hasLatestClose = true;
            }
        }

        bool hasVolume = false;
        const double volumeValue = extractNumericValue(row, {"volume", "Volume"}, &hasVolume);
        if (hasVolume && volumeValue > 0.0) {
            summary.totalVolume += volumeValue;
        }

        summary.recordCount += 1;
    }

    QVector<QVariantMap> summaryData;
    summaryData.reserve(static_cast<int>(summaries.size()));
    for (const auto& [symbol, summary] : summaries) {
        QVariantMap previewRow;
        previewRow["code"] = summary.symbol;
        previewRow["symbol"] = summary.symbol;
        previewRow["name"] = summary.name.isEmpty() ? summary.symbol : summary.name;
        previewRow["date"] = summary.endDate;
        previewRow["timeRange"] = summary.startDate.isEmpty()
            ? QString()
            : (summary.startDate == summary.endDate
                ? summary.startDate
                : summary.startDate + " ~ " + summary.endDate);
        previewRow["recordCount"] = summary.recordCount;
        previewRow["close"] = summary.hasLatestClose ? summary.latestClose : 0.0;
        previewRow["volume"] = summary.totalVolume;

        double periodChange = 0.0;
        if (summary.hasFirstClose && summary.hasLatestClose && summary.firstClose > 0.0) {
            periodChange = ((summary.latestClose - summary.firstClose) / summary.firstClose) * 100.0;
        }
        previewRow["change"] = periodChange;

        summaryData.push_back(previewRow);
    }

    return summaryData;
}

QStringList collectAvailableFields(const QVariantList& data)
{
    QSet<QString> fields;
    for (const QVariant& item : data) {
        if (!item.canConvert<QVariantMap>()) {
            continue;
        }
        const QVariantMap row = item.toMap();
        for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
            const QString key = it.key().trimmed();
            if (!key.isEmpty()) {
                fields.insert(key);
            }
        }
    }

    QStringList result = fields.values();
    result.sort();
    return result;
}

bool hasLatestFullDailyBarFields(const QStringList& availableFields);

DataServiceCache::DataSetInfo buildCleanedDataSetInfo(const QVariantList& cleanedData,
                                                      const QString& currentSymbol,
                                                      const QString& currentStartDate,
                                                      const QString& currentEndDate,
                                                      const QVariantMap& pendingRules)
{
    DataServiceCache::DataSetInfo dataSetInfo;
    QStringList stockCodes;
    QStringList tradeDates;
    QSet<QString> uniqueSymbols;
    QSet<QString> uniqueDates;

    for (const QVariant& item : cleanedData) {
        if (!item.canConvert<QVariantMap>()) {
            continue;
        }

        const QVariantMap row = item.toMap();
        const QString symbol = resolveSymbol(row);
        const QString tradeDate = resolveTradeDate(row);

        if (!symbol.isEmpty() && !uniqueSymbols.contains(symbol)) {
            uniqueSymbols.insert(symbol);
            stockCodes.append(symbol);
        }
        if (!tradeDate.isEmpty() && !uniqueDates.contains(tradeDate)) {
            uniqueDates.insert(tradeDate);
            tradeDates.append(tradeDate);
        }
    }

    tradeDates.sort();
    dataSetInfo.displayName = QString("清洗结果_%1_%2")
        .arg(currentSymbol.isEmpty() ? "ALL" : currentSymbol)
        .arg(QDateTime::currentDateTime().toString("MMdd_HHmmss"));
    dataSetInfo.description = QString("数据清洗结果缓存集");
    if (!currentStartDate.isEmpty() || !currentEndDate.isEmpty()) {
        dataSetInfo.description += QString("，范围: %1 ~ %2").arg(currentStartDate, currentEndDate);
    }

    const QString ruleSummary = summarizeRuleNames(pendingRules);
    if (!ruleSummary.isEmpty()) {
        dataSetInfo.description += QString("，规则: %1").arg(ruleSummary);
    }

    dataSetInfo.sourceType = "cleaning";
    dataSetInfo.createdTime = QDateTime::currentDateTime();
    dataSetInfo.stockCodes = stockCodes;
    dataSetInfo.availableFields = collectAvailableFields(cleanedData);
    dataSetInfo.schemaVersion = 2;
    dataSetInfo.isBacktestReady = hasLatestFullDailyBarFields(dataSetInfo.availableFields);
    dataSetInfo.tags = QStringList{"cleaned", "cleaning_result"};
    if (dataSetInfo.isBacktestReady) {
        dataSetInfo.tags.append("factor_backtest_ready");
        dataSetInfo.tags.append("daily_bar_full_v2");
    } else {
        dataSetInfo.tags.append("legacy_incomplete");
    }
    if (!ruleSummary.isEmpty()) {
        dataSetInfo.tags.append("rule_" + ruleSummary);
    }

    if (!tradeDates.isEmpty()) {
        dataSetInfo.startDate = QDate::fromString(tradeDates.first(), "yyyy-MM-dd");
        dataSetInfo.endDate = QDate::fromString(tradeDates.last(), "yyyy-MM-dd");
    } else {
        dataSetInfo.startDate = QDate::fromString(currentStartDate, "yyyy-MM-dd");
        dataSetInfo.endDate = QDate::fromString(currentEndDate, "yyyy-MM-dd");
    }

    return dataSetInfo;
}

bool hasLatestFullDailyBarFields(const QStringList& availableFields)
{
    static const QStringList requiredFields = {
        "symbol", "trade_date", "open", "high", "low", "close", "pre_close",
        "volume", "turnover", "change_pct", "change_amt", "amplitude",
        "turnover_rate", "pe_ratio", "pb_ratio", "market_cap",
        "circulating_market_cap", "data_source"
    };

    const QSet<QString> fieldSet(availableFields.begin(), availableFields.end());
    for (const QString& field : requiredFields) {
        if (!fieldSet.contains(field)) {
            return false;
        }
    }
    return true;
}

}

DataFetchController::DataFetchController(QObject* parent)
    : QObject(parent)
    , m_dataService(new DataService(this))
    , m_previewModel(new PreviewDataModel(this))
{
    // 设置默认日期（最近30天）
    QDateTime currentDate = QDateTime::currentDateTime();
    QDateTime startDate = currentDate.addDays(-30);
    
    m_startDate = startDate.toString("yyyy-MM-dd");
    m_endDate = currentDate.toString("yyyy-MM-dd");
    
    // 连接信号：控制器 -> 服务
    connect(this, &DataFetchController::requestLoadData,
            m_dataService, &DataService::loadFromDatabase);
    connect(this, &DataFetchController::requestCleanData,
            m_dataService, &DataService::cleanDataAsync);
    
    // 连接信号：服务 -> 控制器
    // 注意：DataService使用queryProgress而不是dataLoadProgress
    connect(m_dataService, &DataService::queryProgress,
            this, &DataFetchController::onDataLoadProgress);
    // DataService使用queryCompleted而不是dataLoadCompleted
    connect(m_dataService, &DataService::queryCompleted,
            this, &DataFetchController::onDataLoadCompleted);
    // DataService使用error而不是dataLoadError
    connect(m_dataService, &DataService::error,
            this, &DataFetchController::onDataLoadError);
        connect(m_dataService, &DataService::cleaningProgressDetail,
            this, &DataFetchController::onDataCleaningProgressDetail);
    // DataService使用cleaningCompleted而不是dataCleaningCompleted
    connect(m_dataService, &DataService::cleaningCompleted,
            this, &DataFetchController::onDataCleaningCompleted);
    
    // 初始化数据库（异步）- DataService会自动初始化
    // 简单的定时器调用，不使用lambda避免语法问题
    QTimer::singleShot(1000, this, SLOT(logInitMessage()));
    
    // 不需要连接延迟清洗槽函数，因为它是通过SLOT()调用的
    // 只需要确保delayedCleanData()在public slots中声明
}

DataFetchController::~DataFetchController()
{
    // 清理
}

void DataFetchController::logInitMessage()
{
}

void DataFetchController::loadFromDatabase(const QString& symbol, const QString& startDate, const QString& endDate)
{
    updateBoolProperty(m_operationInProgress, true, [this]() { emit operationInProgressChanged(); });
    updateStringProperty(m_operationPhase, QStringLiteral("获取数据"), [this]() { emit operationPhaseChanged(); });
    updateStringProperty(m_currentProgressStock, QString(), [this]() { emit currentProgressStockChanged(); });

    // 验证参数
    QString actualSymbol = symbol;
    
    // 如果symbol为空，尝试使用m_symbols中的第一个股票代码
    if (actualSymbol.isEmpty() && !m_symbols.isEmpty()) {
        actualSymbol = m_symbols.first();
    }
    
    // 如果仍然为空，使用空字符串（允许查询所有股票）
    if (actualSymbol.isEmpty()) {
        // 不返回错误，允许空股票代码查询
    }
    
    // 验证日期
    if (startDate.isEmpty() || endDate.isEmpty()) {
        updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
        updateStatus("请设置开始和结束日期", 0);
        emit dataFetchError("日期未设置");
        return;
    }
    
    // 保存当前加载的数据标识
    m_currentSymbol = actualSymbol;
    m_currentStartDate = startDate;
    m_currentEndDate = endDate;
    m_serviceAlreadyCachedCurrentRequest = false;
    
    // 首先检查缓存 - 只从 DataServiceCache 读取，避免同一份数据在 DataManager 中重复保存
    QVariantList cachedData;
    {
        DataServiceCache& cache = DataServiceCache::getInstance();
        cachedData = cache.getCachedData(actualSymbol, startDate, endDate);
    }
    
    if (!cachedData.isEmpty()) {
        // 更新本地数据
        m_fetchedData = cachedData;
        emit fetchedDataChanged();

        // 更新状态
        updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
        updateStatus("使用缓存数据", 100);
        return;
    }
    
    updateStatus("正在从数据库加载数据...", 0);
    
    // 转发请求给DataService
    emit requestLoadData(actualSymbol, startDate, endDate);
}

void DataFetchController::cleanDataAsync(const QVariantMap& rules)
{
    updateBoolProperty(m_operationInProgress, true, [this]() { emit operationInProgressChanged(); });
    updateStringProperty(m_operationPhase, QStringLiteral("清洗数据"), [this]() { emit operationPhaseChanged(); });
    updateStringProperty(m_currentProgressStock, QString(), [this]() { emit currentProgressStockChanged(); });
    emit dataCleaningStarted();

    if (!m_fetchedData.isEmpty()) {
        updateCleanStats(m_fetchedData.size(), 0);
        updateStatus("正在使用当前数据集进行清洗...", 0);
        emit requestCleanData(m_fetchedData, rules);
        return;
    }

    if (m_currentStartDate.isEmpty() || m_currentEndDate.isEmpty()) {
        updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
        emit dataCleaningCompleted(false, "没有可用的数据集，请先加载数据后再清洗", QVariantList());
        return;
    }

    updateStatus("正在回载当前参数对应的数据集...", 0);
    m_pendingRules = rules;
    m_pendingCleanAfterLoad = true;
    loadFromDatabase(m_currentSymbol, m_currentStartDate, m_currentEndDate);
}

void DataFetchController::updateStatus(const QString& message, int progress)
{
    if (progress >= 0) {
        m_progress = progress;
        emit progressChanged();
    }
    
    m_statusMessage = message;
    emit statusMessageChanged();
    
}

void DataFetchController::resetProgressState()
{
    updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
    updateStringProperty(m_operationPhase, QString(), [this]() { emit operationPhaseChanged(); });
    updateStringProperty(m_currentProgressStock, QString(), [this]() { emit currentProgressStockChanged(); });
    updateStatus(QStringLiteral("就绪"), 0);
}

void DataFetchController::updateCleanStats(int inputCount, int outputCount)
{
    const int normalizedInput = qMax(0, inputCount);
    const int normalizedOutput = qMax(0, outputCount);
    const int normalizedRemoved = qMax(0, normalizedInput - normalizedOutput);

    if (m_cleanInputRecordCount != normalizedInput
        || m_cleanOutputRecordCount != normalizedOutput
        || m_cleanRemovedRecordCount != normalizedRemoved) {
        m_cleanInputRecordCount = normalizedInput;
        m_cleanOutputRecordCount = normalizedOutput;
        m_cleanRemovedRecordCount = normalizedRemoved;
        emit cleanStatsChanged();
    }
}

// 接收DataService的数据加载进度
void DataFetchController::onDataLoadProgress(int progress, const QString& message)
{
    updateBoolProperty(m_operationInProgress, true, [this]() { emit operationInProgressChanged(); });
    updateStringProperty(m_operationPhase, QStringLiteral("获取数据"), [this]() { emit operationPhaseChanged(); });
    updateStringProperty(m_currentProgressStock, QString(), [this]() { emit currentProgressStockChanged(); });

    // 更新状态
    updateStatus(message, progress);
    
    // 转发给QML
    emit dataFetchProgress(progress, message);
}

// 接收DataService的数据加载完成结果
void DataFetchController::onDataLoadCompleted(bool success, const QString& message, const QVariantList& data)
{
    const bool continueCleaning = success && m_pendingCleanAfterLoad;
    if (!continueCleaning) {
        updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
    }
    updateStringProperty(m_currentProgressStock, QString(), [this]() { emit currentProgressStockChanged(); });

    if (success) {
        // 保存数据
        m_fetchedData = data;
        emit fetchedDataChanged();
        
        // 更新PreviewDataModel - 在C++中直接更新模型，不传递数据给QML
        if (m_previewModel) {
            const QVector<QVariantMap> dataVector = buildStockSummaryPreviewData(data);
            m_previewModel->updateData(dataVector);
        } else {
            qWarning() << "DataFetchController::onDataLoadCompleted: PreviewModel is null, cannot update";
        }
        
        // 只有服务层未缓存当前请求时，控制器才补缓存。
        if (!m_serviceAlreadyCachedCurrentRequest &&
            !m_currentStartDate.isEmpty() && !m_currentEndDate.isEmpty()) {
            DataServiceCache& cache = DataServiceCache::getInstance();
            if (m_currentSymbol.isEmpty()) {
                cache.cacheData("", m_currentStartDate, m_currentEndDate, data);
            } else {
                cache.cacheData(m_currentSymbol, m_currentStartDate, m_currentEndDate, data);
            }
        }
        
        if (continueCleaning) {
            updateStringProperty(m_operationPhase, QStringLiteral("清洗数据"), [this]() { emit operationPhaseChanged(); });
            updateCleanStats(data.size(), 0);
            updateStatus("数据加载完成，开始清洗...", 25);
            m_pendingCleanAfterLoad = false;
            emit requestCleanData(data, m_pendingRules);
        } else {
            // 更新状态
            updateStatus(message, 100);
            if (m_isFetching) {
                m_isFetching = false;
                emit isFetchingChanged();
            }
        }
        
    } else {
        // 更新状态
        updateStatus("数据加载失败: " + message, 0);
        if (m_isFetching) {
            m_isFetching = false;
            emit isFetchingChanged();
        }
    }
}

// 接收DataService的数据加载错误
void DataFetchController::onDataLoadError(const QString& error)
{
    updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
    updateStringProperty(m_currentProgressStock, QString(), [this]() { emit currentProgressStockChanged(); });
    m_pendingCleanAfterLoad = false;

    qWarning() << "DataFetchController::onDataLoadError:" << error;
    if (m_isFetching) {
        m_isFetching = false;
        emit isFetchingChanged();
    }
    
    // 更新状态
    updateStatus("数据加载错误: " + error, 0);
    
    // 转发给QML
    emit dataFetchError(error);
}

// 接收DataService的数据清洗进度
void DataFetchController::onDataCleaningProgress(int progress, const QString& message)
{
    // 更新状态
    updateStatus(message, progress);
    
    // 转发给QML
    emit dataCleaningProgress(progress, message);
}

void DataFetchController::onDataCleaningProgressDetail(int progress,
                                                       const QString& message,
                                                       const QString& currentStock,
                                                       int keptRecords,
                                                       int removedRecords)
{
    updateBoolProperty(m_operationInProgress, true, [this]() { emit operationInProgressChanged(); });
    updateStringProperty(m_operationPhase, QStringLiteral("清洗数据"), [this]() { emit operationPhaseChanged(); });
    updateStringProperty(m_currentProgressStock, currentStock, [this]() { emit currentProgressStockChanged(); });

    const int inferredInput = qMax(m_cleanInputRecordCount, keptRecords + removedRecords);
    updateCleanStats(inferredInput, keptRecords);
    updateStatus(message, progress);
    emit dataCleaningProgress(progress, message);
}

// 接收DataService的数据清洗完成结果
void DataFetchController::onDataCleaningCompleted(bool success, const QString& message, const QVariantList& cleanedData)
{
    updateStringProperty(m_currentProgressStock, QString(), [this]() { emit currentProgressStockChanged(); });

    if (success) {
        updateCleanStats(m_cleanInputRecordCount > 0 ? m_cleanInputRecordCount : cleanedData.size(), cleanedData.size());
        // 保存清洗后的数据
        m_fetchedData = cleanedData;
        emit fetchedDataChanged();
        
        // 更新PreviewDataModel - 在C++中直接更新模型，不传递数据给QML
        if (m_previewModel) {
            const QVector<QVariantMap> dataVector = buildStockSummaryPreviewData(cleanedData);
            m_previewModel->updateData(dataVector);
        } else {
            qWarning() << "DataFetchController::onDataCleaningCompleted: PreviewModel is null, cannot update";
        }
        
        // 更新状态
        updateStatus(message, 85);

        DataServiceCache& cache = DataServiceCache::getInstance();
        if (cache.isCacheEnabled() && !cleanedData.isEmpty()) {
            updateBoolProperty(m_operationInProgress, true, [this]() { emit operationInProgressChanged(); });
            updateStringProperty(m_operationPhase, QStringLiteral("缓存结果"), [this]() { emit operationPhaseChanged(); });
            updateStatus("正在缓存清洗结果...", 86);
            emit dataCleaningProgress(86, "正在缓存清洗结果...");

            auto cleanedDataHolder = std::make_shared<QVariantList>(cleanedData);
            const QString currentSymbol = m_currentSymbol;
            const QString currentStartDate = m_currentStartDate;
            const QString currentEndDate = m_currentEndDate;
            const QVariantMap pendingRules = m_pendingRules;
            QString submitError;
            const bool submitted = submitToFoundationThreadPool(this,
                [cleanedDataHolder, currentSymbol, currentStartDate, currentEndDate, pendingRules](DataFetchController* controller) {
                    DataServiceCache& asyncCache = DataServiceCache::getInstance();
                    const DataServiceCache::DataSetInfo dataSetInfo = buildCleanedDataSetInfo(*cleanedDataHolder,
                                                                                              currentSymbol,
                                                                                              currentStartDate,
                                                                                              currentEndDate,
                                                                                              pendingRules);
                    invokeOnMainThread(controller, [](DataFetchController* controller) {
                        controller->updateStatus(QStringLiteral("正在序列化并写入缓存..."), 88);
                        controller->dataCleaningProgress(88, QStringLiteral("正在序列化并写入缓存..."));
                    });

                    const int dataSetId = asyncCache.storeDataSet(
                        *cleanedDataHolder,
                        dataSetInfo,
                        [controller](int current, int total) {
                            if (total <= 0) {
                                return;
                            }

                            const int progress = 88 + static_cast<int>((static_cast<double>(current) * 11.0) / static_cast<double>(total));
                            invokeOnMainThread(controller, [progress, current, total](DataFetchController* controller) {
                                const QString message = QString("正在缓存清洗结果: %1/%2").arg(current).arg(total);
                                controller->updateStatus(message, progress);
                                controller->dataCleaningProgress(progress, message);
                            });
                        });

                    invokeOnMainThread(controller, [dataSetId](DataFetchController* controller) {
                        updateBoolProperty(controller->m_operationInProgress, false, [controller]() { emit controller->operationInProgressChanged(); });
                        updateStringProperty(controller->m_currentProgressStock, QString(), [controller]() { emit controller->currentProgressStockChanged(); });

                        if (dataSetId > 0) {
                            controller->updateStatus(QStringLiteral("数据清洗和缓存完成"), 100);
                            emit controller->dataCleaningCompleted(true, QStringLiteral("数据清洗和缓存完成"), QVariantList());
                        } else {
                            controller->updateStatus(QStringLiteral("数据清洗完成，但缓存保存失败"), 100);
                            emit controller->dataCleaningCompleted(true, QStringLiteral("数据清洗完成，但缓存保存失败"), QVariantList());
                        }

                        QTimer::singleShot(1200, controller, [controller]() {
                            controller->resetProgressState();
                        });
                    });
                },
                &submitError);

            if (submitted) {
                return;
            }

            qWarning() << "DataFetchController::onDataCleaningCompleted: 无法提交缓存任务:" << submitError;
            updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
            updateStatus(message + "，但缓存任务提交失败: " + submitError, 100);
            emit dataCleaningCompleted(true, message + "，但缓存任务提交失败: " + submitError, QVariantList());
            QTimer::singleShot(1200, this, [this]() {
                resetProgressState();
            });
            return;
        }

        updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
        
        // 转发给QML - 只传递状态信息，不传递数据
        emit dataCleaningCompleted(true, message, QVariantList());
        QTimer::singleShot(1200, this, [this]() {
            resetProgressState();
        });
    } else {
        updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
        updateCleanStats(0, 0);
        // 更新状态
        updateStatus("数据清洗失败: " + message, 0);
        
        // 转发给QML
        emit dataCleaningCompleted(false, message, QVariantList());
        QTimer::singleShot(1200, this, [this]() {
            resetProgressState();
        });
    }
}

// Property setters
void DataFetchController::setDataSource(const QString& source)
{
    if (m_dataSource != source) {
        m_dataSource = source;
        emit dataSourceChanged();
    }
}

void DataFetchController::setSymbols(const QStringList& symbols)
{
    if (m_symbols != symbols) {
        m_symbols = symbols;
        emit symbolsChanged();
    }
}

void DataFetchController::setStartDate(const QString& date)
{
    if (m_startDate != date) {
        m_startDate = date;
        emit startDateChanged();
    }
}

void DataFetchController::setEndDate(const QString& date)
{
    if (m_endDate != date) {
        m_endDate = date;
        emit endDateChanged();
    }
}

void DataFetchController::setDataType(const QString& type)
{
    if (m_dataType != type) {
        m_dataType = type;
        emit dataTypeChanged();
    }
}

void DataFetchController::setPreviewModel(PreviewDataModel* model)
{
    if (m_previewModel != model) {
        if (m_previewModel) {
            // 如果需要，可以清理旧的模型
        }
        m_previewModel = model;
        emit previewModelChanged();
    }
}

// 延迟清洗数据槽函数
void DataFetchController::delayedCleanData()
{
    if (!m_pendingCleanAfterLoad) {
        return;
    }

    DataServiceCache& cache = DataServiceCache::getInstance();
    QVariantList loadedData = cache.getCachedData(m_currentSymbol, m_currentStartDate, m_currentEndDate);
    
    if (loadedData.isEmpty()) {
        updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
        emit dataCleaningCompleted(false, "数据库查询失败，请检查数据库连接和数据", QVariantList());
        return;
    }
    
    updateCleanStats(loadedData.size(), 0);
    m_pendingCleanAfterLoad = false;
    // 转发请求给DataService
    emit requestCleanData(loadedData, m_pendingRules);
}

// 刷新缓存键列表 - 在C++中遍历，通过信号传递结果
void DataFetchController::refreshCacheKeys()
{
    // 获取DataServiceCache实例
    DataServiceCache& cache = DataServiceCache::getInstance();
    
    // 确保缓存已初始化
    if (!cache.isCacheEnabled()) {
        if (!cache.initializeCache()) {
            qWarning() << "Failed to initialize cache";
            emit cacheKeysRefreshed(QVariantList());
            return;
        }
    }
    
    // 获取所有缓存键 - 在C++中遍历，不传递给QML遍历
    QStringList cacheKeys = cache.getAllDataKeys();
    
    // 转换为QVariantList
    QVariantList result;
    for (const QString& key : cacheKeys) {
        result.append(key);
    }
    
    // 通过信号传递结果给QML
    emit cacheKeysRefreshed(result);
}

// 刷新数据集信息 - 在C++中遍历，通过信号传递结果
void DataFetchController::refreshDataSetInfos()
{
    // 获取DataServiceCache实例
    DataServiceCache& cache = DataServiceCache::getInstance();
    
    // 获取所有数据集信息 - 在C++中遍历，不传递给QML遍历
    auto dataSetInfos = cache.getAllDataSetInfos();
    
    // 转换为QVariantList
    QVariantList result;
    for (const DataServiceCache::DataSetInfo& info : dataSetInfos) {
        QVariantMap map;
        map["id"] = info.id;
        map["displayName"] = info.displayName;
        map["description"] = info.description;
        map["sourceType"] = info.sourceType;
        map["createdTime"] = info.createdTime.toString("yyyy-MM-dd HH:mm:ss");
        map["rowCount"] = info.rowCount;
        map["stockCodes"] = info.stockCodes;
        map["startDate"] = info.startDate.isValid() ? info.startDate.toString("yyyy-MM-dd") : "";
        map["endDate"] = info.endDate.isValid() ? info.endDate.toString("yyyy-MM-dd") : "";
        map["tags"] = info.tags;
        result.append(map);
    }
    
    // 通过信号传递结果给QML
    emit dataSetInfosRefreshed(result);
}

// 辅助函数：增强缓存数据获取
QVariantList DataFetchController::getDataFromCacheEnhanced(DataServiceCache& cache, const QString& key)
{
    // 1. 首先尝试直接通过DataServiceCache获取
    QVariantList data = cache.getData(key);
    
    if (!data.isEmpty()) {
        return data;
    }
    
    // 2. 如果失败，检查是否为data:stock:格式的键，尝试解析参数调用getCachedData
    if (key.startsWith("data:stock:")) {
        // 解析格式：data:stock:[symbol]_[startDate]_[endDate]
        QString suffix = key.mid(11); // 移除"data:stock:"前缀
        QStringList parts = suffix.split('_');
        if (parts.size() >= 3) {
            QString symbol = parts[0];
            QString startDate = parts[1];
            QString endDate = parts[2];
            
            // 对于"ALL"符号，需要转换为空字符串以匹配getCachedData的预期
            if (symbol == "ALL") {
                symbol = "";
            }

            data = cache.getCachedData(symbol, startDate, endDate);
            if (!data.isEmpty()) {
                return data;
            }
        }
    }
    
    // 3. 尝试通过数据集ID获取（如果键是数字字符串）
    bool ok = false;
    int dataId = key.toInt(&ok);
    if (ok && dataId > 0) {
        data = cache.getDataSetById(dataId);
        if (!data.isEmpty()) {
            return data;
        }
    }

    return QVariantList();
}

void DataFetchController::cleanDataFromDataSetId(int dataId, const QVariantMap& rules)
{
    updateBoolProperty(m_operationInProgress, true, [this]() { emit operationInProgressChanged(); });
    updateStringProperty(m_operationPhase, QStringLiteral("清洗数据"), [this]() { emit operationPhaseChanged(); });

    if (dataId <= 0) {
        updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
        emit dataCleaningCompleted(false, "无效的数据集ID", QVariantList());
        return;
    }

    DataServiceCache& cache = DataServiceCache::getInstance();
    QVariantList dataToClean = cache.getDataSetById(dataId);
    if (dataToClean.isEmpty()) {
        updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
        emit dataCleaningCompleted(false, QString("找不到数据集 %1 对应的数据").arg(dataId), QVariantList());
        return;
    }

    DataServiceCache::DataSetInfo info = cache.getDataSetInfo(dataId);
    const QString dataSourceName = info.displayName.isEmpty()
        ? QString("dataset:%1").arg(dataId)
        : info.displayName;

    updateCleanStats(dataToClean.size(), 0);
    updateStatus(QString("正在清洗数据: %1").arg(dataSourceName), 0);
    emit requestCleanData(dataToClean, rules);
}

void DataFetchController::cleanDataFromCacheKey(const QString& cacheKey, const QVariantMap& rules)
{
    updateBoolProperty(m_operationInProgress, true, [this]() { emit operationInProgressChanged(); });
    updateStringProperty(m_operationPhase, QStringLiteral("清洗数据"), [this]() { emit operationPhaseChanged(); });

    if (cacheKey.isEmpty()) {
        updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
        emit dataCleaningCompleted(false, "缓存键为空", QVariantList());
        return;
    }

    DataServiceCache& cache = DataServiceCache::getInstance();
    QVariantList dataToClean = getDataFromCacheEnhanced(cache, cacheKey);
    if (dataToClean.isEmpty()) {
        updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
        emit dataCleaningCompleted(false, QString("找不到缓存 %1 对应的数据").arg(cacheKey), QVariantList());
        return;
    }

    updateCleanStats(dataToClean.size(), 0);
    updateStatus(QString("正在清洗数据: %1").arg(cacheKey), 0);
    emit requestCleanData(dataToClean, rules);
}

// 通过索引清洗缓存数据
void DataFetchController::cleanDataFromCacheByIndex(int cacheIndex, const QVariantMap& rules)
{
    updateBoolProperty(m_operationInProgress, true, [this]() { emit operationInProgressChanged(); });
    updateStringProperty(m_operationPhase, QStringLiteral("清洗数据"), [this]() { emit operationPhaseChanged(); });
    updateStringProperty(m_currentProgressStock, QString(), [this]() { emit currentProgressStockChanged(); });
    emit dataCleaningStarted();

    if (cacheIndex < 0) {
        updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
        emit dataCleaningCompleted(false, "无效的缓存索引", QVariantList());
        return;
    }
    
    // 获取所有缓存信息
    DataServiceCache& cache = DataServiceCache::getInstance();
    QList<DataServiceCache::DataSetInfo> dataSetInfos = cache.getAllDataSetInfos();
    QStringList rawCacheKeys = cache.getAllDataKeys();
    QStringList cacheKeys;
    QStringList representedCacheKeys;

    for (const DataServiceCache::DataSetInfo& info : dataSetInfos) {
        if (!info.displayName.isEmpty()) {
            representedCacheKeys.append(info.displayName);
        }
        if (info.description.startsWith("从缓存存储的数据: ")) {
            representedCacheKeys.append(info.description.mid(QString("从缓存存储的数据: ").size()));
        }
        if (info.description.startsWith("从通用缓存存储的数据: ")) {
            representedCacheKeys.append(info.description.mid(QString("从通用缓存存储的数据: ").size()));
        }
    }

    for (const QString& key : rawCacheKeys) {
        if (!representedCacheKeys.contains(key)) {
            cacheKeys.append(key);
        }
    }
    
    // 计算实际索引对应的数据
    int currentIndex = 0;
    QVariantList dataToClean;
    QString dataSourceName;
    
    // 首先检查数据集信息
    for (const DataServiceCache::DataSetInfo& info : dataSetInfos) {
        if (currentIndex == cacheIndex) {
            cleanDataFromDataSetId(info.id, rules);
            return;
        }
        currentIndex++;
    }
    
    // 如果未在数据集中找到，检查缓存键
    if (dataToClean.isEmpty()) {
        for (const QString& key : cacheKeys) {
            if (currentIndex == cacheIndex) {
                cleanDataFromCacheKey(key, rules);
                return;
            }
            currentIndex++;
        }
    }

    updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
    emit dataCleaningCompleted(false, QString("找不到索引 %1 对应的缓存数据").arg(cacheIndex), QVariantList());
}
// 通用数据获取方法（单选）转发实现
void DataFetchController::fetchDataByType(const QString& dataSource,
                                         const QString& symbol,
                                         const QString& dataType,
                                         const QString& startDate,
                                         const QString& endDate,
                                         const QVariantMap& options) {
    // 参数检查
    if (startDate.isEmpty() || endDate.isEmpty()) {
        updateStatus("开始日期和结束日期不能为空", 0);
        emit dataFetchError("日期未设置");
        return;
    }
    
    // 更新状态
    m_isFetching = true;
    m_progress = 0;
    m_fetchedData.clear();
    updateBoolProperty(m_operationInProgress, true, [this]() { emit operationInProgressChanged(); });
    updateStringProperty(m_operationPhase, QStringLiteral("获取数据"), [this]() { emit operationPhaseChanged(); });
    updateStringProperty(m_currentProgressStock, QString(), [this]() { emit currentProgressStockChanged(); });
    
    emit isFetchingChanged();
    emit progressChanged();
    emit fetchedDataChanged();
    
    updateStatus(QString("开始获取%1数据...").arg(dataType), 0);
    
    // 保存当前参数
    m_currentSymbol = symbol;
    m_currentStartDate = startDate;
    m_currentEndDate = endDate;
    m_serviceAlreadyCachedCurrentRequest = true;
    
    // 直接调用DataService的方法
    m_dataService->fetchDataByType(dataSource, symbol, dataType, startDate, endDate, options);
}