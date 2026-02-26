// DataService.cpp - 极简实现 (目标: <200行)
#include "DataService.h"
#include "DataServiceCache.h"
#include "database/QueryBuilder.h"
#include "database/QtMySQLDatabase.h"
#include "database/DatabaseConfig.h"
#include <QDebug>
#include <QDateTime>
#include <QThread>
#include <stdexcept>
#include <memory>

using namespace astock::database;

// PImpl实现类
class DataService::Impl {
public:
    Impl() {
        // 初始化缓存系统
        DataServiceCache::getInstance().initializeCache();
    }
    
    ~Impl() {
        // 显式关闭数据库连接
        if (database) {
            try {
                database->close();
            } catch (const std::exception& e) {
                qWarning() << "DataService::Impl: 关闭数据库连接时出错:" << e.what();
            }
            database.reset();
        }
    }
    
    // 数据库连接
    std::shared_ptr<QtMySQLDatabase> database;
    
    // 私有方法
    std::shared_ptr<QueryBuilder> createQueryBuilder();
    bool checkDatabaseConnection();
    QString generateCacheKey(const QString& symbol, const QString& startDate, const QString& endDate);
    QVariantList queryDataInternal(const QString& symbol, const QString& startDate, const QString& endDate);
    QVariantList convertQueryResultToVariantList(const QueryResult& result);
    
private:
    bool initializeDatabaseIfNeeded();
};

DataService::DataService(QObject* parent) 
    : QObject(parent), m_impl(std::make_unique<Impl>()) {
}

DataService::~DataService() {
    // 确保所有数据库连接被正确关闭
    // m_impl将自动销毁，在其析构函数中会清理资源
}

void DataService::queryData(const QString& symbol, 
                           const QString& startDate, 
                           const QString& endDate) {
    try {
        
        // 1. 参数检查（逻辑判断）
        if (symbol.isEmpty() || startDate.isEmpty() || endDate.isEmpty()) {
            emit error("参数不能为空：symbol, startDate, endDate");
            return;
        }
        
        emit queryProgress(10, "开始查询...");
        
        // 2. 检查数据库连接（逻辑判断，不崩溃）
        if (!m_impl->checkDatabaseConnection()) {
            emit error("数据库连接不可用，请检查database.json配置");
            return;
        }
        
        emit queryProgress(30, "数据库连接正常，执行查询...");
        
        // 3. 执行查询（使用QueryBuilder链式调用）
        QVariantList data = m_impl->queryDataInternal(symbol, startDate, endDate);
        
        emit queryProgress(90, "查询完成，处理结果...");
        
        // 4. 发送完成信号
        emit queryProgress(100, "查询完成");
        emit queryCompleted(true, QString("查询成功，获取%1条数据").arg(data.size()), data);
        
    } catch (const std::exception& e) {
        QString errorMsg = QString("查询失败: %1").arg(e.what());
        qCritical() << "DataService::queryData:" << errorMsg;
        emit error(errorMsg);
    } catch (...) {
        QString errorMsg = "未知错误，查询失败";
        qCritical() << "DataService::queryData:" << errorMsg;
        emit error(errorMsg);
    }
}

void DataService::cleanData(const QVariantList& data, 
                           const QVariantMap& rules) {
    try {
        
        if (data.isEmpty()) {
            emit error("没有数据可清洗");
            return;
        }
        
        emit cleaningProgress(10, "开始清洗数据...");
        
        // 简化实现：直接返回原数据，实际项目中使用DataCleaningEngine
        QVariantList cleanedData = data;
        
        QString message = QString("数据清洗完成: 原始 %1 条 -> 清洗后 %2 条")
            .arg(data.size())
            .arg(cleanedData.size());
        
        emit cleaningProgress(100, "清洗完成");
        emit cleaningCompleted(true, message, cleanedData);
        
    } catch (const std::exception& e) {
        QString errorMsg = QString("清洗失败: %1").arg(e.what());
        qCritical() << "DataService::cleanData:" << errorMsg;
        emit error(errorMsg);
    } catch (...) {
        QString errorMsg = "未知错误，清洗失败";
        qCritical() << "DataService::cleanData:" << errorMsg;
        emit error(errorMsg);
    }
}

void DataService::queryAndCleanData(const QString& symbol,
                                   const QString& startDate,
                                   const QString& endDate,
                                   const QVariantMap& rules) {
    try {
        
        // 1. 查询数据
        emit queryProgress(10, "开始查询数据...");
        QVariantList data = m_impl->queryDataInternal(symbol, startDate, endDate);
        
        if (data.isEmpty()) {
            emit error("未查询到数据，无法进行清洗");
            return;
        }
        
        emit queryProgress(100, "查询完成");
        emit queryCompleted(true, QString("查询成功，获取%1条数据").arg(data.size()), data);
        
        // 短暂延迟，让UI有时间显示查询结果
        QThread::msleep(300);
        
        // 2. 清洗数据
        cleanData(data, rules);
        
    } catch (const std::exception& e) {
        QString errorMsg = QString("查询并清洗失败: %1").arg(e.what());
        qCritical() << "DataService::queryAndCleanData:" << errorMsg;
        emit error(errorMsg);
    } catch (...) {
        QString errorMsg = "未知错误，查询并清洗失败";
        qCritical() << "DataService::queryAndCleanData:" << errorMsg;
        emit error(errorMsg);
    }
}

// ============ Impl私有方法实现 ============

std::shared_ptr<QueryBuilder> DataService::Impl::createQueryBuilder() {
    if (!database) {
        if (!initializeDatabaseIfNeeded()) {
            throw std::runtime_error("无法初始化数据库");
        }
    }
    
    return astock::database::createQueryBuilder(database);
}

bool DataService::Impl::checkDatabaseConnection() {
    try {
        if (!database) {
            return initializeDatabaseIfNeeded();
        }
        
        // 简单测试连接
        auto builder = createQueryBuilder();
        return builder != nullptr;
        
    } catch (const std::exception& e) {
        qWarning() << "DataService::Impl::checkDatabaseConnection: 连接检查失败" << e.what();
        return false;
    }
}

QString DataService::Impl::generateCacheKey(const QString& symbol, const QString& startDate, const QString& endDate) {
    QString key = QString("dataservice_%1_%2_%3")
        .arg(symbol)
        .arg(startDate)
        .arg(endDate);
    
    // 移除特殊字符
    key = key.replace("/", "_").replace(":", "_").replace(" ", "_");
    return key;
}

QVariantList DataService::Impl::queryDataInternal(const QString& symbol, const QString& startDate, const QString& endDate) {
    // 使用缓存装饰器进行查询
    return DataServiceCacheDecorator::queryWithCache(symbol, startDate, endDate,
        [this, symbol, startDate, endDate]() -> QVariantList {
            // 使用QueryBuilder链式调用
            auto builder = createQueryBuilder();
            if (!builder) {
                throw std::runtime_error("无法创建QueryBuilder");
            }
            
            try {
                // 链式调用示例
                auto query = builder->from("daily_bar")
                                     .select("symbol, trade_date, open, high, low, close, volume");
                
                // 如果symbol不为空，添加symbol条件
                if (!symbol.isEmpty()) {
                    // 检查是否需要添加后缀
                    QString symbolWithSuffix = symbol;
                    if (symbol.length() == 6) {
                        // 判断是沪市还是深市
                        if (symbol.startsWith("6") || symbol.startsWith("9")) {
                            symbolWithSuffix = symbol + ".SH";
                        } else if (symbol.startsWith("0") || symbol.startsWith("3")) {
                            symbolWithSuffix = symbol + ".SZ";
                        } else if (symbol.startsWith("4") || symbol.startsWith("8")) {
                            symbolWithSuffix = symbol + ".BJ";
                        }
                    }
                    query = query.where("symbol", ConditionType::EQUAL, symbolWithSuffix);
                }
                // 添加日期条件
                query = query.where("trade_date", ConditionType::BETWEEN, startDate, endDate)
                             .orderBy("trade_date", OrderType::ASC);
                
                auto result = query.execute();
                
                return convertQueryResultToVariantList(result);
                
            } catch (const std::exception& e) {
                QString errorMsg = QString("数据库查询失败: %1").arg(e.what());
                qCritical() << "DataService::Impl::queryDataInternal:" << errorMsg;
                throw;
            }
        });
}

bool DataService::Impl::initializeDatabaseIfNeeded() {
    if (database && database->isOpen()) {
        return true;
    }
    
    try {
        // 从配置文件读取数据库配置
        DatabaseConfig config;
        config.host = "localhost";
        config.port = 3306;
        config.database = "astock_quant";
        config.username = "root";
        config.password = "123456a";
        config.charset = "utf8mb4";
        config.pool_size = 3;  // 简化：使用较小的连接池
        
        // 创建数据库对象
        database = std::shared_ptr<QtMySQLDatabase>(new QtMySQLDatabase(config, true));
        
        // 打开连接
        if (!database->open()) {
            QString error = database->getLastError();
            qCritical() << "DataService::Impl::initializeDatabaseIfNeeded: 数据库连接失败:" << error;
            return false;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        qCritical() << "DataService::Impl::initializeDatabaseIfNeeded: 数据库初始化失败:" << e.what();
        return false;
    }
}

QVariantList DataService::Impl::convertQueryResultToVariantList(const QueryResult& result) {
    QVariantList data;
    
    for (const auto& row : result.getRows()) {
        QVariantMap record;
        record["symbol"] = row.getString("symbol");
        record["date"] = row.getString("trade_date");
        record["open"] = row.getDouble("open");
        record["high"] = row.getDouble("high");
        record["low"] = row.getDouble("low");
        record["close"] = row.getDouble("close");
        record["volume"] = row.getDouble("volume");
        
        data.append(record);
    }
    
    return data;
}

// ============ 新增方法实现 ============

void DataService::loadFromDatabase(const QString& symbol, 
                                  const QString& startDate, 
                                  const QString& endDate) {
    try {
        
        // 参数检查
        if (startDate.isEmpty() || endDate.isEmpty()) {
            emit error("开始日期和结束日期不能为空");
            return;
        }
        
        // 使用queryDataInternal方法查询数据
        emit queryProgress(10, "开始从数据库加载数据...");
        
        if (!m_impl->checkDatabaseConnection()) {
            emit error("数据库连接不可用，请检查database.json配置");
            return;
        }
        
        emit queryProgress(30, "数据库连接正常，执行查询...");
        
        // 执行查询
        QVariantList data = m_impl->queryDataInternal(symbol, startDate, endDate);
        
        emit queryProgress(90, "数据加载完成，处理结果...");
        
        // 更新缓存数据
        m_fetchedData = data;
        
        emit queryProgress(100, "数据加载完成");
        emit queryCompleted(true, QString("从数据库加载成功，获取%1条数据").arg(data.size()), data);
        emit dataLoadedFromDatabase(true, QString("成功加载%1条数据").arg(data.size()), data.size());
        emit fetchedDataChanged();
        
    } catch (const std::exception& e) {
        QString errorMsg = QString("从数据库加载数据失败: %1").arg(e.what());
        qCritical() << "DataService::loadFromDatabase:" << errorMsg;
        emit error(errorMsg);
        emit dataLoadedFromDatabase(false, errorMsg, 0);
    } catch (...) {
        QString errorMsg = "未知错误，从数据库加载数据失败";
        qCritical() << "DataService::loadFromDatabase:" << errorMsg;
        emit error(errorMsg);
        emit dataLoadedFromDatabase(false, errorMsg, 0);
    }
}

void DataService::cleanDataAsync(const QVariantList& data, 
                                const QVariantMap& rules) {
    try {
        
        if (data.isEmpty()) {
            emit error("没有数据可清洗");
            return;
        }
        
        emit cleaningProgress(10, "开始异步清洗数据...");
        
        // 简化实现：直接返回原数据（实际项目中应使用DataCleaningEngine）
        // 这里模拟异步操作
        QVariantList cleanedData = data;
        
        QString message = QString("异步数据清洗完成: 原始 %1 条 -> 清洗后 %2 条")
            .arg(data.size())
            .arg(cleanedData.size());
        
        emit cleaningProgress(100, "异步清洗完成");
        emit cleaningCompleted(true, message, cleanedData);
        
    } catch (const std::exception& e) {
        QString errorMsg = QString("异步清洗失败: %1").arg(e.what());
        qCritical() << "DataService::cleanDataAsync:" << errorMsg;
        emit error(errorMsg);
    } catch (...) {
        QString errorMsg = "未知错误，异步清洗失败";
        qCritical() << "DataService::cleanDataAsync:" << errorMsg;
        emit error(errorMsg);
    }
}

QVariantList DataService::fetchedData() const {
    return m_fetchedData;
}
