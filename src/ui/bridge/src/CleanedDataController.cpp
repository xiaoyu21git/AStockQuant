#include "CleanedDataController.h"
#include "DataServiceCache.h"
#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QDate>

#include <thread>

namespace {

QString resolveDatasetTradeDate(const QVariantMap& row)
{
    return row.value(QStringLiteral("trade_date")).toString().trimmed().left(10);
}

bool fieldRequiresPositiveValues(const QString& field)
{
    static const QSet<QString> positiveFields = {
        "open", "high", "low", "close", "volume", "turnover",
        "pe_ratio", "pb_ratio", "market_cap", "circulating_market_cap"
    };
    return positiveFields.contains(field.trimmed().toLower());
}

QVariantMap buildFieldDiagnosticsImpl(const QVariantList& data,
                                     const QVariantMap& datasetInfo,
                                     int* tradeDateCountOut = nullptr)
{
    QVariantMap diagnostics;
    QString latestTradeDate = datasetInfo.value("endDate").toString().trimmed();
    QSet<QString> tradeDates;

    if (latestTradeDate.isEmpty()) {
        for (const QVariant& item : data) {
            if (!item.canConvert<QVariantMap>()) {
                continue;
            }
            const QString tradeDate = resolveDatasetTradeDate(item.toMap());
            if (!tradeDate.isEmpty() && (latestTradeDate.isEmpty() || tradeDate > latestTradeDate)) {
                latestTradeDate = tradeDate;
            }
        }
    }

    QHash<QString, int> nonNullCounts;
    QHash<QString, int> positiveCounts;
    QHash<QString, int> latestDateNonNullCounts;
    QHash<QString, int> latestDatePositiveCounts;

    for (const QVariant& item : data) {
        if (!item.canConvert<QVariantMap>()) {
            continue;
        }

        const QVariantMap row = item.toMap();
        const QString tradeDate = resolveDatasetTradeDate(row);
        const bool onLatestDate = !latestTradeDate.isEmpty() && tradeDate == latestTradeDate;
        if (!tradeDate.isEmpty()) {
            tradeDates.insert(tradeDate);
        }

        for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
            const QString field = it.key().trimmed();
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
            if (ok && numericValue > 0.0) {
                positiveCounts[field] += 1;
                if (onLatestDate) {
                    latestDatePositiveCounts[field] += 1;
                }
            }
        }
    }

    if (tradeDateCountOut) {
        *tradeDateCountOut = tradeDates.size();
    }

    const QVariantList availableFields = datasetInfo.value("availableFields").toList();
    for (const QVariant& fieldVariant : availableFields) {
        const QString field = fieldVariant.toString().trimmed();
        if (field.isEmpty()) {
            continue;
        }

        QVariantMap fieldInfo;
        fieldInfo["latestTradeDate"] = latestTradeDate;
        fieldInfo["nonNullCount"] = nonNullCounts.value(field, 0);
        fieldInfo["positiveCount"] = positiveCounts.value(field, 0);
        fieldInfo["latestDateNonNullCount"] = latestDateNonNullCounts.value(field, 0);
        fieldInfo["latestDatePositiveCount"] = latestDatePositiveCounts.value(field, 0);
        fieldInfo["requiresPositiveValues"] = fieldRequiresPositiveValues(field);
        diagnostics[field] = fieldInfo;
    }

    return diagnostics;
}

bool isCleanedDatasetInfo(const ::DataServiceCache::DataSetInfo& info)
{
    const QStringList tags = info.tags;
    if (tags.contains("cleaned") || tags.contains("清洗后")
            || tags.contains("data_cleaned") || tags.contains("cleaning_result")) {
        return true;
    }

    if (info.description.contains(QStringLiteral("清洗"), Qt::CaseInsensitive)) {
        return true;
    }

    return info.sourceType.contains(QStringLiteral("cleaning"), Qt::CaseInsensitive);
}

bool isBacktestSelectableDatasetInfo(const ::DataServiceCache::DataSetInfo& info)
{
    if (info.id <= 0) {
        return false;
    }

    if (info.isBacktestReady) {
        return true;
    }

    return !info.availableFields.isEmpty() && !info.stockCodes.isEmpty();
}

}

namespace ui::bridge {

CleanedDataController::CleanedDataController(QObject* parent)
    : QObject(parent)
    , m_initialized(false)
    , m_loading(false)
    , m_currentDatasetId(-1)
    , m_cache(nullptr)
{
    qDebug() << "CleanedDataController: Created";
}

CleanedDataController::~CleanedDataController()
{
    qDebug() << "CleanedDataController: Destroyed";
}

bool CleanedDataController::initialize()
{
    if (m_initialized) {
        qDebug() << "CleanedDataController: Already initialized";
        emit initializationCompleted(true);
        return true;
    }
    
    updateLoadingState(true);
    
    try {
        qDebug() << "CleanedDataController: Initializing...";
        
        // 获取缓存实例
        m_cache = &::DataServiceCache::getInstance();
        
        // 检查缓存是否已初始化
        if (!m_cache->isCacheEnabled()) {
            qWarning() << "CleanedDataController: Cache is not enabled";
            updateLoadingState(false);
            emit errorOccurred("缓存系统未启用");
            emit initializationCompleted(false);
            return false;
        }
        
        m_initialized = true;

        QObject::connect(
            m_cache,
            &::DataServiceCache::dataSetStored,
            this,
            [this](int, const ::DataServiceCache::DataSetInfo&) {
                if (!m_initialized) {
                    return;
                }
                refreshDatasets();
            }
        );
        
        // 刷新数据集列表
        refreshDatasets();
        
        qDebug() << "✅ CleanedDataController: Initialized successfully";
        
        emit availabilityChanged(true);
        emit initializationCompleted(true);
        
        return true;
        
    } catch (const std::exception& e) {
        QString error = QString("初始化失败: %1").arg(e.what());
        qCritical() << "CleanedDataController:" << error;
        
        updateLoadingState(false);
        emit errorOccurred(error);
        emit initializationCompleted(false);
        
        return false;
    }
}

void CleanedDataController::refreshDatasets()
{
    if (!m_initialized) {
        qWarning() << "CleanedDataController: Not initialized";
        return;
    }
    
    updateLoadingState(true);
    
    try {
        qDebug() << "CleanedDataController: Refreshing datasets...";
        
        // 从缓存获取所有数据集信息
        QVector<::DataServiceCache::DataSetInfo> allInfos = m_cache->getAllDataSetInfos();
        QVariantList datasets;
        
        qDebug() << "CleanedDataController: Got" << allInfos.size() << "datasets from cache";
        
        for (const ::DataServiceCache::DataSetInfo& info : allInfos) {
            if (!isCleanedDatasetInfo(info)) {
                continue;
            }

            QStringList tags = info.tags;
            QVariantMap dataset;
            dataset["id"] = info.id;
            dataset["name"] = info.displayName;
            dataset["displayName"] = info.displayName;
            dataset["description"] = info.description;
            dataset["symbol"] = info.stockCodes.isEmpty() ? "" : info.stockCodes.first();
            dataset["stockCount"] = info.stockCodes.size();
            dataset["stockCodes"] = info.stockCodes;
            dataset["startDate"] = info.startDate.toString("yyyy-MM-dd");
            dataset["endDate"] = info.endDate.toString("yyyy-MM-dd");
            dataset["recordCount"] = info.rowCount;
            dataset["createdTime"] = info.createdTime.toString(Qt::ISODate);
            dataset["tags"] = tags;
            dataset["schemaVersion"] = info.schemaVersion;
            dataset["isBacktestReady"] = info.isBacktestReady;
            dataset["availableFields"] = info.availableFields;
            dataset["sourceType"] = info.sourceType;
            
            QString cleaningRule = "unknown";
            for (const QString& tag : tags) {
                if (tag.startsWith("rule_") || tag.contains("清洗规则", Qt::CaseInsensitive)) {
                    cleaningRule = tag;
                    break;
                }
            }
            dataset["cleaningRule"] = cleaningRule;
            
            datasets.append(dataset);
        }
        
        updateDatasetList(datasets);
        
        qDebug() << "CleanedDataController: Refreshed" << datasets.size() << "datasets";
        
        updateLoadingState(false);
        
    } catch (const std::exception& e) {
        QString error = QString("刷新数据集失败: %1").arg(e.what());
        qWarning() << "CleanedDataController:" << error;
        
        updateLoadingState(false);
        emit errorOccurred(error);
    }
}

void CleanedDataController::loadCleanedData(const QString& symbol,
                                          const QString& startDate,
                                          const QString& endDate)
{
    if (!m_initialized) {
        qWarning() << "CleanedDataController: Not initialized";
        emit errorOccurred("控制器未初始化");
        return;
    }
    
    updateLoadingState(true);
    
    qDebug() << "CleanedDataController: Loading cleaned data for" 
             << symbol << "from" << startDate << "to" << endDate;
    
    // 更新当前选择
    setCurrentSymbol(symbol);
    setCurrentStartDate(startDate);
    setCurrentEndDate(endDate);
    
    try {
        // 异步加载数据
        QTimer::singleShot(0, [this, symbol, startDate, endDate]() {
            // 首先查找匹配的数据集
            QVariantList datasets = m_datasetList;
            int bestDatasetId = -1;
            
            for (const QVariant& datasetVar : datasets) {
                QVariantMap dataset = datasetVar.toMap();
                QString datasetSymbol = dataset["symbol"].toString();
                
                if (datasetSymbol == symbol || symbol.isEmpty()) {
                    // 检查时间范围
                    QDate datasetStartDate = QDate::fromString(dataset["startDate"].toString(), "yyyy-MM-dd");
                    QDate datasetEndDate = QDate::fromString(dataset["endDate"].toString(), "yyyy-MM-dd");
                    QDate queryStartDate = QDate::fromString(startDate, "yyyy-MM-dd");
                    QDate queryEndDate = QDate::fromString(endDate, "yyyy-MM-dd");
                    
                    bool timeMatch = true;
                    if (queryStartDate.isValid() && datasetStartDate.isValid()) {
                        if (datasetStartDate > queryStartDate) {
                            timeMatch = false;
                        }
                    }
                    if (queryEndDate.isValid() && datasetEndDate.isValid()) {
                        if (datasetEndDate < queryEndDate) {
                            timeMatch = false;
                        }
                    }
                    
                    if (timeMatch) {
                        bestDatasetId = dataset["id"].toInt();
                        break;
                    }
                }
            }
            
            if (bestDatasetId > 0) {
                loadDatasetById(bestDatasetId);
            } else {
                qWarning() << "CleanedDataController: No cleaned data found for" << symbol;
                emit errorOccurred(QString("未找到%1的清洗后数据").arg(symbol));
                updateLoadingState(false);
            }
        });
        
    } catch (const std::exception& e) {
        QString error = QString("加载数据失败: %1").arg(e.what());
        qWarning() << "CleanedDataController:" << error;
        
        updateLoadingState(false);
        emit errorOccurred(error);
    }
}

void CleanedDataController::loadDatasetById(int datasetId)
{
    if (!m_initialized || datasetId <= 0) {
        qWarning() << "CleanedDataController: Invalid dataset ID:" << datasetId;
        emit errorOccurred("无效的数据集ID");
        return;
    }
    const int requestedDatasetId = datasetId;
    updateLoadingState(true);
    m_currentDatasetId = datasetId;

    qDebug() << "CleanedDataController: Loading dataset by ID:" << datasetId;

    try {
        QPointer<CleanedDataController> safeThis(this);
        ::DataServiceCache* cache = m_cache;
        std::thread([safeThis, cache, requestedDatasetId]() {
            if (!safeThis || !cache) {
                return;
            }

            QVariantList data = cache->getDataSetById(requestedDatasetId);
            if (data.isEmpty()) {
                QMetaObject::invokeMethod(
                    safeThis.data(),
                    [safeThis, requestedDatasetId]() {
                        if (!safeThis || safeThis->m_currentDatasetId != requestedDatasetId) {
                            return;
                        }
                        qWarning() << "CleanedDataController: Dataset" << requestedDatasetId << "not found or empty";
                        emit safeThis->errorOccurred(QString("数据集%1未找到或为空").arg(requestedDatasetId));
                        safeThis->updateLoadingState(false);
                    },
                    Qt::QueuedConnection);
                return;
            }

            const auto datasetInfoStruct = cache->getDataSetInfo(requestedDatasetId);
            QVariantMap selectedDatasetInfo;
            selectedDatasetInfo["id"] = datasetInfoStruct.id;
            selectedDatasetInfo["displayName"] = datasetInfoStruct.displayName;
            selectedDatasetInfo["name"] = datasetInfoStruct.displayName;
            selectedDatasetInfo["description"] = datasetInfoStruct.description;
            selectedDatasetInfo["symbol"] = datasetInfoStruct.stockCodes.isEmpty() ? QString() : datasetInfoStruct.stockCodes.first();
            selectedDatasetInfo["stockCount"] = datasetInfoStruct.stockCodes.size();
            selectedDatasetInfo["stockCodes"] = datasetInfoStruct.stockCodes;
            selectedDatasetInfo["startDate"] = datasetInfoStruct.startDate.isValid() ? datasetInfoStruct.startDate.toString("yyyy-MM-dd") : QString();
            selectedDatasetInfo["endDate"] = datasetInfoStruct.endDate.isValid() ? datasetInfoStruct.endDate.toString("yyyy-MM-dd") : QString();
            selectedDatasetInfo["recordCount"] = datasetInfoStruct.rowCount;
            selectedDatasetInfo["createdTime"] = datasetInfoStruct.createdTime.toString(Qt::ISODate);
            selectedDatasetInfo["tags"] = datasetInfoStruct.tags;
            selectedDatasetInfo["schemaVersion"] = datasetInfoStruct.schemaVersion;
            selectedDatasetInfo["isBacktestReady"] = datasetInfoStruct.isBacktestReady;
            selectedDatasetInfo["availableFields"] = datasetInfoStruct.availableFields;

            int tradeDateCount = 0;
            const QVariantMap fieldDiagnostics = buildFieldDiagnosticsImpl(data, selectedDatasetInfo, &tradeDateCount);
            selectedDatasetInfo["fieldDiagnostics"] = fieldDiagnostics;
            selectedDatasetInfo["tradeDateCount"] = tradeDateCount;

            QMetaObject::invokeMethod(
                safeThis.data(),
                [safeThis,
                 requestedDatasetId,
                 data = std::move(data),
                 selectedDatasetInfo = std::move(selectedDatasetInfo),
                 fieldDiagnostics]() mutable {
                    if (!safeThis || safeThis->m_currentDatasetId != requestedDatasetId) {
                        return;
                    }

                    safeThis->m_selectedDatasetInfo = selectedDatasetInfo;
                    safeThis->m_selectedDatasetFieldDiagnostics = fieldDiagnostics;
                    safeThis->m_currentDatasetId = requestedDatasetId;

                    if (safeThis->m_selectedDatasetInfo.contains("symbol")) {
                        safeThis->setCurrentSymbol(safeThis->m_selectedDatasetInfo["symbol"].toString());
                    }
                    if (safeThis->m_selectedDatasetInfo.contains("startDate")) {
                        safeThis->setCurrentStartDate(safeThis->m_selectedDatasetInfo["startDate"].toString());
                    }
                    if (safeThis->m_selectedDatasetInfo.contains("endDate")) {
                        safeThis->setCurrentEndDate(safeThis->m_selectedDatasetInfo["endDate"].toString());
                    }

                    safeThis->m_selectedDatasetInfo["fieldDiagnostics"] = safeThis->m_selectedDatasetFieldDiagnostics;
                    emit safeThis->selectedDatasetDiagnosticsChanged();
                    emit safeThis->selectedDatasetChanged();

                    qDebug() << "CleanedDataController: Loaded dataset" << requestedDatasetId
                             << "with" << data.size() << "records";

                    safeThis->emitDataLoaded(data);
                    safeThis->updateLoadingState(false);
                },
                Qt::QueuedConnection);
        }).detach();

    } catch (const std::exception& e) {
        QString error = QString("加载数据集失败: %1").arg(e.what());
        qWarning() << "CleanedDataController:" << error;

        updateLoadingState(false);
        emit errorOccurred(error);
    }
}

void CleanedDataController::searchDatasets(const QString& symbol,
                                         const QString& startDate,
                                         const QString& endDate,
                                         const QString& cleaningRule,
                                         double minDataQuality)
{
    if (!m_initialized) {
        qWarning() << "CleanedDataController: Not initialized";
        return;
    }
    
    updateLoadingState(true);
    
    qDebug() << "CleanedDataController: Searching datasets with criteria:"
             << "Symbol:" << symbol
             << "Start:" << startDate
             << "End:" << endDate
             << "Rule:" << cleaningRule
             << "MinQuality:" << minDataQuality;
    
    try {
        // 异步搜索
        QTimer::singleShot(0, [this, symbol, startDate, endDate, cleaningRule, minDataQuality]() {
            QVariantList filteredDatasets;
            
            for (const QVariant& datasetVar : m_datasetList) {
                QVariantMap dataset = datasetVar.toMap();
                
                // 检查股票代码
                if (!symbol.isEmpty() && dataset["symbol"].toString() != symbol) {
                    continue;
                }
                
                // 检查开始日期
                if (!startDate.isEmpty()) {
                    QDate queryStartDate = QDate::fromString(startDate, "yyyy-MM-dd");
                    QDate datasetStartDate = QDate::fromString(dataset["startDate"].toString(), "yyyy-MM-dd");
                    if (datasetStartDate.isValid() && datasetStartDate < queryStartDate) {
                        continue;
                    }
                }
                
                // 检查结束日期
                if (!endDate.isEmpty()) {
                    QDate queryEndDate = QDate::fromString(endDate, "yyyy-MM-dd");
                    QDate datasetEndDate = QDate::fromString(dataset["endDate"].toString(), "yyyy-MM-dd");
                    if (datasetEndDate.isValid() && datasetEndDate > queryEndDate) {
                        continue;
                    }
                }
                
                // 检查清洗规则
                if (!cleaningRule.isEmpty() && dataset["cleaningRule"].toString() != cleaningRule) {
                    continue;
                }
                
                filteredDatasets.append(dataset);
            }
            
            qDebug() << "CleanedDataController: Found" << filteredDatasets.size() << "datasets";
            emit datasetsFound(filteredDatasets);
            updateLoadingState(false);
        });
        
    } catch (const std::exception& e) {
        QString error = QString("搜索数据集失败: %1").arg(e.what());
        qWarning() << "CleanedDataController:" << error;
        
        updateLoadingState(false);
        emit errorOccurred(error);
    }
}

void CleanedDataController::clearSelection()
{
    qDebug() << "CleanedDataController: Clearing selection";
    
    m_currentSymbol.clear();
    m_currentStartDate.clear();
    m_currentEndDate.clear();
    m_currentDatasetId = -1;
    m_selectedDatasetInfo.clear();
    m_selectedDatasetFieldDiagnostics.clear();
    
    emit symbolChanged(m_currentSymbol);
    emit startDateChanged(m_currentStartDate);
    emit endDateChanged(m_currentEndDate);
    emit selectedDatasetChanged();
    emit selectedDatasetDiagnosticsChanged();
}

QVariantMap CleanedDataController::getDataDateRange()
{
    QVariantMap result;

    QDate currentDate = QDate::currentDate();
    QDate startDate = currentDate.addYears(-1);
    QDate endDate = currentDate;

    result["startDate"] = startDate.toString("yyyy-MM-dd");
    result["endDate"] = endDate.toString("yyyy-MM-dd");

    if (!m_initialized) {
        qDebug() << "CleanedDataController::getDataDateRange: Not initialized, returning default range:"
                 << result["startDate"].toString() << "to" << result["endDate"].toString();
        return result;
    }

    try {
        if (!m_datasetList.isEmpty()) {
            QString earliestDate;
            QString latestDate;

            for (const QVariant& datasetVar : m_datasetList) {
                QVariantMap dataset = datasetVar.toMap();
                QString datasetStartDate = dataset.value("startDate").toString();
                QString datasetEndDate = dataset.value("endDate").toString();

                if (!datasetStartDate.isEmpty()) {
                    if (earliestDate.isEmpty() || datasetStartDate < earliestDate) {
                        earliestDate = datasetStartDate;
                    }
                }

                if (!datasetEndDate.isEmpty()) {
                    if (latestDate.isEmpty() || datasetEndDate > latestDate) {
                        latestDate = datasetEndDate;
                    }
                }
            }

            if (!earliestDate.isEmpty() && !latestDate.isEmpty()) {
                QDate foundStartDate = QDate::fromString(earliestDate, "yyyy-MM-dd");
                QDate foundEndDate = QDate::fromString(latestDate, "yyyy-MM-dd");

                if (foundStartDate.isValid() && foundEndDate.isValid()) {
                    if (foundStartDate.daysTo(foundEndDate) < 30) {
                        qDebug() << "CleanedDataController::getDataDateRange: Found range too small ("
                                 << foundStartDate.daysTo(foundEndDate) << "days), using default range";
                    } else {
                        result["startDate"] = earliestDate;
                        result["endDate"] = latestDate;
                        qDebug() << "CleanedDataController::getDataDateRange: Found date range from datasets:"
                                 << earliestDate << "to" << latestDate;
                        return result;
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        qWarning() << "CleanedDataController::getDataDateRange: Error:" << e.what();
    }

    qDebug() << "CleanedDataController::getDataDateRange: Using default range:"
             << result["startDate"].toString() << "to" << result["endDate"].toString();
    return result;
}

void CleanedDataController::setCurrentSymbol(const QString& symbol)
{
    if (m_currentSymbol != symbol) {
        m_currentSymbol = symbol;
        qDebug() << "CleanedDataController: Current symbol set to:" << symbol;
        emit symbolChanged(symbol);
    }
}

void CleanedDataController::setCurrentStartDate(const QString& date)
{
    if (m_currentStartDate != date) {
        m_currentStartDate = date;
        qDebug() << "CleanedDataController: Current start date set to:" << date;
        emit startDateChanged(date);
    }
}

void CleanedDataController::setCurrentEndDate(const QString& date)
{
    if (m_currentEndDate != date) {
        m_currentEndDate = date;
        qDebug() << "CleanedDataController: Current end date set to:" << date;
        emit endDateChanged(date);
    }
}

void CleanedDataController::updateLoadingState(bool loading)
{
    if (m_loading != loading) {
        m_loading = loading;
        emit loadingChanged(loading);
    }
}

void CleanedDataController::updateDatasetList(const QVariantList& datasets)
{
    m_datasetList = datasets;
    qDebug() << "CleanedDataController: Updated dataset list with" << datasets.size() << "items";
    
    emit datasetListChanged();
    emit datasetsChanged(datasets.size());
}

void CleanedDataController::updateSelectedDataset(int datasetId)
{
    m_currentDatasetId = datasetId;
    
    // 从数据集列表中查找信息
    for (const QVariant& datasetVar : m_datasetList) {
        QVariantMap dataset = datasetVar.toMap();
        if (dataset["id"].toInt() == datasetId) {
            m_selectedDatasetInfo = dataset;
            
            // 更新当前符号和日期
            if (m_selectedDatasetInfo.contains("symbol")) {
                setCurrentSymbol(m_selectedDatasetInfo["symbol"].toString());
            }
            if (m_selectedDatasetInfo.contains("startDate")) {
                setCurrentStartDate(m_selectedDatasetInfo["startDate"].toString());
            }
            if (m_selectedDatasetInfo.contains("endDate")) {
                setCurrentEndDate(m_selectedDatasetInfo["endDate"].toString());
            }
            
            qDebug() << "CleanedDataController: Selected dataset updated:" << datasetId;
            break;
        }
    }
}

QVariantMap CleanedDataController::buildFieldDiagnostics(const QVariantList& data, const QVariantMap& datasetInfo) const
{
    QVariantMap diagnostics;
    QString latestTradeDate = datasetInfo.value("endDate").toString().trimmed();

    if (latestTradeDate.isEmpty()) {
        for (const QVariant& item : data) {
            if (!item.canConvert<QVariantMap>()) {
                continue;
            }
            const QString tradeDate = resolveDatasetTradeDate(item.toMap());
            if (!tradeDate.isEmpty() && (latestTradeDate.isEmpty() || tradeDate > latestTradeDate)) {
                latestTradeDate = tradeDate;
            }
        }
    }

    QHash<QString, int> nonNullCounts;
    QHash<QString, int> positiveCounts;
    QHash<QString, int> latestDateNonNullCounts;
    QHash<QString, int> latestDatePositiveCounts;

    for (const QVariant& item : data) {
        if (!item.canConvert<QVariantMap>()) {
            continue;
        }

        const QVariantMap row = item.toMap();
        const QString tradeDate = resolveDatasetTradeDate(row);
        const bool onLatestDate = !latestTradeDate.isEmpty() && tradeDate == latestTradeDate;

        for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
            const QString field = it.key().trimmed();
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
            if (ok && numericValue > 0.0) {
                positiveCounts[field] += 1;
                if (onLatestDate) {
                    latestDatePositiveCounts[field] += 1;
                }
            }
        }
    }

    const QVariantList availableFields = datasetInfo.value("availableFields").toList();
    for (const QVariant& fieldVariant : availableFields) {
        const QString field = fieldVariant.toString().trimmed();
        if (field.isEmpty()) {
            continue;
        }

        QVariantMap fieldInfo;
        fieldInfo["latestTradeDate"] = latestTradeDate;
        fieldInfo["nonNullCount"] = nonNullCounts.value(field, 0);
        fieldInfo["positiveCount"] = positiveCounts.value(field, 0);
        fieldInfo["latestDateNonNullCount"] = latestDateNonNullCounts.value(field, 0);
        fieldInfo["latestDatePositiveCount"] = latestDatePositiveCounts.value(field, 0);
        fieldInfo["requiresPositiveValues"] = fieldRequiresPositiveValues(field);
        diagnostics[field] = fieldInfo;
    }

    return diagnostics;
}

void CleanedDataController::emitDataLoaded(const QVariantList& data)
{
    if (m_selectedDatasetInfo.isEmpty()) {
        qWarning() << "CleanedDataController: Refusing to emit dataLoaded without selected dataset info";
        emit errorOccurred(QStringLiteral("数据集元信息为空，禁止发送 dataLoaded"));
        return;
    }

    qDebug() << "CleanedDataController: Emitting data loaded with" << data.size() << "records";
    emit dataLoaded(data, m_selectedDatasetInfo);
}

} // namespace ui::bridge