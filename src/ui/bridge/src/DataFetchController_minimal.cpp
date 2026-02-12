// DataFetchController.cpp - 最小版本，只实现基本QML功能
#include "DataFetchController.h"

#include <QDebug>
#include <QDateTime>
#include <QMutexLocker>
#include <QCoreApplication>
#include <QTimer>
#include <QUuid>

DataFetchController::DataFetchController(QObject* parent)
    : QObject(parent)
{
    // 设置默认日期（最近30天）
    QDateTime currentDate = QDateTime::currentDateTime();
    QDateTime startDate = currentDate.addDays(-30);
    
    m_startDate = startDate.toString("yyyy-MM-dd");
    m_endDate = currentDate.toString("yyyy-MM-dd");
    
    // 延迟初始化
    QTimer::singleShot(100, this, &DataFetchController::initialize);
}

DataFetchController::~DataFetchController()
{
    // 清理
}

void DataFetchController::initialize()
{
    try {
        m_initialized = true;
        updateStatus("初始化完成", 100);
        qDebug() << "DataFetchController: Initialized successfully";
        
    } catch (const std::exception& e) {
        qCritical() << "Initialization error:" << e.what();
        updateStatus(QString("初始化错误: %1").arg(e.what()), 0);
    }
}

void DataFetchController::fetchData()
{
    if (!m_initialized) {
        updateStatus("未初始化，请稍后重试", 0);
        emit dataFetchError("未初始化");
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
    
    // 模拟数据获取
    simulateDataFetch();
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
        updateStatus("未初始化", 0);
        emit dataSavedToDatabase(false, "未初始化");
        return;
    }
    
    if (m_fetchedData.isEmpty()) {
        updateStatus("没有数据可保存", 0);
        emit dataSavedToDatabase(false, "没有数据可保存");
        return;
    }
    
    updateStatus("正在保存数据到数据库...", 50);
    
    // 模拟保存
    QTimer::singleShot(1000, this, [this]() {
        updateStatus("数据保存完成", 100);
        emit dataSavedToDatabase(true, "数据保存成功");
    });
}

void DataFetchController::loadFromDatabase(const QString& symbol, const QString& startDate, const QString& endDate)
{
    if (!m_initialized) {
        updateStatus("未初始化", 0);
        emit dataLoadedFromDatabase(false, "未初始化", 0);
        return;
    }
    
    updateStatus("正在从数据库加载数据...", 0);
    
    // 模拟加载
    QTimer::singleShot(1500, this, [this, symbol, startDate, endDate]() {
        QMutexLocker locker(&m_mutex);
        
        // 创建模拟数据
        QVariantList mockData;
        for (int i = 0; i < 10; i++) {
            QVariantMap item;
            item["symbol"] = symbol;
            item["date"] = QDateTime::currentDateTime().addDays(-i).toString("yyyy-MM-dd");
            item["open"] = 10.0 + i * 0.1;
            item["high"] = 10.5 + i * 0.1;
            item["low"] = 9.8 + i * 0.1;
            item["close"] = 10.2 + i * 0.1;
            item["volume"] = 1000000 + i * 10000;
            mockData.append(item);
        }
        
        m_fetchedData = mockData;
        
        updateStatus("数据加载完成", 100);
        emit fetchedDataChanged();
        emit dataLoadedFromDatabase(true, "数据加载成功", mockData.size());
    });
}

void DataFetchController::simulateDataFetch()
{
    // 模拟进度更新
    for (int i = 0; i <= 100; i += 10) {
        QTimer::singleShot(i * 50, this, [this, i]() {
            if (!m_isFetching) return;
            
            m_progress = i;
            QString message = QString("正在获取数据... %1%").arg(i);
            updateStatus(message, i);
            
            emit progressChanged();
            emit dataFetchProgress(i, message);
            
            // 完成时添加数据
            if (i == 100) {
                QMutexLocker locker(&m_mutex);
                
                // 创建模拟数据
                QVariantList mockData;
                for (int j = 0; j < 5; j++) {
                    QVariantMap item;
                    item["symbol"] = m_symbols.isEmpty() ? "000001.SZ" : m_symbols.first();
                    item["date"] = QDateTime::currentDateTime().addDays(-j).toString("yyyy-MM-dd");
                    item["open"] = 10.0 + j * 0.1;
                    item["high"] = 10.5 + j * 0.1;
                    item["low"] = 9.8 + j * 0.1;
                    item["close"] = 10.2 + j * 0.1;
                    item["volume"] = 1000000 + j * 10000;
                    mockData.append(item);
                }
                
                m_fetchedData = mockData;
                m_isFetching = false;
                
                updateStatus("数据获取完成", 100);
                
                emit isFetchingChanged();
                emit fetchedDataChanged();
                emit dataFetchCompleted(true, "数据获取成功", mockData.size());
            }
        });
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