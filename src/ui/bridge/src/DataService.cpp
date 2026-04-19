// DataService.cpp - 极简实现 (目标: <200行)
#include "DataService.h"
#include "DataServiceCache.h"
#include "DataCleaningEngine.h"  // 添加DataCleaningEngine头文件
#include "DataCleaningRuleRegistry.h"
#include "database/QueryBuilder.h"
#include "database/QtMySQLDatabase.h"
#include "database/DatabaseConfig.h"
#include "foundation.h"
#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QDebug>
#include <QDate>
#include <QDateTime>
#include <QFileInfo>
#include <QSet>
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

QVariant normalizeQueryValue(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return QVariant();
    }

    if (value.canConvert<QDateTime>()) {
        const QDateTime dateTime = value.toDateTime();
        if (dateTime.isValid()) {
            return dateTime.toString("yyyy-MM-dd HH:mm:ss");
        }
    }

    if (value.canConvert<QDate>()) {
        const QDate date = value.toDate();
        if (date.isValid()) {
            return date.toString("yyyy-MM-dd");
        }
    }

    return value;
}

QVariantMap convertRowToVariantMap(const QueryResultRow& row)
{
    QVariantMap record;
    for (const auto& entry : row.getValues()) {
        record[entry.first] = normalizeQueryValue(entry.second);
    }

    if (!record.contains("date")) {
        if (record.contains("trade_date")) {
            record["date"] = record.value("trade_date");
        } else if (record.contains("report_date")) {
            record["date"] = record.value("report_date");
        } else if (record.contains("bar_time")) {
            record["date"] = record.value("bar_time");
        } else if (record.contains("publish_time")) {
            record["date"] = record.value("publish_time");
        } else if (record.contains("created_at")) {
            record["date"] = record.value("created_at");
        }
    }

    return record;
}

QVariantList convertResultToVariantList(const QueryResult& result)
{
    QVariantList data;
    for (const auto& row : result.getRows()) {
        data.append(convertRowToVariantMap(row));
    }
    return data;
}

QString escapeSqlLiteral(QString value)
{
    value.replace("'", "''");
    return QString("'%1'").arg(value);
}

QString buildSymbolInClause(const QStringList& symbols)
{
    QStringList escapedSymbols;
    escapedSymbols.reserve(symbols.size());
    for (const QString& symbol : symbols) {
        escapedSymbols.append(escapeSqlLiteral(symbol));
    }
    return escapedSymbols.join(", ");
}

QStringList extractSymbols(const QVariantList& constituents)
{
    QStringList symbols;
    for (const QVariant& item : constituents) {
        const QString symbol = item.toMap().value("symbol").toString().trimmed();
        if (!symbol.isEmpty()) {
            symbols.append(symbol);
        }
    }
    return symbols;
}

QString resolveSnapshotDateString(const QString& requestedEndDate, const QVariantMap& options)
{
    const QString configuredDate = options.value("snapshotDate").toString().trimmed();
    const QString candidate = configuredDate.isEmpty() ? requestedEndDate.trimmed() : configuredDate;
    const QDate parsedDate = QDate::fromString(candidate, "yyyy-MM-dd");
    if (parsedDate.isValid()) {
        return parsedDate.toString("yyyy-MM-dd");
    }
    return QDate::currentDate().toString("yyyy-MM-dd");
}

QString resolveIndexSnapshotDate(std::shared_ptr<QtMySQLDatabase> database,
                                 const QString& indexSymbol,
                                 const QString& requestedSnapshotDate)
{
    if (!database) {
        return requestedSnapshotDate;
    }

    try {
        std::map<QString, QVariant> params;
        params[":index_symbol"] = indexSymbol;
        params[":snapshot_date"] = requestedSnapshotDate;

        const QString sql =
            "SELECT COALESCE(MAX(CASE WHEN start_date <= :snapshot_date THEN start_date END), MIN(start_date)) AS resolved_date "
            "FROM index_constituents WHERE index_symbol = :index_symbol";

        const auto result = database->executeQuery(sql, params);
        if (result.getRows().empty()) {
            return requestedSnapshotDate;
        }

        const QString resolvedDate = result.getRows().front().getString("resolved_date");
        return resolvedDate.trimmed().isEmpty() ? requestedSnapshotDate : resolvedDate.trimmed();
    } catch (const std::exception& e) {
        qWarning() << "resolveIndexSnapshotDate failed:" << e.what();
        return requestedSnapshotDate;
    }
}

QString resolveRepoRootFromAppDir()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 8; ++depth) {
        if (dir.exists(QStringLiteral("astock_engine")) && dir.exists(QStringLiteral("tools"))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return {};
}

QString resolvePythonExecutable(const QString& repoRoot)
{
    const QString configured = qEnvironmentVariable("ASTOCK_PYTHON_EXECUTABLE").trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }

    if (!repoRoot.isEmpty()) {
        const QFileInfo windowsVenv(QDir(repoRoot).filePath(QStringLiteral(".venv/Scripts/python.exe")));
        if (windowsVenv.exists()) {
            return windowsVenv.canonicalFilePath();
        }

        const QFileInfo unixVenv(QDir(repoRoot).filePath(QStringLiteral(".venv/bin/python")));
        if (unixVenv.exists()) {
            return unixVenv.canonicalFilePath();
        }
    }

    return QStringLiteral("python");
}

bool isMarketFallbackSymbol(const QString& symbol)
{
    const QString normalized = symbol.trimmed().toUpper();
    return normalized.isEmpty()
        || normalized == QStringLiteral("MARKET")
        || normalized == QStringLiteral("ALL_MARKET")
        || normalized == QStringLiteral("GLOBAL");
}

template <typename Func>
void invokeOnMainThread(DataService* service, Func&& func)
{
    QPointer<DataService> safeService(service);
    QMetaObject::invokeMethod(service, [safeService, fn = std::forward<Func>(func)]() mutable {
        if (safeService) {
            fn(safeService.data());
        }
    }, Qt::QueuedConnection);
}

template <typename Func>
bool submitToFoundationThreadPool(DataService* service, Func&& func, QString* errorMessage = nullptr)
{
    QPointer<DataService> safeService(service);
    try {
        foundation::Foundation::instance().thread_pool().post(
            [safeService, fn = std::forward<Func>(func)]() mutable {
                if (safeService) {
                    fn(safeService.data());
                }
            }
        );
        return true;
    } catch (const std::exception& e) {
        if (errorMessage) {
            *errorMessage = QString::fromUtf8(e.what());
        }
    } catch (...) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未知线程池错误");
        }
    }
    return false;
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

    QMutex operationMutex;

    DataCleaningRuleRegistry cleaningRuleRegistry;
};

DataService::DataService(QObject* parent) 
    : QObject(parent), m_impl(std::make_unique<Impl>()) {
}

DataService::~DataService() {
    // 确保所有数据库连接被正确关闭
    // m_impl将自动销毁，在其析构函数中会清理资源
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
    return convertResultToVariantList(result);
}

QVector<DataCleaningEngine::CleaningRule> DataService::Impl::convertQmlRulesToCleaningRules(const QVariantMap& qmlRules) {
    const QVector<DataCleaningEngine::CleaningRule> rules = cleaningRuleRegistry.buildRules(qmlRules);

    qDebug() << "================= Converting QML rules to cleaning rules =================";
    qDebug() << "QML rules input:" << qmlRules;
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
    QString submitError;
    if (!submitToFoundationThreadPool(this, [symbol, startDate, endDate](DataService* service) {
        try {
            if (startDate.isEmpty() || endDate.isEmpty()) {
                invokeOnMainThread(service, [](DataService* service) {
                    service->error("开始日期和结束日期不能为空");
                });
                return;
            }

            invokeOnMainThread(service, [](DataService* service) {
                service->queryProgress(10, "开始从数据库加载数据...");
            });

            QVariantList data;
            {
                QMutexLocker locker(&service->m_impl->operationMutex);
                if (!service->m_impl->checkDatabaseConnection()) {
                    invokeOnMainThread(service, [](DataService* service) {
                        service->error("数据库连接不可用，请检查database.json配置");
                    });
                    return;
                }

                invokeOnMainThread(service, [](DataService* service) {
                    service->queryProgress(30, "数据库连接正常，执行查询...");
                });

                data = service->m_impl->queryDataInternal(symbol, startDate, endDate);
            }

            invokeOnMainThread(service, [data](DataService* service) {
                service->queryProgress(90, "数据加载完成，处理结果...");
                service->m_fetchedData = data;
                service->queryProgress(100, "数据加载完成");
                service->queryCompleted(true, QString("从数据库加载成功，获取%1条数据").arg(data.size()), data);
                service->fetchedDataChanged();
            });
        } catch (const std::exception& e) {
            const QString errorMsg = QString("从数据库加载数据失败: %1").arg(e.what());
            qCritical() << "DataService::loadFromDatabase:" << errorMsg;
            invokeOnMainThread(service, [errorMsg](DataService* service) {
                service->error(errorMsg);
            });
        } catch (...) {
            const QString errorMsg = "未知错误，从数据库加载数据失败";
            qCritical() << "DataService::loadFromDatabase:" << errorMsg;
            invokeOnMainThread(service, [errorMsg](DataService* service) {
                service->error(errorMsg);
            });
        }
    }, &submitError)) {
        emit error(QString("线程池不可用，无法开始数据库加载: %1").arg(submitError));
    }
}

void DataService::cleanDataAsync(const QVariantList& data, 
                                const QVariantMap& rules) {
    QString submitError;
    if (!submitToFoundationThreadPool(this, [data, rules](DataService* service) {
        try {
            if (data.isEmpty()) {
                invokeOnMainThread(service, [](DataService* service) {
                    service->error("没有数据可清洗");
                });
                return;
            }

            const QString startMessage = QString("开始清洗，共%1条记录").arg(data.size());
            invokeOnMainThread(service, [startMessage](DataService* service) {
                service->cleaningProgress(0, startMessage);
                service->cleaningProgressDetail(0, startMessage, QString(), 0, 0);
            });

            DataCleaningEngine cleaningEngine;
            QObject::connect(&cleaningEngine, &DataCleaningEngine::cleaningProgress,
                             service, [service](int progress, const QString& message) {
                                 invokeOnMainThread(service, [progress, message](DataService* service) {
                                     service->cleaningProgress(progress, message);
                                 });
                             });
            QObject::connect(&cleaningEngine, &DataCleaningEngine::cleaningProgressDetail,
                             service, [service](int progress,
                                                const QString& message,
                                                const QString& currentStock,
                                                int keptRecords,
                                                int removedRecords) {
                                 invokeOnMainThread(service, [progress, message, currentStock, keptRecords, removedRecords](DataService* service) {
                                     service->cleaningProgressDetail(progress,
                                                                     message,
                                                                     currentStock,
                                                                     keptRecords,
                                                                     removedRecords);
                                 });
                             });
            QObject::connect(&cleaningEngine, &DataCleaningEngine::cleaningError,
                             service, [service](const QString& errorMessage) {
                                 invokeOnMainThread(service, [errorMessage](DataService* service) {
                                     service->error(errorMessage);
                                 });
                             });

            QVector<DataCleaningEngine::CleaningRule> cleaningRules;
            {
                QMutexLocker locker(&service->m_impl->operationMutex);
                cleaningRules = service->m_impl->convertQmlRulesToCleaningRules(rules);
            }

            const QVariantList cleanedData = cleaningEngine.cleanData(data, cleaningRules);
            const QString message = QString("异步数据清洗完成: 原始 %1 条 -> 清洗后 %2 条")
                .arg(data.size())
                .arg(cleanedData.size());

            invokeOnMainThread(service, [message, cleanedData](DataService* service) {
                service->cleaningCompleted(true, message, cleanedData);
            });
        } catch (const std::exception& e) {
            const QString errorMsg = QString("异步清洗失败: %1").arg(e.what());
            qCritical() << "DataService::cleanDataAsync:" << errorMsg;
            invokeOnMainThread(service, [errorMsg](DataService* service) {
                service->error(errorMsg);
            });
        } catch (...) {
            const QString errorMsg = "未知错误，异步清洗失败";
            qCritical() << "DataService::cleanDataAsync:" << errorMsg;
            invokeOnMainThread(service, [errorMsg](DataService* service) {
                service->error(errorMsg);
            });
        }
    }, &submitError)) {
        emit error(QString("线程池不可用，无法开始数据清洗: %1").arg(submitError));
    }
}

QVariantList DataService::fetchedData() const {
    return m_fetchedData;
}

// 指数成分股查询方法实现
void DataService::loadIndexConstituents(const QString& indexSymbol, const QString& snapshotDate) {
    QString submitError;
    if (!submitToFoundationThreadPool(this, [indexSymbol, snapshotDate](DataService* service) {
        try {
            if (indexSymbol.isEmpty()) {
                invokeOnMainThread(service, [](DataService* service) {
                    service->error("指数代码不能为空");
                });
                return;
            }

            QString effectiveSnapshotDate = QDate::fromString(snapshotDate, "yyyy-MM-dd").isValid()
                ? snapshotDate
                : QDate::currentDate().toString("yyyy-MM-dd");

            invokeOnMainThread(service, [](DataService* service) {
                service->queryProgress(10, "开始加载指数成分股...");
            });

            QVariantList formattedData;
            {
                QMutexLocker locker(&service->m_impl->operationMutex);
                if (!service->m_impl->checkDatabaseConnection()) {
                    invokeOnMainThread(service, [](DataService* service) {
                        service->error("数据库连接不可用，请检查database.json配置");
                    });
                    return;
                }

                invokeOnMainThread(service, [](DataService* service) {
                    service->queryProgress(30, "数据库连接正常，执行查询...");
                });

                if (!service->m_impl->database && !service->m_impl->initializeDatabaseIfNeeded()) {
                    throw std::runtime_error("数据库连接不可用");
                }

                QString sql;
                std::map<QString, QVariant> params;

                if (indexSymbol == "BIG_CAP" || indexSymbol == "SMALL_CAP") {
                    sql = "SELECT d.symbol, COALESCE(si.name, d.symbol) AS name, d.market_cap AS weight, d.trade_date AS start_date "
                          "FROM daily_bar d "
                          "LEFT JOIN symbol_info si ON d.symbol = si.symbol "
                          "WHERE d.trade_date = (SELECT MAX(trade_date) FROM daily_bar WHERE trade_date <= :snapshot_date) "
                          "  AND si.asset_class = 'STOCK' AND si.status = 'ACTIVE' AND d.market_cap IS NOT NULL ";
                    params[":snapshot_date"] = QVariant(effectiveSnapshotDate);
                    sql += indexSymbol == "BIG_CAP" ? "ORDER BY market_cap DESC LIMIT 100" : "ORDER BY market_cap ASC LIMIT 100";
                } else {
                    effectiveSnapshotDate = resolveIndexSnapshotDate(service->m_impl->database, indexSymbol, effectiveSnapshotDate);
                    sql = "SELECT ic.constituent_symbol as symbol, "
                          "COALESCE(si.name, ic.constituent_symbol) as name, "
                          "ic.weight, ic.start_date "
                          "FROM index_constituents ic "
                          "LEFT JOIN symbol_info si ON ic.constituent_symbol = si.symbol "
                          "WHERE ic.index_symbol = :index_symbol "
                          "  AND ic.start_date <= :snapshot_date "
                          "  AND (ic.end_date IS NULL OR ic.end_date >= :snapshot_date) "
                          "ORDER BY ic.weight DESC";
                    params[":index_symbol"] = QVariant(indexSymbol);
                    params[":snapshot_date"] = QVariant(effectiveSnapshotDate);
                }

                const auto result = service->m_impl->database->executeQuery(sql, params);
                for (const auto& row : result.getRows()) {
                    QVariantMap formattedRecord;
                    formattedRecord["symbol"] = row.getString("symbol");
                    formattedRecord["name"] = row.getString("name");
                    formattedRecord["weight"] = row.getDouble("weight");
                    formattedRecord["start_date"] = row.getString("start_date");
                    formattedData.append(formattedRecord);
                }
            }

            invokeOnMainThread(service, [formattedData, effectiveSnapshotDate, indexSymbol](DataService* service) {
                service->queryProgress(90, "指数成分股加载完成，处理结果...");
                service->m_fetchedData = formattedData;

                try {
                    const QString cacheKey = DataServiceCache::generateStockCacheKey(
                        "index_constituents_" + indexSymbol,
                        effectiveSnapshotDate,
                        effectiveSnapshotDate
                    );
                    DataServiceCache::getInstance().storeData(cacheKey, formattedData);
                } catch (const std::exception& e) {
                    qWarning() << "保存指数成分股数据到缓存失败:" << e.what();
                }

                service->queryProgress(100, "指数成分股加载完成");
                service->queryCompleted(true, QString("成功加载%1只成分股(%2)").arg(formattedData.size()).arg(effectiveSnapshotDate), formattedData);
                service->fetchedDataChanged();
            });
        } catch (const std::exception& e) {
            const QString errorMsg = QString("加载指数成分股失败: %1").arg(e.what());
            qCritical() << "DataService::loadIndexConstituents:" << errorMsg;
            invokeOnMainThread(service, [errorMsg](DataService* service) {
                service->error(errorMsg);
            });
        } catch (...) {
            const QString errorMsg = "未知错误，加载指数成分股失败";
            qCritical() << "DataService::loadIndexConstituents:" << errorMsg;
            invokeOnMainThread(service, [errorMsg](DataService* service) {
                service->error(errorMsg);
            });
        }
    }, &submitError)) {
        emit error(QString("线程池不可用，无法开始指数成分股加载: %1").arg(submitError));
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
    QString submitError;
    if (!submitToFoundationThreadPool(this, [dataSource, symbol, dataType, startDate, endDate, options](DataService* service) {
        try {
            if (startDate.isEmpty() || endDate.isEmpty()) {
                invokeOnMainThread(service, [](DataService* service) {
                    service->error("开始日期和结束日期不能为空");
                });
                return;
            }

            invokeOnMainThread(service, [dataType](DataService* service) {
                service->queryProgress(5, QString("开始获取%1数据...").arg(dataType));
            });

            QVariantList data;
            {
                QMutexLocker locker(&service->m_impl->operationMutex);
                if (!service->m_impl->checkDatabaseConnection()) {
                    invokeOnMainThread(service, [](DataService* service) {
                        service->error("数据库连接不可用，请检查database.json配置");
                    });
                    return;
                }

                invokeOnMainThread(service, [](DataService* service) {
                    service->queryProgress(15, "数据库连接正常，准备执行查询...");
                });

                if (dataSource == "index") {
                    const QString snapshotDate = resolveSnapshotDateString(endDate, options);

                    if (dataType == "index_constituents" || dataType == "index") {
                        invokeOnMainThread(service, [symbol, snapshotDate](DataService* service) {
                            service->loadIndexConstituents(symbol, snapshotDate);
                        });
                        return;
                    } else if (dataType.startsWith("kline_")) {
                        invokeOnMainThread(service, [symbol, snapshotDate](DataService* service) {
                            service->queryProgress(30, QString("正在获取指数 %1 在 %2 的成分股...").arg(symbol, snapshotDate));
                        });
                        const QVariantList constituents = service->getIndexConstituents(symbol, snapshotDate);
                        if (constituents.isEmpty()) {
                            invokeOnMainThread(service, [symbol, snapshotDate](DataService* service) {
                                service->error(QString("无法获取指数 %1 在 %2 的成分股").arg(symbol, snapshotDate));
                            });
                            return;
                        }
                        invokeOnMainThread(service, [count = constituents.size()](DataService* service) {
                            service->queryProgress(55, QString("已获取 %1 只成分股，开始加载行情数据...").arg(count));
                        });
                        data = service->fetchConstituentKlineData(constituents, dataType, startDate, endDate);
                    } else if (dataType == "financial") {
                        invokeOnMainThread(service, [symbol, snapshotDate](DataService* service) {
                            service->queryProgress(30, QString("正在获取指数 %1 在 %2 的成分股...").arg(symbol, snapshotDate));
                        });
                        const QVariantList constituents = service->getIndexConstituents(symbol, snapshotDate);
                        if (constituents.isEmpty()) {
                            invokeOnMainThread(service, [symbol, snapshotDate](DataService* service) {
                                service->error(QString("无法获取指数 %1 在 %2 的成分股").arg(symbol, snapshotDate));
                            });
                            return;
                        }
                        invokeOnMainThread(service, [count = constituents.size()](DataService* service) {
                            service->queryProgress(55, QString("已获取 %1 只成分股，开始加载财务数据...").arg(count));
                        });
                        data = service->fetchFinancialDataForSymbols(extractSymbols(constituents), startDate, endDate);
                    } else if (dataType == "news") {
                        invokeOnMainThread(service, [symbol, snapshotDate](DataService* service) {
                            service->queryProgress(30, QString("正在获取指数 %1 在 %2 的成分股...").arg(symbol, snapshotDate));
                        });
                        const QVariantList constituents = service->getIndexConstituents(symbol, snapshotDate);
                        if (constituents.isEmpty()) {
                            invokeOnMainThread(service, [symbol, snapshotDate](DataService* service) {
                                service->error(QString("无法获取指数 %1 在 %2 的成分股").arg(symbol, snapshotDate));
                            });
                            return;
                        }
                        invokeOnMainThread(service, [count = constituents.size()](DataService* service) {
                            service->queryProgress(55, QString("已获取 %1 只成分股，开始加载舆情数据...").arg(count));
                        });
                        data = service->fetchNewsDataForSymbols(extractSymbols(constituents), startDate, endDate);
                    } else if (dataType == "historical") {
                        invokeOnMainThread(service, [symbol, snapshotDate](DataService* service) {
                            service->queryProgress(30, QString("正在获取指数 %1 在 %2 的成分股...").arg(symbol, snapshotDate));
                        });
                        const QVariantList constituents = service->getIndexConstituents(symbol, snapshotDate);
                        if (constituents.isEmpty()) {
                            invokeOnMainThread(service, [symbol, snapshotDate](DataService* service) {
                                service->error(QString("无法获取指数 %1 在 %2 的成分股").arg(symbol, snapshotDate));
                            });
                            return;
                        }
                        data = service->fetchPriceTableDataForSymbols("daily_bar", extractSymbols(constituents), startDate, endDate);
                    } else if (dataType == "realtime") {
                        const QVariantList constituents = service->getIndexConstituents(symbol, snapshotDate);
                        if (constituents.isEmpty()) {
                            invokeOnMainThread(service, [symbol, snapshotDate](DataService* service) {
                                service->error(QString("无法获取指数 %1 在 %2 的成分股").arg(symbol, snapshotDate));
                            });
                            return;
                        }
                        data = service->fetchRealtimeDataForSymbols(extractSymbols(constituents), endDate);
                    } else if (dataType == "policy") {
                        const QVariantList constituents = service->getIndexConstituents(symbol, snapshotDate);
                        if (constituents.isEmpty()) {
                            invokeOnMainThread(service, [symbol, snapshotDate](DataService* service) {
                                service->error(QString("无法获取指数 %1 在 %2 的成分股").arg(symbol, snapshotDate));
                            });
                            return;
                        }
                        data = service->fetchPolicyDataForSymbols(extractSymbols(constituents), startDate, endDate, options);
                    } else if (dataType == "alternative") {
                        const QVariantList constituents = service->getIndexConstituents(symbol, snapshotDate);
                        if (constituents.isEmpty()) {
                            invokeOnMainThread(service, [symbol, snapshotDate](DataService* service) {
                                service->error(QString("无法获取指数 %1 在 %2 的成分股").arg(symbol, snapshotDate));
                            });
                            return;
                        }
                        data = service->fetchAlternativeDataForSymbols(extractSymbols(constituents), startDate, endDate, options);
                    } else if (dataType == "derivatives") {
                        data = service->fetchDerivativesData(symbol, startDate, endDate, options);
                    } else {
                        invokeOnMainThread(service, [dataType](DataService* service) {
                            service->error(QString("指数数据不支持的数据类型: %1").arg(dataType));
                        });
                        return;
                    }
                } else if (dataSource == "stock") {
                    invokeOnMainThread(service, [symbol, dataType](DataService* service) {
                        service->queryProgress(35, QString("正在获取 %1 的 %2 数据...").arg(symbol, dataType));
                    });
                    if (dataType.startsWith("kline_")) {
                        data = service->fetchKlineData(symbol, dataType, startDate, endDate);
                    } else if (dataType == "historical") {
                        data = service->fetchHistoricalData(symbol, startDate, endDate);
                    } else if (dataType == "realtime") {
                        data = service->fetchRealtimeData(symbol, startDate, endDate);
                    } else if (dataType == "financial") {
                        data = service->fetchFinancialData(symbol, startDate, endDate);
                    } else if (dataType == "news") {
                        data = service->fetchNewsData(symbol, startDate, endDate);
                    } else if (dataType == "policy") {
                        data = service->fetchPolicyData(symbol, startDate, endDate, options);
                    } else if (dataType == "alternative") {
                        data = service->fetchAlternativeData(symbol, startDate, endDate, options);
                    } else if (dataType == "derivatives") {
                        data = service->fetchDerivativesData(symbol, startDate, endDate, options);
                    } else if (dataType == "index") {
                        data = service->getAvailableIndices();
                    } else {
                        invokeOnMainThread(service, [dataType](DataService* service) {
                            service->error(QString("不支持的数据类型: %1").arg(dataType));
                        });
                        return;
                    }
                } else if (dataSource == "all_market") {
                    invokeOnMainThread(service, [dataType](DataService* service) {
                        service->queryProgress(35, QString("正在获取全市场 %1 数据...").arg(dataType));
                    });
                    if (dataType.startsWith("kline_")) {
                        data = service->fetchAllMarketKlineData(dataType, startDate, endDate);
                    } else if (dataType == "historical") {
                        data = service->fetchHistoricalData(QString(), startDate, endDate);
                    } else if (dataType == "realtime") {
                        data = service->fetchRealtimeData(QString(), startDate, endDate);
                    } else if (dataType == "financial") {
                        data = service->fetchFinancialData(QString(), startDate, endDate);
                    } else if (dataType == "news") {
                        data = service->fetchNewsData(QString(), startDate, endDate);
                    } else if (dataType == "policy") {
                        data = service->fetchPolicyData(QString(), startDate, endDate, options);
                    } else if (dataType == "alternative") {
                        data = service->fetchAlternativeData(QString(), startDate, endDate, options);
                    } else if (dataType == "derivatives") {
                        data = service->fetchDerivativesData(QString(), startDate, endDate, options);
                    } else if (dataType == "index") {
                        data = service->getAvailableIndices();
                    } else {
                        invokeOnMainThread(service, [dataType](DataService* service) {
                            service->error(QString("全市场数据不支持的数据类型: %1").arg(dataType));
                        });
                        return;
                    }
                } else {
                    invokeOnMainThread(service, [dataSource](DataService* service) {
                        service->error(QString("不支持的数据源: %1").arg(dataSource));
                    });
                    return;
                }
            }

            invokeOnMainThread(service, [data, dataSource, symbol, dataType, startDate, endDate](DataService* service) {
                service->queryProgress(85, QString("数据获取完成，正在整理 %1 条结果...").arg(data.size()));
                service->m_fetchedData = data;

                try {
                    const QString cacheKey = DataServiceCache::generateStockCacheKey(
                        dataSource + "_" + symbol + "_" + dataType,
                        startDate,
                        endDate
                    );
                    DataServiceCache::getInstance().storeData(cacheKey, data);
                } catch (const std::exception& e) {
                    qWarning() << "保存数据到缓存失败:" << e.what();
                }

                service->queryProgress(100, QString("数据获取完成，共 %1 条").arg(data.size()));
                service->queryCompleted(true, QString("成功获取%1条数据").arg(data.size()), data);
                service->fetchedDataChanged();
            });
        } catch (const std::exception& e) {
            const QString errorMsg = QString("获取数据失败: %1").arg(e.what());
            qCritical() << "DataService::fetchDataByType:" << errorMsg;
            invokeOnMainThread(service, [errorMsg](DataService* service) {
                service->error(errorMsg);
            });
        } catch (...) {
            const QString errorMsg = "未知错误，获取数据失败";
            qCritical() << "DataService::fetchDataByType:" << errorMsg;
            invokeOnMainThread(service, [errorMsg](DataService* service) {
                service->error(errorMsg);
            });
        }
    }, &submitError)) {
        emit error(QString("线程池不可用，无法开始数据获取: %1").arg(submitError));
    }
}

// 辅助方法：获取指数成分股
QVariantList DataService::getIndexConstituents(const QString& indexSymbol, const QString& snapshotDate) {
    // 这里可以复用loadIndexConstituents的逻辑，但直接返回数据
    // 简化实现：直接查询数据库
    if (!m_impl->checkDatabaseConnection()) {
        return QVariantList();
    }

    QString effectiveSnapshotDate = QDate::fromString(snapshotDate, "yyyy-MM-dd").isValid()
        ? snapshotDate
        : QDate::currentDate().toString("yyyy-MM-dd");
    
    try {
        QString sql;
        std::map<QString, QVariant> params;
        
        if (indexSymbol == "BIG_CAP" || indexSymbol == "SMALL_CAP") {
            sql = "SELECT d.symbol AS symbol, COALESCE(si.name, d.symbol) AS name "
                  "FROM daily_bar d "
                  "LEFT JOIN symbol_info si ON d.symbol = si.symbol "
                  "WHERE d.trade_date = (SELECT MAX(trade_date) FROM daily_bar WHERE trade_date <= :snapshot_date) "
                  "  AND si.asset_class = 'STOCK' AND si.status = 'ACTIVE' AND d.market_cap IS NOT NULL ";
            params[":snapshot_date"] = QVariant(effectiveSnapshotDate);
            if (indexSymbol == "BIG_CAP") {
                sql += "ORDER BY market_cap DESC LIMIT 100";
            } else {
                sql += "ORDER BY market_cap ASC LIMIT 100";
            }
        } else {
            effectiveSnapshotDate = resolveIndexSnapshotDate(m_impl->database, indexSymbol, effectiveSnapshotDate);

            // 真实指数快照
            sql = "SELECT constituent_symbol as symbol, COALESCE(si.name, constituent_symbol) as name "
                  "FROM index_constituents ic "
                  "LEFT JOIN symbol_info si ON ic.constituent_symbol = si.symbol "
                  "WHERE ic.index_symbol = :index_symbol "
                  "  AND ic.start_date <= :snapshot_date "
                  "  AND (ic.end_date IS NULL OR ic.end_date >= :snapshot_date)";
            params[":index_symbol"] = QVariant(indexSymbol);
            params[":snapshot_date"] = QVariant(effectiveSnapshotDate);
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
    if (dataType == "kline_daily") {
        return fetchPriceTableDataForSymbols("daily_bar", extractSymbols(constituents), startDate, endDate);
    }

    if (dataType == "kline_weekly") {
        if (tableExists("weekly_bar")) {
            return fetchPriceTableDataForSymbols("weekly_bar", extractSymbols(constituents), startDate, endDate);
        }
        return fetchAggregatedKlineDataForSymbols("weekly", extractSymbols(constituents), startDate, endDate);
    }

    if (dataType == "kline_monthly") {
        if (tableExists("monthly_bar")) {
            return fetchPriceTableDataForSymbols("monthly_bar", extractSymbols(constituents), startDate, endDate);
        }
        return fetchAggregatedKlineDataForSymbols("monthly", extractSymbols(constituents), startDate, endDate);
    }

    if (dataType == "minute_data") {
        return fetchMinuteDataForSymbols(extractSymbols(constituents), startDate, endDate);
    }

    {
        qWarning() << "Unsupported data type for K-line:" << dataType;
        return QVariantList();
    }
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

QVariantList DataService::fetchPriceTableData(const QString& tableName,
                                             const QString& symbol,
                                             const QString& startDate,
                                             const QString& endDate) {
    if (!m_impl->checkDatabaseConnection()) {
        return QVariantList();
    }
    if (!tableExists(tableName)) {
        throw std::runtime_error(QString("数据表不存在: %1").arg(tableName).toStdString());
    }

    try {
        QString sql = QString("SELECT * FROM %1 WHERE trade_date BETWEEN :start_date AND :end_date")
            .arg(tableName);

        std::map<QString, QVariant> params;
        params[":start_date"] = startDate;
        params[":end_date"] = endDate;

        if (!symbol.trimmed().isEmpty()) {
            sql += " AND symbol = :symbol";
            params[":symbol"] = symbol.trimmed();
        }

        sql += " ORDER BY symbol, trade_date";
        return convertResultToVariantList(m_impl->database->executeQuery(sql, params));
    } catch (const std::exception& e) {
        qCritical() << "DataService::fetchPriceTableData:" << e.what();
        return QVariantList();
    }
}

QVariantList DataService::fetchPriceTableDataForSymbols(const QString& tableName,
                                                       const QStringList& symbols,
                                                       const QString& startDate,
                                                       const QString& endDate) {
    if (symbols.isEmpty()) {
        return QVariantList();
    }
    if (!m_impl->checkDatabaseConnection()) {
        return QVariantList();
    }
    if (!tableExists(tableName)) {
        throw std::runtime_error(QString("数据表不存在: %1").arg(tableName).toStdString());
    }

    try {
        const QString sql = QString(
            "SELECT * FROM %1 WHERE symbol IN (%2) AND trade_date BETWEEN :start_date AND :end_date ORDER BY symbol, trade_date")
            .arg(tableName, buildSymbolInClause(symbols));

        std::map<QString, QVariant> params;
        params[":start_date"] = startDate;
        params[":end_date"] = endDate;
        return convertResultToVariantList(m_impl->database->executeQuery(sql, params));
    } catch (const std::exception& e) {
        qCritical() << "DataService::fetchPriceTableDataForSymbols:" << e.what();
        return QVariantList();
    }
}

QVariantList DataService::fetchAggregatedKlineData(const QString& period,
                                                  const QString& symbol,
                                                  const QString& startDate,
                                                  const QString& endDate) {
    if (symbol.trimmed().isEmpty()) {
        return fetchAggregatedKlineDataForSymbols(period, QStringList(), startDate, endDate);
    }
    return fetchAggregatedKlineDataForSymbols(period, QStringList{symbol.trimmed()}, startDate, endDate);
}

QVariantList DataService::fetchAggregatedKlineDataForSymbols(const QString& period,
                                                            const QStringList& symbols,
                                                            const QString& startDate,
                                                            const QString& endDate) {
    if (!m_impl->checkDatabaseConnection()) {
        return QVariantList();
    }

    const QString periodExpr = (period == "monthly")
        ? "DATE_FORMAT(trade_date, '%Y-%m')"
        : "YEARWEEK(trade_date, 1)";

    QString symbolFilter;
    if (!symbols.isEmpty()) {
        symbolFilter = QString(" AND symbol IN (%1)").arg(buildSymbolInClause(symbols));
    }

    try {
        const QString sql = QString(
            "SELECT agg.symbol, "
            "       agg.period_end AS trade_date, "
            "       first_day.open AS open, "
            "       agg.high AS high, "
            "       agg.low AS low, "
            "       last_day.close AS close, "
            "       first_day.pre_close AS pre_close, "
            "       agg.volume AS volume, "
            "       agg.turnover AS turnover, "
            "       CASE WHEN first_day.pre_close IS NOT NULL AND first_day.pre_close <> 0 THEN ((last_day.close - first_day.pre_close) / first_day.pre_close) * 100 ELSE NULL END AS change_pct, "
            "       CASE WHEN first_day.pre_close IS NOT NULL THEN last_day.close - first_day.pre_close ELSE NULL END AS change_amt, "
            "       CASE WHEN first_day.pre_close IS NOT NULL AND first_day.pre_close <> 0 THEN ((agg.high - agg.low) / first_day.pre_close) * 100 ELSE NULL END AS amplitude, "
            "       agg.turnover_rate AS turnover_rate, "
            "       last_day.pe_ratio AS pe_ratio, "
            "       last_day.pb_ratio AS pb_ratio, "
            "       last_day.market_cap AS market_cap, "
            "       last_day.circulating_market_cap AS circulating_market_cap, "
            "       COALESCE(last_day.data_source, 'AGGREGATED_DAILY') AS data_source "
            "FROM ( "
            "    SELECT symbol, %1 AS period_key, MIN(trade_date) AS period_start, MAX(trade_date) AS period_end, "
            "           MAX(high) AS high, MIN(low) AS low, "
            "           SUM(volume) AS volume, SUM(turnover) AS turnover, SUM(COALESCE(turnover_rate, 0)) AS turnover_rate "
            "    FROM daily_bar "
            "    WHERE trade_date BETWEEN :start_date AND :end_date%2 "
            "    GROUP BY symbol, %1 "
            ") agg "
            "JOIN daily_bar first_day ON first_day.symbol = agg.symbol AND first_day.trade_date = agg.period_start "
            "JOIN daily_bar last_day ON last_day.symbol = agg.symbol AND last_day.trade_date = agg.period_end "
            "ORDER BY agg.symbol, agg.period_end")
            .arg(periodExpr, symbolFilter);

        std::map<QString, QVariant> params;
        params[":start_date"] = startDate;
        params[":end_date"] = endDate;
        return convertResultToVariantList(m_impl->database->executeQuery(sql, params));
    } catch (const std::exception& e) {
        qCritical() << "DataService::fetchAggregatedKlineDataForSymbols:" << e.what();
        return QVariantList();
    }
}

QVariantList DataService::fetchMinuteData(const QString& symbol,
                                         const QString& startDate,
                                         const QString& endDate) {
    if (symbol.trimmed().isEmpty()) {
        return fetchMinuteDataForSymbols(QStringList(), startDate, endDate);
    }
    return fetchMinuteDataForSymbols(QStringList{symbol.trimmed()}, startDate, endDate);
}

QVariantList DataService::fetchMinuteDataForSymbols(const QStringList& symbols,
                                                   const QString& startDate,
                                                   const QString& endDate) {
    if (!m_impl->checkDatabaseConnection()) {
        return QVariantList();
    }
    if (!tableExists("minute_bar")) {
        throw std::runtime_error("数据表不存在: minute_bar");
    }

    QString symbolFilter;
    if (!symbols.isEmpty()) {
        symbolFilter = QString(" AND si.symbol IN (%1)").arg(buildSymbolInClause(symbols));
    }

    try {
        const QString sql = QString(
            "SELECT mb.*, si.symbol "
            "FROM minute_bar mb "
            "JOIN symbol_info si ON si.symbol_id = mb.symbol_id "
            "WHERE mb.bar_time BETWEEN CONCAT(:start_date, ' 00:00:00') AND CONCAT(:end_date, ' 23:59:59')%1 "
            "ORDER BY si.symbol, mb.bar_time")
            .arg(symbolFilter);

        std::map<QString, QVariant> params;
        params[":start_date"] = startDate;
        params[":end_date"] = endDate;
        return convertResultToVariantList(m_impl->database->executeQuery(sql, params));
    } catch (const std::exception& e) {
        qCritical() << "DataService::fetchMinuteDataForSymbols:" << e.what();
        return QVariantList();
    }
}

// 辅助方法：获取个股K线数据
QVariantList DataService::fetchKlineData(const QString& symbol,
                                        const QString& dataType,
                                        const QString& startDate,
                                        const QString& endDate) {
    if (dataType == "kline_daily") {
        return fetchPriceTableData("daily_bar", symbol, startDate, endDate);
    }

    if (dataType == "kline_weekly") {
        if (tableExists("weekly_bar")) {
            return fetchPriceTableData("weekly_bar", symbol, startDate, endDate);
        }
        return fetchAggregatedKlineData("weekly", symbol, startDate, endDate);
    }

    if (dataType == "kline_monthly") {
        if (tableExists("monthly_bar")) {
            return fetchPriceTableData("monthly_bar", symbol, startDate, endDate);
        }
        return fetchAggregatedKlineData("monthly", symbol, startDate, endDate);
    }

    if (dataType == "minute_data") {
        return fetchMinuteData(symbol, startDate, endDate);
    }

    {
        return QVariantList();
    }
}

// 辅助方法：获取全市场K线数据
QVariantList DataService::fetchAllMarketKlineData(const QString& dataType,
                                                 const QString& startDate,
                                                 const QString& endDate) {
    if (dataType == "kline_daily") {
        return fetchPriceTableData("daily_bar", QString(), startDate, endDate);
    }

    if (dataType == "kline_weekly") {
        if (tableExists("weekly_bar")) {
            return fetchPriceTableData("weekly_bar", QString(), startDate, endDate);
        }
        return fetchAggregatedKlineData("weekly", QString(), startDate, endDate);
    }

    if (dataType == "kline_monthly") {
        if (tableExists("monthly_bar")) {
            return fetchPriceTableData("monthly_bar", QString(), startDate, endDate);
        }
        return fetchAggregatedKlineData("monthly", QString(), startDate, endDate);
    }

    if (dataType == "minute_data") {
        return fetchMinuteData(QString(), startDate, endDate);
    }

    {
        return QVariantList();
    }
}

// 辅助方法：获取财务数据（简化实现）
QVariantList DataService::fetchFinancialData(const QString& symbol,
                                            const QString& startDate,
                                            const QString& endDate) {
    if (symbol.trimmed().isEmpty()) {
        return fetchFinancialDataForSymbols(QStringList(), startDate, endDate);
    }
    return fetchFinancialDataForSymbols(QStringList{symbol.trimmed()}, startDate, endDate);
}

QVariantList DataService::fetchFinancialDataForSymbols(const QStringList& symbols,
                                                      const QString& startDate,
                                                      const QString& endDate) {
    if (!m_impl->checkDatabaseConnection()) {
        return QVariantList();
    }
    if (!tableExists("financial_indicator")) {
        throw std::runtime_error("数据表不存在: financial_indicator");
    }

    try {
        QString sql =
            "SELECT fi.*, si.symbol "
            "FROM financial_indicator fi "
            "JOIN symbol_info si ON si.symbol_id = fi.symbol_id "
            "WHERE fi.report_date BETWEEN :start_date AND :end_date";

        if (!symbols.isEmpty()) {
            sql += QString(" AND si.symbol IN (%1)").arg(buildSymbolInClause(symbols));
        }

        sql += " ORDER BY si.symbol, fi.report_date, fi.report_type";

        std::map<QString, QVariant> params;
        params[":start_date"] = startDate;
        params[":end_date"] = endDate;

        return convertResultToVariantList(m_impl->database->executeQuery(sql, params));
    } catch (const std::exception& e) {
        qCritical() << "DataService::fetchFinancialDataForSymbols:" << e.what();
        return QVariantList();
    }
}

QVariantList DataService::fetchHistoricalData(const QString& symbol,
                                             const QString& startDate,
                                             const QString& endDate) {
    return fetchPriceTableData("daily_bar", symbol, startDate, endDate);
}

QVariantList DataService::fetchRealtimeData(const QString& symbol,
                                           const QString& startDate,
                                           const QString& endDate) {
    Q_UNUSED(startDate);
    if (symbol.trimmed().isEmpty()) {
        return fetchRealtimeDataForSymbols(QStringList(), endDate);
    }
    return fetchRealtimeDataForSymbols(QStringList{symbol.trimmed()}, endDate);
}

QVariantList DataService::fetchRealtimeDataForSymbols(const QStringList& symbols,
                                                     const QString& endDate) {
    if (!m_impl->checkDatabaseConnection()) {
        return QVariantList();
    }
    if (!tableExists("daily_bar")) {
        throw std::runtime_error("数据表不存在: daily_bar");
    }

    QString symbolFilter;
    if (!symbols.isEmpty()) {
        symbolFilter = QString(" AND symbol IN (%1)").arg(buildSymbolInClause(symbols));
    }

    try {
        const QString sql = QString(
            "SELECT d.* FROM daily_bar d "
            "JOIN ("
            "    SELECT symbol, MAX(trade_date) AS latest_date "
            "    FROM daily_bar "
            "    WHERE trade_date <= :end_date%1 "
            "    GROUP BY symbol"
            ") latest ON latest.symbol = d.symbol AND latest.latest_date = d.trade_date "
            "ORDER BY d.symbol, d.trade_date")
            .arg(symbolFilter);

        std::map<QString, QVariant> params;
        params[":end_date"] = endDate;
        return convertResultToVariantList(m_impl->database->executeQuery(sql, params));
    } catch (const std::exception& e) {
        qCritical() << "DataService::fetchRealtimeDataForSymbols:" << e.what();
        return QVariantList();
    }
}

// 辅助方法：获取舆情数据（简化实现）
QVariantList DataService::fetchNewsData(const QString& symbol,
                                       const QString& startDate,
                                       const QString& endDate) {
    if (symbol.trimmed().isEmpty()) {
        return fetchNewsDataForSymbols(QStringList(), startDate, endDate);
    }
    return fetchNewsDataForSymbols(QStringList{symbol.trimmed()}, startDate, endDate);
}

QVariantList DataService::fetchNewsDataForSymbols(const QStringList& symbols,
                                                 const QString& startDate,
                                                 const QString& endDate) {
    QVariantList data = fetchGenericTimeSeriesData(
        resolveNewsTable(),
        symbols,
        startDate,
        endDate,
        QStringList{"publish_time", "pub_time", "trade_date", "date", "created_at"},
        QStringList{"symbol", "stock_code", "security_code", "ticker"},
        true);

    if (!data.isEmpty()) {
        return data;
    }

    QString errorMessage;
    ensureExtendedDataImported(QStringLiteral("news"), symbols, startDate, endDate, {}, &errorMessage);
    if (!errorMessage.trimmed().isEmpty()) {
        qWarning() << "DataService::fetchNewsDataForSymbols: import warning:" << errorMessage;
    }

    return fetchGenericTimeSeriesData(
        resolveNewsTable(),
        symbols,
        startDate,
        endDate,
        QStringList{"publish_time", "pub_time", "trade_date", "date", "created_at"},
        QStringList{"symbol", "stock_code", "security_code", "ticker"},
        true);
}

QVariantList DataService::fetchPolicyData(const QString& symbol,
                                         const QString& startDate,
                                         const QString& endDate,
                                         const QVariantMap& options) {
    if (symbol.trimmed().isEmpty()) {
        return fetchPolicyDataForSymbols(QStringList(), startDate, endDate, options);
    }
    return fetchPolicyDataForSymbols(QStringList{symbol.trimmed()}, startDate, endDate, options);
}

QVariantList DataService::fetchPolicyDataForSymbols(const QStringList& symbols,
                                                   const QString& startDate,
                                                   const QString& endDate,
                                                   const QVariantMap& options) {
    QVariantList data = fetchGenericTimeSeriesData(
        QStringLiteral("policy_data"),
        symbols,
        startDate,
        endDate,
        QStringList{"publish_time", "created_at", "trade_date", "date"},
        QStringList{"symbol"},
        true);
    if (!data.isEmpty()) {
        return data;
    }

    QString errorMessage;
    ensureExtendedDataImported(QStringLiteral("policy"), symbols, startDate, endDate, options, &errorMessage);
    if (!errorMessage.trimmed().isEmpty()) {
        qWarning() << "DataService::fetchPolicyDataForSymbols: import warning:" << errorMessage;
    }

    return fetchGenericTimeSeriesData(
        QStringLiteral("policy_data"),
        symbols,
        startDate,
        endDate,
        QStringList{"publish_time", "created_at", "trade_date", "date"},
        QStringList{"symbol"},
        true);
}

QVariantList DataService::fetchAlternativeData(const QString& symbol,
                                              const QString& startDate,
                                              const QString& endDate,
                                              const QVariantMap& options) {
    if (symbol.trimmed().isEmpty()) {
        return fetchAlternativeDataForSymbols(QStringList(), startDate, endDate, options);
    }
    return fetchAlternativeDataForSymbols(QStringList{symbol.trimmed()}, startDate, endDate, options);
}

QVariantList DataService::fetchAlternativeDataForSymbols(const QStringList& symbols,
                                                        const QString& startDate,
                                                        const QString& endDate,
                                                        const QVariantMap& options) {
    QVariantList data = fetchGenericTimeSeriesData(
        QStringLiteral("alternative_data"),
        symbols,
        startDate,
        endDate,
        QStringList{"trade_date", "publish_time", "created_at", "date"},
        QStringList{"symbol"},
        true);
    if (!data.isEmpty()) {
        return data;
    }

    QString errorMessage;
    ensureExtendedDataImported(QStringLiteral("alternative"), symbols, startDate, endDate, options, &errorMessage);
    if (!errorMessage.trimmed().isEmpty()) {
        qWarning() << "DataService::fetchAlternativeDataForSymbols: import warning:" << errorMessage;
    }

    return fetchGenericTimeSeriesData(
        QStringLiteral("alternative_data"),
        symbols,
        startDate,
        endDate,
        QStringList{"trade_date", "publish_time", "created_at", "date"},
        QStringList{"symbol"},
        true);
}

QVariantList DataService::fetchDerivativesData(const QString& symbol,
                                              const QString& startDate,
                                              const QString& endDate,
                                              const QVariantMap& options) {
    if (symbol.trimmed().isEmpty()) {
        return fetchDerivativesDataForSymbols(QStringList(), startDate, endDate, options);
    }
    return fetchDerivativesDataForSymbols(QStringList{symbol.trimmed()}, startDate, endDate, options);
}

QVariantList DataService::fetchDerivativesDataForSymbols(const QStringList& symbols,
                                                        const QString& startDate,
                                                        const QString& endDate,
                                                        const QVariantMap& options) {
    QVariantList data = fetchGenericTimeSeriesData(
        QStringLiteral("derivatives_data"),
        symbols,
        startDate,
        endDate,
        QStringList{"trade_date", "publish_time", "created_at", "date"},
        QStringList{"symbol", "underlying_symbol"},
        true);
    if (!data.isEmpty()) {
        return data;
    }

    QString errorMessage;
    ensureExtendedDataImported(QStringLiteral("derivatives"), symbols, startDate, endDate, options, &errorMessage);
    if (!errorMessage.trimmed().isEmpty()) {
        qWarning() << "DataService::fetchDerivativesDataForSymbols: import warning:" << errorMessage;
    }

    return fetchGenericTimeSeriesData(
        QStringLiteral("derivatives_data"),
        symbols,
        startDate,
        endDate,
        QStringList{"trade_date", "publish_time", "created_at", "date"},
        QStringList{"symbol", "underlying_symbol"},
        true);
}

QVariantList DataService::fetchGenericTimeSeriesData(const QString& tableName,
                                                    const QStringList& symbols,
                                                    const QString& startDate,
                                                    const QString& endDate,
                                                    const QStringList& dateColumns,
                                                    const QStringList& symbolColumns,
                                                    bool allowMarketFallback) {
    if (!checkDatabaseConnectionForFetch()) {
        return QVariantList();
    }

    const QString normalizedTable = tableName.trimmed();
    if (normalizedTable.isEmpty() || !tableExists(normalizedTable)) {
        return QVariantList();
    }

    const QString dateColumn = resolveFirstExistingColumn(normalizedTable, dateColumns);
    if (dateColumn.isEmpty()) {
        throw std::runtime_error(QString("数据表 %1 缺少时间列").arg(normalizedTable).toStdString());
    }

    const QString symbolColumn = resolveFirstExistingColumn(normalizedTable, symbolColumns);

    auto runQuery = [&](const QString& extraPredicate) -> QVariantList {
        QString sql = QString("SELECT * FROM %1 WHERE DATE(%2) BETWEEN :start_date AND :end_date")
            .arg(normalizedTable, dateColumn);
        if (!extraPredicate.trimmed().isEmpty()) {
            sql += QStringLiteral(" AND ") + extraPredicate;
        }
        if (!symbolColumn.isEmpty()) {
            sql += QString(" ORDER BY %1, %2").arg(symbolColumn, dateColumn);
        } else {
            sql += QString(" ORDER BY %1").arg(dateColumn);
        }

        std::map<QString, QVariant> params;
        params[":start_date"] = startDate;
        params[":end_date"] = endDate;
        return executeVariantQueryForFetch(sql, params);
    };

    if (!symbols.isEmpty() && !symbolColumn.isEmpty()) {
        const QVariantList directRows = runQuery(QString("%1 IN (%2)").arg(symbolColumn, buildSymbolInClause(symbols)));
        if (!directRows.isEmpty() || !allowMarketFallback) {
            return directRows;
        }

        return runQuery(QString("(%1 IN ('MARKET', 'ALL_MARKET', 'GLOBAL') OR %1 IS NULL OR TRIM(%1) = '')")
            .arg(symbolColumn));
    }

    return runQuery(QString());
}

bool DataService::ensureExtendedDataImported(const QString& dataType,
                                            const QStringList& symbols,
                                            const QString& startDate,
                                            const QString& endDate,
                                            const QVariantMap& options,
                                            QString* errorMessage) {
    if (m_ensureExtendedDataImportedOverrideForTests) {
        return m_ensureExtendedDataImportedOverrideForTests(
            dataType,
            symbols,
            startDate,
            endDate,
            options,
            errorMessage);
    }

    const QString normalizedType = dataType.trimmed().toLower();
    if (!(normalizedType == QStringLiteral("news")
            || normalizedType == QStringLiteral("policy")
            || normalizedType == QStringLiteral("alternative")
            || normalizedType == QStringLiteral("derivatives"))) {
        return false;
    }

    const QString repoRoot = resolveRepoRootFromAppDir();
    if (repoRoot.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法定位仓库根目录，无法启动扩展数据导入脚本");
        }
        return false;
    }

    const QFileInfo scriptInfo(QDir(repoRoot).filePath(QStringLiteral("tools/import_extended_market_data.py")));
    if (!scriptInfo.exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未找到扩展数据导入脚本: %1").arg(scriptInfo.absoluteFilePath());
        }
        return false;
    }

    QProcess process;
    process.setWorkingDirectory(repoRoot);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString existingPythonPath = environment.value(QStringLiteral("PYTHONPATH"));
    environment.insert(QStringLiteral("PYTHONPATH"), existingPythonPath.isEmpty()
        ? repoRoot
        : (repoRoot + QDir::listSeparator() + existingPythonPath));
    process.setProcessEnvironment(environment);

    QStringList arguments{
        scriptInfo.canonicalFilePath(),
        QStringLiteral("--data-type"), normalizedType,
        QStringLiteral("--start-date"), startDate,
        QStringLiteral("--end-date"), endDate
    };
    if (!symbols.isEmpty()) {
        arguments << QStringLiteral("--symbols") << symbols.join(QStringLiteral(","));
    }

    const QString market = options.value(QStringLiteral("market")).toString().trimmed();
    if (!market.isEmpty()) {
        arguments << QStringLiteral("--market") << market;
    }

    const QString provider = options.value(QStringLiteral("provider")).toString().trimmed();
    if (!provider.isEmpty()) {
        arguments << QStringLiteral("--provider") << provider;
    }

    process.start(resolvePythonExecutable(repoRoot), arguments);
    if (!process.waitForStarted(3000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法启动扩展数据导入进程");
        }
        return false;
    }

    if (!process.waitForFinished(180000)) {
        process.kill();
        process.waitForFinished(1000);
        if (errorMessage) {
            *errorMessage = QStringLiteral("扩展数据导入超时");
        }
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorMessage) {
            const QString standardError = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
            const QString standardOutput = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
            *errorMessage = !standardError.isEmpty()
                ? standardError
                : (!standardOutput.isEmpty()
                    ? standardOutput
                    : QStringLiteral("扩展数据导入失败，exitCode=%1").arg(process.exitCode()));
        }
        return false;
    }

    return true;
}

bool DataService::checkDatabaseConnectionForFetch() const {
    if (m_checkDatabaseConnectionOverrideForTests) {
        return m_checkDatabaseConnectionOverrideForTests();
    }
    return m_impl && m_impl->checkDatabaseConnection();
}

QVariantList DataService::executeVariantQueryForFetch(const QString& sql,
                                                      const std::map<QString, QVariant>& params) const {
    if (m_executeVariantQueryOverrideForTests) {
        return m_executeVariantQueryOverrideForTests(sql, params);
    }
    if (!m_impl || !m_impl->database) {
        return QVariantList();
    }
    return convertResultToVariantList(m_impl->database->executeQuery(sql, params));
}

bool DataService::tableExists(const QString& tableName) const {
    if (m_tableExistsOverrideForTests) {
        return m_tableExistsOverrideForTests(tableName);
    }

    if (!m_impl || !m_impl->database) {
        return false;
    }

    const auto result = m_impl->database->executeQuery(
        "SELECT COUNT(*) AS count FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = :table_name",
        {{":table_name", tableName}});

    return !result.isEmpty() && result.getRow(0).getInt("count") > 0;
}

bool DataService::tableHasColumn(const QString& tableName, const QString& columnName) const {
    if (m_tableHasColumnOverrideForTests) {
        return m_tableHasColumnOverrideForTests(tableName, columnName);
    }

    if (!m_impl || !m_impl->database) {
        return false;
    }

    const auto result = m_impl->database->executeQuery(
        "SELECT COUNT(*) AS count FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = :table_name AND COLUMN_NAME = :column_name",
        {{":table_name", tableName}, {":column_name", columnName}});

    return !result.isEmpty() && result.getRow(0).getInt("count") > 0;
}

QString DataService::resolveFirstExistingColumn(const QString& tableName, const QStringList& candidates) const {
    for (const QString& candidate : candidates) {
        if (tableHasColumn(tableName, candidate)) {
            return candidate;
        }
    }
    return QString();
}

QString DataService::resolveNewsTable() const {
    static const QStringList newsTableCandidates = {
        "news_sentiment",
        "stock_news",
        "news_data",
        "news"
    };

    for (const QString& candidate : newsTableCandidates) {
        if (tableExists(candidate)) {
            return candidate;
        }
    }

    return QString();
}