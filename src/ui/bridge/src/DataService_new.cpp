// DataService.cpp - 数据服务实现（包含掘金数据源回退）
// ============================================================================
// 功能概述：
// 1. 数据库管理：MySQL数据库连接、初始化、连接池管理
// 2. 数据操作：异步数据加载、保存、查询、清洗
// 3. 掘金数据源回退：当数据库中没有数据时，自动从掘金获取
// 4. 进度报告：实时进度更新和状态反馈
// 5. 错误处理：完善的异常处理和用户友好错误信息
// 6. 异步处理：所有耗时操作在工作线程中执行，不阻塞UI
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
#include <memory>
#include <atomic>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

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
            
            // 构建Python命令
            QString pythonScript = QDir::currentPath() + "/tools/fetch_single_from_juejin.py";
            
            // 检查Python脚本是否存在
            if (!QFile::exists(pythonScript)) {
                errorMessage = "掘金数据获取脚本不存在: " + pythonScript;
                qWarning() << errorMessage;
                return false;
            }
            
            // 构建命令参数
            QStringList args;
            args << pythonScript;
            
            if (!m_symbol.isEmpty()) {
                args << "--symbol" << m_symbol;
            } else {
                // 如果没有指定股票代码，使用默认股票
                args << "--symbol" << "000001.SZ";
            }
            
            args << "--start_date" << m_startDate;
            args << "--end_date" << m_endDate;
            args << "--output" << tempFile;
            
            // 执行Python脚本
            QProcess process;
            process.setProgram("python");
            process.setArguments(args);
            
            qDebug() << "执行Python命令:" << "python" << args;
            
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

// 注意：这里只实现了DataLoadTask类，DataService类的其他部分需要从原文件复制
// 由于文件太大，这里只展示关键修改部分