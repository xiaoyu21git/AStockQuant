// DataService.cpp - 数据服务实现
// ============================================================================
// 功能概述：
// 1. 数据库管理：MySQL数据库连接、初始化、连接池管理
// 2. 数据操作：异步数据加载、保存、查询、清洗
// 3. 进度报告：实时进度更新和状态反馈
// 4. 错误处理：完善的异常处理和用户友好错误信息
// 5. 异步处理：所有耗时操作在工作线程中执行，不阻塞UI
//
// 架构设计原则：
// 1. 单一职责：每个类/方法只负责一个明确的功能
// 2. 异步优先：所有耗时操作都异步执行
// 3. 错误恢复：具备后备机制和重试能力
// 4. 进度透明：实时向UI报告操作进度
// 5. 资源管理：自动管理数据库连接和内存资源
// ============================================================================

#include "DataService.h"
#include "DataManager.h"
#include <QDebug>
#include <QThread>
#include <QThreadPool>
#include <QRunnable>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QUuid>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <memory>
#include <atomic>

// Foundation线程池头文件
#include "foundation/thread/thread_pool.hpp"

// EventBus头文件
#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"

// 异步数据加载任务
class DataLoadTask : public QRunnable {
public:
    DataLoadTask(DataService* service, 
                 const QString& symbol, 
                 const QString& startDate, 
                 const QString& endDate)
        : m_service(service)
        , m_symbol(symbol)
        , m_startDate(startDate)
        , m_endDate(endDate)
    {
        setAutoDelete(true);
    }
    
    void run() override {
        try {
            // 通知加载开始
            QMetaObject::invokeMethod(m_service, [this]() {
                emit m_service->dataLoadStarted();
            });
            
            // 更新进度
            QMetaObject::invokeMethod(m_service, [this]() {
                emit m_service->dataLoadProgress(10, "正在连接数据库...");
            });
            
            // 确保数据库已初始化
            if (!m_service->ensureDatabaseInitialized()) {
                QString error = "数据库未初始化";
                QMetaObject::invokeMethod(m_service, [this, error]() {
                    emit m_service->dataLoadError(error);
                });
                return;
            }
            
            // 更新进度
            QMetaObject::invokeMethod(m_service, [this]() {
                emit m_service->dataLoadProgress(30, "正在执行查询...");
            });
            
            // 执行查询
            QVariantList data;
            QString errorMessage;
            bool success = executeQuery(data, errorMessage);
            
            if (success) {
                // 更新进度
                QMetaObject::invokeMethod(m_service, [this, data]() {
                    emit m_service->dataLoadProgress(100, "数据加载完成");
                    emit m_service->dataLoadCompleted(true, 
                        QString("成功加载 %1 条数据").arg(data.size()), 
                        data);
                });
            } else {
                QMetaObject::invokeMethod(m_service, [this, errorMessage]() {
                    emit m_service->dataLoadError(errorMessage);
                });
            }
            
        } catch (const std::exception& e) {
            QString error = QString("数据加载错误: %1").arg(e.what());
            QMetaObject::invokeMethod(m_service, [this, error]() {
                emit m_service->dataLoadError(error);
            });
        }
    }
    
private:
    bool executeQuery(QVariantList& data, QString& errorMessage) {
        try {
            // 获取数据库实例
            auto database = m_service->m_database;
            if (!database) {
                errorMessage = "数据库未初始化";
                return false;
            }
            
            // 构建SQL查询 - 支持空symbol查询所有股票
            QString sql;
            std::map<QString, QVariant> params;
            
            if (m_symbol.isEmpty()) {
                // 查询所有股票 - 按照时间顺序排列（从开始日期到结束日期）
                sql = "SELECT symbol, trade_date, open, high, low, close, volume "
                     "FROM daily_bar "
                     "WHERE trade_date BETWEEN :start_date AND :end_date "
                     "ORDER BY trade_date ASC, symbol ASC";
                qDebug() << "Executing SQL query for ALL stocks (time-ordered):" << sql;
            } else {
                // 查询特定股票 - 按照时间顺序排列
                sql = "SELECT symbol, trade_date, open, high, low, close, volume "
                     "FROM daily_bar "
                     "WHERE symbol = :symbol "
                     "AND trade_date BETWEEN :start_date AND :end_date "
                     "ORDER BY trade_date ASC";
                params[":symbol"] = m_symbol;
                qDebug() << "Executing SQL query for specific stock (time-ordered):" << sql;
            }
            
            // 设置查询参数
            params[":start_date"] = m_startDate;
            params[":end_date"] = m_endDate;
            
            qDebug() << "Parameters: symbol=" << (m_symbol.isEmpty() ? "ALL" : m_symbol)
                     << ", start_date=" << m_startDate 
                     << ", end_date=" << m_endDate;
            
            // 更新进度
            QMetaObject::invokeMethod(m_service, [this]() {
                emit m_service->dataLoadProgress(50, "正在执行数据库查询...");
            });
            
            auto result = database->executeQuery(sql, params);
            
            // 更新进度
            QMetaObject::invokeMethod(m_service, [this]() {
                emit m_service->dataLoadProgress(70, "正在处理查询结果...");
            });
            
            // 转换结果
            for (const auto& row : result.getRows()) {
                QVariantMap record;
                record["symbol"] = row.getString("symbol");
                record["date"] = row.getString("trade_date");
                record["open"] = row.getDouble("open");
                record["high"] = row.getDouble("high");
                record["low"] = row.getDouble("low");
                record["close"] = row.getDouble("close");
                record["volume"] = row.getDouble("volume");
                record["name"] = row.getString("symbol"); // 暂时用symbol作为name
                
                // 计算涨跌幅（需要前一日收盘价，这里简化处理）
                record["change"] = 0.0;
                
                data.append(record);
            }
            
            qDebug() << "Query completed, found" << data.size() << "records";
            
            // 如果没有数据，尝试从掘金获取
            if (data.isEmpty()) {
                qWarning() << "数据库中未找到数据，尝试从掘金获取...";
                
                // 更新进度
                QMetaObject::invokeMethod(m_service, [this]() {
                    emit m_service->dataLoadProgress(80, "数据库中未找到数据，尝试从掘金获取...");
                });
                
                // 尝试从掘金获取数据
                bool juejinSuccess = fetchFromJuejin(data, errorMessage);
                
                if (juejinSuccess && !data.isEmpty()) {
                    qDebug() << "成功从掘金获取" << data.size() << "条数据";
                    return true;
                } else {
                    if (m_symbol.isEmpty()) {
                        errorMessage = QString("数据库和掘金数据源中都没有找到匹配的数据\n"
                                              "时间范围: %1 到 %2\n"
                                              "可能的原因:\n"
                                              "1. 数据表'daily_bar'不存在或为空\n"
                                              "2. 时间范围超出数据库中的数据范围\n"
                                              "3. 数据库和掘金都没有该时间范围内的数据\n\n"
                                              "解决方案:\n"
                                              "1. 使用数据导入工具导入历史数据:\n"
                                              "   python tools/import_from_juejin.py\n"
                                              "2. 检查数据库连接配置是否正确")
                                              .arg(m_startDate)
                                              .arg(m_endDate);
                    } else {
                        errorMessage = QString("数据库和掘金数据源中都没有找到匹配的数据\n"
                                              "股票代码: %1\n"
                                              "时间范围: %2 到 %3\n"
                                              "可能的原因:\n"
                                              "1. 数据库和掘金都没有该股票的数据\n"
                                              "2. 数据表'daily_bar'不存在或为空\n"
                                              "3. 时间范围超出数据库中的数据范围\n\n"
                                              "解决方案:\n"
                                              "1. 使用数据导入工具导入历史数据:\n"
                                              "   python tools/import_from_juejin.py --symbols %1\n"
                                              "2. 或者导入所有A股数据:\n"
                                              "   python tools/import_from_juejin.py\n"
                                              "3. 检查数据库连接配置是否正确")
                                              .arg(m_symbol)
                                              .arg(m_startDate)
                                              .arg(m_endDate);
                    }
                    qWarning() << errorMessage;
                    return false;
                }
            }
            
            return true;
            
        } catch (const std::exception& e) {
            errorMessage = QString("数据库查询错误: %1\n"
                                  "可能的原因:\n"
                                  "1. 数据库连接失败\n"
                                  "2. SQL语法错误\n"
                                  "3. 数据库表不存在\n"
                                  "4. 网络连接问题")
                                  .arg(e.what());
            qCritical() << errorMessage;
            return false;
        }
    }
    
    bool fetchFromJuejin(QVariantList& data, QString& errorMessage) {
        try {
            qDebug() << "开始从掘金获取数据...";
            
            // 创建临时文件路径
            QString tempDir = QDir::tempPath();
            QString tempFile = tempDir + "/juejin_data_" + QUuid::createUuid().toString() + ".json";
            
            // 构建Python命令 - 直接调用Python模块
            QStringList args;
            args << "-c";
            
            // Python代码：直接调用掘金API
            QString pythonCode = QString(R"(
import sys
import os
import json
from datetime import datetime

# 添加项目根目录到路径
project_root = r"%1"
if project_root not in sys.path:
    sys.path.insert(0, project_root)

# 添加tools目录到路径
tools_path = os.path.join(project_root, "tools")
if tools_path not in sys.path:
    sys.path.insert(0, tools_path)

try:
    # 直接导入，因为tools目录已经在sys.path中
    from import_from_juejin import fetch_daily_bars_from_juejin
    
    symbol = r"%2"
    start_date_str = r"%3"
    end_date_str = r"%4"
    
    # 转换日期
    start_date = datetime.strptime(start_date_str, "%%Y-%%m-%%d").date()
    end_date = datetime.strptime(end_date_str, "%%Y-%%m-%%d").date()
    
    print(f"从掘金获取数据: {symbol}, {start_date} 到 {end_date}")
    
    # 获取数据
    raw_data = fetch_daily_bars_from_juejin(symbol, start_date, end_date)
    
    # 转换数据格式
    result = []
    for item in raw_data:
        result.append({
            "symbol": symbol,
            "trade_date": item["trade_date"].strftime("%%Y-%%m-%%d") if hasattr(item["trade_date"], "strftime") else str(item["trade_date"]),
            "open": float(item.get("open", 0.0)),
            "high": float(item.get("high", 0.0)),
            "low": float(item.get("low", 0.0)),
            "close": float(item.get("close", 0.0)),
            "volume": float(item.get("volume", 0.0)),
            "change_pct": float(item.get("change_pct", 0.0))
        })
    
    # 保存到文件
    with open(r"%5", 'w', encoding='utf-8') as f:
        json.dump(result, f, ensure_ascii=False, indent=2)
    
    print(f"数据已保存到: {r'%5'}, 共 {len(result)} 条记录")
    sys.exit(0)
    
except Exception as e:
    print(f"错误: {e}", file=sys.stderr)
    sys.exit(1)
)").arg(QDir::currentPath())  // 项目根目录（当前目录就是项目根目录）
                .arg(m_symbol.isEmpty() ? "000001.SZ" : m_symbol)
                .arg(m_startDate)
                .arg(m_endDate)
                .arg(tempFile);
            
            args << pythonCode;
            
            // 执行Python脚本
            QProcess process;
            process.setProgram("python");
            process.setArguments(args);
            
            qDebug() << "执行Python命令: python" << args[0] << "[python code...]";
            
            // 启动进程
            process.start();
            
            // 等待进程完成（最多30秒）
            if (!process.waitForFinished(30000)) {
                errorMessage = "掘金数据获取超时";
                qWarning() << errorMessage;
                return false;
            }
            
            // 检查退出状态
            if (process.exitCode() != 0) {
                QString stdErr = QString::fromUtf8(process.readAllStandardError());
                errorMessage = QString("掘金数据获取失败: %1").arg(stdErr);
                qWarning() << errorMessage;
                return false;
            }
            
            // 读取输出文件
            QFile file(tempFile);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                errorMessage = "无法读取掘金数据文件";
                qWarning() << errorMessage;
                return false;
            }
            
            QByteArray jsonData = file.readAll();
            file.close();
            
            // 删除临时文件
            QFile::remove(tempFile);
            
            // 解析JSON数据
            QJsonDocument doc = QJsonDocument::fromJson(jsonData);
            if (doc.isNull() || !doc.isArray()) {
                errorMessage = "掘金数据格式错误";
                qWarning() << errorMessage;
                return false;
            }
            
            QJsonArray jsonArray = doc.array();
            
            // 转换数据格式
            for (const QJsonValue& value : jsonArray) {
                if (value.isObject()) {
                    QJsonObject obj = value.toObject();
                    
                    QVariantMap record;
                    record["symbol"] = obj.value("symbol").toString();
                    record["date"] = obj.value("trade_date").toString();
                    record["open"] = obj.value("open").toDouble();
                    record["high"] = obj.value("high").toDouble();
                    record["low"] = obj.value("low").toDouble();
                    record["close"] = obj.value("close").toDouble();
                    record["volume"] = obj.value("volume").toDouble();
                    record["name"] = obj.value("symbol").toString(); // 暂时用symbol作为name
                    record["change"] = obj.value("change_pct").toDouble();
                    
                    data.append(record);
                }
            }
            
            qDebug() << "从掘金获取到" << data.size() << "条数据";
            
            // 如果获取到数据，保存到数据库
            if (!data.isEmpty()) {
                saveToDatabase(data);
            }
            
            return true;
            
        } catch (const std::exception& e) {
            errorMessage = QString("掘金数据获取错误: %1").arg(e.what());
            qCritical() << errorMessage;
            return false;
        }
    }
    
    void saveToDatabase(const QVariantList& data) {
        try {
            qDebug() << "将掘金数据保存到数据库...";
            
            // 获取数据库实例
            auto database = m_service->m_database;
            if (!database) {
                qWarning() << "数据库未初始化，无法保存掘金数据";
                return;
            }
            
            // 批量保存数据
            for (const QVariant& item : data) {
                QVariantMap record = item.toMap();
                
                // 构建SQL插入语句
                QString sql = "INSERT INTO daily_bar (symbol, trade_date, open, high, low, close, volume) "
                             "VALUES (:symbol, :trade_date, :open, :high, :low, :close, :volume) "
                             "ON DUPLICATE KEY UPDATE "
                             "open = VALUES(open), high = VALUES(high), low = VALUES(low), "
                             "close = VALUES(close), volume = VALUES(volume)";
                
                std::map<QString, QVariant> params;
                params[":symbol"] = record["symbol"].toString();
                params[":trade_date"] = record["date"].toString();
                params[":open"] = record["open"].toDouble();
                params[":high"] = record["high"].toDouble();
                params[":low"] = record["low"].toDouble();
                params[":close"] = record["close"].toDouble();
                params[":volume"] = record["volume"].toDouble();
                
                try {
                    database->executeUpdate(sql, params);
                } catch (const std::exception& e) {
                    qWarning() << "保存掘金数据失败:" << e.what();
                }
            }
            
            qDebug() << "掘金数据已保存到数据库";
            
        } catch (const std::exception& e) {
            qWarning() << "保存掘金数据到数据库失败:" << e.what();
        }
    }
    
private:
    DataService* m_service;
    QString m_symbol;
    QString m_startDate;
    QString m_endDate;
};

DataService::DataService(QObject* parent)
    : QObject(parent)
    , m_cleaningEngine(std::make_unique<DataCleaningEngine>())
{
    qDebug() << "DataService: Created";
    
    // 创建Foundation线程池（IO密集型，适合数据库操作）
    try {
        m_threadPool = foundation::thread::ThreadPoolFactory::create_io_intensive();
        qDebug() << "✅ Foundation thread pool created successfully";
    } catch (const std::exception& e) {
        QString error = QString("Failed to create foundation thread pool: %1\n"
                              "Possible causes:\n"
                              "1. Foundation library not properly linked\n"
                              "2. Memory allocation failure\n"
                              "3. System resource limits exceeded\n"
                              "4. Thread pool configuration error")
                              .arg(e.what());
        qCritical() << error;
        qWarning() << "⚠️  Foundation thread pool creation failed, will fall back to Qt thread pool when needed";
        // 线程池指针保持为空，异步方法将回退到Qt线程池
    }
    
    // 连接DataCleaningEngine的信号到DataService的信号
    QObject::connect(m_cleaningEngine.get(), &DataCleaningEngine::cleaningProgress,
        this, &DataService::dataCleaningProgress);
}

DataService::~DataService()
{
    // 清理
}

bool DataService::initializeDatabase()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_databaseInitialized) {
        qDebug() << "Database already initialized";
        return true;
    }
    
    try {
        qDebug() << "DataService: Initializing database...";
        
        // 检查可用驱动
        QStringList drivers = QSqlDatabase::drivers();
        qDebug() << "Available Qt database drivers:" << drivers;
        
        // 创建数据库配置
        astock::database::DatabaseConfig config;
        config.host = "localhost";
        config.port = 3306;
        config.database = "astock_quant";
        config.username = "root";
        config.password = "123456a";
        config.charset = "utf8mb4";
        config.pool_size = 5;
        
        // 根据可用驱动选择驱动类型
        if (drivers.contains("QMYSQL")) {
            config.driver = "QMYSQL";
        } else if (drivers.contains("QODBC")) {
            config.driver = "QODBC";
        } else {
            qCritical() << "No suitable database driver available for MySQL";
            emit databaseInitialized(false, "没有可用的MySQL数据库驱动");
            return false;
        }
        
        // 创建数据库对象
        m_database = std::make_shared<astock::database::QtMySQLDatabase>(config, true);
        
        // 打开连接
        if (!m_database->open()) {
            QString error = m_database->getLastError(); // 直接使用QString
            qCritical() << "Failed to open MySQL database:" << error;
            emit databaseInitialized(false, "数据库连接失败: " + error);
            return false;
        }
        
        m_databaseInitialized = true;
        qDebug() << "✅ DataService: Database initialized successfully";
        emit databaseInitialized(true, "数据库初始化成功");
        
        return true;
        
    } catch (const std::exception& e) {
        QString error = QString("数据库初始化错误: %1").arg(e.what());
        qCritical() << error;
        emit databaseInitialized(false, error);
        return false;
    }
}

void DataService::loadDataAsync(const QString& symbol, const QString& startDate, const QString& endDate)
{
    qDebug() << "DataService::loadDataAsync:" << symbol << startDate << endDate;
    
    // 检查线程池是否已初始化
    if (!m_threadPool) {
        qWarning() << "Thread pool not initialized, falling back to Qt thread pool";
        // 回退到Qt线程池
        DataLoadTask* task = new DataLoadTask(this, symbol, startDate, endDate);
        QThreadPool::globalInstance()->start(task);
        return;
    }
    
    // 使用Foundation线程池提交任务
    m_threadPool->post([this, symbol, startDate, endDate]() {
        try {
            // 通知加载开始
            QMetaObject::invokeMethod(this, [this]() {
                emit dataLoadStarted();
            });
            
            // 更新进度
            QMetaObject::invokeMethod(this, [this]() {
                emit dataLoadProgress(10, "正在连接数据库...");
            });
            
            // 确保数据库已初始化
            if (!ensureDatabaseInitialized()) {
                QString error = "数据库未初始化";
                QMetaObject::invokeMethod(this, [this, error]() {
                    emit dataLoadError(error);
                });
                return;
            }
            
            // 更新进度
            QMetaObject::invokeMethod(this, [this]() {
                emit dataLoadProgress(30, "正在执行查询...");
            });
            
            // 执行查询
            QVariantList data;
            QString errorMessage;
            bool success = false;
            
            try {
                // 获取数据库实例
                auto database = m_database;
                if (!database) {
                    errorMessage = "数据库未初始化";
                    success = false;
                } else {
                    // 构建SQL查询 - 支持空symbol查询所有股票
                    QString sql;
                    std::map<QString, QVariant> params;
                    
                    if (symbol.isEmpty()) {
                        // 查询所有股票 - 按照时间顺序排列（从开始日期到结束日期）
                        sql = "SELECT symbol, trade_date, open, high, low, close, volume "
                             "FROM daily_bar "
                             "WHERE trade_date BETWEEN :start_date AND :end_date "
                             "ORDER BY trade_date ASC, symbol ASC";
                        qDebug() << "Executing SQL query for ALL stocks (time-ordered):" << sql;
                    } else {
                        // 查询特定股票 - 按照时间顺序排列
                        sql = "SELECT symbol, trade_date, open, high, low, close, volume "
                             "FROM daily_bar "
                             "WHERE symbol = :symbol "
                             "AND trade_date BETWEEN :start_date AND :end_date "
                             "ORDER BY trade_date ASC";
                        params[":symbol"] = symbol;
                        qDebug() << "Executing SQL query for specific stock (time-ordered):" << sql;
                    }
                    
                    // 设置查询参数
                    params[":start_date"] = startDate;
                    params[":end_date"] = endDate;
                    
                    qDebug() << "Parameters: symbol=" << (symbol.isEmpty() ? "ALL" : symbol)
                             << ", start_date=" << startDate 
                             << ", end_date=" << endDate;
                    
                    // 更新进度
                    QMetaObject::invokeMethod(this, [this]() {
                        emit dataLoadProgress(50, "正在执行数据库查询...");
                    });
                    
                    auto result = database->executeQuery(sql, params);
                    
                    // 更新进度
                    QMetaObject::invokeMethod(this, [this]() {
                        emit dataLoadProgress(70, "正在处理查询结果...");
                    });
                    
                    // 转换结果
                    for (const auto& row : result.getRows()) {
                        QVariantMap record;
                        record["symbol"] = row.getString("symbol");
                        record["date"] = row.getString("trade_date");
                        record["open"] = row.getDouble("open");
                        record["high"] = row.getDouble("high");
                        record["low"] = row.getDouble("low");
                        record["close"] = row.getDouble("close");
                        record["volume"] = row.getDouble("volume");
                        record["name"] = row.getString("symbol"); // 暂时用symbol作为name
                        
                        // 计算涨跌幅（需要前一日收盘价，这里简化处理）
                        record["change"] = 0.0;
                        
                        data.append(record);
                    }
                    
                    qDebug() << "Query completed, found" << data.size() << "records";
                    
                    // 如果没有数据，尝试从掘金获取
                    if (data.isEmpty()) {
                        qWarning() << "数据库中未找到数据，尝试从掘金获取...";
                        
                        // 更新进度
                        QMetaObject::invokeMethod(this, [this]() {
                            emit dataLoadProgress(80, "数据库中未找到数据，尝试从掘金获取...");
                        });
                        
                        // 尝试从掘金获取数据
                        bool juejinSuccess = fetchFromJuejinInLambda(data, errorMessage, symbol, startDate, endDate);
                        
                        if (juejinSuccess && !data.isEmpty()) {
                            qDebug() << "成功从掘金获取" << data.size() << "条数据";
                            success = true;
                        } else {
                            if (symbol.isEmpty()) {
                                errorMessage = QString("数据库和掘金数据源中都没有找到匹配的数据\n"
                                                      "时间范围: %1 到 %2\n"
                                                      "可能的原因:\n"
                                                      "1. 数据表'daily_bar'不存在或为空\n"
                                                      "2. 时间范围超出数据库中的数据范围\n"
                                                      "3. 数据库和掘金都没有该时间范围内的数据\n\n"
                                                      "解决方案:\n"
                                                      "1. 使用数据导入工具导入历史数据:\n"
                                                      "   python tools/import_from_juejin.py\n"
                                                      "2. 检查数据库连接配置是否正确")
                                                      .arg(startDate)
                                                      .arg(endDate);
                            } else {
                                errorMessage = QString("数据库和掘金数据源中都没有找到匹配的数据\n"
                                                      "股票代码: %1\n"
                                                      "时间范围: %2 到 %3\n"
                                                      "可能的原因:\n"
                                                      "1. 数据库和掘金都没有该股票的数据\n"
                                                      "2. 数据表'daily_bar'不存在或为空\n"
                                                      "3. 时间范围超出数据库中的数据范围\n\n"
                                                      "解决方案:\n"
                                                      "1. 使用数据导入工具导入历史数据:\n"
                                                      "   python tools/import_from_juejin.py --symbols %1\n"
                                                      "2. 或者导入所有A股数据:\n"
                                                      "   python tools/import_from_juejin.py\n"
                                                      "3. 检查数据库连接配置是否正确")
                                                      .arg(symbol)
                                                      .arg(startDate)
                                                      .arg(endDate);
                            }
                            qWarning() << errorMessage;
                            success = false;
                        }
                    } else {
                        success = true;
                    }
                }
                
            } catch (const std::exception& e) {
                errorMessage = QString("数据库查询错误: %1\n"
                                      "可能的原因:\n"
                                      "1. 数据库连接失败\n"
                                      "2. SQL语法错误\n"
                                      "3. 数据库表不存在\n"
                                      "4. 网络连接问题")
                                      .arg(e.what());
                qCritical() << errorMessage;
                success = false;
            }
            
            if (success) {
                // 更新进度
                QMetaObject::invokeMethod(this, [this, data]() {
                    emit dataLoadProgress(100, "数据加载完成");
                    emit dataLoadCompleted(true, 
                        QString("成功加载 %1 条数据").arg(data.size()), 
                        data);
                });
            } else {
                QMetaObject::invokeMethod(this, [this, errorMessage]() {
                    emit dataLoadError(errorMessage);
                });
            }
            
        } catch (const std::exception& e) {
            QString error = QString("数据加载错误: %1").arg(e.what());
            QMetaObject::invokeMethod(this, [this, error]() {
                emit dataLoadError(error);
            });
        }
    });
}

// 异步数据清洗任务
class DataCleaningTask : public QRunnable {
public:
    DataCleaningTask(DataService* service,
                     const QVariantList& data,
                     const QVariantMap& rules)
        : m_service(service)
        , m_data(data)
        , m_rules(rules)
    {
        setAutoDelete(true);
    }
    
    void run() override {
        try {
            // 通知清洗开始
            QMetaObject::invokeMethod(m_service, [this]() {
                emit m_service->dataCleaningStarted();
            });
            
            // 在工作线程中创建独立的DataCleaningEngine实例
            std::unique_ptr<DataCleaningEngine> cleaningEngine = std::make_unique<DataCleaningEngine>();
            
            // 连接DataCleaningEngine的信号到DataService的信号
            QObject::connect(cleaningEngine.get(), &DataCleaningEngine::cleaningProgress,
                m_service, [this](int progress, const QString& message) {
                    QMetaObject::invokeMethod(m_service, [this, progress, message]() {
                        emit m_service->dataCleaningProgress(progress, message);
                    });
                });
            
            // 转换规则
            QVector<DataCleaningEngine::CleaningRule> cleaningRules = 
                m_service->convertRules(m_rules);
            
            qDebug() << "Data cleaning: processing" << m_data.size() 
                     << "records with" << cleaningRules.size() << "rules";
            
            // 直接使用DataCleaningEngine清洗数据，它会自动发送进度信号
            QVariantList cleanedData = cleaningEngine->cleanData(m_data, cleaningRules);
            
            // 获取清洗统计信息
            DataCleaningEngine::CleaningStats stats = cleaningEngine->getLastCleaningStats();
            
            // 生成完成消息
            QString message = QString("数据清洗完成: 原始 %1 条 -> 清洗后 %2 条 (移除 %3 条)")
                .arg(stats.totalRecords)
                .arg(stats.cleanedRecords)
                .arg(stats.removedRecords);
            
            qDebug() << "Data cleaning completed:" << message;
            qDebug() << "Cleaning stats: total=" << stats.totalRecords
                     << ", cleaned=" << stats.cleanedRecords
                     << ", removed=" << stats.removedRecords
                     << ", duration=" << stats.durationMs << "ms";
            
            // 将清洗后的数据保存到DataManager缓存
            QMetaObject::invokeMethod(m_service, [this, cleanedData]() {
                // 保存到DataManager缓存
                DataManager::instance()->storeData("cleaned_stock_data", cleanedData);
                qDebug() << "Cleaned data saved to DataManager cache, count:" << cleanedData.size();
            });
            
            // 通知清洗完成
            QMetaObject::invokeMethod(m_service, [this, cleanedData, message]() {
                emit m_service->dataCleaningCompleted(true, message, cleanedData);
            });
            
        } catch (const std::exception& e) {
            QString error = QString("数据清洗错误: %1").arg(e.what());
            qCritical() << error;
            QMetaObject::invokeMethod(m_service, [this, error]() {
                emit m_service->dataCleaningError(error);
            });
        }
    }
    
private:
    DataService* m_service;
    QVariantList m_data;
    QVariantMap m_rules;
};

void DataService::cleanDataAsync(const QVariantList& data, const QVariantMap& rules)
{
    qDebug() << "DataService::cleanDataAsync: data size =" << data.size() 
             << ", rules count =" << rules.size();
    
    if (data.isEmpty()) {
        qDebug() << "No data to clean";
        emit dataCleaningError("没有数据可清洗");
        return;
    }
    
    // 对于小数据集（小于1000条），同步处理
    if (data.size() < 1000) {
        qDebug() << "Small dataset, cleaning synchronously";
        try {
            // 通知清洗开始
            emit dataCleaningStarted();
            
            // 转换规则
            QVector<DataCleaningEngine::CleaningRule> cleaningRules = convertRules(rules);
            
            // 执行清洗
            QVariantList cleanedData = m_cleaningEngine->cleanData(data, cleaningRules);
            
            // 获取统计信息
            DataCleaningEngine::CleaningStats stats = m_cleaningEngine->getLastCleaningStats();
            QString message = QString("数据清洗完成: 原始 %1 条 -> 清洗后 %2 条")
                .arg(stats.totalRecords)
                .arg(stats.cleanedRecords);
            
            emit dataCleaningCompleted(true, message, cleanedData);
            
        } catch (const std::exception& e) {
            QString error = QString("数据清洗错误: %1").arg(e.what());
            emit dataCleaningError(error);
        }
        return;
    }
    
    // 检查线程池是否已初始化 - 回退机制
    if (!m_threadPool) {
        qWarning() << "⚠️  Foundation thread pool not initialized, falling back to Qt thread pool";
        // 回退到Qt线程池
        DataCleaningTask* task = new DataCleaningTask(this, data, rules);
        QThreadPool::globalInstance()->start(task);
        qDebug() << "✅ Async data cleaning task started for" << data.size() << "records (Qt thread pool fallback)";
        return;
    }
    
    // 使用Foundation线程池提交任务
    m_threadPool->post([this, data, rules]() {
        try {
            // 通知清洗开始
            QMetaObject::invokeMethod(this, [this]() {
                emit dataCleaningStarted();
            });
            
            // 在工作线程中创建独立的DataCleaningEngine实例
            std::unique_ptr<DataCleaningEngine> cleaningEngine = std::make_unique<DataCleaningEngine>();
            
            // 连接DataCleaningEngine的信号到DataService的信号
            QObject::connect(cleaningEngine.get(), &DataCleaningEngine::cleaningProgress,
                this, [this](int progress, const QString& message) {
                    QMetaObject::invokeMethod(this, [this, progress, message]() {
                        emit dataCleaningProgress(progress, message);
                    });
                });
            
            // 转换规则
            QVector<DataCleaningEngine::CleaningRule> cleaningRules = convertRules(rules);
            
            qDebug() << "Data cleaning: processing" << data.size() 
                     << "records with" << cleaningRules.size() << "rules";
            
            // 直接使用DataCleaningEngine清洗数据，它会自动发送进度信号
            QVariantList cleanedData = cleaningEngine->cleanData(data, cleaningRules);
            
            // 获取清洗统计信息
            DataCleaningEngine::CleaningStats stats = cleaningEngine->getLastCleaningStats();
            
            // 生成完成消息
            QString message = QString("数据清洗完成: 原始 %1 条 -> 清洗后 %2 条 (移除 %3 条)")
                .arg(stats.totalRecords)
                .arg(stats.cleanedRecords)
                .arg(stats.removedRecords);
            
            qDebug() << "Data cleaning completed:" << message;
            qDebug() << "Cleaning stats: total=" << stats.totalRecords
                     << ", cleaned=" << stats.cleanedRecords
                     << ", removed=" << stats.removedRecords
                     << ", duration=" << stats.durationMs << "ms";
            
            // 将清洗后的数据保存到DataManager缓存
            QMetaObject::invokeMethod(this, [this, cleanedData]() {
                // 保存到DataManager缓存
                DataManager::instance()->storeData("cleaned_stock_data", cleanedData);
                qDebug() << "Cleaned data saved to DataManager cache, count:" << cleanedData.size();
            });
            
            // 通知清洗完成
            QMetaObject::invokeMethod(this, [this, cleanedData, message]() {
                emit dataCleaningCompleted(true, message, cleanedData);
            });
            
        } catch (const std::exception& e) {
            QString error = QString("数据清洗错误: %1").arg(e.what());
            qCritical() << error;
            QMetaObject::invokeMethod(this, [this, error]() {
                emit dataCleaningError(error);
            });
        }
    });
    
    qDebug() << "Async data cleaning task started for" << data.size() << "records (Foundation thread pool)";
}

void DataService::fetchDataAsync(const QStringList& symbols, const QString& startDate, const QString& endDate)
{
    qDebug() << "DataService::fetchDataAsync: symbols =" << symbols << "startDate =" << startDate << "endDate =" << endDate;
    
    // 检查参数
    if (symbols.isEmpty()) {
        qDebug() << "No symbols provided";
        emit dataFetchError("未提供股票代码");
        return;
    }
    
    if (startDate.isEmpty() || endDate.isEmpty()) {
        qDebug() << "Invalid date range";
        emit dataFetchError("未设置时间范围");
        return;
    }
    
    // 通知开始
    emit dataFetchStarted();
    emit dataFetchProgress(10, "开始获取数据...");
    
    // 检查线程池是否已初始化 - 回退机制
    if (!m_threadPool) {
        qWarning() << "⚠️  Foundation thread pool not initialized, falling back to Qt thread pool";
        // 回退到Qt线程池
        QThreadPool::globalInstance()->start([this, symbols, startDate, endDate]() {
            try {
                // 模拟进度更新
                QMetaObject::invokeMethod(this, [this]() {
                    emit dataFetchProgress(30, "正在连接数据源...");
                });
                
                // 模拟数据处理
                QThread::msleep(500);
                
                QMetaObject::invokeMethod(this, [this]() {
                    emit dataFetchProgress(60, "正在获取数据...");
                });
                
                QThread::msleep(1000);
                
                // 检查数据库是否已初始化
                if (!ensureDatabaseInitialized()) {
                    QMetaObject::invokeMethod(this, [this]() {
                        emit dataFetchError("数据库未初始化");
                    });
                    return;
                }
                
                // 尝试从数据库获取数据
                QVariantList data;
                
                // 对于每个股票代码，尝试获取数据
                for (int i = 0; i < symbols.size(); i++) {
                    const QString& symbol = symbols[i];
                    int progress = 60 + int(30 * i / symbols.size());
                    
                    QMetaObject::invokeMethod(this, [this, progress, symbol]() {
                        emit dataFetchProgress(progress, QString("正在获取 %1 数据...").arg(symbol));
                    });
                    
                    // 构建SQL查询
                    QString sql = "SELECT symbol, trade_date, open, high, low, close, volume "
                                 "FROM daily_bar "
                                 "WHERE symbol = :symbol "
                                 "AND trade_date BETWEEN :start_date AND :end_date "
                                 "ORDER BY trade_date ASC";
                    
                    std::map<QString, QVariant> params;
                    params[":symbol"] = symbol;
                    params[":start_date"] = startDate;
                    params[":end_date"] = endDate;
                    
                    try {
                        auto result = m_database->executeQuery(sql, params);
                        
                        for (const auto& row : result.getRows()) {
                            QVariantMap record;
                            record["symbol"] = row.getString("symbol");
                            record["date"] = row.getString("trade_date");
                            record["open"] = row.getDouble("open");
                            record["high"] = row.getDouble("high");
                            record["low"] = row.getDouble("low");
                            record["close"] = row.getDouble("close");
                            record["volume"] = row.getDouble("volume");
                            record["name"] = row.getString("symbol");
                            record["change"] = 0.0;
                            
                            data.append(record);
                        }
                        
                        qDebug() << "Fetched" << result.getRows().size() << "records for" << symbol;
                        
                    } catch (const std::exception& e) {
                        qWarning() << "Failed to fetch data for" << symbol << ":" << e.what();
                    }
                    
                    QThread::msleep(100);
                }
                
                // 如果没有获取到数据，返回错误信息
                if (data.isEmpty()) {
                    QMetaObject::invokeMethod(this, [this]() {
                        emit dataFetchProgress(90, "数据库中没有找到数据...");
                    });
                    
                    QMetaObject::invokeMethod(this, [this]() {
                        emit dataFetchError("数据库中没有找到数据，请先导入数据或检查时间范围");
                    });
                    return;
                }
                
                // 完成
                QMetaObject::invokeMethod(this, [this, data]() {
                    emit dataFetchProgress(100, "数据获取完成");
                    emit dataFetchCompleted(true, QString("成功获取 %1 条数据").arg(data.size()), data);
                });
                
            } catch (const std::exception& e) {
                QString error = QString("数据获取错误: %1").arg(e.what());
                QMetaObject::invokeMethod(this, [this, error]() {
                    emit dataFetchError(error);
                });
            }
        });
        return;
    }
    
    // 使用Foundation线程池提交任务
    m_threadPool->post([this, symbols, startDate, endDate]() {
        try {
            // 模拟进度更新
            QMetaObject::invokeMethod(this, [this]() {
                emit dataFetchProgress(30, "正在连接数据源...");
            });
            
            // 模拟数据处理
            QThread::msleep(500);
            
            QMetaObject::invokeMethod(this, [this]() {
                emit dataFetchProgress(60, "正在获取数据...");
            });
            
            QThread::msleep(1000);
            
            // 检查数据库是否已初始化
            if (!ensureDatabaseInitialized()) {
                QMetaObject::invokeMethod(this, [this]() {
                    emit dataFetchError("数据库未初始化");
                });
                return;
            }
            
            // 尝试从数据库获取数据
            QVariantList data;
            
            // 对于每个股票代码，尝试获取数据
            for (int i = 0; i < symbols.size(); i++) {
                const QString& symbol = symbols[i];
                int progress = 60 + int(30 * i / symbols.size());
                
                QMetaObject::invokeMethod(this, [this, progress, symbol]() {
                    emit dataFetchProgress(progress, QString("正在获取 %1 数据...").arg(symbol));
                });
                
                // 构建SQL查询
                QString sql = "SELECT symbol, trade_date, open, high, low, close, volume "
                             "FROM daily_bar "
                             "WHERE symbol = :symbol "
                             "AND trade_date BETWEEN :start_date AND :end_date "
                             "ORDER BY trade_date ASC";
                
                std::map<QString, QVariant> params;
                params[":symbol"] = symbol;
                params[":start_date"] = startDate;
                params[":end_date"] = endDate;
                
                try {
                    auto result = m_database->executeQuery(sql, params);
                    
                    for (const auto& row : result.getRows()) {
                        QVariantMap record;
                        record["symbol"] = row.getString("symbol");
                        record["date"] = row.getString("trade_date");
                        record["open"] = row.getDouble("open");
                        record["high"] = row.getDouble("high");
                        record["low"] = row.getDouble("low");
                        record["close"] = row.getDouble("close");
                        record["volume"] = row.getDouble("volume");
                        record["name"] = row.getString("symbol");
                        record["change"] = 0.0;
                        
                        data.append(record);
                    }
                    
                    qDebug() << "Fetched" << result.getRows().size() << "records for" << symbol;
                    
                } catch (const std::exception& e) {
                    qWarning() << "Failed to fetch data for" << symbol << ":" << e.what();
                }
                
                QThread::msleep(100);
            }
            
            // 如果没有获取到数据，返回错误信息
            if (data.isEmpty()) {
                QMetaObject::invokeMethod(this, [this]() {
                    emit dataFetchProgress(90, "数据库中没有找到数据...");
                });
                
                QMetaObject::invokeMethod(this, [this]() {
                    emit dataFetchError("数据库中没有找到数据，请先导入数据或检查时间范围");
                });
                return;
            }
            
            // 完成
            QMetaObject::invokeMethod(this, [this, data]() {
                emit dataFetchProgress(100, "数据获取完成");
                emit dataFetchCompleted(true, QString("成功获取 %1 条数据").arg(data.size()), data);
            });
            
        } catch (const std::exception& e) {
            QString error = QString("数据获取错误: %1").arg(e.what());
            QMetaObject::invokeMethod(this, [this, error]() {
                emit dataFetchError(error);
            });
        }
    });
}

// 辅助方法：从数据库加载Python保存的数据
void DataService::loadDataFromDatabaseAfterFetch(const QString& requestId, const QStringList& symbols, 
                                                const QString& startDate, const QString& endDate,
                                                QVariantList& data, std::atomic<bool>& completed)
{
    try {
        qDebug() << "Loading data from database after Python fetch, requestId:" << requestId;
        
        // 确保数据库已初始化
        if (!ensureDatabaseInitialized()) {
            emit dataFetchError("数据库未初始化");
            completed = true;
            return;
        }
        
        // 从数据库加载数据
        QVariantList loadedData;
        
        for (const QString& symbol : symbols) {
            // 构建SQL查询
            QString sql = "SELECT symbol, trade_date, open, high, low, close, volume "
                         "FROM daily_bar "
                         "WHERE symbol = :symbol "
                         "AND trade_date BETWEEN :start_date AND :end_date "
                         "ORDER BY trade_date ASC";
            
            std::map<QString, QVariant> params;
            params[":symbol"] = symbol;
            params[":start_date"] = startDate;
            params[":end_date"] = endDate;
            
            try {
                auto result = m_database->executeQuery(sql, params);
                
                for (const auto& row : result.getRows()) {
                    QVariantMap record;
                    record["symbol"] = row.getString("symbol");
                    record["date"] = row.getString("trade_date");
                    record["open"] = row.getDouble("open");
                    record["high"] = row.getDouble("high");
                    record["low"] = row.getDouble("low");
                    record["close"] = row.getDouble("close");
                    record["volume"] = row.getDouble("volume");
                    record["name"] = row.getString("symbol");
                    record["change"] = 0.0;
                    
                    loadedData.append(record);
                }
                
                qDebug() << "Loaded" << result.getRows().size() << "records for" << symbol << "from database";
                
            } catch (const std::exception& e) {
                qWarning() << "Failed to load data for" << symbol << "from database:" << e.what();
            }
        }
        
        // 更新数据
        data = loadedData;
        
        if (data.isEmpty()) {
            emit dataFetchError("数据库中没有找到数据，请检查数据源连接");
        } else {
            emit dataFetchProgress(100, "数据加载完成");
            emit dataFetchCompleted(true, QString("成功获取 %1 条数据").arg(data.size()), data);
        }
        
        completed = true;
        
    } catch (const std::exception& e) {
        QString error = QString("数据库加载错误: %1").arg(e.what());
        emit dataFetchError(error);
        completed = true;
    }
}

// 异步数据保存任务
class DataSaveTask : public QRunnable {
public:
    DataSaveTask(DataService* service, const QVariantList& data)
        : m_service(service)
        , m_data(data)
    {
        setAutoDelete(true);
    }
    
    void run() override {
        try {
            // 通知保存开始
            QMetaObject::invokeMethod(m_service, [this]() {
                emit m_service->dataSaveStarted();
            });
            
            // 更新进度
            QMetaObject::invokeMethod(m_service, [this]() {
                emit m_service->dataSaveProgress(10, "正在连接数据库...");
            });
            
            // 确保数据库已初始化
            if (!m_service->ensureDatabaseInitialized()) {
                QString error = "数据库未初始化";
                QMetaObject::invokeMethod(m_service, [this, error]() {
                    emit m_service->dataSaveError(error);
                });
                return;
            }
            
            // 更新进度
            QMetaObject::invokeMethod(m_service, [this]() {
                emit m_service->dataSaveProgress(30, "正在准备数据...");
            });
            
            // 执行数据保存
            int savedCount = 0;
            QString errorMessage;
            bool success = executeSave(savedCount, errorMessage);
            
            if (success) {
                // 更新进度
                QMetaObject::invokeMethod(m_service, [this, savedCount]() {
                    emit m_service->dataSaveProgress(100, "数据保存完成");
                    emit m_service->dataSaveCompleted(true, 
                        QString("成功保存 %1 条数据").arg(savedCount), 
                        savedCount);
                });
            } else {
                QMetaObject::invokeMethod(m_service, [this, errorMessage]() {
                    emit m_service->dataSaveError(errorMessage);
                });
            }
            
        } catch (const std::exception& e) {
            QString error = QString("数据保存错误: %1").arg(e.what());
            QMetaObject::invokeMethod(m_service, [this, error]() {
                emit m_service->dataSaveError(error);
            });
        }
    }
    
private:
    bool executeSave(int& savedCount, QString& errorMessage) {
        try {
            // 获取数据库实例
            auto database = m_service->m_database;
            if (!database) {
                errorMessage = "数据库未初始化";
                return false;
            }
            
            qDebug() << "DataSaveTask: Saving" << m_data.size() << "records to database";
            
            // 更新进度
            QMetaObject::invokeMethod(m_service, [this]() {
                emit m_service->dataSaveProgress(50, "正在执行数据库插入...");
            });
            
            // 批量保存数据
            for (int i = 0; i < m_data.size(); i++) {
                const QVariantMap& record = m_data[i].toMap();
                
                // 构建SQL插入语句
                QString sql = "INSERT INTO daily_bar (symbol, trade_date, open, high, low, close, volume) "
                             "VALUES (:symbol, :trade_date, :open, :high, :low, :close, :volume) "
                             "ON DUPLICATE KEY UPDATE "
                             "open = VALUES(open), high = VALUES(high), low = VALUES(low), "
                             "close = VALUES(close), volume = VALUES(volume)";
                
                std::map<QString, QVariant> params;
                params[":symbol"] = record["symbol"].toString();
                params[":trade_date"] = record["date"].toString();
                params[":open"] = record["open"].toDouble();
                params[":high"] = record["high"].toDouble();
                params[":low"] = record["low"].toDouble();
                params[":close"] = record["close"].toDouble();
                params[":volume"] = record["volume"].toDouble();
                
                try {
                    database->executeUpdate(sql, params);
                    savedCount++;
                    
                    // 每保存100条数据更新一次进度
                    if (i % 100 == 0) {
                        int progress = 50 + int(40 * i / m_data.size());
                        QMetaObject::invokeMethod(m_service, [this, progress, i]() {
                            emit m_service->dataSaveProgress(progress, 
                                QString("正在保存数据... (%1/%2)").arg(i).arg(m_data.size()));
                        });
                    }
                    
                } catch (const std::exception& e) {
                    qWarning() << "Failed to save record" << i << ":" << e.what();
                    // 继续保存其他记录
                }
            }
            
            qDebug() << "DataSaveTask: Saved" << savedCount << "records successfully";
            
            if (savedCount == 0) {
                errorMessage = "未能保存任何数据，请检查数据格式和数据库连接";
                return false;
            }
            
            return true;
            
        } catch (const std::exception& e) {
            errorMessage = QString("数据库保存错误: %1\n"
                                  "可能的原因:\n"
                                  "1. 数据库连接失败\n"
                                  "2. SQL语法错误\n"
                                  "3. 数据库表不存在\n"
                                  "4. 数据格式错误")
                                  .arg(e.what());
            qCritical() << errorMessage;
            return false;
        }
    }
    
private:
    DataService* m_service;
    QVariantList m_data;
};

void DataService::saveDataAsync(const QVariantList& data)
{
    qDebug() << "DataService::saveDataAsync: data size =" << data.size();
    
    if (data.isEmpty()) {
        qDebug() << "No data to save";
        emit dataSaveError("没有数据可保存");
        return;
    }
    
    // 检查线程池是否已初始化 - 回退机制
    if (!m_threadPool) {
        qWarning() << "⚠️  Foundation thread pool not initialized, falling back to Qt thread pool";
        // 回退到Qt线程池
        DataSaveTask* task = new DataSaveTask(this, data);
        QThreadPool::globalInstance()->start(task);
        qDebug() << "✅ Async data save task started for" << data.size() << "records (Qt thread pool fallback)";
        return;
    }
    
    // 使用Foundation线程池提交任务
    m_threadPool->post([this, data]() {
        try {
            // 通知保存开始
            QMetaObject::invokeMethod(this, [this]() {
                emit dataSaveStarted();
            });
            
            // 更新进度
            QMetaObject::invokeMethod(this, [this]() {
                emit dataSaveProgress(10, "正在连接数据库...");
            });
            
            // 确保数据库已初始化
            if (!ensureDatabaseInitialized()) {
                QString error = "数据库未初始化";
                QMetaObject::invokeMethod(this, [this, error]() {
                    emit dataSaveError(error);
                });
                return;
            }
            
            // 更新进度
            QMetaObject::invokeMethod(this, [this]() {
                emit dataSaveProgress(30, "正在准备数据...");
            });
            
            // 执行数据保存
            int savedCount = 0;
            QString errorMessage;
            bool success = false;
            
            try {
                // 获取数据库实例
                auto database = m_database;
                if (!database) {
                    errorMessage = "数据库未初始化";
                    success = false;
                } else {
                    qDebug() << "Saving" << data.size() << "records to database using foundation thread pool";
                    
                    // 更新进度
                    QMetaObject::invokeMethod(this, [this]() {
                        emit dataSaveProgress(50, "正在执行数据库插入...");
                    });
                    
                    // 批量保存数据
                    for (int i = 0; i < data.size(); i++) {
                        const QVariantMap& record = data[i].toMap();
                        
                        // 构建SQL插入语句
                        QString sql = "INSERT INTO daily_bar (symbol, trade_date, open, high, low, close, volume) "
                                     "VALUES (:symbol, :trade_date, :open, :high, :low, :close, :volume) "
                                     "ON DUPLICATE KEY UPDATE "
                                     "open = VALUES(open), high = VALUES(high), low = VALUES(low), "
                                     "close = VALUES(close), volume = VALUES(volume)";
                        
                        std::map<QString, QVariant> params;
                        params[":symbol"] = record["symbol"].toString();
                        params[":trade_date"] = record["date"].toString();
                        params[":open"] = record["open"].toDouble();
                        params[":high"] = record["high"].toDouble();
                        params[":low"] = record["low"].toDouble();
                        params[":close"] = record["close"].toDouble();
                        params[":volume"] = record["volume"].toDouble();
                        
                        try {
                            database->executeUpdate(sql, params);
                            savedCount++;
                            
                            // 每保存100条数据更新一次进度
                            if (i % 100 == 0) {
                                int progress = 50 + int(40 * i / data.size());
                                QMetaObject::invokeMethod(this, [this, progress, i, data]() {
                                    emit dataSaveProgress(progress,
                                        QString("正在保存数据... (%1/%2)").arg(i).arg(data.size()));
                                });
                            }
                            
                        } catch (const std::exception& e) {
                            qWarning() << "Failed to save record" << i << ":" << e.what();
                            // 继续保存其他记录
                        }
                    }
                    
                    qDebug() << "Saved" << savedCount << "records successfully using foundation thread pool";
                    
                    if (savedCount == 0) {
                        errorMessage = "未能保存任何数据，请检查数据格式和数据库连接";
                        success = false;
                    } else {
                        success = true;
                    }
                }
                
            } catch (const std::exception& e) {
                errorMessage = QString("数据库保存错误: %1\n"
                                      "可能的原因:\n"
                                      "1. 数据库连接失败\n"
                                      "2. SQL语法错误\n"
                                      "3. 数据库表不存在\n"
                                      "4. 数据格式错误")
                                      .arg(e.what());
                qCritical() << errorMessage;
                success = false;
            }
            
            if (success) {
                // 更新进度
                QMetaObject::invokeMethod(this, [this, savedCount]() {
                    emit dataSaveProgress(100, "数据保存完成");
                    emit dataSaveCompleted(true, 
                        QString("成功保存 %1 条数据").arg(savedCount), 
                        savedCount);
                });
            } else {
                QMetaObject::invokeMethod(this, [this, errorMessage]() {
                    emit dataSaveError(errorMessage);
                });
            }
            
        } catch (const std::exception& e) {
            QString error = QString("数据保存错误: %1").arg(e.what());
            QMetaObject::invokeMethod(this, [this, error]() {
                emit dataSaveError(error);
            });
        }
    });
    
    qDebug() << "Async data save task started for" << data.size() << "records (Foundation thread pool)";
}
 
// 取消当前操作
void DataService::cancelCurrentOperation()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_operationInProgress) {
        qDebug() << "Cancelling current operation...";
        m_operationInProgress = false;
        
        // 发送取消信号
        emit dataFetchError("操作已取消");
        emit dataSaveError("操作已取消");
        emit dataLoadError("操作已取消");
        emit dataCleaningError("操作已取消");
    } else {
        qDebug() << "No operation in progress to cancel";
    }
}

// 规则转换方法
QVector<DataCleaningEngine::CleaningRule> DataService::convertRules(const QVariantMap& rules)
{
    QVector<DataCleaningEngine::CleaningRule> cleaningRules;
    
    // 检查规则是否为空
    if (rules.isEmpty()) {
        qDebug() << "No cleaning rules provided, using default rules";
        
        // 使用DataCleaningEngine的默认规则集
        return m_cleaningEngine->createDefaultRuleSet();
    }
    
    // 转换规则
    for (auto it = rules.begin(); it != rules.end(); ++it) {
        QString ruleName = it.key();
        QVariant ruleValue = it.value();
        
        // 解析规则
        if (ruleValue.type() == QVariant::Map) {
            QVariantMap ruleMap = ruleValue.toMap();
            
            // 解析规则类型
            QString ruleTypeStr = ruleMap.value("type", "custom").toString();
            DataCleaningEngine::CleaningRuleType ruleType;
            
            if (ruleTypeStr == "time_range" || ruleTypeStr == "timeRange") {
                ruleType = DataCleaningEngine::RULE_TIME_RANGE;
            } else if (ruleTypeStr == "price_filter" || ruleTypeStr == "priceFilter") {
                ruleType = DataCleaningEngine::RULE_PRICE_FILTER;
            } else if (ruleTypeStr == "volume_filter" || ruleTypeStr == "volumeFilter") {
                ruleType = DataCleaningEngine::RULE_VOLUME_FILTER;
            } else if (ruleTypeStr == "completeness_check" || ruleTypeStr == "completenessCheck") {
                ruleType = DataCleaningEngine::RULE_COMPLETENESS_CHECK;
            } else if (ruleTypeStr == "outlier_detection" || ruleTypeStr == "outlierDetection") {
                ruleType = DataCleaningEngine::RULE_OUTLIER_DETECTION;
            } else if (ruleTypeStr == "duplicate_removal" || ruleTypeStr == "duplicateRemoval") {
                ruleType = DataCleaningEngine::RULE_DUPLICATE_REMOVAL;
            } else if (ruleTypeStr == "format_validation" || ruleTypeStr == "formatValidation") {
                ruleType = DataCleaningEngine::RULE_FORMAT_VALIDATION;
            } else {
                ruleType = DataCleaningEngine::RULE_CUSTOM_FILTER;
            }
            
            // 创建规则
            QString description = ruleMap.value("description", "").toString();
            DataCleaningEngine::CleaningRule rule(ruleType, ruleName, description);
            
            // 设置参数
            if (ruleMap.contains("parameters")) {
                rule.parameters = ruleMap["parameters"].toMap();
            } else {
                // 如果没有parameters字段，将整个ruleMap作为参数
                rule.parameters = ruleMap;
                // 移除已使用的字段
                rule.parameters.remove("type");
                rule.parameters.remove("description");
            }
            
            // 设置启用状态
            rule.enabled = ruleMap.value("enabled", true).toBool();
            
            cleaningRules.append(rule);
            
            qDebug() << "Converted rule:" << ruleName 
                     << ", type=" << ruleTypeStr 
                     << ", enabled=" << rule.enabled;
        } else {
            // 简单规则 - 作为自定义规则处理
            QString description = QString("Custom rule for %1").arg(ruleName);
            DataCleaningEngine::CleaningRule rule(
                DataCleaningEngine::RULE_CUSTOM_FILTER, 
                ruleName, 
                description
            );
            
            // 将值作为参数
            QVariantMap params;
            params["value"] = ruleValue;
            rule.parameters = params;
            rule.enabled = true;
            
            cleaningRules.append(rule);
            
            qDebug() << "Converted simple rule:" << ruleName 
                     << ", value=" << ruleValue.toString();
        }
    }
    
    qDebug() << "Converted" << cleaningRules.size() << "cleaning rules";
    return cleaningRules;
}

// 确保数据库已初始化
bool DataService::ensureDatabaseInitialized()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_databaseInitialized) {
        return true;
    }
    
    // 尝试初始化数据库
    return initializeDatabase();
}

// 在lambda函数中使用的掘金数据获取方法
bool DataService::fetchFromJuejinInLambda(QVariantList& data, QString& errorMessage, 
                                         const QString& symbol, const QString& startDate, const QString& endDate)
{
    try {
        qDebug() << "开始从掘金获取数据...";
        
        // 创建临时文件路径
        QString tempDir = QDir::tempPath();
        QString tempFile = tempDir + "/juejin_data_" + QUuid::createUuid().toString() + ".json";
        
        // 构建Python命令 - 直接调用Python模块
        QStringList args;
        args << "-c";
        
            // Python代码：直接调用掘金API
            QString pythonCode = QString(R"(
import sys
import os
import json
from datetime import datetime

# 添加项目根目录到路径
project_root = r"%1"
if project_root not in sys.path:
    sys.path.insert(0, project_root)

# 添加tools目录到路径
tools_path = os.path.join(project_root, "tools")
if tools_path not in sys.path:
    sys.path.insert(0, tools_path)

try:
    # 直接导入，因为tools目录已经在sys.path中
    from import_from_juejin import fetch_daily_bars_from_juejin
    
    symbol = r"%2"
    start_date_str = r"%3"
    end_date_str = r"%4"
    
    # 转换日期
    start_date = datetime.strptime(start_date_str, "%%Y-%%m-%%d").date()
    end_date = datetime.strptime(end_date_str, "%%Y-%%m-%%d").date()
    
    print(f"从掘金获取数据: {symbol}, {start_date} 到 {end_date}")
    
    # 获取数据
    raw_data = fetch_daily_bars_from_juejin(symbol, start_date, end_date)
    
    # 转换数据格式
    result = []
    for item in raw_data:
        result.append({
            "symbol": symbol,
            "trade_date": item["trade_date"].strftime("%%Y-%%m-%%d") if hasattr(item["trade_date"], "strftime") else str(item["trade_date"]),
            "open": float(item.get("open", 0.0)),
            "high": float(item.get("high", 0.0)),
            "low": float(item.get("low", 0.0)),
            "close": float(item.get("close", 0.0)),
            "volume": float(item.get("volume", 0.0)),
            "change_pct": float(item.get("change_pct", 0.0))
        })
    
    # 保存到文件
    with open(r"%5", 'w', encoding='utf-8') as f:
        json.dump(result, f, ensure_ascii=False, indent=2)
    
    print(f"数据已保存到: {r'%5'}, 共 {len(result)} 条记录")
    sys.exit(0)
    
except Exception as e:
    print(f"错误: {e}", file=sys.stderr)
    sys.exit(1)
)").arg(QDir::currentPath())  // 项目根目录（当前目录就是项目根目录）
                .arg(symbol.isEmpty() ? "000001.SZ" : symbol)
                .arg(startDate)
                .arg(endDate)
                .arg(tempFile);
        
        args << pythonCode;
        
        // 执行Python脚本
        QProcess process;
        process.setProgram("python");
        process.setArguments(args);
        
        qDebug() << "执行Python命令: python" << args[0] << "[python code...]";
        
        // 启动进程
        process.start();
        
        // 等待进程完成（最多30秒）
        if (!process.waitForFinished(30000)) {
            errorMessage = "掘金数据获取超时";
            qWarning() << errorMessage;
            return false;
        }
        
        // 检查退出状态
        if (process.exitCode() != 0) {
            QString stdErr = QString::fromUtf8(process.readAllStandardError());
            errorMessage = QString("掘金数据获取失败: %1").arg(stdErr);
            qWarning() << errorMessage;
            return false;
        }
        
        // 读取输出文件
        QFile file(tempFile);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            errorMessage = "无法读取掘金数据文件";
            qWarning() << errorMessage;
            return false;
        }
        
        QByteArray jsonData = file.readAll();
        file.close();
        
        // 删除临时文件
        QFile::remove(tempFile);
        
        // 解析JSON数据
        QJsonDocument doc = QJsonDocument::fromJson(jsonData);
        if (doc.isNull() || !doc.isArray()) {
            errorMessage = "掘金数据格式错误";
            qWarning() << errorMessage;
            return false;
        }
        
        QJsonArray jsonArray = doc.array();
        
        // 转换数据格式
        for (const QJsonValue& value : jsonArray) {
            if (value.isObject()) {
                QJsonObject obj = value.toObject();
                
                QVariantMap record;
                record["symbol"] = obj.value("symbol").toString();
                record["date"] = obj.value("trade_date").toString();
                record["open"] = obj.value("open").toDouble();
                record["high"] = obj.value("high").toDouble();
                record["low"] = obj.value("low").toDouble();
                record["close"] = obj.value("close").toDouble();
                record["volume"] = obj.value("volume").toDouble();
                record["name"] = obj.value("symbol").toString(); // 暂时用symbol作为name
                record["change"] = obj.value("change_pct").toDouble();
                
                data.append(record);
            }
        }
        
        qDebug() << "从掘金获取到" << data.size() << "条数据";
        
        // 如果获取到数据，保存到数据库
        if (!data.isEmpty()) {
            saveJuejinDataToDatabase(data);
        }
        
        return true;
        
    } catch (const std::exception& e) {
        errorMessage = QString("掘金数据获取错误: %1").arg(e.what());
        qCritical() << errorMessage;
        return false;
    }
}

// 保存掘金数据到数据库
void DataService::saveJuejinDataToDatabase(const QVariantList& data)
{
    try {
        qDebug() << "将掘金数据保存到数据库...";
        
        // 获取数据库实例
        auto database = m_database;
        if (!database) {
            qWarning() << "数据库未初始化，无法保存掘金数据";
            return;
        }
        
        // 批量保存数据
        for (const QVariant& item : data) {
            QVariantMap record = item.toMap();
            
            // 构建SQL插入语句
            QString sql = "INSERT INTO daily_bar (symbol, trade_date, open, high, low, close, volume) "
                         "VALUES (:symbol, :trade_date, :open, :high, :low, :close, :volume) "
                         "ON DUPLICATE KEY UPDATE "
                         "open = VALUES(open), high = VALUES(high), low = VALUES(low), "
                         "close = VALUES(close), volume = VALUES(volume)";
            
            std::map<QString, QVariant> params;
            params[":symbol"] = record["symbol"].toString();
            params[":trade_date"] = record["date"].toString();
            params[":open"] = record["open"].toDouble();
            params[":high"] = record["high"].toDouble();
            params[":low"] = record["low"].toDouble();
            params[":close"] = record["close"].toDouble();
            params[":volume"] = record["volume"].toDouble();
            
            try {
                database->executeUpdate(sql, params);
            } catch (const std::exception& e) {
                qWarning() << "保存掘金数据失败:" << e.what();
            }
        }
        
        qDebug() << "掘金数据已保存到数据库";
        
    } catch (const std::exception& e) {
        qWarning() << "保存掘金数据到数据库失败:" << e.what();
    }
}
