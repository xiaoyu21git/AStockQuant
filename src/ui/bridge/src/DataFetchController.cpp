// DataFetchController.cpp - 改进版本，支持模型和数据缓存
#include "DataFetchController.h"
#include "DataService.h"
#include "CleaningResultModel.h"
#include "DataManager.h"

#include <QDebug>
#include <QDateTime>

DataFetchController::DataFetchController(QObject* parent)
    : QObject(parent)
    , m_dataService(new DataService(this))
    , m_cleaningResultModel(new CleaningResultModel(this))
{
    // 设置默认日期（最近30天）
    QDateTime currentDate = QDateTime::currentDateTime();
    QDateTime startDate = currentDate.addDays(-30);
    
    m_startDate = startDate.toString("yyyy-MM-dd");
    m_endDate = currentDate.toString("yyyy-MM-dd");
    
    qDebug() << "DataFetchController: Created with embedded DataService and CleaningResultModel";
    
    // 连接信号：控制器 -> 服务
    connect(this, &DataFetchController::requestLoadData,
            m_dataService, &DataService::loadDataAsync);
    connect(this, &DataFetchController::requestCleanData,
            m_dataService, &DataService::cleanDataAsync);
    connect(this, &DataFetchController::requestCancelOperation,
            m_dataService, &DataService::cancelCurrentOperation);
    
    // 连接信号：服务 -> 控制器
    connect(m_dataService, &DataService::dataLoadProgress,
            this, &DataFetchController::onDataLoadProgress);
    connect(m_dataService, &DataService::dataLoadCompleted,
            this, &DataFetchController::onDataLoadCompleted);
    connect(m_dataService, &DataService::dataLoadError,
            this, &DataFetchController::onDataLoadError);
    connect(m_dataService, &DataService::dataCleaningProgress,
            this, &DataFetchController::onDataCleaningProgress);
    connect(m_dataService, &DataService::dataCleaningCompleted,
            this, &DataFetchController::onDataCleaningCompleted);
    connect(m_dataService, &DataService::dataCleaningError,
            this, &DataFetchController::onDataCleaningError);
    
    qDebug() << "DataFetchController: All signals connected to DataService";
    
    // 初始化数据库（异步）
    QTimer::singleShot(1000, this, [this]() {
        qDebug() << "DataFetchController: Initializing database...";
        bool initialized = m_dataService->initializeDatabase();
        if (initialized) {
            qDebug() << "DataFetchController: Database initialized successfully";
        } else {
            qDebug() << "DataFetchController: Database initialization failed, will use mock data";
        }
    });
}

DataFetchController::~DataFetchController()
{
    // 清理
}

void DataFetchController::fetchData()
{
    qDebug() << "DataFetchController::fetchData() - Forwarding request to DataService";
    
    if (m_symbols.isEmpty()) {
        updateStatus("请选择至少一个股票代码", 0);
        emit dataFetchError("未选择股票代码");
        return;
    }
    
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
        
        // 转发取消请求给DataService
        emit requestCancelOperation();
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
            
            // 转换规则
            QVector<DataCleaningEngine::CleaningRule> cleaningRules = 
                m_dataService->convertRules(rules);
            
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
    qDebug() << "DataFetchController::cleanDataAsync() - Getting data from DataManager cache";
    
    // 从DataManager获取当前缓存的数据
    QVariantList data = DataManager::instance()->getData("current_stock_data");
    
    if (data.isEmpty()) {
        qDebug() << "No data to clean in cache, checking if we have user parameters";
        
        // 检查是否有用户之前选择的参数
        if (m_currentStartDate.isEmpty() || m_currentEndDate.isEmpty()) {
            qDebug() << "No user parameters found, cannot query database";
            emit dataCleaningError("没有可用的数据参数，请先添加数据源并选择时间范围");
            return;
        }
        
        qDebug() << "Using user parameters to query database:";
        qDebug() << "  Symbol:" << (m_currentSymbol.isEmpty() ? "ALL" : m_currentSymbol);
        qDebug() << "  Start Date:" << m_currentStartDate;
        qDebug() << "  End Date:" << m_currentEndDate;
        
        // 更新状态
        updateStatus("缓存为空，正在从数据库重新查询数据...", 0);
        
        // 使用用户参数重新查询数据库
        // 这里直接调用loadFromDatabase，它会处理缓存和查询逻辑
        loadFromDatabase(m_currentSymbol, m_currentStartDate, m_currentEndDate);
        
        // 设置一个定时器，等待数据加载完成后再进行清洗
        QTimer::singleShot(1500, this, [this, rules]() {
            // 再次检查缓存
            QVariantList loadedData = DataManager::instance()->getData("current_stock_data");
            if (loadedData.isEmpty()) {
                qDebug() << "Failed to load data from database, cache still empty";
                emit dataCleaningError("数据库查询失败，请检查数据库连接和数据");
                return;
            }
            
            qDebug() << "Data loaded successfully from database, found" << loadedData.size() << "items, proceeding with cleaning";
            
            // 转发请求给DataService
            emit requestCleanData(loadedData, rules);
        });
        
        return;
    }
    
    qDebug() << "DataFetchController::cleanDataAsync() - Found" << data.size() << "items in cache, forwarding to DataService";
    
    // 转发请求给DataService
    emit requestCleanData(data, rules);
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
        
        // 缓存数据到DataManager - 支持数据集（空symbol）和单个股票
        if (!m_currentStartDate.isEmpty() && !m_currentEndDate.isEmpty()) {
            if (m_currentSymbol.isEmpty()) {
                // 数据集缓存：使用特殊键名
                QString datasetKey = QString("dataset_%1_%2").arg(m_currentStartDate).arg(m_currentEndDate);
                DataManager::instance()->storeData(datasetKey, data);
                qDebug() << "Dataset cached with key:" << datasetKey << "from" << m_currentStartDate << "to" << m_currentEndDate;
            } else {
                // 单个股票缓存
                DataManager::instance()->cacheStockData(m_currentSymbol, m_currentStartDate, m_currentEndDate, data);
                qDebug() << "Data cached for" << m_currentSymbol << "from" << m_currentStartDate << "to" << m_currentEndDate;
            }
        }
        
        // 同时将数据存储到通用缓存键，供清洗模块使用
        DataManager::instance()->storeData("current_stock_data", data);
        qDebug() << "Data also stored to 'current_stock_data' cache for cleaning module, count:" << data.size();
        
        // 更新状态
        updateStatus(message, 100);
        
        // 转发给QML
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
        
        // 更新CleaningResultModel
        updateCleaningResultModel(cleanedData);
        
        // 更新状态
        updateStatus(message, 100);
        
        // 转发给QML
        emit dataCleaningCompleted(true, message, cleanedData);
    } else {
        // 更新状态
        updateStatus("数据清洗失败: " + message, 0);
        
        // 转发给QML
        emit dataCleaningCompleted(false, message, QVariantList());
    }
}

// 接收DataService的数据清洗错误
void DataFetchController::onDataCleaningError(const QString& error)
{
    qDebug() << "DataFetchController::onDataCleaningError:" << error;
    
    // 更新状态
    updateStatus("数据清洗错误: " + error, 0);
    
    // 转发给QML
    emit dataCleaningError(error);
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

// 更新清洗结果模型
void DataFetchController::updateCleaningResultModel(const QVariantList& cleanedData)
{
    qDebug() << "DataFetchController::updateCleaningResultModel: Updating model with" << cleanedData.size() << "items";
    
    if (!m_cleaningResultModel) {
        qWarning() << "DataFetchController::updateCleaningResultModel: CleaningResultModel is null";
        return;
    }
    
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
    
    // 更新模型
    m_cleaningResultModel->updateResults(results);
    
    qDebug() << "DataFetchController::updateCleaningResultModel: Model updated with" << results.size() << "items";
}
