// DataFetchController.cpp - 改进版本，支持模型和数据缓存
#include "DataFetchController.h"
#include "DataFetchFieldContractUtils.h"
#include "DataService.h"
#include "cleaning/CleaningEngine.h"
#include "DataServiceCache.h"
#include "CacheDetailPreviewModel.h"
#include "PreviewDataModel.h"
#include "foundation.h"

#include <QMetaObject>
#include <QPointer>
#include <QDebug>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>
#include <QUrl>
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

QString describeDataTypeLabel(const QString& dataType)
{
    if (dataType == QStringLiteral("kline_daily")) {
        return QStringLiteral("日线");
    }
    if (dataType == QStringLiteral("kline_weekly")) {
        return QStringLiteral("周线");
    }
    if (dataType == QStringLiteral("kline_monthly")) {
        return QStringLiteral("月线");
    }
    if (dataType == QStringLiteral("minute_data")) {
        return QStringLiteral("分钟");
    }
    if (dataType == QStringLiteral("realtime")) {
        return QStringLiteral("实时");
    }
    if (dataType == QStringLiteral("historical")) {
        return QStringLiteral("历史");
    }
    if (dataType == QStringLiteral("financial")) {
        return QStringLiteral("财务");
    }
    if (dataType == QStringLiteral("news")) {
        return QStringLiteral("舆情");
    }
    if (dataType == QStringLiteral("policy")) {
        return QStringLiteral("政策");
    }
    if (dataType == QStringLiteral("alternative")) {
        return QStringLiteral("另类");
    }
    if (dataType == QStringLiteral("index_constituents")) {
        return QStringLiteral("指数成分");
    }
    if (dataType == QStringLiteral("index_list")) {
        return QStringLiteral("指数列表");
    }
    if (dataType == QStringLiteral("derivatives")) {
        return QStringLiteral("衍生品");
    }
    return dataType;
}

QString resolveExportFilePath(const QString& destination, const QString& format)
{
    QString localPath = destination.trimmed();
    const QUrl url(localPath);
    if (url.isValid() && url.isLocalFile()) {
        localPath = url.toLocalFile();
    }

    if (localPath.isEmpty()) {
        return {};
    }

    QFileInfo fileInfo(localPath);
    if (!fileInfo.suffix().isEmpty()) {
        return localPath;
    }

    const QString normalizedFormat = format.trimmed().toLower();
    if (normalizedFormat == QStringLiteral("csv") || normalizedFormat == QStringLiteral("json")) {
        return localPath + QStringLiteral(".") + normalizedFormat;
    }

    return localPath;
}

QString csvEscapedValue(const QString& text)
{
    QString escaped = text;
    escaped.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    if (escaped.contains(',') || escaped.contains('"') || escaped.contains('\n') || escaped.contains('\r')) {
        return QStringLiteral("\"") + escaped + QStringLiteral("\"");
    }
    return escaped;
}

int computeBatchProgress(int completedCount, int totalCount, int currentProgress)
{
    if (totalCount <= 0) {
        return qBound(0, currentProgress, 100);
    }

    const int clampedCompleted = qBound(0, completedCount, totalCount);
    const int clampedCurrent = qBound(0, currentProgress, 100);
    const double slice = 100.0 / static_cast<double>(totalCount);
    const double progress = static_cast<double>(clampedCompleted) * slice
        + (static_cast<double>(clampedCurrent) * slice / 100.0);
    return qBound(0, static_cast<int>(progress), 100);
}

QString buildBatchCacheKey(const QString& dataSource,
                           const QString& symbol,
                           const QString& startDate,
                           const QString& endDate)
{
    const QString normalizedSource = dataSource.trimmed().isEmpty()
        ? QStringLiteral("all_market")
        : dataSource.trimmed();
    const QString normalizedSymbol = symbol.trimmed().isEmpty()
        ? QStringLiteral("ALL")
        : symbol.trimmed();

    return QStringLiteral("batch_preview:%1:%2:%3:%4:%5")
        .arg(normalizedSource)
        .arg(normalizedSymbol)
        .arg(QStringLiteral("all_types"))
        .arg(startDate.trimmed())
        .arg(endDate.trimmed());
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

QVector<QVariantMap> buildRawPreviewData(const QVariantList& data,
                                         const QString& dataTypeKey,
                                         const QString& sourceLabel);

QVector<QVariantMap> buildDailySummaryPreviewData(const QVariantList& data,
                                                 const QString& dataTypeKey,
                                                 const QString& sourceLabel)
{
    QVector<QVariantMap> previewData;
    QHash<QString, StockPreviewSummary> summaries;
    QStringList symbolOrder;

    for (const QVariant& item : data) {
        if (!item.canConvert<QVariantMap>()) {
            continue;
        }

        const QVariantMap row = item.toMap();
        const QString symbol = resolveSymbol(row).trimmed();
        if (symbol.isEmpty()) {
            continue;
        }

        StockPreviewSummary& summary = summaries[symbol];
        if (summary.symbol.isEmpty()) {
            summary.symbol = symbol;
            symbolOrder.append(symbol);
        }

        const QString name = row.value(
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
                                row.value("名称", symbol))))))).toString().trimmed();
        if (summary.name.isEmpty() && !name.isEmpty()) {
            summary.name = name;
        }

        const QString tradeDate = resolveTradeDate(row);
        if (!tradeDate.isEmpty()) {
            if (summary.startDate.isEmpty() || tradeDate < summary.startDate) {
                summary.startDate = tradeDate;
            }
            if (summary.endDate.isEmpty() || tradeDate > summary.endDate) {
                summary.endDate = tradeDate;
            }
        }

        ++summary.recordCount;

        bool hasClose = false;
        const double closeValue = extractNumericValue(row, {"close", "Close", "price"}, &hasClose);
        if (hasClose) {
            if (!summary.hasFirstClose) {
                summary.firstClose = closeValue;
                summary.hasFirstClose = true;
            }
            summary.latestClose = closeValue;
            summary.hasLatestClose = true;
        }

        bool hasVolume = false;
        const double volumeValue = extractNumericValue(row, {"volume", "Volume"}, &hasVolume);
        if (hasVolume) {
            summary.totalVolume += volumeValue;
        }
    }

    previewData.reserve(symbolOrder.size());
    const QString normalizedDataType = dataTypeKey.trimmed().isEmpty()
        ? QStringLiteral("kline_daily")
        : dataTypeKey.trimmed();
    const QString normalizedSource = sourceLabel.trimmed().isEmpty()
        ? QStringLiteral("日线汇总")
        : sourceLabel.trimmed() + QStringLiteral("汇总");

    for (const QString& symbol : symbolOrder) {
        const StockPreviewSummary summary = summaries.value(symbol);

        QVariantMap previewRow;
        previewRow["code"] = summary.symbol;
        previewRow["symbol"] = summary.symbol;
        previewRow["name"] = summary.name.isEmpty() ? summary.symbol : summary.name;
        previewRow["date"] = summary.endDate.isEmpty() ? summary.startDate : summary.endDate;
        if (!summary.startDate.isEmpty() && !summary.endDate.isEmpty() && summary.startDate != summary.endDate) {
            previewRow["timeRange"] = summary.startDate + QStringLiteral(" ~ ") + summary.endDate;
        } else {
            previewRow["timeRange"] = summary.startDate.isEmpty() ? summary.endDate : summary.startDate;
        }
        previewRow["source"] = normalizedSource;
        previewRow["dataType"] = normalizedDataType;
        previewRow["recordCount"] = summary.recordCount;

        if (summary.hasFirstClose) {
            previewRow["open"] = summary.firstClose;
        }
        if (summary.hasLatestClose) {
            previewRow["close"] = summary.latestClose;
        }
        if (summary.totalVolume > 0.0) {
            previewRow["volume"] = summary.totalVolume;
        }
        if (summary.hasFirstClose && summary.hasLatestClose && summary.firstClose > 0.0) {
            previewRow["change"] = ((summary.latestClose - summary.firstClose) / summary.firstClose) * 100.0;
        }

        previewData.push_back(previewRow);
    }

    return previewData;
}

QVector<QVariantMap> buildPreviewDataForDisplay(const QVariantList& data,
                                                const QStringList& requestedTypes,
                                                const QString& sourceLabel)
{
    QVariantList dailyRows;
    QVariantList otherRows;
    const bool summarizeDaily = requestedTypes.contains(QStringLiteral("kline_daily"));

    for (const QVariant& item : data) {
        if (!item.canConvert<QVariantMap>()) {
            continue;
        }

        const QVariantMap row = item.toMap();
        const QString rowDataType = row.value("dataType", row.value("data_type", row.value("dataSourceType", ""))).toString().trimmed();
        if (summarizeDaily && rowDataType == QStringLiteral("kline_daily")) {
            dailyRows.append(row);
        } else {
            otherRows.append(row);
        }
    }

    QVector<QVariantMap> previewData;
    if (!dailyRows.isEmpty()) {
        const QString dailySourceLabel = sourceLabel.trimmed().isEmpty()
            ? QStringLiteral("日线")
            : sourceLabel.trimmed();
        const QVector<QVariantMap> dailyPreview = buildDailySummaryPreviewData(dailyRows,
                                                                              QStringLiteral("kline_daily"),
                                                                              dailySourceLabel);
        previewData.reserve(dailyPreview.size() + otherRows.size());
        previewData += dailyPreview;
    } else {
        previewData.reserve(otherRows.size());
    }

    const QVector<QVariantMap> otherPreview = buildRawPreviewData(otherRows, QString(), QString());
    previewData += otherPreview;
    return previewData;
}

QVariantList annotateFetchedRows(const QVariantList& data,
                                const QString& dataTypeKey,
                                const QString& sourceLabel)
{
    QVariantList annotatedData;
    annotatedData.reserve(data.size());

    for (const QVariant& item : data) {
        if (!item.canConvert<QVariantMap>()) {
            continue;
        }

        QVariantMap row = item.toMap();
        if (!dataTypeKey.trimmed().isEmpty()) {
            row["dataType"] = dataTypeKey.trimmed();
        }
        if (!sourceLabel.trimmed().isEmpty()) {
            row["source"] = sourceLabel.trimmed();
        } else if (!row.contains("source")) {
            const QString fallbackSource = row.value("dataSource", row.value("type", QString())).toString().trimmed();
            if (!fallbackSource.isEmpty()) {
                row["source"] = fallbackSource;
            }
        }
        annotatedData.append(row);
    }

    return annotatedData;
}

QVector<QVariantMap> buildRawPreviewData(const QVariantList& data,
                                         const QString& dataTypeKey,
                                         const QString& sourceLabel)
{
    QVector<QVariantMap> previewData;
    previewData.reserve(data.size());

    for (const QVariant& item : data) {
        if (!item.canConvert<QVariantMap>()) {
            continue;
        }

        const QVariantMap row = item.toMap();
        const QString symbol = resolveSymbol(row);
        const QString name = row.value(
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
                                row.value("名称", symbol))))))).toString().trimmed();
        const QString tradeDate = resolveTradeDate(row);
        const QString source = sourceLabel.isEmpty()
            ? row.value("source", row.value("dataSource", row.value("type", ""))).toString().trimmed()
            : sourceLabel;
        const QString dataType = !dataTypeKey.trimmed().isEmpty()
            ? dataTypeKey.trimmed()
            : row.value("dataType", row.value("data_type", row.value("dataSourceType", ""))).toString().trimmed();

        QVariantMap previewRow;
        previewRow["code"] = symbol;
        previewRow["symbol"] = symbol;
        previewRow["name"] = name.isEmpty() ? symbol : name;
        previewRow["date"] = tradeDate;
        previewRow["timeRange"] = tradeDate;
        previewRow["source"] = source;
        previewRow["dataType"] = dataType;
        previewRow["recordCount"] = 1;

        bool hasOpen = false;
        const double openValue = extractNumericValue(row, {"open", "Open"}, &hasOpen);
        if (hasOpen) {
            previewRow["open"] = openValue;
        }

        bool hasClose = false;
        const double closeValue = extractNumericValue(row, {"close", "Close", "price"}, &hasClose);
        if (hasClose) {
            previewRow["close"] = closeValue;
        }

        bool hasHigh = false;
        const double highValue = extractNumericValue(row, {"high", "High"}, &hasHigh);
        if (hasHigh) {
            previewRow["high"] = highValue;
        }

        bool hasLow = false;
        const double lowValue = extractNumericValue(row, {"low", "Low"}, &hasLow);
        if (hasLow) {
            previewRow["low"] = lowValue;
        }

        bool hasVolume = false;
        const double volumeValue = extractNumericValue(row, {"volume", "Volume"}, &hasVolume);
        if (hasVolume) {
            previewRow["volume"] = volumeValue;
        }

        bool hasChange = false;
        const double changeValue = extractNumericValue(row, {"change", "Change", "change_pct", "changePercent"}, &hasChange);
        if (hasChange) {
            previewRow["change"] = changeValue;
        } else if (hasOpen && hasClose && openValue > 0.0) {
            previewRow["change"] = ((closeValue - openValue) / openValue) * 100.0;
        }

        previewData.push_back(previewRow);
    }

    return previewData;
}

QStringList collectAvailableFields(const QVariantList& data)
{
    return factor::bridge::collectContractAvailableFields(data);
}

bool hasLatestFullDailyBarFields(const QStringList& availableFields);

DataServiceCache::DataSetInfo buildCleanedDataSetInfo(const QVariantList& cleanedData,
                                                      const QString& currentSymbol,
                                                      const QString& currentStartDate,
                                                      const QString& currentEndDate,
                                                      const QStringList& selectedDataTypes,
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
    dataSetInfo.availableFields = factor::bridge::collectObservedCleanedDataFields(cleanedData);
    dataSetInfo.schemaVersion = 2;
    dataSetInfo.isBacktestReady = hasLatestFullDailyBarFields(dataSetInfo.availableFields);
    dataSetInfo.tags = QStringList{"cleaned", "cleaning_result"};
    dataSetInfo.tags.append(factor::bridge::buildSelectedDataTypeTags(selectedDataTypes, cleanedData));
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
    const QSet<QString> fieldSet(availableFields.begin(), availableFields.end());
    for (const QString& field : factor::bridge::marketBarBacktestReadyFields()) {
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
    , m_cachePreviewModel(new PreviewDataModel(this))
    , m_cacheDetailPreviewModel(new CacheDetailPreviewModel(this))
{
    // 设置默认日期（最近30天）
    QDateTime currentDate = QDateTime::currentDateTime();
    QDateTime startDate = currentDate.addDays(-30);
    
    m_startDate = startDate.toString("yyyy-MM-dd");
    m_endDate = currentDate.toString("yyyy-MM-dd");
    
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

QString DataFetchController::buildBatchCacheKey(const QString& dataSource,
                                               const QString& symbol,
                                               const QString& startDate,
                                               const QString& endDate) const
{
    return ::buildBatchCacheKey(dataSource, symbol, startDate, endDate);
}

void DataFetchController::refreshCacheState()
{
    refreshCacheKeys();
    refreshDataSetInfos();
}

void DataFetchController::logInitMessage()
{
}

void DataFetchController::fetchDataTypesBySource(const QString& dataSource,
                                                 const QString& symbol,
                                                 const QStringList& dataTypes,
                                                 const QString& startDate,
                                                 const QString& endDate,
                                                 const QVariantMap& options)
{
    if (startDate.isEmpty() || endDate.isEmpty()) {
        updateStatus("开始日期和结束日期不能为空", 0);
        emit dataFetchError("日期未设置");
        return;
    }

    if (dataTypes.isEmpty()) {
        updateStatus("请选择至少一种数据类型", 0);
        emit dataFetchError("未选择数据类型");
        return;
    }

    if (m_batchFetchInProgress) {
        updateStatus("已有数据源查询正在进行", 0);
        emit dataFetchError("批量查询进行中");
        return;
    }

    m_batchFetchInProgress = true;
    m_batchFetchHadFailure = false;
    m_pendingFetchDataTypes = dataTypes;
    m_activeBatchDataTypes = dataTypes;
    m_currentSelectedDataTypes = dataTypes;
    m_batchFetchTotalCount = dataTypes.size();
    m_batchFetchCompletedCount = 0;
    m_activeBatchDataSource = dataSource;
    m_activeBatchSymbol = symbol;
    m_activeBatchStartDate = startDate;
    m_activeBatchEndDate = endDate;
    m_activeBatchOptions = options;
    m_activeBatchCacheKey = this->buildBatchCacheKey(dataSource, symbol, startDate, endDate);

    {
        DataServiceCache& cache = DataServiceCache::getInstance();
        const QVariantList cachedData = cache.getData(m_activeBatchCacheKey);
        if (!cachedData.isEmpty()) {
            const QVariantList annotatedData = annotateFetchedRows(cachedData, QString(), QString());
            m_fetchedData = annotatedData;
            emit fetchedDataChanged();

            if (m_previewModel) {
                ++m_previewBuildGeneration;
                const QVector<QVariantMap> dataVector = buildPreviewDataForDisplay(annotatedData, m_activeBatchDataTypes, QString());
                m_previewModel->updateData(dataVector);
            }

            m_batchFetchInProgress = false;
            m_batchFetchHadFailure = false;
            m_pendingFetchDataTypes.clear();
            m_activeBatchDataSource.clear();
            m_activeBatchSymbol.clear();
            m_activeBatchStartDate.clear();
            m_activeBatchEndDate.clear();
            m_activeBatchOptions.clear();
            m_activeBatchDataType.clear();
            m_activeBatchDataTypes.clear();
            m_activeBatchCacheKey.clear();

            m_isFetching = false;
            emit isFetchingChanged();
            updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
            updateStatus(QStringLiteral("使用缓存数据"), 100);
            refreshCacheState();
            return;
        }
    }

    m_isFetching = true;
    m_progress = 0;
    m_fetchedData.clear();
    if (m_previewModel) {
        m_previewModel->clearData();
    }
    updateBoolProperty(m_operationInProgress, true, [this]() { emit operationInProgressChanged(); });
    updateStringProperty(m_operationPhase, QStringLiteral("获取数据"), [this]() { emit operationPhaseChanged(); });
    updateStringProperty(m_currentProgressStock, QString(), [this]() { emit currentProgressStockChanged(); });

    emit isFetchingChanged();
    emit progressChanged();
    emit fetchedDataChanged();

    m_currentSymbol = symbol;
    m_currentStartDate = startDate;
    m_currentEndDate = endDate;
    m_serviceAlreadyCachedCurrentRequest = true;

    startNextBatchFetch();
}

void DataFetchController::startNextBatchFetch()
{
    if (!m_batchFetchInProgress) {
        return;
    }

    if (m_pendingFetchDataTypes.isEmpty()) {
        finishBatchFetch();
        return;
    }

    m_activeBatchDataType = m_pendingFetchDataTypes.takeFirst();
    updateStatus(QString("开始获取%1数据...").arg(describeDataTypeLabel(m_activeBatchDataType)), 0);
    m_dataService->fetchDataByType(m_activeBatchDataSource,
                                   m_activeBatchSymbol,
                                   m_activeBatchDataType,
                                   m_activeBatchStartDate,
                                   m_activeBatchEndDate,
                                   m_activeBatchOptions);
}

void DataFetchController::finishBatchFetch()
{
    const bool hadFailure = m_batchFetchHadFailure;

    if (!hadFailure && !m_activeBatchCacheKey.isEmpty() && !m_fetchedData.isEmpty()) {
        DataServiceCache::getInstance().storeData(m_activeBatchCacheKey, m_fetchedData);
    }

    m_batchFetchInProgress = false;
    m_batchFetchHadFailure = false;
    m_pendingFetchDataTypes.clear();
    m_activeBatchDataSource.clear();
    m_activeBatchSymbol.clear();
    m_activeBatchStartDate.clear();
    m_activeBatchEndDate.clear();
    m_activeBatchOptions.clear();
    m_activeBatchDataType.clear();
    m_activeBatchDataTypes.clear();
    m_activeBatchCacheKey.clear();
    m_batchFetchTotalCount = 0;
    m_batchFetchCompletedCount = 0;

    m_isFetching = false;
    emit isFetchingChanged();

    if (hadFailure) {
        updateStatus(QStringLiteral("数据获取完成，部分数据源失败"), 100);
    } else {
        updateStatus(QStringLiteral("数据获取完成"), 100);
    }

    refreshCacheState();
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

    if (m_fetchedData.isEmpty()) {
        updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
        emit dataCleaningCompleted(false, "没有可用的数据集，请先加载数据后再清洗", QVariantList());
        return;
    }

    updateStatus("正在使用当前数据集进行清洗...", 0);
    emit requestCleanData(m_fetchedData, rules);
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

    const int effectiveProgress = m_batchFetchInProgress
        ? computeBatchProgress(m_batchFetchCompletedCount, m_batchFetchTotalCount, progress)
        : progress;
    const QString effectiveMessage = (m_batchFetchInProgress && m_batchFetchTotalCount > 1)
        ? QString("[%1/%2] %3").arg(m_batchFetchCompletedCount + 1).arg(m_batchFetchTotalCount).arg(message)
        : message;

    // 更新状态
    updateStatus(effectiveMessage, effectiveProgress);
    
    // 转发给QML
    emit dataFetchProgress(effectiveProgress, effectiveMessage);
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
        const QString batchDataType = m_batchFetchInProgress ? m_activeBatchDataType : QString();
        const QString batchSourceLabel = batchDataType.isEmpty() ? QString() : describeDataTypeLabel(batchDataType);
        const QVariantList annotatedData = annotateFetchedRows(data, batchDataType, batchSourceLabel);

        if (m_fetchedData.isEmpty()) {
            m_fetchedData = annotatedData;
        } else {
            m_fetchedData.append(annotatedData);
        }
        emit fetchedDataChanged();
        
        // 更新PreviewDataModel - 在C++中直接更新模型，不传递数据给QML
        if (m_previewModel) {
            ++m_previewBuildGeneration;
            const QVector<QVariantMap> dataVector = buildPreviewDataForDisplay(annotatedData, m_activeBatchDataTypes, batchSourceLabel);
            if (m_previewModel->count() == 0) {
                m_previewModel->updateData(dataVector);
            } else {
                m_previewModel->addDataBatch(dataVector);
            }

        } else {
            qWarning() << "DataFetchController::onDataLoadCompleted: PreviewModel is null, cannot update";
        }

        if (!m_batchFetchInProgress) {
            refreshCacheState();
        } else {
            refreshDataSetInfos();
        }

        if (m_batchFetchInProgress) {
            m_batchFetchCompletedCount = qMin(m_batchFetchCompletedCount + 1, m_batchFetchTotalCount);
        }
        
        if (continueCleaning) {
            updateStringProperty(m_operationPhase, QStringLiteral("清洗数据"), [this]() { emit operationPhaseChanged(); });
            updateCleanStats(annotatedData.size(), 0);
            updateStatus("数据加载完成，开始清洗...",
                         m_batchFetchInProgress
                             ? computeBatchProgress(m_batchFetchCompletedCount, m_batchFetchTotalCount, 100)
                             : 25);
            m_pendingCleanAfterLoad = false;
            emit requestCleanData(annotatedData, m_pendingRules);
        } else if (!m_batchFetchInProgress) {
            // 单次获取结束时再收尾；批量模式由 finishBatchFetch() 统一收尾
            updateStatus(message, 100);
            if (m_isFetching) {
                m_isFetching = false;
                emit isFetchingChanged();
            }
        }

        if (m_batchFetchInProgress) {
            if (m_pendingFetchDataTypes.isEmpty()) {
                finishBatchFetch();
            } else {
                startNextBatchFetch();
            }
        }
        
    } else {
        // 更新状态
        if (m_batchFetchInProgress) {
            m_batchFetchCompletedCount = qMin(m_batchFetchCompletedCount + 1, m_batchFetchTotalCount);
            updateStatus("数据加载失败: " + message,
                         computeBatchProgress(m_batchFetchCompletedCount, m_batchFetchTotalCount, 100));
        } else {
            updateStatus("数据加载失败: " + message, 0);
        }
        m_batchFetchHadFailure = true;
        if (m_isFetching && !m_batchFetchInProgress) {
            m_isFetching = false;
            emit isFetchingChanged();
        }

        if (m_batchFetchInProgress) {
            if (m_pendingFetchDataTypes.isEmpty()) {
                finishBatchFetch();
            } else {
                startNextBatchFetch();
            }
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
    if (m_batchFetchInProgress) {
        m_batchFetchHadFailure = true;
        m_batchFetchCompletedCount = qMin(m_batchFetchCompletedCount + 1, m_batchFetchTotalCount);
        if (m_pendingFetchDataTypes.isEmpty()) {
            finishBatchFetch();
        } else {
            updateStatus(QStringLiteral("数据加载错误: %1").arg(error),
                         computeBatchProgress(m_batchFetchCompletedCount, m_batchFetchTotalCount, 100));
            startNextBatchFetch();
        }
    } else if (m_isFetching) {
        m_isFetching = false;
        emit isFetchingChanged();
    }
    
    // 更新状态
    if (!m_batchFetchInProgress) {
        updateStatus("数据加载错误: " + error, 0);
    }
    
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
        auto cleanedDataHolder = std::make_shared<QVariantList>(cleanedData);
        
        // 预览构建只做数据转换，放到后台线程，避免阻塞清洗完成关键路径。
        if (m_previewModel) {
            const quint64 previewBuildGeneration = ++m_previewBuildGeneration;
            QString previewSubmitError;
            const bool previewSubmitted = submitToFoundationThreadPool(this,
                [cleanedDataHolder, previewBuildGeneration](DataFetchController* controller) {
                    auto previewData = std::make_shared<QVector<QVariantMap>>(
                        buildRawPreviewData(*cleanedDataHolder, QStringLiteral("清洗结果"), QString()));
                    invokeOnMainThread(controller,
                                       [previewData, previewBuildGeneration](DataFetchController* controller) {
                                           if (controller->m_previewBuildGeneration != previewBuildGeneration) {
                                               return;
                                           }
                                           if (!controller->m_previewModel) {
                                               qWarning() << "DataFetchController::onDataCleaningCompleted: PreviewModel became null before async update";
                                               return;
                                           }
                                           controller->m_previewModel->updateData(*previewData);
                                       });
                },
                &previewSubmitError);
            if (!previewSubmitted) {
                qWarning() << "DataFetchController::onDataCleaningCompleted: 无法提交预览构建任务:" << previewSubmitError;
            }
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
                                                                                              controller->m_currentSelectedDataTypes,
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

void DataFetchController::setCachePreviewModel(PreviewDataModel* model)
{
    if (m_cachePreviewModel == model) {
        return;
    }

    m_cachePreviewModel = model;
    emit cachePreviewModelChanged();
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

bool DataFetchController::resolveCacheEntryByIndex(int cacheIndex,
                                                  QString& cacheKey,
                                                  int& dataId,
                                                  QString& displayName) const
{
    cacheKey.clear();
    dataId = -1;
    displayName.clear();

    if (cacheIndex < 0) {
        return false;
    }

    DataServiceCache& cache = DataServiceCache::getInstance();
    const QList<DataServiceCache::DataSetInfo> dataSetInfos = cache.getAllDataSetInfos();
    const QStringList rawCacheKeys = cache.getAllDataKeys();

    QStringList representedCacheKeys;
    for (const DataServiceCache::DataSetInfo& info : dataSetInfos) {
        if (!info.displayName.isEmpty()) {
            representedCacheKeys.append(info.displayName);
        }
        if (info.description.startsWith(QStringLiteral("从缓存存储的数据: "))) {
            representedCacheKeys.append(info.description.mid(QStringLiteral("从缓存存储的数据: ").size()));
        }
        if (info.description.startsWith(QStringLiteral("从通用缓存存储的数据: "))) {
            representedCacheKeys.append(info.description.mid(QStringLiteral("从通用缓存存储的数据: ").size()));
        }
    }

    int currentIndex = 0;
    for (const DataServiceCache::DataSetInfo& info : dataSetInfos) {
        if (currentIndex == cacheIndex) {
            dataId = info.id;
            displayName = info.displayName;
            return true;
        }
        ++currentIndex;
    }

    for (const QString& key : rawCacheKeys) {
        if (representedCacheKeys.contains(key)) {
            continue;
        }
        if (currentIndex == cacheIndex) {
            cacheKey = key;
            displayName = key;
            return true;
        }
        ++currentIndex;
    }

    return false;
}

void DataFetchController::clearAllCache()
{
    DataServiceCache& cache = DataServiceCache::getInstance();
    if (!cache.isCacheEnabled() && !cache.initializeCache()) {
        qWarning() << "DataFetchController::clearAllCache: Failed to initialize cache";
        return;
    }

    cache.clearAllCache();
    m_fetchedData.clear();
    emit fetchedDataChanged();
    refreshCacheKeys();
    refreshDataSetInfos();
}

void DataFetchController::clearDataCache()
{
    DataServiceCache& cache = DataServiceCache::getInstance();
    if (!cache.isCacheEnabled() && !cache.initializeCache()) {
        qWarning() << "DataFetchController::clearDataCache: Failed to initialize cache";
        return;
    }

    cache.clearDataCache();
    refreshCacheKeys();
    refreshDataSetInfos();
}

void DataFetchController::clearCleaningCache()
{
    DataServiceCache& cache = DataServiceCache::getInstance();
    if (!cache.isCacheEnabled() && !cache.initializeCache()) {
        qWarning() << "DataFetchController::clearCleaningCache: Failed to initialize cache";
        return;
    }

    cache.clearCleaningCache();
    refreshCacheKeys();
    refreshDataSetInfos();
}

void DataFetchController::previewCacheByIndex(int cacheIndex)
{
    DataServiceCache& cache = DataServiceCache::getInstance();
    if (!cache.isCacheEnabled() && !cache.initializeCache()) {
        updateStatus(QStringLiteral("缓存初始化失败，无法预览"), 0);
        if (m_cachePreviewModel) {
            m_cachePreviewModel->clearData();
        }
        if (m_cacheDetailPreviewModel) {
            m_cacheDetailPreviewModel->clearData();
        }
        return;
    }

    if (cacheIndex < 0) {
        if (m_cachePreviewModel) {
            m_cachePreviewModel->clearData();
        }
        if (m_cacheDetailPreviewModel) {
            m_cacheDetailPreviewModel->clearData();
        }
        return;
    }

    QString cacheKey;
    int dataId = -1;
    QString displayName;
    if (!resolveCacheEntryByIndex(cacheIndex, cacheKey, dataId, displayName)) {
        if (m_cachePreviewModel) {
            m_cachePreviewModel->clearData();
        }
        if (m_cacheDetailPreviewModel) {
            m_cacheDetailPreviewModel->clearData();
        }
        updateStatus(QStringLiteral("请选择要预览的缓存项"), 0);
        return;
    }

    QVariantList previewSourceData;
    if (dataId > 0) {
        previewSourceData = cache.getDataSetById(dataId);
    } else if (!cacheKey.isEmpty()) {
        previewSourceData = getDataFromCacheEnhanced(cache, cacheKey);
        if (previewSourceData.isEmpty()) {
            previewSourceData = cache.getData(cacheKey);
        }
    }

    if (previewSourceData.isEmpty()) {
        if (m_cachePreviewModel) {
            m_cachePreviewModel->clearData();
        }
        if (m_cacheDetailPreviewModel) {
            m_cacheDetailPreviewModel->clearData();
        }
        updateStatus(QStringLiteral("缓存项没有可预览的数据"), 0);
        return;
    }

    // 缓存管理页需要查看缓存中的原始行内容，而不是复用数据页的摘要预览。
    // 统一走原始预览构造，同时用显示名兜住 source 标签，避免无 source/dataType 的缓存行被模型分类过滤成 0 行。
    QVector<QVariantMap> previewRows = buildRawPreviewData(previewSourceData, QString(), displayName);

    if (m_cachePreviewModel) {
        m_cachePreviewModel->updateData(previewRows);
    }
    if (m_cacheDetailPreviewModel) {
        m_cacheDetailPreviewModel->setSourceData(previewSourceData);
    }

    updateStatus(QStringLiteral("已加载缓存预览: %1").arg(displayName), 100);
}

bool DataFetchController::exportCurrentCacheDetailPreview(const QString& destination, const QString& format)
{
    if (!m_cacheDetailPreviewModel) {
        updateStatus(QStringLiteral("当前没有可导出的缓存预览"), 0);
        return false;
    }

    const QVariantList filteredRows = m_cacheDetailPreviewModel->filteredRows();
    const QVariantList visibleFields = m_cacheDetailPreviewModel->visibleFields();
    if (filteredRows.isEmpty() || visibleFields.isEmpty()) {
        updateStatus(QStringLiteral("当前筛选结果为空，无法导出"), 0);
        return false;
    }

    const QString localPath = resolveExportFilePath(destination, format);
    if (localPath.isEmpty()) {
        updateStatus(QStringLiteral("导出路径无效"), 0);
        return false;
    }

    const QString normalizedFormat = !format.trimmed().isEmpty()
        ? format.trimmed().toLower()
        : QFileInfo(localPath).suffix().trimmed().toLower();
    if (normalizedFormat != QStringLiteral("json") && normalizedFormat != QStringLiteral("csv")) {
        updateStatus(QStringLiteral("仅支持导出 JSON 或 CSV"), 0);
        return false;
    }

    QSaveFile file(localPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        updateStatus(QStringLiteral("导出失败，无法写入文件: %1").arg(localPath), 0);
        return false;
    }

    if (normalizedFormat == QStringLiteral("json")) {
        QVariantList exportRows;
        exportRows.reserve(filteredRows.size());
        for (const QVariant& rowVariant : filteredRows) {
            const QVariantMap row = rowVariant.toMap();
            QVariantMap exportRow;
            for (const QVariant& fieldVariant : visibleFields) {
                const QString fieldName = fieldVariant.toString().trimmed();
                if (!fieldName.isEmpty()) {
                    exportRow.insert(fieldName, row.value(fieldName));
                }
            }
            exportRows.append(exportRow);
        }

        const QJsonDocument document = QJsonDocument::fromVariant(exportRows);
        file.write(document.toJson(QJsonDocument::Indented));
    } else {
        QTextStream stream(&file);
        QStringList header;
        for (const QVariant& fieldVariant : visibleFields) {
            header.append(csvEscapedValue(fieldVariant.toString().trimmed()));
        }
        stream << header.join(',') << '\n';

        for (const QVariant& rowVariant : filteredRows) {
            const QVariantMap row = rowVariant.toMap();
            QStringList values;
            values.reserve(visibleFields.size());
            for (const QVariant& fieldVariant : visibleFields) {
                const QString fieldName = fieldVariant.toString().trimmed();
                const QVariant value = row.value(fieldName);
                values.append(csvEscapedValue(value.isNull() ? QString() : value.toString()));
            }
            stream << values.join(',') << '\n';
        }
        stream.flush();
    }

    if (!file.commit()) {
        updateStatus(QStringLiteral("导出失败，无法提交文件: %1").arg(localPath), 0);
        return false;
    }

    updateStatus(QStringLiteral("已导出当前筛选结果: %1").arg(localPath), 100);
    return true;
}

bool DataFetchController::deleteCacheByIndex(int cacheIndex)
{
    DataServiceCache& cache = DataServiceCache::getInstance();
    if (!cache.isCacheEnabled() && !cache.initializeCache()) {
        updateStatus(QStringLiteral("缓存初始化失败，无法删除"), 0);
        return false;
    }

    QString cacheKey;
    int dataId = -1;
    QString displayName;
    if (!resolveCacheEntryByIndex(cacheIndex, cacheKey, dataId, displayName)) {
        updateStatus(QStringLiteral("请选择要删除的缓存项"), 0);
        return false;
    }

    bool removed = false;
    if (dataId > 0) {
        removed = cache.removeDataSetById(dataId);
    } else if (!cacheKey.isEmpty()) {
        cache.removeData(cacheKey);
        removed = !cache.hasData(cacheKey);
    }

    if (!removed) {
        updateStatus(QStringLiteral("删除缓存失败: %1").arg(displayName), 0);
        return false;
    }

    if (m_cachePreviewModel) {
        m_cachePreviewModel->clearData();
    }
    if (m_cacheDetailPreviewModel) {
        m_cacheDetailPreviewModel->clearData();
    }

    updateStatus(QStringLiteral("已删除缓存: %1").arg(displayName), 100);
    refreshCacheState();
    return true;
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
    m_currentSelectedDataTypes = factor::bridge::extractSelectedDataTypesFromTags(info.tags);
    if (m_currentSelectedDataTypes.isEmpty()) {
        m_currentSelectedDataTypes = factor::bridge::resolveSelectedDataTypes({}, dataToClean);
    }
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

    m_currentSelectedDataTypes = factor::bridge::resolveSelectedDataTypes({}, dataToClean);

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
    emit 0(false, QString("找不到索引 %1 对应的缓存数据").arg(cacheIndex), QVariantList());
}
// 通用数据获取方法（单选）转发实现
void DataFetchController::fetchDataByType(const QString& dataSource,
                                         const QString& symbol,
                                         const QString& dataType,
                                         const QString& startDate,
                                         const QString& endDate,
                                         const QVariantMap& options) {
    fetchDataTypesBySource(dataSource, symbol, QStringList{dataType}, startDate, endDate, options);
}