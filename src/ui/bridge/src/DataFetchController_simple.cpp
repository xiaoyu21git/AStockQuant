/如果/ DataFetchController.cpp - 简化版本，专注于QML集成
#include "DataFetchController.h"

#include <QDebug>
#include <QDateTime>
#include <QMutexLocker>
#include <QCoreApplication>
#include <QTimer>
#include <QUuid>

// 事件类型定义
constexpr const char* EVENT_DATA_FETCH_REQUEST = "data.fetch.request";
constexpr const char* EVENT_DATA_FETCH_PROGRESS = "data.fetch.progress";
constexpr const char* EVENT_DATA_FETCH_COMPLETE = "data.fetch.complete";
constexpr const char* EVENT_DATA_FETCH_ERROR = "data.fetch.error";
constexpr const char* EVENT_DATABASE_SAVE_RESULT = "database.save.result";
constexpr const char* EVENT_DATABASE_LOAD_RESULT = "database.load.result";

DataFetchController::DataFetchController(QObject* parent)
    : QObject(parent)
{
    // 设置默认日期（最近30天）
    QDateTime currentDate = QDateTime::currentDateTime();
    QDateTime startDate = currentDate.addDays(-30);
    
    m_startDate = startDate.toString("yyyy-MM-dd");
    m_endDate = currentDate.toString("yyyy-MM-dd");
    
    // 延迟初始化EventBus（避免在构造函数中阻塞）
    QTimer::singleShot(100, this, &DataFetchController::initializeEventBus);
}

DataFetchController::~DataFetchController()
{
    // 取消事件订阅
    if (m_eventBus && m_eventSubscription.isValid()) {
        m_eventBus->unsubscribe(m_eventSubscription);
    }
}

void DataFetchController::initializeEventBus()
{
    try {
        // 获取全局EventBus实例
        m_eventBus = engine::GlobalEventBus::instance();
        
        if (!m_eventBus) {
            qWarning() << "Failed to get EventBus instance";
            updateStatus("EventBus初始化失败", 0);
            return;
        }
        
        // 启动EventBus
        if (!m_eventBus->is_running()) {
            m_eventBus->start();
        }
        
        // 订阅数据获取相关事件
        auto handler = [this](const engine::EventFormat& event) {
            QMetaObject::invokeMethod(this, [this, event]() {
                QString eventType = QString::fromStdString(event.type);
                
                if (eventType == EVENT_DATA_FETCH_PROGRESS) {
                    handleDataProgressEvent(event);
                } else if (eventType == EVENT_DATA_FETCH_COMPLETE) {
                    handleDataCompleteEvent(event);
                } else if (eventType == EVENT_DATA_FETCH_ERROR) {
                    handleDataErrorEvent(event);
                } else if (eventType == EVENT_DATABASE_SAVE_RESULT) {
                    handleDatabaseSaveEvent(event);
                } else if (eventType == EVENT_DATABASE_LOAD_RESULT) {
                    handleDatabaseLoadEvent(event);
                }
            });
        };
        
        // 订阅多个事件类型
        m_eventSubscription = m_eventBus->subscribe(
            EVENT_DATA_FETCH_PROGRESS,
            handler,
            nullptr, // 无过滤器
            0 // 最高优先级
        );
        
        // 订阅其他事件类型
        m_eventBus->subscribe(EVENT_DATA_FETCH_COMPLETE, handler);
        m_eventBus->subscribe(EVENT_DATA_FETCH_ERROR, handler);
        m_eventBus->subscribe(EVENT_DATABASE_SAVE_RESULT, handler);
        m_eventBus->subscribe(EVENT_DATABASE_LOAD_RESULT, handler);
        
        m_initialized = true;
        updateStatus("EventBus初始化完成", 100);
        qDebug() << "DataFetchController: EventBus initialized successfully";
        
    } catch (const std::exception& e) {
        qCritical() << "EventBus initialization error:" << e.what();
        updateStatus(QString("EventBus初始化错误: %1").arg(e.what()), 0);
    }
}

void DataFetchController::fetchData()
{
    if (!m_initialized) {
        updateStatus("EventBus未初始化，请稍后重试", 0);
        emit dataFetchError("EventBus未初始化");
        return;
    }
    
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
    
    // 发送数据获取请求
    sendDataFetchRequest();
}

void DataFetchController::cancelFetch()
{
    if (m_isFetching) {
        m_isFetching = false;
        emit isFetchingChanged();
        updateStatus("数据获取已取消", 0);
    }
}

void DataFetchController::clearData()
{
    QMutexLocker locker(&m_mutex);
    m_fetchedData.clear();
    emit fetchedDataChanged();
    updateStatus("数据已清空", 0);
}

void DataFetchController::saveToDatabase()
{
    if (!m_initialized) {
        updateStatus("EventBus未初始化", 0);
        emit dataSavedToDatabase(false, "EventBus未初始化");
        return;
    }
    
    if (m_fetchedData.isEmpty()) {
        updateStatus("没有数据可保存", 0);
        emit dataSavedToDatabase(false, "没有数据可保存");
        return;
    }
    
    updateStatus("正在保存数据到数据库...", 0);
    
    try {
        // 创建数据库保存事件
        engine::EventFormat saveEvent;
        saveEvent.type = "database.save.request";
        saveEvent.source = engine::Event_Core::EventSource::USER;
        saveEvent.generate_id();
        
        // 添加数据到事件
        QVariantMap saveData;
        saveData["data"] = m_fetchedData;
        saveData["symbols"] = m_symbols;
        saveData["dataType"] = m_dataType;
        
        // 将QVariantMap转换为JSON字符串
        // 这里简化处理，实际项目中需要完整的序列化
        saveEvent.set("data_count", m_fetchedData.size());
        saveEvent.set("symbols_count", m_symbols.size());
        saveEvent.set("data_type", m_dataType.toStdString());
        
        // 发布事件
        auto result = m_eventBus->publish(saveEvent);
        
        if (result) {
            updateStatus("数据库保存请求已发送", 50);
        } else {
            updateStatus("数据库保存请求发送失败", 0);
            emit dataSavedToDatabase(false, "请求发送失败");
        }
        
    } catch (const std::exception& e) {
        qCritical() << "Database save error:" << e.what();
        updateStatus(QString("数据库保存错误: %1").arg(e.what()), 0);
        emit dataSavedToDatabase(false, QString("错误: %1").arg(e.what()));
    }
}

void DataFetchController::loadFromDatabase(const QString& symbol, const QString& startDate, const QString& endDate)
{
    if (!m_initialized) {
        updateStatus("EventBus未初始化", 0);
        emit dataLoadedFromDatabase(false, "EventBus未初始化", 0);
        return;
    }
    
    updateStatus("正在从数据库加载数据...", 0);
    
    try {
        // 创建数据库加载事件
        engine::EventFormat loadEvent;
        loadEvent.type = "database.load.request";
        loadEvent.source = engine::Event_Core::EventSource::USER;
        loadEvent.generate_id();
        
        // 添加查询参数到事件
        loadEvent.set("symbol", symbol.toStdString());
        loadEvent.set("start_date", startDate.toStdString());
        loadEvent.set("end_date", endDate.toStdString());
        loadEvent.set("data_type", m_dataType.toStdString());
        
        // 发布事件
        auto result = m_eventBus->publish(loadEvent);
        
        if (result) {
            updateStatus("数据库加载请求已发送", 50);
        } else {
            updateStatus("数据库加载请求发送失败", 0);
            emit dataLoadedFromDatabase(false, "请求发送失败", 0);
        }
        
    } catch (const std::exception& e) {
        qCritical() << "Database load error:" << e.what();
        updateStatus(QString("数据库加载错误: %1").arg(e.what()), 0);
        emit dataLoadedFromDatabase(false, QString("错误: %1").arg(e.what()), 0);
    }
}

void DataFetchController::sendDataFetchRequest()
{
    try {
        // 创建数据获取事件
        engine::EventFormat fetchEvent;
        fetchEvent.type = EVENT_DATA_FETCH_REQUEST;
        fetchEvent.source = engine::Event_Core::EventSource::USER;
        fetchEvent.generate_id();
        
        // 构建请求数据
        fetchEvent.set("data_source", m_dataSource.toStdString());
        fetchEvent.set("symbols", m_symbols.join(",").toStdString());
        fetchEvent.set("start_date", m_startDate.toStdString());
        fetchEvent.set("end_date", m_endDate.toStdString());
        fetchEvent.set("data_type", m_dataType.toStdString());
        fetchEvent.set("request_id", QUuid::createUuid().toString().toStdString());
        
        // 发布事件
        auto result = m_eventBus->publish(fetchEvent);
        
        if (result) {
            updateStatus("数据获取请求已发送", 10);
        } else {
            updateStatus("数据获取请求发送失败", 0);
            emit dataFetchError("请求发送失败");
            
            m_isFetching = false;
            emit isFetchingChanged();
        }
        
    } catch (const std::exception& e) {
        qCritical() << "Data fetch request error:" << e.what();
        updateStatus(QString("数据获取请求错误: %1").arg(e.what()), 0);
        emit dataFetchError(QString("请求错误: %1").arg(e.what()));
        
        m_isFetching = false;
        emit isFetchingChanged();
    }
}

void DataFetchController::handleDataProgressEvent(const engine::EventFormat& event)
{
    if (!m_isFetching) return;
    
    try {
        // 从事件中提取进度信息
        auto progressOpt = event.get<int>("progress");
        auto messageOpt = event.get<std::string>("message");
        
        int progress = progressOpt.value_or(0);
        QString message = messageOpt ? QString::fromStdString(*messageOpt) : "正在获取数据...";
        
        m_progress = progress;
        updateStatus(message, progress);
        
        emit progressChanged();
        emit dataFetchProgress(progress, message);
        
    } catch (const std::exception& e) {
        qWarning() << "Progress event handling error:" << e.what();
    }
}

void DataFetchController::handleDataCompleteEvent(const engine::EventFormat& event)
{
    QMutexLocker locker(&m_mutex);
    
    try {
        m_isFetching = false;
        m_progress = 100;
        
        // 从事件中提取数据
        auto dataCountOpt = event.get<int>("count");
        auto messageOpt = event.get<std::string>("message");
        auto dataOpt = event.get<std::string>("data");
        
        int dataCount = dataCountOpt.value_or(0);
        QString message = messageOpt ? QString::fromStdString(*messageOpt) : "数据获取完成";
        
        QVariantList fetchedData;
        
        // 尝试从事件中提取真实数据
        if (dataOpt && !dataOpt->empty()) {
            try {
                // 解析JSON数据
                QString jsonData = QString::fromStdString(*dataOpt);
                QJsonDocument doc = QJsonDocument::fromJson(jsonData.toUtf8());
                
                if (doc.isArray()) {
                    QJsonArray jsonArray = doc.array();
                    for (const QJsonValue& value : jsonArray) {
                        if (value.isObject()) {
                            QJsonObject obj = value.toObject();
                            QVariantMap item;
                            
                            // 提取标准字段
                            if (obj.contains("symbol")) {
                                item["symbol"] = obj["symbol"].toString();
                            }
                            if (obj.contains("date")) {
                                item["date"] = obj["date"].toString();
                            }
                            if (obj.contains("trade_date")) {
                                item["date"] = obj["trade_date"].toString();
                            }
                            if (obj.contains("open")) {
                                item["open"] = obj["open"].toDouble();
                            }
                            if (obj.contains("high")) {
                                item["high"] = obj["high"].toDouble();
                            }
                            if (obj.contains("low")) {
                                item["low"] = obj["low"].toDouble();
                            }
                            if (obj.contains("close")) {
                                item["close"] = obj["close"].toDouble();
                            }
                            if (obj.contains("volume")) {
                                item["volume"] = obj["volume"].toDouble();
                            }
                            
                            // 如果没有symbol，使用默认值
                            if (!item.contains("symbol") && !m_symbols.isEmpty()) {
                                item["symbol"] = m_symbols.first();
                            }
                            
                            fetchedData.append(item);
                        }
                    }
                }
                
                qDebug() << "Successfully parsed" << fetchedData.size() << "records from event data";
                
            } catch (const std::exception& e) {
                qWarning() << "Failed to parse event data:" << e.what();
                // 如果解析失败，不创建模拟数据，保持空列表
                // 这符合"没有就是没有"的原则
            }
        } else {
            // 如果没有数据字段，保持空列表
            // 这符合"没有就是没有"的原则
        }
        
        // 更新数据
        m_fetchedData = fetchedData;
        
        updateStatus(message, 100);
        
        emit isFetchingChanged();
        emit progressChanged();
        emit fetchedDataChanged();
        emit dataFetchCompleted(true, message, fetchedData.size());
        
        qDebug() << "Data fetch completed:" << fetchedData.size() << "records, message:" << message;
        
    } catch (const std::exception& e) {
        qCritical() << "Complete event handling error:" << e.what();
        updateStatus(QString("数据处理错误: %1").arg(e.what()), 0);
        emit dataFetchError(QString("数据处理错误: %1").arg(e.what()));
        
        m_isFetching = false;
        emit isFetchingChanged();
    }
}

void DataFetchController::handleDataErrorEvent(const engine::EventFormat& event)
{
    QMutexLocker locker(&m_mutex);
    
    try {
        m_isFetching = false;
        
        // 从事件中提取错误信息
        auto errorOpt = event.get<std::string>("error");
        QString errorMessage = errorOpt ? QString::fromStdString(*errorOpt) : "数据获取失败";
        
        updateStatus(errorMessage, 0);
        
        emit isFetchingChanged();
        emit dataFetchError(errorMessage);
        
        qWarning() << "Data fetch error:" << errorMessage;
        
    } catch (const std::exception& e) {
        qCritical() << "Error event handling error:" << e.what();
    }
}

void DataFetchController::handleDatabaseSaveEvent(const engine::EventFormat& event)
{
    try {
        // 从事件中提取保存结果
        auto successOpt = event.get<bool>("success");
        auto messageOpt = event.get<std::string>("message");
        
        bool success = successOpt.value_or(false);
        QString message = messageOpt ? QString::fromStdString(*messageOpt) : "数据库保存完成";
        
        updateStatus(message, success ? 100 : 0);
        emit dataSavedToDatabase(success, message);
        
        qDebug() << "Database save result:" << message;
        
    } catch (const std::exception& e) {
        qWarning() << "Database save event handling error:" << e.what();
    }
}

void DataFetchController::handleDatabaseLoadEvent(const engine::EventFormat& event)
{
    QMutexLocker locker(&m_mutex);
    
    try {
        // 从事件中提取加载结果
        auto successOpt = event.get<bool>("success");
        auto messageOpt = event.get<std::string>("message");
        auto dataCountOpt = event.get<int>("count");
        auto dataOpt = event.get<std::string>("data");
        
        bool success = successOpt.value_or(false);
        QString message = messageOpt ? QString::fromStdString(*messageOpt) : "数据库加载完成";
        int dataCount = dataCountOpt.value_or(0);
        
        QVariantList loadedData;
        
        if (success && dataOpt && !dataOpt->empty()) {
            try {
                // 解析JSON数据
                QString jsonData = QString::fromStdString(*dataOpt);
                QJsonDocument doc = QJsonDocument::fromJson(jsonData.toUtf8());
                
                if (doc.isArray()) {
                    QJsonArray jsonArray = doc.array();
                    for (const QJsonValue& value : jsonArray) {
                        if (value.isObject()) {
                            QJsonObject obj = value.toObject();
                            QVariantMap item;
                            
                            // 提取标准字段
                            if (obj.contains("symbol")) {
                                item["symbol"] = obj["symbol"].toString();
                            }
                            if (obj.contains("date")) {
                                item["date"] = obj["date"].toString();
                            }
                            if (obj.contains("trade_date")) {
                                item["date"] = obj["trade_date"].toString();
                            }
                            if (obj.contains("open")) {
                                item["open"] = obj["open"].toDouble();
                            }
                            if (obj.contains("high")) {
                                item["high"] = obj["high"].toDouble();
                            }
                            if (obj.contains("low")) {
                                item["low"] = obj["low"].toDouble();
                            }
                            if (obj.contains("close")) {
                                item["close"] = obj["close"].toDouble();
                            }
                            if (obj.contains("volume")) {
                                item["volume"] = obj["volume"].toDouble();
                            }
                            
                            loadedData.append(item);
                        }
                    }
                }
                
                qDebug() << "Successfully parsed" << loadedData.size() << "records from database load event";
                
            } catch (const std::exception& e) {
                qWarning() << "Failed to parse database load data:" << e.what();
                // 如果解析失败，保持空列表
            }
        }
        
        // 更新数据
        m_fetchedData = loadedData;
        if (!loadedData.isEmpty()) {
            emit fetchedDataChanged();
        }
        
        updateStatus(message, success ? 100 : 0);
        emit dataLoadedFromDatabase(success, message, loadedData.size());
        
        qDebug() << "Database load result:" << message << loadedData.size() << "records";
        
    } catch (const std::exception& e) {
        qWarning() << "Database load event handling error:" << e.what();
    }
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