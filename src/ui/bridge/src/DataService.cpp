// DataService.cpp - 极简实现 (目标: <200行)
#include "DataService.h"
#include "DataServiceCache.h"
#include "DataCleaningEngine.h"  // 添加DataCleaningEngine头文件
#include "database/QueryBuilder.h"
#include "database/QtMySQLDatabase.h"
#include "database/DatabaseConfig.h"
#include <QDebug>
#include <QDateTime>
#include <QThread>
#include <stdexcept>
#include <memory>

using namespace astock::database;

namespace {

QStringList fullDailyBarFields()
{
    return {
        "symbol", "trade_date", "open", "high", "low", "close", "pre_close",
        "volume", "turnover", "change_pct", "change_amt", "amplitude",
        "turnover_rate", "pe_ratio", "pb_ratio", "market_cap",
        "circulating_market_cap", "data_source"
    };
}

QVariant readFieldValue(const QueryResultRow& row, const QString& field)
{
    static const QSet<QString> numericFields = {
        "open", "high", "low", "close", "pre_close", "volume", "turnover",
        "change_pct", "change_amt", "amplitude", "turnover_rate", "pe_ratio",
        "pb_ratio", "market_cap", "circulating_market_cap"
    };

    if (numericFields.contains(field)) {
        return row.getDouble(field);
    }
    return row.getString(field);
}

}

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
    bool initializeDatabaseIfNeeded();
    QString generateCacheKey(const QString& symbol, const QString& startDate, const QString& endDate);
    QVariantList queryDataInternal(const QString& symbol, const QString& startDate, const QString& endDate);
    QVariantList convertQueryResultToVariantList(const QueryResult& result);
    QVariantList convertIndexConstituentsResultToVariantList(const QueryResult& result);
    
    // 规则转换方法
    QVector<DataCleaningEngine::CleaningRule> convertQmlRulesToCleaningRules(const QVariantMap& qmlRules);
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
        
        // 实际调用DataCleaningEngine进行清洗
        DataCleaningEngine cleaningEngine;
        
        // 将QML规则转换为DataCleaningEngine规则
        QVector<DataCleaningEngine::CleaningRule> cleaningRules = m_impl->convertQmlRulesToCleaningRules(rules);
        
        // 执行清洗
        QVariantList cleanedData = cleaningEngine.cleanData(data, cleaningRules);
        
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
                // 链式调用示例 - 使用daily_bar表直接查询数据，并连接symbol_info获取名称
                auto query = builder->from("daily_bar d")
                                     .select(
                                         "d.symbol, s.name, d.trade_date, "
                                         "d.open, d.high, d.low, d.close, d.pre_close, "
                                         "d.volume, d.turnover, d.change_pct, d.change_amt, "
                                         "d.amplitude, d.turnover_rate, d.pe_ratio, d.pb_ratio, "
                                         "d.market_cap, d.circulating_market_cap, d.data_source"
                                     )
                                     .join("symbol_info s", "d.symbol = s.symbol", "LEFT");
                
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
                    query = query.where("symbol", ConditionType::TYPES_EQUAL, symbolWithSuffix);
                }
                // 添加日期条件
                query = query.where("trade_date", ConditionType::TYPES_BETWEEN, startDate, endDate)
                             .orderBy("trade_date", OrderType::Order_ASC);
                
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
        
        // 创建数据库对象 - 禁用连接池以避免"device or resource busy"错误
        database = std::shared_ptr<QtMySQLDatabase>(new QtMySQLDatabase(config, false));
        
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
        record["name"] = row.getString("name");  // 添加股票名称
        record["date"] = row.getString("trade_date");
        record["trade_date"] = row.getString("trade_date");
        record["open"] = row.getDouble("open");
        record["high"] = row.getDouble("high");
        record["low"] = row.getDouble("low");
        record["close"] = row.getDouble("close");
        record["pre_close"] = row.getDouble("pre_close");
        record["volume"] = row.getDouble("volume");
        record["turnover"] = row.getDouble("turnover");
        record["change_pct"] = row.getDouble("change_pct");
        record["change_amt"] = row.getDouble("change_amt");
        record["amplitude"] = row.getDouble("amplitude");
        record["turnover_rate"] = row.getDouble("turnover_rate");
        record["pe_ratio"] = row.getDouble("pe_ratio");
        record["pb_ratio"] = row.getDouble("pb_ratio");
        record["market_cap"] = row.getDouble("market_cap");
        record["circulating_market_cap"] = row.getDouble("circulating_market_cap");
        record["data_source"] = row.getString("data_source");
        
        data.append(record);
    }
    
    return data;
}

QVector<DataCleaningEngine::CleaningRule> DataService::Impl::convertQmlRulesToCleaningRules(const QVariantMap& qmlRules) {
    QVector<DataCleaningEngine::CleaningRule> rules;
    
    qDebug() << "================= Converting QML rules to cleaning rules =================";
    qDebug() << "QML rules input:" << qmlRules;
    qDebug() << "Keys in qmlRules:" << qmlRules.keys();
    
    // 解析QML规则格式
    // QML规则格式示例:
    // {
    //   "outlierFilter": true,
    //   "missingValue": true,
    //   "timeRange": {
    //     "enabled": true,
    //     "start": "2026-01-30",
    //     "end": "2026-01-30"
    //   },
    //   "dataCleaning": true,
    //   "market": {"aShares": true}
    // }
    
    // 1. 时间范围过滤
    if (qmlRules.contains("timeRange")) {
        QVariantMap timeRangeRule = qmlRules["timeRange"].toMap();
        qDebug() << "timeRange rule found:" << timeRangeRule;
        qDebug() << "timeRange enabled:" << timeRangeRule.value("enabled", false).toBool();
        
        if (timeRangeRule.value("enabled", false).toBool()) {
            QString startDate = timeRangeRule.value("start", "").toString();
            QString endDate = timeRangeRule.value("end", "").toString();
            
            qDebug() << "Time range dates - start:" << startDate << "end:" << endDate;
            
            if (!startDate.isEmpty() && !endDate.isEmpty()) {
                DataCleaningEngine::CleaningRule rule(
                    DataCleaningEngine::RULE_TIME_RANGE,
                    "时间范围过滤",
                    QString("过滤时间范围: %1 至 %2").arg(startDate).arg(endDate)
                );
                rule.parameters["startDate"] = startDate;
                rule.parameters["endDate"] = endDate;
                rule.enabled = true;
                rules.append(rule);
                
                qDebug() << "✅ Added time range rule:" << startDate << "to" << endDate;
            } else {
                qDebug() << "⚠️ Time range rule has empty dates, skipping";
            }
        } else {
            qDebug() << "Time range rule is disabled";
        }
    } else {
        qDebug() << "No timeRange key found in qmlRules";
    }
    
    // 2. 异常值检测
    bool outlierFilter = qmlRules.value("outlierFilter", false).toBool();
    qDebug() << "outlierFilter value:" << outlierFilter;
    
    if (outlierFilter) {
        DataCleaningEngine::CleaningRule rule(
            DataCleaningEngine::RULE_OUTLIER_DETECTION,
            "异常值检测",
            "检测并过滤价格和成交量的异常值"
        );
        rule.parameters["priceDeviation"] = 3.0;
        rule.parameters["volumeDeviation"] = 5.0;
        rule.enabled = true;
        rules.append(rule);
        
        qDebug() << "✅ Added outlier detection rule";
    }
    
    // 3. 缺失值处理 (对应completeness check)
    bool missingValue = qmlRules.value("missingValue", false).toBool();
    qDebug() << "missingValue value:" << missingValue;
    
    if (missingValue) {
        DataCleaningEngine::CleaningRule rule(
            DataCleaningEngine::RULE_COMPLETENESS_CHECK,
            "完整性检查",
            "检查数据字段完整性，过滤缺失值"
        );
        rule.parameters["requiredFields"] = QStringList{"symbol", "date", "open", "high", "low", "close", "volume"};
        rule.enabled = true;
        rules.append(rule);
        
        qDebug() << "✅ Added completeness check rule";
    }
    
    // 4. 基本数据清洗 (对应format validation)
    bool dataCleaning = qmlRules.value("dataCleaning", false).toBool();
    qDebug() << "dataCleaning value:" << dataCleaning;
    
    if (dataCleaning) {
        // 格式验证
        DataCleaningEngine::CleaningRule formatRule(
            DataCleaningEngine::RULE_FORMAT_VALIDATION,
            "格式验证",
            "验证数据格式正确性"
        );
        formatRule.parameters["symbolPattern"] = "^[0-9]{6}\\.[A-Z]{2}$";
        formatRule.parameters["datePattern"] = "^(\\d{4}[-./]\\d{2}[-./]\\d{2}|\\d{2}[-./]\\d{2}[-./]\\d{4})$";
        formatRule.enabled = true;
        rules.append(formatRule);
        
        // 价格过滤
        DataCleaningEngine::CleaningRule priceRule(
            DataCleaningEngine::RULE_PRICE_FILTER,
            "价格过滤",
            "过滤异常价格数据"
        );
        priceRule.parameters["minPrice"] = 0.01;
        priceRule.parameters["maxPrice"] = 10000.0;
        priceRule.parameters["checkOpen"] = true;
        priceRule.parameters["checkHigh"] = true;
        priceRule.parameters["checkLow"] = true;
        priceRule.parameters["checkClose"] = true;
        priceRule.enabled = true;
        rules.append(priceRule);
        
        // 成交量过滤
        DataCleaningEngine::CleaningRule volumeRule(
            DataCleaningEngine::RULE_VOLUME_FILTER,
            "成交量过滤",
            "过滤异常成交量数据"
        );
        volumeRule.parameters["minVolume"] = 0;
        volumeRule.parameters["maxVolume"] = 1000000000;
        volumeRule.enabled = true;
        rules.append(volumeRule);
        
        qDebug() << "✅ Added data cleaning rules (format, price, volume)";
    }
    
    // 5. 重复数据删除 (始终启用)
    DataCleaningEngine::CleaningRule duplicateRule(
        DataCleaningEngine::RULE_DUPLICATE_REMOVAL,
        "重复数据删除",
        "删除重复的数据记录"
    );
    duplicateRule.parameters["keyFields"] = QStringList{"symbol", "date"};
    duplicateRule.enabled = true;
    rules.append(duplicateRule);
    
    qDebug() << "================= Rules conversion completed =================";
    qDebug() << "Total rules converted:" << rules.size();
    for (int i = 0; i < rules.size(); ++i) {
        const auto& rule = rules[i];
        qDebug() << "  Rule" << i << ":" << rule.name << "type:" << rule.type << "enabled:" << rule.enabled;
        qDebug() << "    Parameters:" << rule.parameters;
    }
    
    return rules;
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
        
        // 创建DataCleaningEngine实例
        DataCleaningEngine cleaningEngine;
        
        // 将QML规则转换为DataCleaningEngine规则
        QVector<DataCleaningEngine::CleaningRule> cleaningRules = m_impl->convertQmlRulesToCleaningRules(rules);
        
        // 执行清洗
        QVariantList cleanedData = cleaningEngine.cleanData(data, cleaningRules);
        
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

// 指数成分股查询方法实现
void DataService::loadIndexConstituents(const QString& indexSymbol) {
    try {
        if (indexSymbol.isEmpty()) {
            emit error("指数代码不能为空");
            return;
        }
        
        emit queryProgress(10, "开始加载指数成分股...");
        
        if (!m_impl->checkDatabaseConnection()) {
            emit error("数据库连接不可用，请检查database.json配置");
            return;
        }
        
        emit queryProgress(30, "数据库连接正常，执行查询...");
        
                // 查询指数成分股 - 直接使用数据库连接执行原生SQL
        if (!m_impl->database) {
            if (!m_impl->initializeDatabaseIfNeeded()) {
                throw std::runtime_error("数据库连接不可用");
            }
        }
        
        QString sql;
        std::map<QString, QVariant> params;
        
        if (indexSymbol == "BIG_CAP" || indexSymbol == "SMALL_CAP") {
            // 处理虚拟指数：大盘股/小盘股
            sql = "SELECT symbol, name, market_cap as weight, '2024-01-01' as start_date "
                  "FROM symbol_info "
                  "WHERE asset_class = 'STOCK' AND status = 'ACTIVE' ";
            
            if (indexSymbol == "BIG_CAP") {
                sql += "ORDER BY market_cap DESC LIMIT 100";
            } else {
                sql += "ORDER BY market_cap ASC LIMIT 100";
            }
            
            qDebug() << "查询虚拟指数:" << indexSymbol << "SQL:" << sql;
        } else {
            // 查询真实指数成分股
            sql = "SELECT ic.constituent_symbol as symbol, "
                  "COALESCE(si.name, ic.constituent_symbol) as name, "
                  "ic.weight, ic.start_date "
                  "FROM index_constituents ic "
                  "LEFT JOIN symbol_info si ON ic.constituent_symbol = si.symbol "
                  "WHERE ic.index_symbol = :index_symbol AND ic.status = :status "
                  "ORDER BY ic.weight DESC";
            
            params[":index_symbol"] = QVariant(indexSymbol);
            params[":status"] = QVariant("ACTIVE");  // status为ACTIVE表示当前有效
            qDebug() << "查询真实指数:" << indexSymbol << "SQL:" << sql;
        }
        auto result = m_impl->database->executeQuery(sql, params);
        
        // 直接转换查询结果，避免使用convertQueryResultToVariantList（该方法用于普通数据查询）
        QVariantList formattedData;
        for (const auto& row : result.getRows()) {
            QVariantMap formattedRecord;
            formattedRecord["symbol"] = row.getString("symbol");
            formattedRecord["name"] = row.getString("name");
            formattedRecord["weight"] = row.getDouble("weight");
            formattedRecord["start_date"] = row.getString("start_date");
            formattedData.append(formattedRecord);
        }
        
                emit queryProgress(90, "指数成分股加载完成，处理结果...");
        
        // 更新缓存数据
        m_fetchedData = formattedData;
        
                // 将指数成分股数据保存到缓存中
                try {
                    QString currentDate = QDate::currentDate().toString("yyyy-MM-dd");
                    QString cacheKey = DataServiceCache::generateStockCacheKey(
                        "index_constituents_" + indexSymbol,
                        currentDate,
                        currentDate
                    );
            
                    // 保存到缓存
                    DataServiceCache::getInstance().storeData(cacheKey, formattedData);
            
                    qDebug() << "指数成分股数据已保存到缓存，缓存键:" << cacheKey << "数据量:" << formattedData.size() << "条";
            
                } catch (const std::exception& e) {
                    qWarning() << "保存指数成分股数据到缓存失败:" << e.what();
                }
        
        emit queryProgress(100, "指数成分股加载完成");
        emit queryCompleted(true, QString("成功加载%1只成分股").arg(formattedData.size()), formattedData);
        emit fetchedDataChanged();
        
    } catch (const std::exception& e) {
        QString errorMsg = QString("加载指数成分股失败: %1").arg(e.what());
        qCritical() << "DataService::loadIndexConstituents:" << errorMsg;
        emit error(errorMsg);
    } catch (...) {
        QString errorMsg = "未知错误，加载指数成分股失败";
        qCritical() << "DataService::loadIndexConstituents:" << errorMsg;
        emit error(errorMsg);
    }
}

QVariantList DataService::getAvailableIndices() {
    try {
        if (!m_impl->checkDatabaseConnection()) {
            qWarning() << "DataService::getAvailableIndices: 数据库连接不可用";
            return QVariantList();
        }
        
        auto builder = m_impl->createQueryBuilder();
        if (!builder) {
            throw std::runtime_error("无法创建QueryBuilder");
        }
        
        // 查询symbol_info表中所有指数类型的标的
        auto query = builder->from("symbol_info")
                               .select("symbol, name")
                               .where("asset_class", ConditionType::TYPES_EQUAL, "INDEX")
                               .where("status", ConditionType::TYPES_EQUAL, "ACTIVE")
                               .orderBy("symbol", OrderType::Order_ASC);
        
        auto result = query.execute();
        
        QVariantList indices;
        for (const auto& row : result.getRows()) {
            QVariantMap index;
            index["symbol"] = row.getString("symbol");
            index["name"] = row.getString("name");
            indices.append(index);
        }
        
        // 如果没有查到指数数据，返回默认的指数列表
        if (indices.isEmpty()) {
            qWarning() << "DataService::getAvailableIndices: 数据库中未找到指数数据，返回默认列表";
            indices = QVariantList({
                QVariantMap{{"symbol", "000300.SH"}, {"name", "沪深300"}},
                QVariantMap{{"symbol", "000905.SH"}, {"name", "中证500"}},
                QVariantMap{{"symbol", "000016.SH"}, {"name", "上证50"}},
                QVariantMap{{"symbol", "399006.SZ"}, {"name", "创业板指"}},
                QVariantMap{{"symbol", "000852.SH"}, {"name", "中证1000"}},
                // 指数大盘股
                QVariantMap{{"symbol", "BIG_CAP"}, {"name", "指数大盘股"}},
                // 指数小盘股
                QVariantMap{{"symbol", "SMALL_CAP"}, {"name", "指数小盘股"}}
            });
        }
        
        qDebug() << "Available indices count:" << indices.size();
        return indices;
        
    } catch (const std::exception& e) {
        QString errorMsg = QString("获取可用指数列表失败: %1").arg(e.what());
        qCritical() << "DataService::getAvailableIndices:" << errorMsg;
        return QVariantList();
    } catch (...) {
        QString errorMsg = "未知错误，获取可用指数列表失败";
        qCritical() << "DataService::getAvailableIndices:" << errorMsg;
        return QVariantList();
    }
}
// 通用数据获取方法（单选）实现
void DataService::fetchDataByType(const QString& dataSource,
                                 const QString& symbol,
                                 const QString& dataType,
                                 const QString& startDate,
                                 const QString& endDate,
                                 const QVariantMap& options) {
    try {
        qDebug() << "DataService::fetchDataByType called with:";
        qDebug() << "  dataSource:" << dataSource;
        qDebug() << "  symbol:" << symbol;
        qDebug() << "  dataType:" << dataType;
        qDebug() << "  startDate:" << startDate;
        qDebug() << "  endDate:" << endDate;
        qDebug() << "  options:" << options;
        
        // 参数检查
        if (startDate.isEmpty() || endDate.isEmpty()) {
            emit error("开始日期和结束日期不能为空");
            return;
        }
        
        emit queryProgress(10, "开始获取数据...");
        
        if (!m_impl->checkDatabaseConnection()) {
            emit error("数据库连接不可用，请检查database.json配置");
            return;
        }
        
        emit queryProgress(30, "数据库连接正常，执行查询...");
        
        QVariantList data;
        
        // 根据dataSource和dataType决定如何查询数据
        if (dataSource == "index") {
            // 指数成分股数据
            if (dataType == "index_constituents") {
                // 查询指数成分股
                loadIndexConstituents(symbol);
                return;  // loadIndexConstituents会自己发出信号
            } else if (dataType.startsWith("kline_")) {
                // 查询指数成分股的K线数据
                // 首先获取指数成分股
                QVariantList constituents = getIndexConstituents(symbol);
                if (constituents.isEmpty()) {
                    emit error(QString("无法获取指数 %1 的成分股").arg(symbol));
                    return;
                }
                
                // 然后查询每个成分股的K线数据
                data = fetchConstituentKlineData(constituents, dataType, startDate, endDate);
            } else {
                emit error(QString("指数数据不支持的数据类型: %1").arg(dataType));
                return;
            }
        } else if (dataSource == "stock") {
            // 个股数据
            if (dataType.startsWith("kline_")) {
                // K线数据
                data = fetchKlineData(symbol, dataType, startDate, endDate);
            } else if (dataType == "financial") {
                // 财务数据
                data = fetchFinancialData(symbol, startDate, endDate);
            } else if (dataType == "news") {
                // 舆情数据
                data = fetchNewsData(symbol, startDate, endDate);
            } else {
                emit error(QString("不支持的数据类型: %1").arg(dataType));
                return;
            }
        } else if (dataSource == "all_market") {
            // 全市场数据
            if (dataType.startsWith("kline_")) {
                // 全市场K线数据
                data = fetchAllMarketKlineData(dataType, startDate, endDate);
            } else {
                emit error(QString("全市场数据不支持的数据类型: %1").arg(dataType));
                return;
            }
        } else {
            emit error(QString("不支持的数据源: %1").arg(dataSource));
            return;
        }
        
                emit queryProgress(90, "数据获取完成，处理结果...");
        
        // 更新缓存数据
        m_fetchedData = data;
        
                // 将数据保存到缓存中
                try {
                    QString cacheKey = DataServiceCache::generateStockCacheKey(
                        dataSource + "_" + symbol + "_" + dataType,
                        startDate,
                        endDate
                    );
            
                    // 保存到缓存
                    DataServiceCache::getInstance().storeData(cacheKey, data);
            
                    qDebug() << "数据已保存到缓存，缓存键:" << cacheKey << "数据量:" << data.size() << "条";
            
                } catch (const std::exception& e) {
                    qWarning() << "保存数据到缓存失败:" << e.what();
                }
        
        emit queryProgress(100, "数据获取完成");
        emit queryCompleted(true, QString("成功获取%1条数据").arg(data.size()), data);
        emit fetchedDataChanged();
        
    } catch (const std::exception& e) {
        QString errorMsg = QString("获取数据失败: %1").arg(e.what());
        qCritical() << "DataService::fetchDataByType:" << errorMsg;
        emit error(errorMsg);
    } catch (...) {
        QString errorMsg = "未知错误，获取数据失败";
        qCritical() << "DataService::fetchDataByType:" << errorMsg;
        emit error(errorMsg);
    }
}

// 辅助方法：获取指数成分股
QVariantList DataService::getIndexConstituents(const QString& indexSymbol) {
    // 这里可以复用loadIndexConstituents的逻辑，但直接返回数据
    // 简化实现：直接查询数据库
    if (!m_impl->checkDatabaseConnection()) {
        return QVariantList();
    }
    
    try {
        QString sql;
        std::map<QString, QVariant> params;
        
        if (indexSymbol == "BIG_CAP" || indexSymbol == "SMALL_CAP") {
            // 虚拟指数
            sql = "SELECT symbol, name FROM symbol_info WHERE asset_class = 'STOCK' AND status = 'ACTIVE' ";
            if (indexSymbol == "BIG_CAP") {
                sql += "ORDER BY market_cap DESC LIMIT 100";
            } else {
                sql += "ORDER BY market_cap ASC LIMIT 100";
            }
        } else {
            // 真实指数
            sql = "SELECT constituent_symbol as symbol, COALESCE(si.name, constituent_symbol) as name "
                  "FROM index_constituents ic "
                  "LEFT JOIN symbol_info si ON ic.constituent_symbol = si.symbol "
                  "WHERE ic.index_symbol = :index_symbol AND ic.status = :status";
            params[":index_symbol"] = QVariant(indexSymbol);
            params[":status"] = QVariant("ACTIVE");
        }
        
        auto result = m_impl->database->executeQuery(sql, params);
        
        QVariantList constituents;
        for (const auto& row : result.getRows()) {
            QVariantMap constituent;
            constituent["symbol"] = row.getString("symbol");
            constituent["name"] = row.getString("name");
            constituents.append(constituent);
        }
        
        return constituents;
        
    } catch (const std::exception& e) {
        qCritical() << "DataService::getIndexConstituents:" << e.what();
        return QVariantList();
    }
}

// 辅助方法：获取成分股K线数据
QVariantList DataService::fetchConstituentKlineData(const QVariantList& constituents,
                                                   const QString& dataType,
                                                   const QString& startDate,
                                                   const QString& endDate) {
    QVariantList allData;
    
    // 根据dataType确定表名和字段
    QString tableName;
    QStringList fields;
    
    if (dataType == "kline_daily") {
        tableName = "daily_bar";
        fields = fullDailyBarFields();
    } else if (dataType == "kline_weekly") {
        tableName = "weekly_bar";
        fields = QStringList{"symbol", "trade_date", "open", "high", "low", "close", "volume"};
    } else if (dataType == "kline_monthly") {
        tableName = "monthly_bar";
        fields = QStringList{"symbol", "trade_date", "open", "high", "low", "close", "volume"};
    } else {
        qWarning() << "Unsupported data type for K-line:" << dataType;
        return allData;
    }
    
    // 分批查询，避免SQL语句过长
    const int batchSize = 50;
    for (int i = 0; i < constituents.size(); i += batchSize) {
        int end = qMin(i + batchSize, constituents.size());
        
        // 构建symbol列表
        QStringList symbols;
        for (int j = i; j < end; j++) {
            symbols.append(constituents[j].toMap()["symbol"].toString());
        }
        
        // 查询这批symbol的数据
        QVariantList batchData = fetchBatchKlineData(tableName, fields, symbols, startDate, endDate);
        allData.append(batchData);
        
        // 更新进度
        int progress = 10 + (i * 80 / constituents.size());
        emit queryProgress(progress, QString("正在获取成分股数据 (%1/%2)...").arg(i + batchSize).arg(constituents.size()));
    }
    
    return allData;
}

// 辅助方法：批量获取K线数据
QVariantList DataService::fetchBatchKlineData(const QString& tableName,
                                             const QStringList& fields,
                                             const QStringList& symbols,
                                             const QString& startDate,
                                             const QString& endDate) {
    if (symbols.isEmpty()) {
        return QVariantList();
    }
    
    try {
        // 构建IN条件
        QString symbolCondition = "symbol IN (";
        for (int i = 0; i < symbols.size(); i++) {
            if (i > 0) symbolCondition += ", ";
            symbolCondition += QString("'%1'").arg(symbols[i]);
        }
        symbolCondition += ")";
        
        // 构建SQL。这里不能再按每个 symbol 固定截断，否则像沪深300近一年这类数据会被硬切成 30000 条。
        QString sql = QString("SELECT %1 FROM %2 WHERE %3 AND trade_date BETWEEN '%4' AND '%5' ORDER BY symbol, trade_date")
            .arg(fields.join(", "))
            .arg(tableName)
            .arg(symbolCondition)
            .arg(startDate)
            .arg(endDate);
        
        auto result = m_impl->database->executeQuery(sql, std::map<QString, QVariant>());
        
        QVariantList data;
        for (const auto& row : result.getRows()) {
            QVariantMap record;
            for (const QString& field : fields) {
                QVariant value = readFieldValue(row, field);
                record[field] = value;

                // 清洗引擎要求标准K线字段使用 date，这里兼容 trade_date 查询结果。
                if (field == "trade_date") {
                    record["date"] = value;
                }
            }
            data.append(record);
        }
        
        return data;
        
    } catch (const std::exception& e) {
        qCritical() << "DataService::fetchBatchKlineData:" << e.what();
        return QVariantList();
    }
}

// 辅助方法：获取个股K线数据
QVariantList DataService::fetchKlineData(const QString& symbol,
                                        const QString& dataType,
                                        const QString& startDate,
                                        const QString& endDate) {
    // 确定表名
    QString tableName;
    if (dataType == "kline_daily") {
        tableName = "daily_bar";
    } else if (dataType == "kline_weekly") {
        tableName = "weekly_bar";
    } else if (dataType == "kline_monthly") {
        tableName = "monthly_bar";
    } else {
        return QVariantList();
    }
    
    try {
        const QStringList fields = (dataType == "kline_daily")
            ? fullDailyBarFields()
            : QStringList{"symbol", "trade_date", "open", "high", "low", "close", "volume"};

        QString sql = QString("SELECT %1 FROM %2 WHERE symbol = :symbol AND trade_date BETWEEN :start_date AND :end_date ORDER BY trade_date LIMIT 500")
            .arg(fields.join(", "))
            .arg(tableName);
        
        std::map<QString, QVariant> params;
        params[":symbol"] = QVariant(symbol);
        params[":start_date"] = QVariant(startDate);
        params[":end_date"] = QVariant(endDate);
        
        auto result = m_impl->database->executeQuery(sql, params);
        
        QVariantList data;
        for (const auto& row : result.getRows()) {
            QVariantMap record;
            for (const QString& field : fields) {
                record[field] = readFieldValue(row, field);
                if (field == "trade_date") {
                    record["date"] = row.getString("trade_date");
                }
            }
            data.append(record);
        }
        
        return data;
        
    } catch (const std::exception& e) {
        qCritical() << "DataService::fetchKlineData:" << e.what();
        return QVariantList();
    }
}

// 辅助方法：获取全市场K线数据
QVariantList DataService::fetchAllMarketKlineData(const QString& dataType,
                                                 const QString& startDate,
                                                 const QString& endDate) {
    // 确定表名
    QString tableName;
    if (dataType == "kline_daily") {
        tableName = "daily_bar";
    } else if (dataType == "kline_weekly") {
        tableName = "weekly_bar";
    } else if (dataType == "kline_monthly") {
        tableName = "monthly_bar";
    } else {
        return QVariantList();
    }
    
    try {
        // 限制返回数量，避免数据量过大
        const QStringList fields = (dataType == "kline_daily")
            ? fullDailyBarFields()
            : QStringList{"symbol", "trade_date", "open", "high", "low", "close", "volume"};

        QString sql = QString("SELECT %1 FROM %2 WHERE trade_date BETWEEN :start_date AND :end_date ORDER BY symbol, trade_date LIMIT 10000")
            .arg(fields.join(", "))
            .arg(tableName);
        
        std::map<QString, QVariant> params;
        params[":start_date"] = QVariant(startDate);
        params[":end_date"] = QVariant(endDate);
        
        auto result = m_impl->database->executeQuery(sql, params);
        
        QVariantList data;
        for (const auto& row : result.getRows()) {
            QVariantMap record;
            for (const QString& field : fields) {
                record[field] = readFieldValue(row, field);
                if (field == "trade_date") {
                    record["date"] = row.getString("trade_date");
                }
            }
            data.append(record);
        }
        
        return data;
        
    } catch (const std::exception& e) {
        qCritical() << "DataService::fetchAllMarketKlineData:" << e.what();
        return QVariantList();
    }
}

// 辅助方法：获取财务数据（简化实现）
QVariantList DataService::fetchFinancialData(const QString& symbol,
                                            const QString& startDate,
                                            const QString& endDate) {
    // 这里需要根据实际的财务数据表结构来实现
    // 简化实现：返回空数据
    qDebug() << "Financial data fetching not implemented yet for symbol:" << symbol;
    return QVariantList();
}

// 辅助方法：获取舆情数据（简化实现）
QVariantList DataService::fetchNewsData(const QString& symbol,
                                       const QString& startDate,
                                       const QString& endDate) {
    // 这里需要根据实际的舆情数据表结构来实现
    // 简化实现：返回空数据
    qDebug() << "News data fetching not implemented yet for symbol:" << symbol;
    return QVariantList();
}