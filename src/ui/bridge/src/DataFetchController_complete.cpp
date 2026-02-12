// DataFetchController_complete.cpp - 完整的QtMySQLDatabase版本
#include "DataFetchController.h"
#include "DataCleaningEngine.h"
#include "database/QtMySQLDatabase.h"

#include <QDebug>
#include <QDateTime>
#include <QMutexLocker>
#include <QCoreApplication>
#include <QTimer>
#include <QUuid>
#include <QFile>
#include <QDir>
#include <memory>

using namespace astock::database;

// 全局数据库实例
static std::shared_ptr<QtMySQLDatabase> g_mysqlDb;
static std::mutex g_dbMutex;

// 获取数据库实例
std::shared_ptr<QtMySQLDatabase> getMySQLDatabase() {
    std::lock_guard<std::mutex> lock(g_dbMutex);
    
    if (!g_mysqlDb) {
        try {
            // 创建数据库配置
            DatabaseConfig config;
            config.host = "localhost";
            config.port = 3306;
            config.database = "astock_quant";
            config.username = "root";
            config.password = "123456a";  // 请修改为你的密码
            config.charset = "utf8mb4";
            config.pool_size = 5;
            
            // 创建数据库对象（使用连接池）
            g_mysqlDb = std::make_shared<QtMySQLDatabase>(config, true);
            
            // 打开连接
            if (!g_mysqlDb->open()) {
                qCritical() << "Failed to open MySQL database:" << g_mysqlDb->getLastError();
                g_mysqlDb.reset();
                return nullptr;
            }
            
            qDebug() << "✅ MySQL database connected successfully using QMYSQL driver";
            
        } catch (const std::exception& e) {
            qCritical() << "Failed to create MySQL database:" << e.what();
            g_mysqlDb.reset();
            return nullptr;
        }
    }
    
    return g_mysqlDb;
}

DataFetchController::DataFetchController(QObject* parent)
    : QObject(parent)
{
    // 设置默认日期（最近30天）
    QDateTime currentDate = QDateTime::currentDateTime();
    QDateTime startDate = currentDate.addDays(-30);
    
    m_startDate = startDate.toString("yyyy-MM-dd");
    m_endDate = currentDate.toString("yyyy-MM-dd");
    
    // 创建数据清洗引擎
    m_cleaningEngine = std::make_unique<DataCleaningEngine>();
    
    // 立即初始化（不使用定时器）
    initialize();
}

DataFetchController::~DataFetchController()
{
    // 清理
}

void DataFetchController::initialize()
{
    qDebug() << "DataFetchController::initialize() called";
    
    try {
        // 测试数据库连接
        auto db = getMySQLDatabase();
        if (db) {
            QString version = db->getDatabaseVersion();
            qDebug() << "Database version:" << version;
            updateStatus(QString("初始化完成，数据库版本: %1").arg(version), 100);
        } else {
            updateStatus("初始化完成（数据库连接失败）", 100);
        }
        
        m_initialized = true;
        qDebug() << "DataFetchController: Initialized successfully";
        
    } catch (const std::exception& e) {
        qCritical() << "Initialization error:" << e.what();
        updateStatus(QString("初始化错误: %1").arg(e.what()), 0);
    }
}

void DataFetchController::fetchData()
{
    qDebug() << "DataFetchController::fetchData() called";
    
    if (!m_initialized) {
        qWarning() << "Not initialized, calling initialize()";
        initialize();
    }
    
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
    qDebug() << "DataFetchController::saveToDatabase() called";
    
    if (!m_initialized) {
        qWarning() << "Not initialized, calling initialize()";
        initialize();
    }
    
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
    
    updateStatus("正在保存数据到MySQL数据库...", 0);
    
    // 异步执行数据库保存
    QTimer::singleShot(100, this, [this]() {
        QMutexLocker locker(&m_mutex);
        
        QString errorMessage;
        bool success = false;
        int savedCount = 0;
        
        try {
            auto db = getMySQLDatabase();
            if (!db) {
                errorMessage = "数据库连接失败";
                updateStatus("数据库连接失败", 0);
                emit dataSavedToDatabase(false, errorMessage);
                return;
            }
            
            qDebug() << "Saving data to MySQL database using QtMySQLDatabase...";
            
            // 准备批量参数
            std::vector<std::map<QString, QVariant>> batchParams;
            
            for (const QVariant& itemVar : m_fetchedData) {
                QVariantMap item = itemVar.toMap();
                
                std::map<QString, QVariant> params;
                params["symbol"] = item["symbol"].toString();
                params["trade_date"] = item["date"].toString();
                params["open"] = item["open"].toDouble();
                params["high"] = item["high"].toDouble();
                params["low"] = item["low"].toDouble();
                params["close"] = item["close"].toDouble();
                params["volume"] = item["volume"].toDouble();
                
                batchParams.push_back(params);
            }
            
            // 执行批量更新
            QString sql = "INSERT INTO daily_bar (symbol, trade_date, open, high, low, close, volume) "
                         "VALUES (:symbol, :trade_date, :open, :high, :low, :close, :volume) "
                         "ON DUPLICATE KEY UPDATE "
                         "open = VALUES(open), high = VALUES(high), low = VALUES(low), "
                         "close = VALUES(close), volume = VALUES(volume)";
            
            savedCount = db->executeBatchUpdate(sql, batchParams);
            
            success = true;
            updateStatus(QString("数据保存完成，共保存 %1 条记录").arg(savedCount), 100);
            emit dataSavedToDatabase(true, QString("数据保存成功，共保存 %1 条记录").arg(savedCount));
            
        } catch (const QtMySQLException& e) {
            errorMessage = QString("数据库操作失败: %1").arg(e.what());
            qCritical() << errorMessage;
            updateStatus("数据库操作失败", 0);
            emit dataSavedToDatabase(false, errorMessage);
        } catch (const std::exception& e) {
            errorMessage = QString("数据库操作异常: %1").arg(e.what());
            qCritical() << errorMessage;
            updateStatus("数据库操作异常", 0);
            emit dataSavedToDatabase(false, errorMessage);
        } catch (...) {
            errorMessage = "未知数据库错误";
            qCritical() << errorMessage;
            updateStatus("未知数据库错误", 0);
            emit dataSavedToDatabase(false, errorMessage);
        }
    });
}

void DataFetchController::loadFromDatabase(const QString& symbol, const QString& startDate, const QString& endDate)
{
    qDebug() << "DataFetchController::loadFromDatabase() called with symbol:" << symbol 
             << "startDate:" << startDate << "endDate:" << endDate;
    
    if (!m_initialized) {
        qWarning() << "Not initialized, calling initialize()";
        initialize();
    }
    
    if (!m_initialized) {
        updateStatus("未初始化", 0);
        emit dataLoadedFromDatabase(false, "未初始化", 0);
        return;
    }
    
    updateStatus("正在从数据库加载数据...", 0);
    
    // 异步执行数据库查询
    QTimer::singleShot(100, this, [this, symbol, startDate, endDate]() {
        QMutexLocker locker(&m_mutex);
        
        QVariantList realData;
        QString errorMessage;
        bool success = false;
        
        try {
            auto db = getMySQLDatabase();
            if (!db) {
                errorMessage = "数据库连接失败";
                updateStatus("数据库连接失败", 0);
                emit dataLoadedFromDatabase(false, errorMessage, 0);
                return;
            }
            
            qDebug() << "Loading data from MySQL database using QtMySQLDatabase...";
            
            // 检查表是否存在
            if (!db->tableExists("daily_bar")) {
                errorMessage = "daily_bar表不存在于MySQL数据库中";
                qWarning() << errorMessage;
                updateStatus("数据表不存在", 0);
                emit dataLoadedFromDatabase(false, errorMessage, 0);
                return;
            }
            
            // 构建查询
            QString sql;
            std::map<QString, QVariant> params;
            
            if (symbol.isEmpty()) {
                sql = "SELECT symbol, trade_date, open, high, low, close, volume FROM daily_bar";
                if (!startDate.isEmpty() && !endDate.isEmpty()) {
                    sql += " WHERE trade_date BETWEEN :start_date AND :end_date";
                    params["start_date"] = startDate;
                    params["end_date"] = endDate;
                }
                sql += " ORDER BY trade_date DESC LIMIT 100";
            } else {
                sql = "SELECT symbol, trade_date, open, high, low, close, volume FROM daily_bar WHERE symbol = :symbol";
                params["symbol"] = symbol;
                
                if (!startDate.isEmpty() && !endDate.isEmpty()) {
                    sql += " AND trade_date BETWEEN :start_date AND :end_date";
                    params["start_date"] = startDate;
                    params["end_date"] = endDate;
                }
                sql += " ORDER BY trade_date DESC LIMIT 100";
            }
            
            qDebug() << "执行MySQL查询:" << sql;
            
            // 执行查询
            QueryResult result = db->executeQuery(sql, params);
            
            // 处理查询结果
            int count = 0;
            for (const auto& row : result.getRows()) {
                QVariantMap item;
                item["symbol"] = row.getString("symbol");
                item["date"] = row.getString("trade_date");
                item["open"] = row.getDouble("open");
                item["high"] = row.getDouble("high");
                item["low"] = row.getDouble("low");
                item["close"] = row.getDouble("close");
                item["volume"] = row.getDouble("volume");
                realData.append(item);
                count++;
            }
            
            if (count == 0) {
                errorMessage = "没有找到匹配的数据";
                qWarning() << errorMessage;
                updateStatus("没有找到数据", 0);
                emit dataLoadedFromDatabase(false, errorMessage, 0);
                return;
            }
            
            m_fetchedData = realData;
            success = true;
            
            updateStatus(QString("数据加载完成，共 %1 条记录").arg(count), 100);
            emit fetchedDataChanged();
            emit dataLoadedFromDatabase(true, QString("数据加载成功，共 %1 条记录").arg(count), count);
            
        } catch (const QtMySQLException& e) {
            errorMessage = QString("数据库操作失败: %1").arg(e.what());
            qCritical() << errorMessage;
            updateStatus("数据库操作失败", 0);
            emit dataLoadedFromDatabase(false, errorMessage, 0);
        } catch (const std::exception& e) {
            errorMessage = QString("数据库操作异常: %1").arg(e.what());
            qCritical() << errorMessage;
            updateStatus("数据库操作异常", 0);
            emit dataLoadedFromDatabase(false, errorMessage, 0);
        } catch (...) {
            errorMessage = "未知数据库错误";
            qCritical() << errorMessage;
            updateStatus("未知数据库错误", 0);
            emit dataLoadedFromDatabase(false, errorMessage, 0);
        }
    });
}

QVariantList DataFetchController::cleanData(const QVariantList& data, const QVariantMap& rules)
{
    qDebug() << "DataFetchController::cleanData called with" << data.size() << "items and" << rules.size() << "rules";
    
    if (data.isEmpty()) {
        qDebug() << "No data to clean";
        return QVariantList();
    }
    
    if (!m_cleaningEngine) {
        qWarning() << "Data cleaning engine not initialized";
        return data;
    }
    
    // 将QML规则转换为数据清洗引擎规则
    QVector<DataCleaningEngine::CleaningRule> cleaningRules;
    
    // 时间范围过滤
    if (rules.contains("timeRange")) {
        QVariantMap timeRangeRule = rules["timeRange"].toMap();
        DataCleaningEngine::CleaningRule rule(
            DataCleaningEngine::RULE_TIME_RANGE,
            "时间范围过滤",
            "过滤指定时间范围外的数据"
        );
        rule.parameters["startDate"] = timeRangeRule["start"].toString();
        rule.parameters["endDate"] = timeRangeRule["end"].toString();
        rule.enabled = true;
        cleaningRules.append(rule);
    }
    
    // 价格过滤
    if (rules.contains("priceFilter") && rules["priceFilter"].toMap()["enabled"].toBool()) {
        QVariantMap priceRule = rules["priceFilter"].toMap();
        DataCleaningEngine::CleaningRule rule(
            DataCleaningEngine::RULE_PRICE_FILTER,
            "价格过滤",
            "过滤异常价格数据"
        );
        rule.parameters["minPrice"] = priceRule["min"].toDouble();
        rule.parameters["maxPrice"] = priceRule["max"].toDouble();
        rule.parameters["checkOpen"] = true;
        rule.parameters["checkHigh"] = true;
        rule.parameters["checkLow"] = true;
        rule.parameters["checkClose"] = true;
        rule.enabled = true;
        cleaningRules.append(rule);
    }
    
    // 成交量过滤
    if (rules.contains("volumeFilter") && rules["volumeFilter"].toMap()["enabled"].toBool()) {
        QVariantMap volumeRule = rules["volumeFilter"].toMap();
        DataCleaningEngine::CleaningRule rule(
            DataCleaningEngine::RULE_VOLUME_FILTER,
            "成交量过滤",
            "过滤异常成交量数据"
        );
        rule.parameters["minVolume"] = volumeRule["min"].toDouble();
        rule.parameters["maxVolume"] = 1000000000.0; // 默认最大值
        rule.enabled = true;
        cleaningRules.append(rule);
    }
    
    // 完整性检查
    if (rules.contains("completenessFilter") && rules["completenessFilter"].toBool()) {
        DataCleaningEngine::CleaningRule rule(
            DataCleaningEngine::RULE_COMPLETENESS_CHECK,
            "完整性检查",
            "检查数据字段完整性"
        );
        rule.parameters["requiredFields"] = QStringList{"symbol", "date", "open", "high", "low", "close", "volume"};
        rule.enabled = true;
        cleaningRules.append(rule);
    }
    
    // 异常值检测
    if (rules.contains("outlierFilter") && rules["outlierFilter"].toBool()) {
        DataCleaningEngine::CleaningRule rule(
            DataCleaningEngine::RULE_OUTLIER_DETECTION,
            "异常值检测",
            "检测并过滤异常值"
        );
        rule.parameters["priceDeviation"] = 3.0;
        rule.parameters["volumeDeviation"] = 5.0;
        rule.enabled = true;
        cleaningRules.append(rule);
    }
    
    // 重复数据删除
    if (rules.contains("duplicateFilter") && rules["duplicateFilter"].toBool()) {
        DataCleaningEngine::CleaningRule rule(
            DataCleaningEngine::RULE_DUPLICATE_REMOVAL,
            "重复数据删除",
            "删除重复的数据记录"
        );
        rule.parameters["keyFields"] = QStringList{"symbol", "date"};
        rule.enabled = true;
        cleaningRules.append(rule);
    }
    
    // 格式验证
    if (rules.contains("formatValidation") && rules["formatValidation"].toBool()) {
        DataCleaningEngine::CleaningRule rule(
            DataCleaningEngine::RULE_FORMAT_VALIDATION,
            "格式验证",
            "验证数据格式正确性"
        );
        rule.parameters["symbolPattern"] = "^[0-9]{6}\\.[A-Z]{2}$";
        rule.parameters["datePattern"] = "^\\d{4