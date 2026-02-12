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

# 添加astock_engine目录到路径
astock_engine_path = os.path.join(project_root, "astock_engine")
if astock_engine_path not in sys.path:
    sys.path.insert(0, astock_engine_path)

# 添加tools目录到路径
tools_path = os.path.join(project_root, "tools")
if tools_path not in sys.path:
    sys.path.insert(0, tools_path)

try:
    from tools.import_from_juejin import fetch_daily_bars_from_juejin
    
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
)").arg("g:\\C++\\AStockQuantEngine")  // 项目根目录（硬编码绝对路径）
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
    , m_cleaningEngine