#include "CleanedDataController.h"
#include "DataCacheAdapter.h"
#include "DataCleaningServiceRefactored.h"
#include "AppStoragePaths.h"
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

bool isCleanedDatasetInfo(const QVariantMap& info)
{
    const QStringList tags = info.value("tags").toStringList();
    if (tags.contains("cleaned") || tags.contains("清洗后")
            || tags.contains("data_cleaned") || tags.contains("cleaning_result")) {
        return true;
    }

    if (info.value("description").toString().contains(QStringLiteral("清洗"), Qt::CaseInsensitive)) {
        return true;
    }

    return info.value("sourceType").toString().contains(QStringLiteral("cleaning"), Qt::CaseInsensitive);
}

bool isBacktestSelectableDatasetInfo(const QVariantMap& info)
{
    if (info.value("id", -1).toInt() <= 0) {
        return false;
    }

    if (info.value("isBacktestReady").toBool()) {
        return true;
    }

    return !info.value("availableFields").toStringList().isEmpty()
        && !info.value("stockCodes").toStringList().isEmpty();
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
    INTERNAL_DEBUG_STREAM << "CleanedDataController: Created";
}

CleanedDataController::~CleanedDataController()
{
    INTERNAL_DEBUG_STREAM << "CleanedDataController: Destroyed";
}

bool CleanedDataController::initialize()
{
    if (m_initialized) {
        INTERNAL_DEBUG_STREAM << "CleanedDataController: Already initialized";
        emit initializationCompleted(true);
        return true;
    }
    
    updateLoadingState(true);
    
    try {
        INTERNAL_DEBUG_STREAM << "CleanedDataController: Initializing...";
        
        // 获取缓存实例，未初始化则自动初始化
        m_cache = &DataCacheAdapter::instance();
        if (!m_cache->isInitialized()) {
            m_cache->initialize(::bridge::storage::persistentDatasetRootDir());
        }
        if (!m_cache->isInitialized()) {
            INTERNAL_WARN_STREAM << "CleanedDataController: Cache failed to initialize";
            updateLoadingState(false);
            emit errorOccurred("缓存系统初始化失败");
            emit initializationCompleted(false);
            return false;
        }
        
        m_initialized = true;

        QObject::connect(
            m_cache,
            &DataCacheAdapter::dataSetStored,
            this,
            [this](int, QVariantMap) {
                if (!m_initialized) return;
                refreshDatasets();
            }
        );
        
        // 监听增量更新完成 → 自动刷新数据集列表
        if (auto* svc = DataCleaningServiceRefactored::instance()) {
            connect(svc, &DataCleaningServiceRefactored::incrementalUpdateFinished,
                    this, [this](int, bool ok, int newRows, const QString&) {
                if (ok && newRows > 0) refreshDatasets();
            });
        }

        // 刷新数据集列表
        refreshDatasets();

        INTERNAL_DEBUG_STREAM << "✅ CleanedDataController: Initialized successfully";
        
        emit availabilityChanged(true);
        emit initializationCompleted(true);
        
        return true;
        
    } catch (const std::exception& e) {
        QString error = QString("初始化失败: %1").arg(e.what());
        INTERNAL_ERROR_STREAM << "CleanedDataController:" << error.toStdString();

        
        updateLoadingState(false);
        emit errorOccurred(error);
        emit initializationCompleted(false);
        
        return false;
    }
}

void CleanedDataController::refreshDatasets()
{
    if (!m_initialized) {
        INTERNAL_WARN_STREAM << "CleanedDataController: Not initialized";
        return;
    }
    
    updateLoadingState(true);
    
    try {
        INTERNAL_DEBUG_STREAM << "CleanedDataController: Refreshing datasets...";
        
        // 从缓存获取所有数据集信息
        QVector<QVariantMap> allInfos = m_cache->getAllDataSetInfos();
        QVariantList datasets;

        INTERNAL_DEBUG_STREAM << "CleanedDataController: Got" << allInfos.size() << "datasets from cache";

        for (const QVariantMap& info : allInfos) {
            // 因子回测页只显示已清洗的缓存
            if (!isCleanedDatasetInfo(info)) {
                continue;
            }
            if (info.value("id", -1).toInt() <= 0) {
                continue;
            }

            QStringList tags = info.value("tags").toStringList();
            QStringList stockCodes = info.value("stockCodes").toStringList();
            QVariantMap dataset;
            dataset["id"] = info.value("id");
            dataset["name"] = info.value("displayName");
            dataset["displayName"] = info.value("displayName");
            dataset["description"] = info.value("description");
            dataset["symbol"] = stockCodes.isEmpty() ? "" : stockCodes.first();
            dataset["stockCount"] = stockCodes.size();
            dataset["stockCodes"] = stockCodes;
            dataset["startDate"] = info.value("startDate").toString();
            dataset["endDate"] = info.value("endDate").toString();
            dataset["recordCount"] = info.value("rowCount");
            qint64 created = info.value("createdAt", 0).toLongLong();
            dataset["createdTime"] = created > 0 ? QDateTime::fromSecsSinceEpoch(created).toString(Qt::ISODate) : "";
            dataset["tags"] = tags;
            dataset["schemaVersion"] = info.value("schemaVersion");
            dataset["isBacktestReady"] = info.value("isBacktestReady");
            dataset["availableFields"] = info.value("availableFields");
            dataset["sourceType"] = info.value("sourceType");
            
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
        
        INTERNAL_DEBUG_STREAM << "CleanedDataController: Refreshed" << datasets.size() << "datasets";
        
        updateLoadingState(false);
        
    } catch (const std::exception& e) {
        QString error = QString("刷新数据集失败: %1").arg(e.what());
        INTERNAL_WARN_STREAM << "CleanedDataController:" << error.toStdString();

        updateLoadingState(false);
        emit errorOccurred(error);
    }
}

void CleanedDataController::loadCleanedData(const QString& symbol,
                                          const QString& startDate,
                                          const QString& endDate)
{
    if (!m_initialized) {
        INTERNAL_WARN_STREAM << "CleanedDataController: Not initialized";
        emit errorOccurred("控制器未初始化");
        return;
    }
    
    updateLoadingState(true);
    
    INTERNAL_DEBUG_STREAM << "CleanedDataController: Loading cleaned data for"
             << symbol.toStdString() << "from" << startDate.toStdString() << "to" << endDate.toStdString();
    
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
                INTERNAL_WARN_STREAM << "CleanedDataController: No cleaned data found for" << symbol.toStdString();
                emit errorOccurred(QString("未找到%1的清洗后数据").arg(symbol));
                updateLoadingState(false);
            }
        });
        
    } catch (const std::exception& e) {
        QString error = QString("加载数据失败: %1").arg(e.what());
        INTERNAL_WARN_STREAM << "CleanedDataController:" << error.toStdString();

        updateLoadingState(false);
        emit errorOccurred(error);
    }
}

void CleanedDataController::loadDatasetById(int datasetId)
{
    if (!m_initialized || datasetId <= 0) {
        INTERNAL_WARN_STREAM << "CleanedDataController: Invalid dataset ID:" << datasetId;
        emit errorOccurred("无效的数据集ID");
        return;
    }
    const int requestedDatasetId = datasetId;
    updateLoadingState(true);
    m_currentDatasetId = datasetId;

    INTERNAL_DEBUG_STREAM << "CleanedDataController: Loading dataset by ID:" << datasetId;

    try {
        QPointer<CleanedDataController> safeThis(this);
        DataCacheAdapter* cache = m_cache;
        std::thread([safeThis, cache, requestedDatasetId]() {
            if (!safeThis || !cache) {
                return;
            }

            QVariantMap selectedDatasetInfo = cache->getDataSetInfo(requestedDatasetId);
            if (selectedDatasetInfo.isEmpty()) {
                QMetaObject::invokeMethod(safeThis.data(), [safeThis, requestedDatasetId]() {
                    if (!safeThis || safeThis->m_currentDatasetId != requestedDatasetId) return;
                    emit safeThis->errorOccurred(QString("数据集%1未找到或为空").arg(requestedDatasetId));
                    safeThis->updateLoadingState(false);
                }, Qt::QueuedConnection);
                return;
            }

            selectedDatasetInfo["name"] = selectedDatasetInfo.value("displayName");
            QStringList sc = selectedDatasetInfo.value("stockCodes").toStringList();
            selectedDatasetInfo["symbol"] = sc.isEmpty() ? QString() : sc.first();
            selectedDatasetInfo["stockCount"] = sc.size();
            selectedDatasetInfo["recordCount"] = selectedDatasetInfo.value("rowCount");
            qint64 created = selectedDatasetInfo.value("createdAt", 0).toLongLong();
            selectedDatasetInfo["createdTime"] = created > 0 ? QDateTime::fromSecsSinceEpoch(created).toString(Qt::ISODate) : "";

            QMetaObject::invokeMethod(
                safeThis.data(),
                [safeThis,
                 requestedDatasetId,
                 selectedDatasetInfo = std::move(selectedDatasetInfo)]() mutable {
                    if (!safeThis || safeThis->m_currentDatasetId != requestedDatasetId) {
                        return;
                    }

                    safeThis->m_selectedDatasetInfo = selectedDatasetInfo;
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

                    emit safeThis->selectedDatasetChanged();
                    safeThis->updateLoadingState(false);
                },
                Qt::QueuedConnection);
        }).detach();

    } catch (const std::exception& e) {
        QString error = QString("加载数据集失败: %1").arg(e.what());
        INTERNAL_WARN_STREAM << "CleanedDataController:" << error.toStdString();

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
        INTERNAL_WARN_STREAM << "CleanedDataController: Not initialized";
        return;
    }

    updateLoadingState(true);

    INTERNAL_DEBUG_STREAM << "CleanedDataController: Searching datasets with criteria:"
             << "Symbol:" << symbol.toStdString()
             << "Start:" << startDate.toStdString()
             << "End:" << endDate.toStdString()
             << "Rule:" << cleaningRule.toStdString()
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
            
            INTERNAL_DEBUG_STREAM << "CleanedDataController: Found" << filteredDatasets.size() << "datasets";
            emit datasetsFound(filteredDatasets);
            updateLoadingState(false);
        });
        
    } catch (const std::exception& e) {
        QString error = QString("搜索数据集失败: %1").arg(e.what());
        INTERNAL_WARN_STREAM << "CleanedDataController:" << error.toStdString();

        updateLoadingState(false);
        emit errorOccurred(error);
    }
}

void CleanedDataController::clearSelection()
{
    INTERNAL_DEBUG_STREAM << "CleanedDataController: Clearing selection";
    
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
        INTERNAL_DEBUG_STREAM << "CleanedDataController::getDataDateRange: Not initialized, returning default range:"
                 << result["startDate"].toString().toStdString() << "to" << result["endDate"].toString().toStdString();
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
                        INTERNAL_DEBUG_STREAM << "CleanedDataController::getDataDateRange: Found range too small ("
                                 << foundStartDate.daysTo(foundEndDate) << "days), using default range";
                    } else {
                        result["startDate"] = earliestDate;
                        result["endDate"] = latestDate;
                        INTERNAL_DEBUG_STREAM << "CleanedDataController::getDataDateRange: Found date range from datasets:"
                                 << earliestDate.toStdString() << "to" << latestDate.toStdString();
                        return result;
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        INTERNAL_WARN_STREAM << "CleanedDataController::getDataDateRange: Error:" << e.what();
    }

    INTERNAL_DEBUG_STREAM << "CleanedDataController::getDataDateRange: Using default range:"
             << result["startDate"].toString().toStdString() << "to" << result["endDate"].toString().toStdString();
    return result;
}

void CleanedDataController::setCurrentSymbol(const QString& symbol)
{
    if (m_currentSymbol != symbol) {
        m_currentSymbol = symbol;
        INTERNAL_DEBUG_STREAM << "CleanedDataController: Current symbol set to:" << symbol.toStdString();
        emit symbolChanged(symbol);
    }
}

void CleanedDataController::setCurrentStartDate(const QString& date)
{
    if (m_currentStartDate != date) {
        m_currentStartDate = date;
        INTERNAL_DEBUG_STREAM << "CleanedDataController: Current start date set to:" << date.toStdString();
        emit startDateChanged(date);
    }
}

void CleanedDataController::setCurrentEndDate(const QString& date)
{
    if (m_currentEndDate != date) {
        m_currentEndDate = date;
        INTERNAL_DEBUG_STREAM << "CleanedDataController: Current end date set to:" << date.toStdString();
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
    INTERNAL_DEBUG_STREAM << "CleanedDataController: Updated dataset list with" << datasets.size() << "items";
    
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
            
            INTERNAL_DEBUG_STREAM << "CleanedDataController: Selected dataset updated:" << datasetId;
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
        INTERNAL_WARN_STREAM << "CleanedDataController: Refusing to emit dataLoaded without selected dataset info";
        emit errorOccurred(QStringLiteral("数据集元信息为空，禁止发送 dataLoaded"));
        return;
    }

    INTERNAL_DEBUG_STREAM << "CleanedDataController: Emitting data loaded with" << data.size() << "records";
    emit dataLoaded(data, m_selectedDatasetInfo);
}

} // namespace ui::bridge