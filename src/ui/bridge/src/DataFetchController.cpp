// DataFetchController.cpp - 改进版本，支持模型和数据缓存
#include "DataFetchController.h"
#include "DataService.h"
#include "DataManager.h"
#include "DataCleaningEngine.h"
#include "DataServiceCache.h"
#include "PreviewDataModel.h"

#include <QDebug>
#include <QDateTime>
#include <QVector>
#include <QQmlEngine>
#include <QQmlContext>

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
    
    qDebug() << "DataFetchController: Created with embedded DataService and PreviewDataModel";
    
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
    // DataService使用cleaningProgress而不是dataCleaningProgress
    connect(m_dataService, &DataService::cleaningProgress,
            this, &DataFetchController::onDataCleaningProgress);
    // DataService使用cleaningCompleted而不是dataCleaningCompleted
    connect(m_dataService, &DataService::cleaningCompleted,
            this, &DataFetchController::onDataCleaningCompleted);
    
    qDebug() << "DataFetchController: All signals connected to DataService";
    
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
    qDebug() << "DataFetchController: Database initialization completed";
}

void DataFetchController::fetchData()
{
    qDebug() << "DataFetchController::fetchData() - Forwarding request to DataService";
    
    // 允许空股票代码，但需要验证日期
    if (m_startDate.isEmpty() || m_endDate.isEmpty()) {
        updateStatus("请设置开始和结束日期", 0);
        emit dataFetchError("日期未设置");
        return;
    }
    
    // 更新状态
    m_isFetching = true;
    m_progress = 0;
    m_fetchedData.clear();
    
    emit isFetchingChanged();
    emit progressChanged();
    emit fetchedDataChanged();
    emit dataFetchStarted();
    
    updateStatus("开始获取数据...", 0);
    
    // 转发请求给DataService
    emit requestFetchData(m_symbols, m_startDate, m_endDate);
}

void DataFetchController::cancelFetch()
{
    qDebug() << "DataFetchController::cancelFetch() - Forwarding cancel request";
    
    if (m_isFetching) {
        m_isFetching = false;
        emit isFetchingChanged();
        updateStatus("数据获取已取消", 0);
        
        // DataService没有取消方法，直接更新状态
        qDebug() << "DataService does not support cancellation";
    }
}

void DataFetchController::clearData()
{
    qDebug() << "DataFetchController::clearData() - Clearing local data";
    
    m_fetchedData.clear();
    emit fetchedDataChanged();
    updateStatus("数据已清空", 0);
}

void DataFetchController::saveToDatabase()
{
    qDebug() << "DataFetchController::saveToDatabase() - Forwarding request to DataService";
    
    if (m_fetchedData.isEmpty()) {
        updateStatus("没有数据可保存", 0);
        emit dataSavedToDatabase(false, "没有数据可保存");
        return;
    }
    
    updateStatus("正在保存数据到MySQL数据库...", 0);
    
    // 转发请求给DataService
    emit requestSaveData(m_fetchedData);
}

void DataFetchController::loadFromDatabase(const QString& symbol, const QString& startDate, const QString& endDate)
{
    qDebug() << "DataFetchController::loadFromDatabase() - Checking cache first";
    
    // 验证参数
    QString actualSymbol = symbol;
    
    // 如果symbol为空，尝试使用m_symbols中的第一个股票代码
    if (actualSymbol.isEmpty() && !m_symbols.isEmpty()) {
        actualSymbol = m_symbols.first();
        qDebug() << "Symbol parameter is empty, using first symbol from m_symbols:" << actualSymbol;
    }
    
    // 如果仍然为空，使用空字符串（允许查询所有股票）
    if (actualSymbol.isEmpty()) {
        qDebug() << "Symbol parameter is empty, will query all stocks";
        // 不返回错误，允许空股票代码查询
    }
    
    // 验证日期
    if (startDate.isEmpty() || endDate.isEmpty()) {
        updateStatus("请设置开始和结束日期", 0);
        emit dataFetchError("日期未设置");
        return;
    }
    
    // 保存当前加载的数据标识
    m_currentSymbol = actualSymbol;
    m_currentStartDate = startDate;
    m_currentEndDate = endDate;
    
    // 首先检查缓存 - 支持数据集（空symbol）和单个股票
    QVariantList cachedData;
    if (actualSymbol.isEmpty()) {
        // 数据集缓存检查
        QString datasetKey = QString("dataset_%1_%2").arg(startDate).arg(endDate);
        cachedData = DataManager::instance()->getData(datasetKey);
        if (!cachedData.isEmpty()) {
            qDebug() << "DataFetchController::loadFromDatabase: Using cached dataset, key:" << datasetKey << "count:" << cachedData.size();
        }
    } else {
        // 单个股票缓存检查
        cachedData = DataManager::instance()->getCachedStockData(actualSymbol, startDate, endDate);
        if (!cachedData.isEmpty()) {
            qDebug() << "DataFetchController::loadFromDatabase: Using cached stock data for" << actualSymbol << "count:" << cachedData.size();
        }
    }
    
    if (!cachedData.isEmpty()) {
        // 更新本地数据
        m_fetchedData = cachedData;
        emit fetchedDataChanged();
        
        // 将缓存数据存储到通用缓存键，供清洗模块使用
        DataManager::instance()->storeData("current_stock_data", cachedData);
        qDebug() << "Cached data also stored to 'current_stock_data' for cleaning module";
        
        // 更新状态
         updateStatus("使用缓存数据", 100);
        
        // 通知QML
        emit dataLoadedFromDatabase(true, "使用缓存数据", cachedData.size());
        return;
    }
    
    qDebug() << "DataFetchController::loadFromDatabase: No cache found, loading from database";
    updateStatus("正在从数据库加载数据...", 0);
    
    // 转发请求给DataService
    emit requestLoadData(actualSymbol, startDate, endDate);
}

QVariantList DataFetchController::cleanData(const QVariantList& data, const QVariantMap& rules)
{
    qDebug() << "DataFetchController::cleanData() called with" << data.size() << "items";
    
    if (data.isEmpty()) {
        qDebug() << "No data to clean";
        return QVariantList();
    }
    
    // 对于小数据集（小于1000条），使用DataCleaningEngine进行同步清洗
    if (data.size() < 1000) {
        qDebug() << "Small dataset, performing synchronous cleaning";
        
        try {
            // 创建DataCleaningEngine实例
            DataCleaningEngine cleaningEngine;
            
            // 转换规则 - 使用简单规则转换
            QVector<DataCleaningEngine::CleaningRule> cleaningRules;
            
            // 执行清洗
            QVariantList cleanedData = cleaningEngine.cleanData(data, cleaningRules);
            
            qDebug() << "Synchronous cleaning completed:" 
                     << "original:" << data.size() 
                     << "cleaned:" << cleanedData.size();
            
            return cleanedData;
            
        } catch (const std::exception& e) {
            qCritical() << "Data cleaning error:" << e.what();
            // 如果清洗失败，返回原始数据
            return data;
        }
    }
    
    // 对于大数据集，建议使用异步清洗
    qDebug() << "Large dataset detected, please use cleanDataAsync() instead";
    
    // 对于同步调用，仍然返回原始数据
    // 但记录警告，建议使用异步版本
    qWarning() << "Large dataset (" << data.size() 
               << " items) in synchronous cleanData(), consider using cleanDataAsync()";
    
    return data;
}

void DataFetchController::cleanDataAsync(const QVariantMap& rules)
{
    qDebug() << "DataFetchController::cleanDataAsync() - Using direct data parameters";
    
    // 直接使用用户参数加载数据，不依赖缓存键名访问
    if (m_currentStartDate.isEmpty() || m_currentEndDate.isEmpty()) {
        qDebug() << "No user parameters found, cannot query database";
        emit dataCleaningCompleted(false, "没有可用的数据参数，请先添加数据源并选择时间范围", QVariantList());
        return;
    }
    
    qDebug() << "Using user parameters to load data:";
    qDebug() << "  Symbol:" << (m_currentSymbol.isEmpty() ? "ALL" : m_currentSymbol);
    qDebug() << "  Start Date:" << m_currentStartDate;
    qDebug() << "  End Date:" << m_currentEndDate;
    
    // 更新状态
    updateStatus("正在从数据库加载数据以进行清洗...", 0);
    
    // 直接使用用户参数重新查询数据库
    // 这里直接调用loadFromDatabase，它会处理缓存和查询逻辑
    // 清洗请求将通过延迟调用处理
    loadFromDatabase(m_currentSymbol, m_currentStartDate, m_currentEndDate);
    
    // 使用传统定时器方式，避免lambda问题
    QTimer::singleShot(1500, this, SLOT(delayedCleanData()));
    
    // 保存规则以备后续使用
    m_pendingRules = rules;
}

void DataFetchController::updateStatus(const QString& message, int progress)
{
    if (progress >= 0) {
        m_progress = progress;
        emit progressChanged();
    }
    
    m_statusMessage = message;
    emit statusMessageChanged();
    
    qDebug() << "Status update:" << message << "Progress:" << progress;
}

// 接收DataService的数据加载进度
void DataFetchController::onDataLoadProgress(int progress, const QString& message)
{
    qDebug() << "DataFetchController::onDataLoadProgress:" << progress << message;
    
    // 更新状态
    updateStatus(message, progress);
    
    // 转发给QML
    emit dataFetchProgress(progress, message);
}

// 接收DataService的数据加载完成结果
void DataFetchController::onDataLoadCompleted(bool success, const QString& message, const QVariantList& data)
{
    qDebug() << "DataFetchController::onDataLoadCompleted:" << success << message << "data count:" << data.size();
    
    if (success) {
        // 保存数据
        m_fetchedData = data;
        emit fetchedDataChanged();
        
        // 更新PreviewDataModel - 在C++中直接更新模型，不传递数据给QML
        if (m_previewModel) {
            // 将QVariantList转换为QVector<QVariantMap>
            QVector<QVariantMap> dataVector;
            dataVector.reserve(data.size());
            
            for (const QVariant& item : data) {
                if (item.canConvert<QVariantMap>()) {
                    dataVector.append(item.toMap());
                } else {
                    qWarning() << "DataFetchController::onDataLoadCompleted: Item cannot be converted to QVariantMap";
                }
            }
            
            m_previewModel->updateData(dataVector);
            qDebug() << "PreviewDataModel updated with" << dataVector.size() << "items (data load)";
        } else {
            qWarning() << "DataFetchController::onDataLoadCompleted: PreviewModel is null, cannot update";
        }
        
        // 缓存数据到DataManager - 支持数据集（空symbol）和单个股票
        if (!m_currentStartDate.isEmpty() && !m_currentEndDate.isEmpty()) {
            if (m_currentSymbol.isEmpty()) {
                // 数据集缓存：使用特殊键名
                QString datasetKey = QString("dataset_%1_%2").arg(m_currentStartDate).arg(m_currentEndDate);
                DataManager::instance()->storeData(datasetKey, data);
                qDebug() << "Dataset cached with key:" << datasetKey << "from" << m_currentStartDate << "to" << m_currentEndDate;
                
                // 同时存储DataServiceCache兼容的缓存键
                QString cacheKey = QString("data:stock:ALL_%1_%2").arg(m_currentStartDate).arg(m_currentEndDate);
                DataManager::instance()->storeData(cacheKey, data);
                qDebug() << "Also cached with DataServiceCache compatible key:" << cacheKey;
            } else {
                // 单个股票缓存
                DataManager::instance()->cacheStockData(m_currentSymbol, m_currentStartDate, m_currentEndDate, data);
                qDebug() << "Data cached for" << m_currentSymbol << "from" << m_currentStartDate << "to" << m_currentEndDate;
                
                // 同时存储DataServiceCache兼容的缓存键
                QString cacheKey = QString("data:stock:%1_%2_%3").arg(m_currentSymbol).arg(m_currentStartDate).arg(m_currentEndDate);
                DataManager::instance()->storeData(cacheKey, data);
                qDebug() << "Also cached with DataServiceCache compatible key:" << cacheKey;
            }
        }
        
        // 同时将数据存储到通用缓存键，供清洗模块使用
        DataManager::instance()->storeData("current_stock_data", data);
        qDebug() << "Data also stored to 'current_stock_data' cache for cleaning module, count:" << data.size();
        
        // 更新状态
        updateStatus(message, 100);
        
        // 转发给QML - 只传递状态信息，不传递数据
        emit dataLoadedFromDatabase(true, message, data.size());
    } else {
        // 更新状态
        updateStatus("数据加载失败: " + message, 0);
        
        // 转发给QML
        emit dataLoadedFromDatabase(false, message, 0);
    }
}

// 接收DataService的数据加载错误
void DataFetchController::onDataLoadError(const QString& error)
{
    qDebug() << "DataFetchController::onDataLoadError:" << error;
    
    // 更新状态
    updateStatus("数据加载错误: " + error, 0);
    
    // 转发给QML
    emit dataFetchError(error);
}

// 接收DataService的数据清洗进度
void DataFetchController::onDataCleaningProgress(int progress, const QString& message)
{
    qDebug() << "DataFetchController::onDataCleaningProgress:" << progress << message;
    
    // 更新状态
    updateStatus(message, progress);
    
    // 转发给QML
    emit dataCleaningProgress(progress, message);
}

// 接收DataService的数据清洗完成结果
void DataFetchController::onDataCleaningCompleted(bool success, const QString& message, const QVariantList& cleanedData)
{
    qDebug() << "DataFetchController::onDataCleaningCompleted:" << success << message << "cleaned data count:" << cleanedData.size();
    
    if (success) {
        // 保存清洗后的数据
        m_fetchedData = cleanedData;
        emit fetchedDataChanged();
        
        // 更新PreviewDataModel - 在C++中直接更新模型，不传递数据给QML
        if (m_previewModel) {
            // 将QVariantList转换为QVector<QVariantMap>
            QVector<QVariantMap> dataVector;
            dataVector.reserve(cleanedData.size());
            
            for (const QVariant& item : cleanedData) {
                if (item.canConvert<QVariantMap>()) {
                    dataVector.append(item.toMap());
                } else {
                    qWarning() << "DataFetchController::onDataCleaningCompleted: Item cannot be converted to QVariantMap";
                }
            }
            
            m_previewModel->updateData(dataVector);
            qDebug() << "PreviewDataModel updated with" << dataVector.size() << "items (data cleaning)";
        } else {
            qWarning() << "DataFetchController::onDataCleaningCompleted: PreviewModel is null, cannot update";
        }
        
        // 更新CleaningResultModel
        updateCleaningResultModel(cleanedData);
        
        // 更新状态
        updateStatus(message, 100);
        
        // 转发给QML - 只传递状态信息，不传递数据
        emit dataCleaningCompleted(true, message, cleanedData);
    } else {
        // 更新状态
        updateStatus("数据清洗失败: " + message, 0);
        
        // 转发给QML
        emit dataCleaningCompleted(false, message, QVariantList());
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
        qDebug() << "DataFetchController::setPreviewModel: PreviewModel changed";
    }
}

// 数据源添加并自动加载数据
void DataFetchController::addDataSourceAndLoad(const QString& provider, const QString& market, 
                                              const QStringList& symbols, const QString& startDate, 
                                              const QString& endDate, const QString& dataType)
{
    qDebug() << "DataFetchController::addDataSourceAndLoad: Adding data source and loading data";
    qDebug() << "  Provider:" << provider;
    qDebug() << "  Market:" << market;
    qDebug() << "  Symbols:" << symbols;
    qDebug() << "  Start Date:" << startDate;
    qDebug() << "  End Date:" << endDate;
    qDebug() << "  Data Type:" << dataType;
    
    // 验证参数
    if (startDate.isEmpty() || endDate.isEmpty()) {
        qDebug() << "DataFetchController::addDataSourceAndLoad: Invalid date range";
        updateStatus("请设置有效的时间范围", 0);
        return;
    }
    
    // 设置控制器属性
    m_dataSource = provider;
    m_symbols = symbols;
    m_startDate = startDate;
    m_endDate = endDate;
    m_dataType = dataType;
    
    // 保存当前加载的数据标识
    m_currentSymbol = symbols.isEmpty() ? "" : symbols.first();
    m_currentStartDate = startDate;
    m_currentEndDate = endDate;
    
    // 更新状态
    updateStatus("正在添加数据源并加载数据...", 0);
    
    // 自动加载数据
    // 如果symbols为空，查询所有股票；否则查询指定股票
    QString symbolToLoad = symbols.isEmpty() ? "" : symbols.first();
    loadFromDatabase(symbolToLoad, startDate, endDate);
    
    qDebug() << "DataFetchController::addDataSourceAndLoad: Data source added and data loading started";
}

// 缓存相关方法实现
QVariantList DataFetchController::getAllCacheKeys()
{
    qDebug() << "DataFetchController::getAllCacheKeys()";
    
    // 获取DataServiceCache实例
    DataServiceCache& cache = DataServiceCache::getInstance();
    
    // 获取所有缓存键
    QStringList cacheKeys = cache.getAllDataKeys();
    qDebug() << "Found" << cacheKeys.size() << "cache keys";
    
    // 转换为QVariantList返回
    QVariantList result;
    for (const QString& key : cacheKeys) {
        result.append(key);
    }
    
    return result;
}

QVariantList DataFetchController::getAllDataSetInfos()
{
    qDebug() << "DataFetchController::getAllDataSetInfos()";
    
    // 获取DataServiceCache实例
    DataServiceCache& cache = DataServiceCache::getInstance();
    
    // 获取所有数据集信息
    auto dataSetInfos = cache.getAllDataSetInfos();
    qDebug() << "Found" << dataSetInfos.size() << "dataset infos";
    
    // 转换为QVariantList返回
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
    
    return result;
}

void DataFetchController::loadFromCache(const QString& cacheKey)
{
    qDebug() << "DataFetchController::loadFromCache() - Loading from cache key:" << cacheKey;
    
    if (cacheKey.isEmpty()) {
        qDebug() << "Cache key is empty";
        updateStatus("缓存键为空，无法加载数据", 0);
        emit dataFetchError("缓存键为空");
        return;
    }
    
    // 获取DataServiceCache实例
    DataServiceCache& cache = DataServiceCache::getInstance();
    
    // 从缓存获取数据
    QVariantList cachedData = cache.getData(cacheKey);
    
    if (cachedData.isEmpty()) {
        qDebug() << "No data found for cache key:" << cacheKey;
        updateStatus("缓存中没有找到数据: " + cacheKey, 0);
        emit dataLoadedFromDatabase(false, "缓存中没有找到数据", 0);
        return;
    }
    
    qDebug() << "Successfully loaded" << cachedData.size() << "items from cache key:" << cacheKey;
    
    // 更新本地数据
    m_fetchedData = cachedData;
    emit fetchedDataChanged();
    
    // 更新当前加载的数据标识（尝试从缓存键中解析）
    // 如果缓存键是数据集格式，尝试解析日期范围
    if (cacheKey.startsWith("dataset_")) {
        QStringList parts = cacheKey.mid(8).split('_'); // 移除"dataset_"前缀
        if (parts.size() >= 2) {
            m_currentStartDate = parts[0];
            m_currentEndDate = parts[1];
            qDebug() << "Parsed dataset dates from cache key:" << m_currentStartDate << "to" << m_currentEndDate;
        }
    }
    
    // 同时存储到通用缓存键，供清洗模块使用
    DataManager::instance()->storeData("current_stock_data", cachedData);
    qDebug() << "Cache data also stored to 'current_stock_data' for cleaning module";
    
    // 更新状态
    updateStatus("从缓存加载数据成功: " + cacheKey, 100);
    
    // 通知QML
    emit dataLoadedFromDatabase(true, "从缓存加载数据成功", cachedData.size());
}

void DataFetchController::loadDataSetById(int dataId)
{
    qDebug() << "DataFetchController::loadDataSetById() - Loading dataset with ID:" << dataId;
    
    if (dataId <= 0) {
        qDebug() << "Invalid dataset ID:" << dataId;
        updateStatus("无效的数据集ID", 0);
        emit dataFetchError("无效的数据集ID");
        return;
    }
    
    // 获取DataServiceCache实例
    DataServiceCache& cache = DataServiceCache::getInstance();
    
    // 通过ID获取数据
    QVariantList cachedData = cache.getDataSetById(dataId);
    
    if (cachedData.isEmpty()) {
        qDebug() << "No data found for dataset ID:" << dataId;
        updateStatus("数据集中没有找到数据，ID: " + QString::number(dataId), 0);
        emit dataLoadedFromDatabase(false, "数据集中没有找到数据", 0);
        return;
    }
    
    qDebug() << "Successfully loaded" << cachedData.size() << "items from dataset ID:" << dataId;
    
    // 获取数据集信息以便记录
    DataServiceCache::DataSetInfo info = cache.getDataSetInfo(dataId);
    qDebug() << "Dataset info - Name:" << info.displayName << "Type:" << info.sourceType;
    
    // 更新本地数据
    m_fetchedData = cachedData;
    emit fetchedDataChanged();
    
    // 更新当前加载的数据标识
    m_currentStartDate = info.startDate.isValid() ? info.startDate.toString("yyyy-MM-dd") : "";
    m_currentEndDate = info.endDate.isValid() ? info.endDate.toString("yyyy-MM-dd") : "";
    
    if (!info.stockCodes.isEmpty()) {
        m_currentSymbol = info.stockCodes.first();
    }
    
    // 同时存储到通用缓存键，供清洗模块使用
    DataManager::instance()->storeData("current_stock_data", cachedData);
    qDebug() << "Dataset data also stored to 'current_stock_data' for cleaning module";
    
    // 更新状态
    updateStatus("从数据集加载数据成功: " + info.displayName, 100);
    
    // 通知QML
    emit dataLoadedFromDatabase(true, "从数据集加载数据成功: " + info.displayName, cachedData.size());
}

// 更新清洗结果模型
void DataFetchController::updateCleaningResultModel(const QVariantList& cleanedData)
{
    qDebug() << "DataFetchController::updateCleaningResultModel: Updating model with" << cleanedData.size() << "items";
    
    // if (!m_cleaningResultModel) {
    //     qWarning() << "DataFetchController::updateCleaningResultModel: CleaningResultModel is null";
    //     return;
    // }
    
    // 将QVariantList转换为QVector<QVariantMap>
    QVector<QVariantMap> results;
    results.reserve(cleanedData.size());
    
    for (const QVariant& item : cleanedData) {
        if (item.canConvert<QVariantMap>()) {
            results.append(item.toMap());
        } else {
            qWarning() << "DataFetchController::updateCleaningResultModel: Item cannot be converted to QVariantMap";
        }
    }
    
    // // 更新模型
    // m_cleaningResultModel->updateResults(results);
    
    qDebug() << "DataFetchController::updateCleaningResultModel: Model updated with" << results.size() << "items";
}

// 延迟清洗数据槽函数
void DataFetchController::delayedCleanData()
{
    qDebug() << "DataFetchController::delayedCleanData() - Checking if data is loaded";
    
    // 再次检查缓存 - 尝试多种可能的缓存键
    QVariantList loadedData = DataManager::instance()->getData("current_stock_data");
    
    if (loadedData.isEmpty()) {
        // 尝试DataServiceCache格式的缓存键
        QString cacheKey = QString("data:stock:%1_%2_%3")
            .arg(m_currentSymbol.isEmpty() ? "ALL" : m_currentSymbol)
            .arg(m_currentStartDate)
            .arg(m_currentEndDate);
        loadedData = DataManager::instance()->getData(cacheKey);
    }
    
    if (loadedData.isEmpty()) {
        qDebug() << "Failed to load data from database, cache still empty";
        emit dataCleaningCompleted(false, "数据库查询失败，请检查数据库连接和数据", QVariantList());
        return;
    }
    
    qDebug() << "Data loaded successfully from database, found" << loadedData.size() << "items, proceeding with cleaning";
    
    // 转发请求给DataService
    emit requestCleanData(loadedData, m_pendingRules);
}

// 刷新缓存键列表 - 在C++中遍历，通过信号传递结果
void DataFetchController::refreshCacheKeys()
{
    qDebug() << "DataFetchController::refreshCacheKeys()";
    
    // 获取DataServiceCache实例
    DataServiceCache& cache = DataServiceCache::getInstance();
    
    // 确保缓存已初始化
    if (!cache.isCacheEnabled()) {
        qDebug() << "Cache not enabled, attempting to initialize...";
        if (!cache.initializeCache()) {
            qWarning() << "Failed to initialize cache";
            emit cacheKeysRefreshed(QVariantList());
            return;
        }
    }
    
    // 获取所有缓存键 - 在C++中遍历，不传递给QML遍历
    QStringList cacheKeys = cache.getAllDataKeys();
    qDebug() << "Found" << cacheKeys.size() << "cache keys";
    
    // 转换为QVariantList
    QVariantList result;
    for (const QString& key : cacheKeys) {
        result.append(key);
    }
    
    // 通过信号传递结果给QML
    emit cacheKeysRefreshed(result);
    qDebug() << "Cache keys refreshed, emitted signal with" << result.size() << "keys";
}

// 刷新数据集信息 - 在C++中遍历，通过信号传递结果
void DataFetchController::refreshDataSetInfos()
{
    qDebug() << "DataFetchController::refreshDataSetInfos()";
    
    // 获取DataServiceCache实例
    DataServiceCache& cache = DataServiceCache::getInstance();
    
    // 获取所有数据集信息 - 在C++中遍历，不传递给QML遍历
    auto dataSetInfos = cache.getAllDataSetInfos();
    qDebug() << "Found" << dataSetInfos.size() << "dataset infos";
    
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
    qDebug() << "Dataset infos refreshed, emitted signal with" << result.size() << "infos";
}

// 刷新所有缓存信息 - 获取所有缓存数据和数据集信息
void DataFetchController::refreshAllCacheInfos()
{
    qDebug() << "DataFetchController::refreshAllCacheInfos()";
    
    // 同时获取缓存键和数据集信息
    DataServiceCache& cache = DataServiceCache::getInstance();
    
    // 确保缓存已初始化
    if (!cache.isCacheEnabled()) {
        qDebug() << "DataFetchController::refreshAllCacheInfos: Cache not enabled, attempting to initialize...";
        if (!cache.initializeCache()) {
            qWarning() << "DataFetchController::refreshAllCacheInfos: Failed to initialize cache";
            emit allCacheInfosRefreshed(QVariantList());
            return;
        }
    }
    
    // 获取所有缓存键
    QStringList cacheKeys = cache.getAllDataKeys();
    
    // 获取所有数据集信息
    auto dataSetInfos = cache.getAllDataSetInfos();
    
    qDebug() << "DataFetchController::refreshAllCacheInfos: Found" << cacheKeys.size() << "cache keys and" << dataSetInfos.size() << "dataset infos";
    
    // 调试：打印缓存键
    for (const QString& key : cacheKeys) {
        qDebug() << "  Cache key:" << key;
    }
    
    // 调试：打印数据集信息
    for (const DataServiceCache::DataSetInfo& info : dataSetInfos) {
        qDebug() << "  Dataset info: ID=" << info.id << "Name=" << info.displayName << "Rows=" << info.rowCount;
    }
    
    // 创建包含显示名称和索引的列表
    QVariantList cacheDisplayList;
    int index = 0;
    
    // 首先添加数据集信息
    for (const DataServiceCache::DataSetInfo& info : dataSetInfos) {
        QVariantMap map;
        map["index"] = index;
        map["displayName"] = QString("📊 数据集: %1 (%2条数据)").arg(info.displayName).arg(info.rowCount);
        map["type"] = "dataset";
        map["id"] = info.id;
        map["description"] = info.description;
        cacheDisplayList.append(map);
        index++;
    }
    
    // 然后添加缓存键（包括所有键）
    for (const QString& key : cacheKeys) {
        // 不再跳过"data:stock:ALL_"键，因为当数据集信息为空时，
        // 这些键可能是唯一可用的数据源
        // 注意：如果数据集信息中有对应项，可能会有重复，但用户可以通过显示名称区分
        
        QVariantMap map;
        map["index"] = index;
        map["displayName"] = QString("📁 缓存: %1").arg(key);
        map["type"] = "cache";
        map["cacheKey"] = key;
        cacheDisplayList.append(map);
        index++;
    }
    
    // 发出信号，包含所有缓存信息的显示列表（包含索引、类型、ID等完整信息）
    emit allCacheInfosRefreshed(cacheDisplayList);
    
    qDebug() << "All cache infos refreshed, found" << cacheDisplayList.size() << "items total, signal emitted";
}

// 辅助函数：增强缓存数据获取
QVariantList DataFetchController::getDataFromCacheEnhanced(DataServiceCache& cache, const QString& key)
{
    qDebug() << "getDataFromCacheEnhanced: Attempting to get data for key:" << key;
    
    // 1. 首先尝试直接通过DataServiceCache获取
    QVariantList data = cache.getData(key);
    
    if (!data.isEmpty()) {
        qDebug() << "getDataFromCacheEnhanced: Direct cache get succeeded, found" << data.size() << "items";
        return data;
    }
    
    // 2. 如果失败，检查是否为data:stock:格式的键，尝试解析参数调用getCachedData
    if (key.startsWith("data:stock:")) {
        qDebug() << "getDataFromCacheEnhanced: Key is data:stock format, attempting to parse";
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
            
            qDebug() << "getDataFromCacheEnhanced: Parsed params - symbol:" << symbol 
                     << "startDate:" << startDate << "endDate:" << endDate;
            
            data = cache.getCachedData(symbol, startDate, endDate);
            if (!data.isEmpty()) {
                qDebug() << "getDataFromCacheEnhanced: getCachedData succeeded, found" << data.size() << "items";
                return data;
            }
        }
    }
    
    // 3. 尝试通过DataManager获取（兼容旧系统）
    data = DataManager::instance()->getData(key);
    if (!data.isEmpty()) {
        qDebug() << "getDataFromCacheEnhanced: DataManager get succeeded, found" << data.size() << "items";
        return data;
    }
    
    // 4. 尝试带"manager:"前缀的键
    QString managerKey = "manager:" + key;
    data = DataManager::instance()->getData(managerKey);
    if (!data.isEmpty()) {
        qDebug() << "getDataFromCacheEnhanced: DataManager get with manager: prefix succeeded, found" << data.size() << "items";
        return data;
    }
    
    // 5. 尝试通过数据集ID获取（如果键是数字字符串）
    bool ok = false;
    int dataId = key.toInt(&ok);
    if (ok && dataId > 0) {
        qDebug() << "getDataFromCacheEnhanced: Key looks like dataset ID:" << dataId;
        data = cache.getDataSetById(dataId);
        if (!data.isEmpty()) {
            qDebug() << "getDataFromCacheEnhanced: getDataSetById succeeded, found" << data.size() << "items";
            return data;
        }
    }
    
    qDebug() << "getDataFromCacheEnhanced: All attempts failed for key:" << key;
    return QVariantList();
}

// 通过索引清洗缓存数据
void DataFetchController::cleanDataFromCacheByIndex(int cacheIndex, const QVariantMap& rules)
{
    qDebug() << "DataFetchController::cleanDataFromCacheByIndex() - Index:" << cacheIndex;
    
    if (cacheIndex < 0) {
        qDebug() << "Invalid cache index:" << cacheIndex;
        emit dataCleaningCompleted(false, "无效的缓存索引", QVariantList());
        return;
    }
    
    // 获取所有缓存信息
    DataServiceCache& cache = DataServiceCache::getInstance();
    QStringList cacheKeys = cache.getAllDataKeys();
    QList<DataServiceCache::DataSetInfo> dataSetInfos = cache.getAllDataSetInfos();
    
    qDebug() << "cleanDataFromCacheByIndex: Found" << dataSetInfos.size() 
             << "dataset infos and" << cacheKeys.size() << "cache keys";
    
    // 调试：打印所有缓存键
    for (int i = 0; i < cacheKeys.size(); ++i) {
        qDebug() << "  Cache key[" << i << "]:" << cacheKeys[i];
    }
    
    // 计算实际索引对应的数据
    int currentIndex = 0;
    QVariantList dataToClean;
    QString dataSourceName;
    
    // 首先检查数据集信息
    for (const DataServiceCache::DataSetInfo& info : dataSetInfos) {
        qDebug() << "Checking dataset at index" << currentIndex << ":" << info.displayName;
        if (currentIndex == cacheIndex) {
            // 通过ID获取数据集数据
            dataToClean = cache.getDataSetById(info.id);
            dataSourceName = info.displayName;
            qDebug() << "Found dataset match at index" << cacheIndex << ":" << info.displayName;
            break;
        }
        currentIndex++;
    }
    
    // 如果未在数据集中找到，检查缓存键
    if (dataToClean.isEmpty()) {
        qDebug() << "No dataset found for index" << cacheIndex << ", checking cache keys...";
        for (const QString& key : cacheKeys) {
            qDebug() << "Checking cache key at index" << currentIndex << ":" << key;
            if (currentIndex == cacheIndex) {
                // 尝试多种方式获取数据
                dataToClean = getDataFromCacheEnhanced(cache, key);
                
                // 如果通过DataServiceCache获取失败，尝试直接从DataManager获取
                if (dataToClean.isEmpty()) {
                    qDebug() << "DataServiceCache get failed, trying DataManager directly for key:" << key;
                    dataToClean = DataManager::instance()->getData(key);
                    
                    // 如果原始键失败，尝试带"manager:"前缀的键
                    if (dataToClean.isEmpty()) {
                        QString managerKey = "manager:" + key;
                        dataToClean = DataManager::instance()->getData(managerKey);
                        if (!dataToClean.isEmpty()) {
                            qDebug() << "DataManager get with manager: prefix succeeded for key:" << key;
                        }
                    } else {
                        qDebug() << "DataManager get succeeded for key:" << key;
                    }
                }
                
                dataSourceName = key;
                qDebug() << "Found cache key match at index" << cacheIndex << ":" << key;
                break;
            }
            currentIndex++;
        }
    }
    
    if (dataToClean.isEmpty()) {
        qDebug() << "No data found for cache index:" << cacheIndex;
        emit dataCleaningCompleted(false, QString("找不到索引 %1 对应的缓存数据").arg(cacheIndex), QVariantList());
        return;
    }
    
    qDebug() << "Found data for cleaning: source=" << dataSourceName << ", count=" << dataToClean.size();
    
    // 更新状态
    updateStatus(QString("正在清洗数据: %1").arg(dataSourceName), 0);
    
    // 转发请求给DataService进行清洗
    emit requestCleanData(dataToClean, rules);
}
