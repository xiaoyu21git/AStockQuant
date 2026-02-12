// FoundationThreadPoolIntegration.cpp
// ============================================================================
// 示例：如何直接集成foundation线程池到现有Qt项目中
// 
// 本示例展示：
// 1. 如何创建和使用foundation线程池
// 2. 如何将Qt信号槽与foundation线程池集成
// 3. 如何实现异步数据库操作
// 4. 如何确保线程安全
// ============================================================================

#include <QObject>
#include <QDebug>
#include <QThread>
#include <QMetaObject>
#include <QVariantList>
#include <memory>
#include <functional>

// Foundation线程池头文件
#include "foundation/thread/thread_pool.hpp"

// 数据库相关头文件
#include "database/QtMySQLDatabase.h"

// ============================================================================
// 示例1：基础集成 - 使用foundation线程池执行异步任务
// ============================================================================

class AsyncDataProcessor : public QObject {
    Q_OBJECT
    
public:
    AsyncDataProcessor(QObject* parent = nullptr) 
        : QObject(parent) {
        // 创建foundation线程池（4个线程）
        m_threadPool = foundation::thread::ThreadPoolFactory::create_fixed(4);
    }
    
    ~AsyncDataProcessor() {
        // 关闭线程池
        if (m_threadPool) {
            m_threadPool->shutdown(true);
        }
    }
    
    // 异步处理数据
    void processDataAsync(const QVariantList& data) {
        qDebug() << "Starting async data processing for" << data.size() << "items";
        
        // 使用foundation线程池提交任务
        auto future = foundation::thread::async(*m_threadPool, [this, data]() {
            // 在工作线程中执行耗时操作
            QVariantList result;
            
            for (const auto& item : data) {
                // 模拟数据处理
                QThread::msleep(10);
                
                // 处理数据
                QVariantMap processedItem = item.toMap();
                processedItem["processed"] = true;
                processedItem["timestamp"] = QDateTime::currentDateTime().toString();
                
                result.append(processedItem);
            }
            
            // 返回结果
            return result;
        });
        
        // 异步获取结果（非阻塞）
        foundation::thread::async(*m_threadPool, [this, future = std::move(future)]() mutable {
            try {
                // 等待结果（在工作线程中）
                QVariantList result = future.get();
                
                // 通过信号槽机制将结果发送回主线程
                QMetaObject::invokeMethod(this, [this, result]() {
                    emit processingCompleted(result);
                });
                
            } catch (const std::exception& e) {
                QString error = QString("Processing error: %1").arg(e.what());
                QMetaObject::invokeMethod(this, [this, error]() {
                    emit processingError(error);
                });
            }
        });
    }
    
signals:
    void processingStarted();
    void processingProgress(int progress, const QString& message);
    void processingCompleted(const QVariantList& result);
    void processingError(const QString& error);
    
private:
    std::shared_ptr<foundation::thread::IExecutor> m_threadPool;
};

// ============================================================================
// 示例2：数据库操作集成 - 使用foundation线程池执行数据库查询
// ============================================================================

class DatabaseService : public QObject {
    Q_OBJECT
    
public:
    DatabaseService(QObject* parent = nullptr) 
        : QObject(parent) {
        // 创建IO密集型线程池（适合数据库操作）
        m_threadPool = foundation::thread::ThreadPoolFactory::create_io_intensive();
    }
    
    // 异步数据库查询
    void queryDataAsync(const QString& sql, const std::map<QString, QVariant>& params) {
        qDebug() << "Starting async database query:" << sql;
        
        // 确保数据库已初始化
        if (!ensureDatabaseInitialized()) {
            emit queryError("Database not initialized");
            return;
        }
        
        // 提交数据库查询任务到线程池
        m_threadPool->post([this, sql, params]() {
            try {
                // 通知查询开始
                QMetaObject::invokeMethod(this, [this]() {
                    emit queryStarted();
                });
                
                // 执行数据库查询（在工作线程中）
                auto result = m_database->executeQuery(sql, params);
                
                // 转换结果
                QVariantList data;
                for (const auto& row : result.getRows()) {
                    QVariantMap record;
                    // 根据实际列名转换数据
                    for (const auto& col : row.getColumns()) {
                        record[col.c_str()] = row.getString(col).c_str();
                    }
                    data.append(record);
                }
                
                // 发送结果回主线程
                QMetaObject::invokeMethod(this, [this, data]() {
                    emit queryCompleted(data);
                });
                
            } catch (const std::exception& e) {
                QString error = QString("Database query error: %1").arg(e.what());
                QMetaObject::invokeMethod(this, [this, error]() {
                    emit queryError(error);
                });
            }
        });
    }
    
    // 批量异步查询
    void batchQueryAsync(const std::vector<std::pair<QString, std::map<QString, QVariant>>>& queries) {
        qDebug() << "Starting batch async queries:" << queries.size() << "queries";
        
        // 使用并行算法处理批量查询
        auto futures = foundation::thread::parallel_transform(*m_threadPool, 
            queries.begin(), queries.end(),
            [this](const auto& query) -> QVariantList {
                try {
                    auto result = m_database->executeQuery(query.first, query.second);
                    
                    QVariantList data;
                    for (const auto& row : result.getRows()) {
                        QVariantMap record;
                        for (const auto& col : row.getColumns()) {
                            record[col.c_str()] = row.getString(col).c_str();
                        }
                        data.append(record);
                    }
                    return data;
                    
                } catch (const std::exception& e) {
                    qWarning() << "Query failed:" << e.what();
                    return QVariantList();
                }
            });
        
        // 收集所有结果
        m_threadPool->post([this, futures = std::move(futures)]() mutable {
            try {
                QVariantList allResults;
                
                for (auto& future : futures) {
                    try {
                        QVariantList result = future.get();
                        allResults.append(result);
                    } catch (const std::exception& e) {
                        qWarning() << "Failed to get query result:" << e.what();
                    }
                }
                
                // 发送合并结果回主线程
                QMetaObject::invokeMethod(this, [this, allResults]() {
                    emit batchQueryCompleted(allResults);
                });
                
            } catch (const std::exception& e) {
                QString error = QString("Batch query error: %1").arg(e.what());
                QMetaObject::invokeMethod(this, [this, error]() {
                    emit queryError(error);
                });
            }
        });
    }
    
signals:
    void queryStarted();
    void queryProgress(int progress, const QString& message);
    void queryCompleted(const QVariantList& data);
    void batchQueryCompleted(const QVariantList& allResults);
    void queryError(const QString& error);
    
private:
    bool ensureDatabaseInitialized() {
        // 初始化数据库连接
        if (!m_database) {
            try {
                astock::database::DatabaseConfig config;
                config.host = "localhost";
                config.port = 3306;
                config.database = "astock_quant";
                config.username = "root";
                config.password = "123456a";
                config.charset = "utf8mb4";
                config.pool_size = 5;
                config.driver = "QMYSQL";
                
                m_database = std::make_shared<astock::database::QtMySQLDatabase>(config, true);
                
                if (!m_database->open()) {
                    qCritical() << "Failed to open database";
                    return false;
                }
                
            } catch (const std::exception& e) {
                qCritical() << "Database initialization error:" << e.what();
                return false;
            }
        }
        return true;
    }
    
private:
    std::shared_ptr<foundation::thread::IExecutor> m_threadPool;
    std::shared_ptr<astock::database::QtMySQLDatabase> m_database;
};

// ============================================================================
// 示例3：高级集成 - 带进度报告和取消支持的异步操作
// ============================================================================

class AdvancedAsyncService : public QObject {
    Q_OBJECT
    
public:
    AdvancedAsyncService(QObject* parent = nullptr) 
        : QObject(parent)
        , m_cancelled(false) {
        // 创建CPU感知线程池
        m_threadPool = foundation::thread::ThreadPoolFactory::create_cpu_aware();
    }
    
    // 启动长时间运行的任务
    void startLongRunningTask(const QVariantList& inputData) {
        // 重置取消标志
        m_cancelled.store(false);
        
        // 提交任务到线程池
        m_threadPool->post([this, inputData]() {
            try {
                // 通知任务开始
                QMetaObject::invokeMethod(this, [this]() {
                    emit taskStarted();
                });
                
                QVariantList results;
                int totalItems = inputData.size();
                
                for (int i = 0; i < totalItems; i++) {
                    // 检查是否被取消
                    if (m_cancelled.load()) {
                        QMetaObject::invokeMethod(this, [this]() {
                            emit taskCancelled();
                        });
                        return;
                    }
                    
                    // 处理当前项
                    const auto& item = inputData[i];
                    QVariantMap result = processItem(item.toMap());
                    results.append(result);
                    
                    // 更新进度
                    int progress = (i + 1) * 100 / totalItems;
                    QString message = QString("Processing item %1/%2").arg(i + 1).arg(totalItems);
                    
                    QMetaObject::invokeMethod(this, [this, progress, message]() {
                        emit taskProgress(progress, message);
                    });
                    
                    // 模拟处理时间
                    QThread::msleep(50);
                }
                
                // 任务完成
                QMetaObject::invokeMethod(this, [this, results]() {
                    emit taskCompleted(results);
                });
                
            } catch (const std::exception& e) {
                QString error = QString("Task error: %1").arg(e.what());
                QMetaObject::invokeMethod(this, [this, error]() {
                    emit taskError(error);
                });
            }
        });
    }
    
    // 取消当前任务
    void cancelTask() {
        m_cancelled.store(true);
    }
    
signals:
    void taskStarted();
    void taskProgress(int progress, const QString& message);
    void taskCompleted(const QVariantList& results);
    void taskError(const QString& error);
    void taskCancelled();
    
private:
    QVariantMap processItem(const QVariantMap& item) {
        // 模拟数据处理
        QVariantMap result = item;
        result["processed"] = true;
        result["processingTime"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
        
        // 模拟一些计算
        double value = item.value("value", 0.0).toDouble();
        result["squared"] = value * value;
        result["sqrt"] = std::sqrt(std::abs(value));
        
        return result;
    }
    
private:
    std::shared_ptr<foundation::thread::IExecutor> m_threadPool;
    std::atomic<bool> m_cancelled;
};

// ============================================================================
// 示例4：集成到现有DataService的简化版本
// ============================================================================

class IntegratedDataService : public QObject {
    Q_OBJECT
    
public:
    IntegratedDataService(QObject* parent = nullptr) 
        : QObject(parent) {
        // 使用全局线程池实例
        m_threadPool = FOUNDATION_THREADS;
    }
    
    // 异步加载数据（集成版本）
    void loadDataAsyncIntegrated(const QString& symbol, 
                                const QString& startDate, 
                                const QString& endDate) {
        qDebug() << "Integrated loadDataAsync:" << symbol << startDate << endDate;
        
        // 使用foundation线程池提交任务
        m_threadPool->post([this, symbol, startDate, endDate]() {
            try {
                // 通知开始
                QMetaObject::invokeMethod(this, [this]() {
                    emit dataLoadStarted();
                });
                
                // 确保数据库已初始化
                if (!ensureDatabaseInitialized()) {
                    QMetaObject::invokeMethod(this, [this]() {
                        emit dataLoadError("Database not initialized");
                    });
                    return;
                }
                
                // 构建查询
                QString sql = "SELECT symbol, trade_date, open, high, low, close, volume "
                             "FROM daily_bar "
                             "WHERE symbol = :symbol "
                             "AND trade_date BETWEEN :start_date AND :end_date "
                             "ORDER BY trade_date ASC";
                
                std::map<QString, QVariant> params;
                params[":symbol"] = symbol;
                params[":start_date"] = startDate;
                params[":end_date"] = endDate;
                
                // 执行查询
                auto result = m_database->executeQuery(sql, params);
                
                // 转换结果
                QVariantList data;
                for (const auto& row : result.getRows()) {
                    QVariantMap record;
                    record["symbol"] = row.getString("symbol").c_str();
                    record["date"] = row.getString("trade_date").c_str();
                    record["open"] = row.getDouble("open");
                    record["high"] = row.getDouble("high");
                    record["low"] = row.getDouble("low");
                    record["close"] = row.getDouble("close");
                    record["volume"] = row.getDouble("volume");
                    
                    data.append(record);
                }
                
                // 发送结果
                QMetaObject::invokeMethod(this, [this, data]() {
                    emit dataLoadCompleted(data);
                });
                
            } catch (const std::exception& e) {
                QString error = QString("Load error: %1").arg(e.what());
                QMetaObject::invokeMethod(this, [this, error]() {
                    emit dataLoadError(error);
                });
            }
        });
    }
    
signals:
    void dataLoadStarted();
    void dataLoadProgress(int progress, const QString& message);
    void dataLoadCompleted(const QVariantList& data);
    void dataLoadError(const QString& error);
    
private:
    bool ensureDatabaseInitialized() {
        // 数据库初始化逻辑
        // ...
        return true;
    }
    
private:
    std::shared_ptr<foundation::thread::IExecutor> m_threadPool;
    std::shared_ptr<astock::database::QtMySQLDatabase> m_database;
};

// ============================================================================
// 使用示例
// ============================================================================

void demonstrateIntegration() {
    qDebug() << "=== Foundation Thread Pool Integration Demo ===";
    
    // 示例1：基础集成
    {
        qDebug() << "\n1. Basic Integration Example:";
        AsyncDataProcessor processor;
        
        QObject::connect(&processor, &AsyncDataProcessor::processingCompleted,
            [](const QVariantList& result) {
                qDebug() << "Processing completed, got" << result.size() << "items";
            });
        
        QVariantList testData;
        for (int i = 0; i < 10; i++) {
            testData.append(QVariantMap{{"id", i}, {"value", i * 10}});
        }
        
        processor.processDataAsync(testData);
    }
    
    // 示例3：高级集成（带取消）
    {
        qDebug() << "\n3. Advanced Integration with Cancellation:";
        AdvancedAsyncService service;
        
        QObject::connect(&service, &AdvancedAsyncService::taskProgress,
            [](int progress, const QString& message) {
                qDebug() << "Progress:" << progress << "% -" << message;
            });
        
        QVariantList largeData;
        for (int i = 0; i < 100; i++) {
            largeData.append(QVariantMap{{"id", i}, {"value", i * 5}});
        }
        
        service.startLongRunningTask(largeData);
        
        // 模拟取消
        QTimer::singleShot(1000, [&service]() {
            qDebug() << "Cancelling task...";
            service.cancelTask();
        });
    }
    
    qDebug() << "\n=== Demo Complete ===";
}

// 主函数（示例）
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    
    // 演示集成
    demonstrateIntegration();
    
    return app.exec();
}

// 包含Q_OBJECT的类需要moc处理
#include "FoundationThreadPoolIntegration.moc"