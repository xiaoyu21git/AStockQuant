// DataServiceConnectionExample.cpp
// 示例：如何连接DataFetchController和DataService

#include "DataFetchController.h"
#include "DataService.h"
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    qDebug() << "=== DataService Connection Example ===";
    
    // 1. 创建控制器和服务
    DataFetchController* controller = new DataFetchController();
    DataService* service = new DataService();
    
    qDebug() << "Created DataFetchController and DataService";
    
    // 2. 连接信号：控制器 -> 服务
    QObject::connect(controller, &DataFetchController::requestLoadData,
                     service, &DataService::loadDataAsync);
    QObject::connect(controller, &DataFetchController::requestCleanData,
                     service, &DataService::cleanDataAsync);
    QObject::connect(controller, &DataFetchController::requestCancelOperation,
                     service, &DataService::cancelCurrentOperation);
    
    qDebug() << "Connected controller -> service signals";
    
    // 3. 连接信号：服务 -> 控制器
    QObject::connect(service, &DataService::dataLoadProgress,
                     controller, &DataFetchController::onDataLoadProgress);
    QObject::connect(service, &DataService::dataLoadCompleted,
                     controller, &DataFetchController::onDataLoadCompleted);
    QObject::connect(service, &DataService::dataLoadError,
                     controller, &DataFetchController::onDataLoadError);
    QObject::connect(service, &DataService::dataCleaningProgress,
                     controller, &DataFetchController::onDataCleaningProgress);
    QObject::connect(service, &DataService::dataCleaningCompleted,
                     controller, &DataFetchController::onDataCleaningCompleted);
    QObject::connect(service, &DataService::dataCleaningError,
                     controller, &DataFetchController::onDataCleaningError);
    
    qDebug() << "Connected service -> controller signals";
    
    // 4. 连接信号：控制器 -> QML（示例）
    QObject::connect(controller, &DataFetchController::dataLoadedFromDatabase,
                     [](bool success, const QString& message, int dataCount) {
        qDebug() << "QML received dataLoadedFromDatabase:"
                 << success << message << "count:" << dataCount;
    });
    
    QObject::connect(controller, &DataFetchController::dataCleaningCompleted,
                     [](bool success, const QString& message, const QVariantList& data) {
        qDebug() << "QML received dataCleaningCompleted:"
                 << success << message << "data count:" << data.size();
    });
    
    qDebug() << "Connected controller -> QML signals (example)";
    
    // 5. 初始化数据库
    qDebug() << "Initializing database...";
    bool dbInitialized = service->initializeDatabase();
    
    if (!dbInitialized) {
        qDebug() << "Database initialization failed, but continuing with mock data...";
    } else {
        qDebug() << "Database initialized successfully";
    }
    
    // 6. 模拟QML调用
    qDebug() << "\n=== Simulating QML calls ===";
    
    // 模拟加载数据
    qDebug() << "1. QML calls loadFromDatabase('600000.SH', '2026-01-01', '2026-12-31')";
    controller->loadFromDatabase("600000.SH", "2026-01-01", "2026-12-31");
    
    // 等待数据加载完成
    QTimer::singleShot(3000, [controller, service]() {
        qDebug() << "\n2. Data loading should be complete by now";
        
        // 检查是否有数据
        if (controller->fetchedData().isEmpty()) {
            qDebug() << "No data loaded, using mock data for cleaning demo";
            
            // 创建模拟数据
            QVariantList mockData;
            for (int i = 0; i < 5000; i++) {
                QVariantMap record;
                record["symbol"] = "600000.SH";
                record["date"] = QString("2026-%1-%2").arg(i % 12 + 1).arg(i % 28 + 1);
                record["open"] = 10.0 + (i % 100) / 10.0;
                record["high"] = 12.0 + (i % 100) / 10.0;
                record["low"] = 8.0 + (i % 100) / 10.0;
                record["close"] = 11.0 + (i % 100) / 10.0;
                record["volume"] = 1000000 + i * 1000;
                mockData.append(record);
            }
            
            // 设置清洗规则
            QVariantMap rules;
            QVariantMap priceFilter;
            priceFilter["enabled"] = true;
            priceFilter["min"] = 0.0;
            priceFilter["max"] = 1000.0;
            rules["priceFilter"] = priceFilter;
            rules["completenessFilter"] = true;
            rules["duplicateFilter"] = true;
            
            // 模拟数据清洗
            qDebug() << "3. QML calls cleanDataAsync with" << mockData.size() << "records";
            controller->cleanDataAsync(mockData, rules);
        } else {
            qDebug() << "Data loaded successfully, count:" << controller->fetchedData().size();
            
            // 使用真实数据进行清洗
            QVariantMap rules;
            rules["completenessFilter"] = true;
            
            qDebug() << "3. QML calls cleanDataAsync with real data";
            controller->cleanDataAsync(controller->fetchedData(), rules);
        }
    });
    
    // 等待清洗完成
    QTimer::singleShot(6000, [&app]() {
        qDebug() << "\n=== Example completed ===";
        qDebug() << "All operations should be complete";
        qDebug() << "Check the logs above for progress and results";
        
        app.quit();
    });
    
    return app.exec();
}