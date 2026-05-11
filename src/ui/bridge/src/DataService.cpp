// DataService.cpp - 极简实现 (目标: <200行)
#include "DataService.h"
#include "DataServiceCache.h"
#include "cleaning/CleaningEngine.h"
#include "cleaning/rules/FieldStandardizationRule.h"
#include "DataFetchFieldContractUtils.h"
#include "database/QueryBuilder.h"
#include "database/QtMySQLDatabase.h"
#include "database/DatabaseConfig.h"
#include "foundation.h"
#include <QCoreApplication>
#include <QDir>
#include <QHash>
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
    return factor::bridge::MarketBarFieldKeys::backtestReady().orderedValues();
}

QVariant readFieldValue(const QueryResultRow& row, const QString& field)
{
    static const QSet<QString> numericFields = {
        QString(factor::bridge::MarketBarFieldKeys::OPEN),
        QString(factor::bridge::MarketBarFieldKeys::HIGH),
        QString(factor::bridge::MarketBarFieldKeys::LOW),
        QString(factor::bridge::MarketBarFieldKeys::CLOSE),
        QString(factor::bridge::MarketBarFieldKeys::PRE_CLOSE),
        QString(factor::bridge::MarketBarFieldKeys::VOLUME),
        QString(factor::bridge::MarketBarFieldKeys::TURNOVER),
        QString(factor::bridge::MarketBarFieldKeys::CHANGE_PCT),
        QString(factor::bridge::MarketBarFieldKeys::CHANGE_AMT),
        QString(factor::bridge::MarketBarFieldKeys::AMPLITUDE),
        QString(factor::bridge::MarketBarFieldKeys::TURNOVER_RATE),
        QString(factor::bridge::MarketBarFieldKeys::PE_RATIO),
        QString(factor::bridge::MarketBarFieldKeys::PB_RATIO),
        QString(factor::bridge::MarketBarFieldKeys::MARKET_CAP),
        QString(factor::bridge::MarketBarFieldKeys::CIRCULATING_MARKET_CAP)
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

QString syntheticIndexDisplayName(const QString& indexSymbol)
{
    if (indexSymbol == QStringLiteral("BIG_CAP")) {
        return QStringLiteral("大盘股");
    }
    if (indexSymbol == QStringLiteral("SMALL_CAP")) {
        return QStringLiteral("小盘股");
    }
    return indexSymbol.trimmed();
}

QString resolveIndexDisplayName(const std::shared_ptr<QtMySQLDatabase>& database,
                               const QString& indexSymbol)
{
    const QString normalizedSymbol = indexSymbol.trimmed();
    if (normalizedSymbol.isEmpty()) {
        return {};
    }

    if (normalizedSymbol == QStringLiteral("BIG_CAP") || normalizedSymbol == QStringLiteral("SMALL_CAP")) {
        return syntheticIndexDisplayName(normalizedSymbol);
    }

    if (!database) {
        return normalizedSymbol;
    }

    try {
        const auto result = database->executeQuery(
            QStringLiteral("SELECT COALESCE(name, symbol) AS name FROM symbol_info WHERE symbol = :symbol LIMIT 1"),
            {{QStringLiteral(":symbol"), normalizedSymbol}});
        if (!result.isEmpty()) {
            const QString name = result.getRow(0).getString(QStringLiteral("name")).trimmed();
            if (!name.isEmpty()) {
                return name;
            }
        }
    } catch (const std::exception& e) {
        qWarning() << "resolveIndexDisplayName failed:" << e.what();
    }

    return normalizedSymbol;
}

QVariantMap buildConstituentMetadata(const QVariantMap& constituent)
{
    QVariantMap metadata;

    const QString industryCode = constituent.value(QStringLiteral("industry_code")).toString().trimmed();
    if (!industryCode.isEmpty()) {
        metadata.insert(QStringLiteral("industry_code"), industryCode);
    }

    const QString indexSymbol = constituent.value(QStringLiteral("index_symbol")).toString().trimmed();
    if (!indexSymbol.isEmpty()) {
        metadata.insert(QStringLiteral("index_symbol"), indexSymbol);
    }

    const QString indexName = constituent.value(QStringLiteral("index_name")).toString().trimmed();
    if (!indexName.isEmpty()) {
        metadata.insert(QStringLiteral("index_name"), indexName);
    }

    const QString snapshotDate = constituent.value(QStringLiteral("index_snapshot_date")).toString().trimmed();
    if (!snapshotDate.isEmpty()) {
        metadata.insert(QStringLiteral("index_snapshot_date"), snapshotDate);
    }

    return metadata;
}

QHash<QString, QVariantMap> buildConstituentMetadataBySymbol(const QVariantList& constituents)
{
    QHash<QString, QVariantMap> metadataBySymbol;
    for (const QVariant& item : constituents) {
        if (!item.canConvert<QVariantMap>()) {
            continue;
        }

        const QVariantMap constituent = item.toMap();
        const QString symbol = constituent.value(QStringLiteral("symbol")).toString().trimmed();
        if (symbol.isEmpty()) {
            continue;
        }

        const QVariantMap metadata = buildConstituentMetadata(constituent);
        if (!metadata.isEmpty()) {
            metadataBySymbol.insert(symbol, metadata);
        }
    }

    return metadataBySymbol;
}

void mergeMetadataIntoRow(QVariantMap& row, const QVariantMap& metadata)
{
    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
        const QString key = it.key();
        const QVariant value = it.value();
        if (!value.isValid() || value.isNull()) {
            continue;
        }

        const QString existingValue = row.value(key).toString().trimmed();
        if (!row.contains(key) || existingValue.isEmpty()) {
            row.insert(key, value);
        }
    }
}

QVariantList enrichRowsWithConstituentMetadata(const QVariantList& data,
                                              const QVariantList& constituents)
{
    const QHash<QString, QVariantMap> metadataBySymbol = buildConstituentMetadataBySymbol(constituents);
    if (metadataBySymbol.isEmpty()) {
        return data;
    }

    QVariantList enrichedData;
    enrichedData.reserve(data.size());
    for (const QVariant& item : data) {
        if (!item.canConvert<QVariantMap>()) {
            enrichedData.append(item);
            continue;
        }

        QVariantMap row = item.toMap();
        const QString symbol = row.value(QStringLiteral("symbol")).toString().trimmed();
        const auto metadataIt = metadataBySymbol.constFind(symbol);
        if (metadataIt != metadataBySymbol.constEnd()) {
            mergeMetadataIntoRow(row, metadataIt.value());
        }
        enrichedData.append(row);
    }

    return enrichedData;
}

QString describeDataTypeLabel(const QString& dataType)
{
    if (dataType == QStringLiteral("kline_daily")) {
        return QStringLiteral("日线");
    }
    if (dataType == QStringLiteral("kline_weekly")) {
        return QStringLiteral("周线");
    }
    if (dataType == QStringLiteral("kline_monthly")) {
        return QStringLiteral("月线");
    }
    if (dataType == QStringLiteral("minute_data")) {
        return QStringLiteral("分钟");
    }
    if (dataType == QStringLiteral("realtime")) {
        return QStringLiteral("实时");
    }
    if (dataType == QStringLiteral("historical")) {
        return QStringLiteral("历史");
    }
    if (dataType == QStringLiteral("financial")) {
        return QStringLiteral("财务");
    }
    if (dataType == QStringLiteral("news")) {
        return QStringLiteral("舆情");
    }
    if (dataType == QStringLiteral("policy")) {
        return QStringLiteral("政策");
    }
    if (dataType == QStringLiteral("alternative")) {
        return QStringLiteral("另类");
    }
    if (dataType == QStringLiteral("derivatives")) {
        return QStringLiteral("衍生品");
    }
    if (dataType == QStringLiteral("index_constituents")) {
        return QStringLiteral("指数成分");
    }
    if (dataType == QStringLiteral("index_list")) {
        return QStringLiteral("指数列表");
    }
    return dataType;
}

void emitQueryProgressUpdate(DataService* service, int progress, const QString& message)
{
    if (!service) {
        return;
    }

    QPointer<DataService> safeService(service);
    QMetaObject::invokeMethod(service, [safeService, progress, message]() mutable {
        if (safeService) {
            safeService.data()->queryProgress(progress, message);
        }
    }, Qt::QueuedConnection);
}

QString buildBatchProgressMessage(const QString& label, int completedBatches, int totalBatches)
{
    if (label.trimmed().isEmpty()) {
        return QStringLiteral("正在处理数据 (%1/%2 批)").arg(completedBatches).arg(totalBatches);
    }
    return QStringLiteral("%1 (%2/%3 批)").arg(label).arg(completedBatches).arg(totalBatches);
}

template <typename BatchQuery>
QVariantList collectBatchedQueryResults(DataService* service,
                                        const QStringList& symbols,
                                        int batchSize,
                                        int progressStart,
                                        int progressSpan,
                                        const QString& progressLabel,
                                        BatchQuery&& queryBatch)
{
    const int effectiveBatchSize = qMax(1, batchSize);
    const bool reportProgress = service && progressSpan > 0;
    const int totalBatches = symbols.isEmpty()
        ? 1
        : (symbols.size() + effectiveBatchSize - 1) / effectiveBatchSize;

    if (reportProgress) {
        emitQueryProgressUpdate(service, qBound(0, progressStart, 100), buildBatchProgressMessage(progressLabel, 0, totalBatches));
    }

    QVariantList combinedResults;
    if (symbols.isEmpty()) {
        const QVariantList batchResults = queryBatch(QStringList());
        for (const QVariant& item : batchResults) {
            combinedResults.append(item);
        }
        if (reportProgress) {
            emitQueryProgressUpdate(service,
                                    qBound(0, progressStart + progressSpan, 100),
                                    buildBatchProgressMessage(progressLabel, 1, 1));
        }
        return combinedResults;
    }

    for (int batchIndex = 0; batchIndex < totalBatches; ++batchIndex) {
        const QStringList batchSymbols = symbols.mid(batchIndex * effectiveBatchSize, effectiveBatchSize);
        const QVariantList batchResults = queryBatch(batchSymbols);
        for (const QVariant& item : batchResults) {
            combinedResults.append(item);
        }

        if (reportProgress) {
            const int completedBatches = batchIndex + 1;
            const int progress = progressStart + (progressSpan * completedBatches) / totalBatches;
            emitQueryProgressUpdate(service, qBound(0, progress, 100), buildBatchProgressMessage(progressLabel, completedBatches, totalBatches));
        }
    }

    return combinedResults;
}

QVariantList executeSingleQueryWithProgress(DataService* service,
                                           int progressStart,
                                           int progressSpan,
                                           const QString& progressLabel,
                                           const std::function<QVariantList()>& query)
{
    const bool reportProgress = service && progressSpan > 0;
    if (reportProgress) {
        emitQueryProgressUpdate(service, qBound(0, progressStart, 100), progressLabel);
    }

    const QVariantList results = query();

    if (reportProgress) {
        emitQueryProgressUpdate(service, qBound(0, progressStart + progressSpan, 100), progressLabel);
    }

    return results;
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
    
        QMutex operationMutex;
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
                                         "d.symbol, s.name, TRIM(COALESCE(s.industry, '')) AS industry_code, d.trade_date, "
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



void DataService::cleanDataAsync(const QVariantList& data, 
                                const QVariantMap& rules) {
    Q_UNUSED(rules);
    QString submitError;
    if (!submitToFoundationThreadPool(this, [data](DataService* service) {
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

            factor::bridge::CleaningEngine cleaningEngine;

            // 添加字段标准化规则，自动补充 symbol_info 通用字段
            auto fieldStdRule = std::make_unique<factor::bridge::FieldStandardizationRule>();
            // 使用与 DataService 相同的默认数据库连接
            fieldStdRule->setDatabaseConnectionName(QStringLiteral("qt_sql_default_connection"));
            cleaningEngine.addRule(std::move(fieldStdRule));

            QObject::connect(&cleaningEngine, &factor::bridge::CleaningEngine::progress,
                             service, [service](int percent, const QString& msg) {
                                 invokeOnMainThread(service, [percent, msg](DataService* service) {
                                     service->cleaningProgress(percent, msg);
                                 });
                             });
            QObject::connect(&cleaningEngine, &factor::bridge::CleaningEngine::progressDetail,
                             service, [service](int percent, const QString& msg,
                                                const QString& sym, int kept, int removed) {
                                 invokeOnMainThread(service, [percent, msg, sym, kept, removed](DataService* service) {
                                     service->cleaningProgressDetail(percent, msg, sym, kept, removed);
                                 });
                             });
            QObject::connect(&cleaningEngine, &factor::bridge::CleaningEngine::errorOccurred,
                             service, [service](const QString& errorMessage) {
                                 invokeOnMainThread(service, [errorMessage](DataService* service) {
                                     service->error(errorMessage);
                                 });
                             });

            const QVariantList cleanedData = cleaningEngine.clean(data);
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
                service->queryProgress(0, "开始加载指数成分股...");
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
                    service->queryProgress(1, "数据库连接正常，执行查询...");
                });

                if (!service->m_impl->database && !service->m_impl->initializeDatabaseIfNeeded()) {
                    throw std::runtime_error("数据库连接不可用");
                }

                    const QString normalizedIndexSymbol = indexSymbol.trimmed();
                    const QString indexDisplayName = resolveIndexDisplayName(service->m_impl->database, normalizedIndexSymbol);

                    QString sql;
                std::map<QString, QVariant> params;

                    if (normalizedIndexSymbol == "BIG_CAP" || normalizedIndexSymbol == "SMALL_CAP") {
                      sql = "SELECT d.symbol, COALESCE(si.name, d.symbol) AS name, "
                          "TRIM(COALESCE(si.industry, '')) AS industry_code, "
                          "d.market_cap AS weight, d.trade_date AS start_date "
                          "FROM daily_bar d "
                          "LEFT JOIN symbol_info si ON d.symbol = si.symbol "
                          "WHERE d.trade_date = (SELECT MAX(trade_date) FROM daily_bar WHERE trade_date <= :snapshot_date) "
                          "  AND si.asset_class = 'STOCK' AND si.status = 'ACTIVE' AND d.market_cap IS NOT NULL ";
                    params[":snapshot_date"] = QVariant(effectiveSnapshotDate);
                      sql += normalizedIndexSymbol == "BIG_CAP" ? "ORDER BY market_cap DESC LIMIT 100" : "ORDER BY market_cap ASC LIMIT 100";
                } else {
                      effectiveSnapshotDate = resolveIndexSnapshotDate(service->m_impl->database, normalizedIndexSymbol, effectiveSnapshotDate);
                    sql = "SELECT ic.constituent_symbol as symbol, "
                          "COALESCE(si.name, ic.constituent_symbol) as name, "
                          "TRIM(COALESCE(si.industry, '')) AS industry_code, "
                          "ic.weight, ic.start_date "
                          "FROM index_constituents ic "
                          "LEFT JOIN symbol_info si ON ic.constituent_symbol = si.symbol "
                          "WHERE ic.index_symbol = :index_symbol "
                          "  AND ic.start_date <= :snapshot_date "
                          "  AND (ic.end_date IS NULL OR ic.end_date >= :snapshot_date) "
                          "ORDER BY ic.weight DESC";
                      params[":index_symbol"] = QVariant(normalizedIndexSymbol);
                    params[":snapshot_date"] = QVariant(effectiveSnapshotDate);
                }

                const auto result = service->m_impl->database->executeQuery(sql, params);
                const auto& rows = result.getRows();
                const int totalRows = static_cast<int>(rows.size());
                for (int rowIndex = 0; rowIndex < totalRows; ++rowIndex) {
                    const auto& row = rows[rowIndex];
                    QVariantMap formattedRecord;
                    formattedRecord["symbol"] = row.getString("symbol");
                    formattedRecord["name"] = row.getString("name");
                    const QString industryCode = row.getString("industry_code").trimmed();
                    if (!industryCode.isEmpty()) {
                        formattedRecord["industry_code"] = industryCode;
                    }
                    formattedRecord["index_symbol"] = normalizedIndexSymbol;
                    formattedRecord["index_name"] = indexDisplayName;
                    formattedRecord["index_snapshot_date"] = effectiveSnapshotDate;
                    formattedRecord["weight"] = row.getDouble("weight");
                    formattedRecord["start_date"] = row.getString("start_date");
                    formattedData.append(formattedRecord);

                    const int progress = 2 + ((rowIndex + 1) * 96) / qMax(1, totalRows);
                    invokeOnMainThread(service, [progress, rowIndex, totalRows](DataService* service) {
                        service->queryProgress(progress, QString("正在处理指数成分股 %1/%2...").arg(rowIndex + 1).arg(totalRows));
                    });
                }
            }

            invokeOnMainThread(service, [formattedData, effectiveSnapshotDate, indexSymbol](DataService* service) {
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
                service->queryProgress(0, QString("开始获取%1数据...").arg(dataType));
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

                if (dataSource == "index") {
                    const QString snapshotDate = resolveSnapshotDateString(endDate, options);

                    if (dataType == "index_constituents") {
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
                        data = service->fetchConstituentKlineData(
                            constituents,
                            dataType,
                            startDate,
                            endDate,
                            0,
                            100,
                            QStringLiteral("正在加载行情数据"));
                        data = enrichRowsWithConstituentMetadata(data, constituents);
                    } else if (dataType == "financial") {
                        const QVariantList constituents = service->getIndexConstituents(symbol, snapshotDate);
                        if (constituents.isEmpty()) {
                            invokeOnMainThread(service, [symbol, snapshotDate](DataService* service) {
                                service->error(QString("无法获取指数 %1 在 %2 的成分股").arg(symbol, snapshotDate));
                            });
                            return;
                        }
                        data = service->fetchFinancialDataForSymbols(
                            extractSymbols(constituents),
                            startDate,
                            endDate,
                            0,
                            100,
                            QStringLiteral("正在加载财务数据"));
                        data = enrichRowsWithConstituentMetadata(data, constituents);
                    } else if (dataType == "news") {
                        const QVariantList constituents = service->getIndexConstituents(symbol, snapshotDate);
                        if (constituents.isEmpty()) {
                            invokeOnMainThread(service, [symbol, snapshotDate](DataService* service) {
                                service->error(QString("无法获取指数 %1 在 %2 的成分股").arg(symbol, snapshotDate));
                            });
                            return;
                        }
                        data = service->fetchNewsDataForSymbols(
                            extractSymbols(constituents),
                            startDate,
                            endDate,
                            0,
                            100,
                            QStringLiteral("正在加载舆情数据"));
                        data = enrichRowsWithConstituentMetadata(data, constituents);
                    } else if (dataType == "historical") {
                        const QVariantList constituents = service->getIndexConstituents(symbol, snapshotDate);
                        if (constituents.isEmpty()) {
                            invokeOnMainThread(service, [symbol, snapshotDate](DataService* service) {
                                service->error(QString("无法获取指数 %1 在 %2 的成分股").arg(symbol, snapshotDate));
                            });
                            return;
                        }
                        data = service->fetchPriceTableDataForSymbols(
                            "daily_bar",
                            extractSymbols(constituents),
                            startDate,
                            endDate,
                            0,
                            100,
                            QStringLiteral("正在加载历史数据"));
                        data = enrichRowsWithConstituentMetadata(data, constituents);
                    } else if (dataType == "realtime") {
                        const QVariantList constituents = service->getIndexConstituents(symbol, snapshotDate);
                        if (constituents.isEmpty()) {
                            invokeOnMainThread(service, [symbol, snapshotDate](DataService* service) {
                                service->error(QString("无法获取指数 %1 在 %2 的成分股").arg(symbol, snapshotDate));
                            });
                            return;
                        }
                        data = service->fetchRealtimeDataForSymbols(
                            extractSymbols(constituents),
                            endDate,
                            0,
                            100,
                            QStringLiteral("正在加载实时数据"));
                        data = enrichRowsWithConstituentMetadata(data, constituents);
                    } else if (dataType == "policy") {
                        const QVariantList constituents = service->getIndexConstituents(symbol, snapshotDate);
                        if (constituents.isEmpty()) {
                            invokeOnMainThread(service, [symbol, snapshotDate](DataService* service) {
                                service->error(QString("无法获取指数 %1 在 %2 的成分股").arg(symbol, snapshotDate));
                            });
                            return;
                        }
                        data = service->fetchPolicyDataForSymbols(
                            extractSymbols(constituents),
                            startDate,
                            endDate,
                            options,
                            0,
                            100,
                            QStringLiteral("正在加载政策数据"));
                        data = enrichRowsWithConstituentMetadata(data, constituents);
                    } else if (dataType == "alternative") {
                        const QVariantList constituents = service->getIndexConstituents(symbol, snapshotDate);
                        if (constituents.isEmpty()) {
                            invokeOnMainThread(service, [symbol, snapshotDate](DataService* service) {
                                service->error(QString("无法获取指数 %1 在 %2 的成分股").arg(symbol, snapshotDate));
                            });
                            return;
                        }
                        data = service->fetchAlternativeDataForSymbols(
                            extractSymbols(constituents),
                            startDate,
                            endDate,
                            options,
                            0,
                            100,
                            QStringLiteral("正在加载另类数据"));
                        data = enrichRowsWithConstituentMetadata(data, constituents);
                    } else if (dataType == "derivatives") {
                        const QVariantList constituents = service->getIndexConstituents(symbol, snapshotDate);
                        if (constituents.isEmpty()) {
                            invokeOnMainThread(service, [symbol, snapshotDate](DataService* service) {
                                service->error(QString("无法获取指数 %1 在 %2 的成分股").arg(symbol, snapshotDate));
                            });
                            return;
                        }
                        data = service->fetchDerivativesDataForSymbols(
                            extractSymbols(constituents),
                            startDate,
                            endDate,
                            options,
                            0,
                            100,
                            QStringLiteral("正在加载衍生品数据"));
                        data = enrichRowsWithConstituentMetadata(data, constituents);
                    } else {
                        invokeOnMainThread(service, [dataType](DataService* service) {
                            service->error(QString("指数数据不支持的数据类型: %1").arg(dataType));
                        });
                        return;
                    }
                } else if (dataSource == "stock") {
                    invokeOnMainThread(service, [symbol, dataType](DataService* service) {
                        service->queryProgress(0, QString("正在获取 %1 的 %2 数据...").arg(symbol, dataType));
                    });
                    if (dataType.startsWith("kline_")) {
                        data = service->fetchKlineData(
                            symbol,
                            dataType,
                            startDate,
                            endDate,
                            0,
                            100,
                            QStringLiteral("正在加载%1数据").arg(describeDataTypeLabel(dataType)));
                    } else if (dataType == "historical") {
                        data = service->fetchPriceTableDataForSymbols(
                            "daily_bar",
                            symbol.trimmed().isEmpty() ? QStringList() : QStringList{symbol.trimmed()},
                            startDate,
                            endDate,
                            0,
                            100,
                            QStringLiteral("正在加载历史数据"));
                    } else if (dataType == "realtime") {
                        data = service->fetchRealtimeDataForSymbols(
                            symbol.trimmed().isEmpty() ? QStringList() : QStringList{symbol.trimmed()},
                            endDate,
                            0,
                            100,
                            QStringLiteral("正在加载实时数据"));
                    } else if (dataType == "financial") {
                        data = service->fetchFinancialDataForSymbols(
                            symbol.trimmed().isEmpty() ? QStringList() : QStringList{symbol.trimmed()},
                            startDate,
                            endDate,
                            0,
                            100,
                            QStringLiteral("正在加载财务数据"));
                    } else if (dataType == "news") {
                        data = service->fetchNewsDataForSymbols(
                            symbol.trimmed().isEmpty() ? QStringList() : QStringList{symbol.trimmed()},
                            startDate,
                            endDate,
                            0,
                            100,
                            QStringLiteral("正在加载舆情数据"));
                    } else if (dataType == "policy") {
                        data = service->fetchPolicyDataForSymbols(
                            symbol.trimmed().isEmpty() ? QStringList() : QStringList{symbol.trimmed()},
                            startDate,
                            endDate,
                            options,
                            0,
                            100,
                            QStringLiteral("正在加载政策数据"));
                    } else if (dataType == "alternative") {
                        data = service->fetchAlternativeDataForSymbols(
                            symbol.trimmed().isEmpty() ? QStringList() : QStringList{symbol.trimmed()},
                            startDate,
                            endDate,
                            options,
                            0,
                            100,
                            QStringLiteral("正在加载另类数据"));
                    } else if (dataType == "derivatives") {
                        data = service->fetchDerivativesDataForSymbols(
                            symbol.trimmed().isEmpty() ? QStringList() : QStringList{symbol.trimmed()},
                            startDate,
                            endDate,
                            options,
                            0,
                            100,
                            QStringLiteral("正在加载衍生品数据"));
                    } else if (dataType == "index_list") {
                        data = service->getAvailableIndices();
                    } else {
                        invokeOnMainThread(service, [dataType](DataService* service) {
                            service->error(QString("不支持的数据类型: %1").arg(dataType));
                        });
                        return;
                    }
                } else if (dataSource == "all_market") {
                    invokeOnMainThread(service, [dataType](DataService* service) {
                        service->queryProgress(0, QString("正在获取全市场 %1 数据...").arg(dataType));
                    });
                    if (dataType.startsWith("kline_")) {
                        data = service->fetchAllMarketKlineData(
                            dataType,
                            startDate,
                            endDate,
                            0,
                            100,
                            QStringLiteral("正在加载全市场%1数据").arg(describeDataTypeLabel(dataType)));
                    } else if (dataType == "historical") {
                        data = service->fetchAllMarketKlineData(
                            "kline_daily",
                            startDate,
                            endDate,
                            0,
                            100,
                            QStringLiteral("正在加载全市场历史数据"));
                    } else if (dataType == "realtime") {
                        data = service->fetchRealtimeDataForSymbols(
                            QStringList(),
                            endDate,
                            0,
                            100,
                            QStringLiteral("正在加载全市场实时数据"));
                    } else if (dataType == "financial") {
                        data = service->fetchFinancialDataForSymbols(
                            QStringList(),
                            startDate,
                            endDate,
                            0,
                            100,
                            QStringLiteral("正在加载全市场财务数据"));
                    } else if (dataType == "news") {
                        data = service->fetchNewsDataForSymbols(
                            QStringList(),
                            startDate,
                            endDate,
                            0,
                            100,
                            QStringLiteral("正在加载全市场舆情数据"));
                    } else if (dataType == "policy") {
                        data = service->fetchPolicyDataForSymbols(
                            QStringList(),
                            startDate,
                            endDate,
                            options,
                            0,
                            100,
                            QStringLiteral("正在加载全市场政策数据"));
                    } else if (dataType == "alternative") {
                        data = service->fetchAlternativeDataForSymbols(
                            QStringList(),
                            startDate,
                            endDate,
                            options,
                            0,
                            100,
                            QStringLiteral("正在加载全市场另类数据"));
                    } else if (dataType == "derivatives") {
                        data = service->fetchDerivativesDataForSymbols(
                            QStringList(),
                            startDate,
                            endDate,
                            options,
                            0,
                            100,
                            QStringLiteral("正在加载全市场衍生品数据"));
                    } else if (dataType == "index_list") {
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
                service->m_fetchedData = data;

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

    const QString normalizedIndexSymbol = indexSymbol.trimmed();
    QString effectiveSnapshotDate = QDate::fromString(snapshotDate, "yyyy-MM-dd").isValid()
        ? snapshotDate
        : QDate::currentDate().toString("yyyy-MM-dd");
    
    try {
        QString sql;
        std::map<QString, QVariant> params;
        
        const QString indexDisplayName = resolveIndexDisplayName(m_impl->database, normalizedIndexSymbol);

        if (normalizedIndexSymbol == "BIG_CAP" || normalizedIndexSymbol == "SMALL_CAP") {
            sql = "SELECT d.symbol AS symbol, COALESCE(si.name, d.symbol) AS name, "
                  "TRIM(COALESCE(si.industry, '')) AS industry_code "
                  "FROM daily_bar d "
                  "LEFT JOIN symbol_info si ON d.symbol = si.symbol "
                  "WHERE d.trade_date = (SELECT MAX(trade_date) FROM daily_bar WHERE trade_date <= :snapshot_date) "
                  "  AND si.asset_class = 'STOCK' AND si.status = 'ACTIVE' AND d.market_cap IS NOT NULL ";
            params[":snapshot_date"] = QVariant(effectiveSnapshotDate);
            if (normalizedIndexSymbol == "BIG_CAP") {
                sql += "ORDER BY market_cap DESC LIMIT 100";
            } else {
                sql += "ORDER BY market_cap ASC LIMIT 100";
            }
        } else {
            effectiveSnapshotDate = resolveIndexSnapshotDate(m_impl->database, normalizedIndexSymbol, effectiveSnapshotDate);

            // 真实指数快照
            sql = "SELECT constituent_symbol as symbol, COALESCE(si.name, constituent_symbol) as name, "
                  "TRIM(COALESCE(si.industry, '')) AS industry_code "
                  "FROM index_constituents ic "
                  "LEFT JOIN symbol_info si ON ic.constituent_symbol = si.symbol "
                  "WHERE ic.index_symbol = :index_symbol "
                  "  AND ic.start_date <= :snapshot_date "
                  "  AND (ic.end_date IS NULL OR ic.end_date >= :snapshot_date)";
            params[":index_symbol"] = QVariant(normalizedIndexSymbol);
            params[":snapshot_date"] = QVariant(effectiveSnapshotDate);
        }
        
        auto result = m_impl->database->executeQuery(sql, params);
        
        QVariantList constituents;
        for (const auto& row : result.getRows()) {
            QVariantMap constituent;
            constituent["symbol"] = row.getString("symbol");
            constituent["name"] = row.getString("name");
            const QString industryCode = row.getString("industry_code").trimmed();
            if (!industryCode.isEmpty()) {
                constituent["industry_code"] = industryCode;
            }
            constituent["index_symbol"] = normalizedIndexSymbol;
            constituent["index_name"] = indexDisplayName;
            constituent["index_snapshot_date"] = effectiveSnapshotDate;
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
                                                   const QString& endDate,
                                                   int progressStart,
                                                   int progressSpan,
                                                   const QString& progressLabel) {
    if (dataType == "kline_daily") {
        return fetchPriceTableDataForSymbols("daily_bar", extractSymbols(constituents), startDate, endDate, progressStart, progressSpan, progressLabel);
    }

    if (dataType == "kline_weekly") {
        if (tableExists("weekly_bar")) {
            return fetchPriceTableDataForSymbols("weekly_bar", extractSymbols(constituents), startDate, endDate, progressStart, progressSpan, progressLabel);
        }
        return fetchAggregatedKlineDataForSymbols("weekly", extractSymbols(constituents), startDate, endDate, progressStart, progressSpan, progressLabel);
    }

    if (dataType == "kline_monthly") {
        if (tableExists("monthly_bar")) {
            return fetchPriceTableDataForSymbols("monthly_bar", extractSymbols(constituents), startDate, endDate, progressStart, progressSpan, progressLabel);
        }
        return fetchAggregatedKlineDataForSymbols("monthly", extractSymbols(constituents), startDate, endDate, progressStart, progressSpan, progressLabel);
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
        const bool includeIndustryCode = tableName == QStringLiteral("daily_bar");
        QString sql = includeIndustryCode
            ? QString(
                "SELECT d.*, TRIM(COALESCE(s.industry, '')) AS industry_code "
                "FROM daily_bar d "
                "LEFT JOIN symbol_info s ON s.symbol = d.symbol "
                "WHERE d.trade_date BETWEEN :start_date AND :end_date")
            : QString("SELECT * FROM %1 WHERE trade_date BETWEEN :start_date AND :end_date")
                .arg(tableName);

        std::map<QString, QVariant> params;
        params[":start_date"] = startDate;
        params[":end_date"] = endDate;

        if (!symbol.trimmed().isEmpty()) {
            sql += includeIndustryCode ? " AND d.symbol = :symbol" : " AND symbol = :symbol";
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
                                                       const QString& endDate,
                                                       int progressStart,
                                                       int progressSpan,
                                                       const QString& progressLabel) {
    if (!m_impl->checkDatabaseConnection()) {
        return QVariantList();
    }
    if (!tableExists(tableName)) {
        throw std::runtime_error(QString("数据表不存在: %1").arg(tableName).toStdString());
    }

    try {
        const QString label = progressLabel.isEmpty()
            ? QStringLiteral("正在加载%1数据").arg(tableName)
            : progressLabel;

        return collectBatchedQueryResults(this,
                                          symbols,
                                          100,
                                          progressStart,
                                          progressSpan,
                                          label,
                                          [this, tableName, startDate, endDate](const QStringList& batchSymbols) -> QVariantList {
                                              const bool includeIndustryCode = tableName == QStringLiteral("daily_bar");
                                              QString sql = includeIndustryCode
                                                  ? QString(
                                                      "SELECT d.*, TRIM(COALESCE(s.industry, '')) AS industry_code "
                                                      "FROM daily_bar d "
                                                      "LEFT JOIN symbol_info s ON s.symbol = d.symbol "
                                                      "WHERE d.trade_date BETWEEN :start_date AND :end_date")
                                                  : QString("SELECT * FROM %1 WHERE trade_date BETWEEN :start_date AND :end_date")
                                                      .arg(tableName);

                                              if (!batchSymbols.isEmpty()) {
                                                  sql += includeIndustryCode
                                                      ? QStringLiteral(" AND d.symbol IN (%1)").arg(buildSymbolInClause(batchSymbols))
                                                      : QStringLiteral(" AND symbol IN (%1)").arg(buildSymbolInClause(batchSymbols));
                                              }

                                              sql += " ORDER BY symbol, trade_date";

                                              std::map<QString, QVariant> params;
                                              params[":start_date"] = startDate;
                                              params[":end_date"] = endDate;
                                              return convertResultToVariantList(m_impl->database->executeQuery(sql, params));
                                          });
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
                                                            const QString& endDate,
                                                            int progressStart,
                                                            int progressSpan,
                                                            const QString& progressLabel) {
    if (!m_impl->checkDatabaseConnection()) {
        return QVariantList();
    }

    const QString periodExpr = (period == "monthly")
        ? "DATE_FORMAT(trade_date, '%Y-%m')"
        : "YEARWEEK(trade_date, 1)";

    try {
        const QString label = progressLabel.isEmpty()
            ? QStringLiteral("正在加载%1数据").arg(period == "monthly" ? QStringLiteral("月线") : QStringLiteral("周线"))
            : progressLabel;

        return collectBatchedQueryResults(this,
                                          symbols,
                                          100,
                                          progressStart,
                                          progressSpan,
                                          label,
                                          [this, periodExpr, startDate, endDate](const QStringList& batchSymbols) -> QVariantList {
                                              const QString batchFilter = batchSymbols.isEmpty()
                                                  ? QString()
                                                  : QString(" AND symbol IN (%1)").arg(buildSymbolInClause(batchSymbols));

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
                                                  .arg(periodExpr, batchFilter);

                                              std::map<QString, QVariant> params;
                                              params[":start_date"] = startDate;
                                              params[":end_date"] = endDate;
                                              return convertResultToVariantList(m_impl->database->executeQuery(sql, params));
                                          });
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
                                        const QString& endDate,
                                        int progressStart,
                                        int progressSpan,
                                        const QString& progressLabel) {
    if (dataType == "kline_daily") {
        return fetchPriceTableDataForSymbols("daily_bar", symbol.trimmed().isEmpty() ? QStringList() : QStringList{symbol.trimmed()}, startDate, endDate, progressStart, progressSpan, progressLabel);
    }

    if (dataType == "kline_weekly") {
        if (tableExists("weekly_bar")) {
            return fetchPriceTableDataForSymbols("weekly_bar", symbol.trimmed().isEmpty() ? QStringList() : QStringList{symbol.trimmed()}, startDate, endDate, progressStart, progressSpan, progressLabel);
        }
        return fetchAggregatedKlineDataForSymbols("weekly", symbol.trimmed().isEmpty() ? QStringList() : QStringList{symbol.trimmed()}, startDate, endDate, progressStart, progressSpan, progressLabel);
    }

    if (dataType == "kline_monthly") {
        if (tableExists("monthly_bar")) {
            return fetchPriceTableDataForSymbols("monthly_bar", symbol.trimmed().isEmpty() ? QStringList() : QStringList{symbol.trimmed()}, startDate, endDate, progressStart, progressSpan, progressLabel);
        }
        return fetchAggregatedKlineDataForSymbols("monthly", symbol.trimmed().isEmpty() ? QStringList() : QStringList{symbol.trimmed()}, startDate, endDate, progressStart, progressSpan, progressLabel);
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
                                                 const QString& endDate,
                                                 int progressStart,
                                                 int progressSpan,
                                                 const QString& progressLabel) {
    if (dataType == "kline_daily") {
        return fetchPriceTableDataForSymbols("daily_bar", QStringList(), startDate, endDate, progressStart, progressSpan, progressLabel);
    }

    if (dataType == "kline_weekly") {
        if (tableExists("weekly_bar")) {
            return fetchPriceTableDataForSymbols("weekly_bar", QStringList(), startDate, endDate, progressStart, progressSpan, progressLabel);
        }
        return fetchAggregatedKlineDataForSymbols("weekly", QStringList(), startDate, endDate, progressStart, progressSpan, progressLabel);
    }

    if (dataType == "kline_monthly") {
        if (tableExists("monthly_bar")) {
            return fetchPriceTableDataForSymbols("monthly_bar", QStringList(), startDate, endDate, progressStart, progressSpan, progressLabel);
        }
        return fetchAggregatedKlineDataForSymbols("monthly", QStringList(), startDate, endDate, progressStart, progressSpan, progressLabel);
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
                                                      const QString& endDate,
                                                      int progressStart,
                                                      int progressSpan,
                                                      const QString& progressLabel) {
    if (!m_impl->checkDatabaseConnection()) {
        return QVariantList();
    }
    if (!tableExists("financial_indicator")) {
        throw std::runtime_error("数据表不存在: financial_indicator");
    }

    try {
        const QString baseSql =
            "SELECT fi.*, si.symbol "
            "FROM financial_indicator fi "
            "JOIN symbol_info si ON si.symbol_id = fi.symbol_id "
            "WHERE fi.report_date BETWEEN :start_date AND :end_date";

        std::map<QString, QVariant> params;
        params[":start_date"] = startDate;
        params[":end_date"] = endDate;

        return collectBatchedQueryResults(this,
                                          symbols,
                                          100,
                                          progressStart,
                                          progressSpan,
                                          progressLabel.isEmpty() ? QStringLiteral("正在加载财务数据") : progressLabel,
                                          [this, baseSql, params](const QStringList& batchSymbols) -> QVariantList {
                                              QString batchSql = baseSql;
                                              if (!batchSymbols.isEmpty()) {
                                                  batchSql += QString(" AND si.symbol IN (%1)").arg(buildSymbolInClause(batchSymbols));
                                              }

                                              batchSql += " ORDER BY si.symbol, fi.report_date, fi.report_type";
                                              return convertResultToVariantList(m_impl->database->executeQuery(batchSql, params));
                                          });
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
                                                     const QString& endDate,
                                                     int progressStart,
                                                     int progressSpan,
                                                     const QString& progressLabel) {
    if (!m_impl->checkDatabaseConnection()) {
        return QVariantList();
    }
    if (!tableExists("daily_bar")) {
        throw std::runtime_error("数据表不存在: daily_bar");
    }

    try {
        const QString label = progressLabel.isEmpty()
            ? QStringLiteral("正在加载实时数据")
            : progressLabel;

        return collectBatchedQueryResults(this,
                                          symbols,
                                          100,
                                          progressStart,
                                          progressSpan,
                                          label,
                                          [this, endDate](const QStringList& batchSymbols) -> QVariantList {
                                              QString symbolFilter;
                                              if (!batchSymbols.isEmpty()) {
                                                  symbolFilter = QString(" AND d.symbol IN (%1)").arg(buildSymbolInClause(batchSymbols));
                                              }

                                              const QString sql = QString(
                                                  "SELECT d.*, TRIM(COALESCE(s.industry, '')) AS industry_code "
                                                  "FROM daily_bar d "
                                                  "LEFT JOIN symbol_info s ON s.symbol = d.symbol "
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
                                          });
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
                                                 const QString& endDate,
                                                 int progressStart,
                                                 int progressSpan,
                                                 const QString& progressLabel) {
    return fetchGenericTimeSeriesData(
        resolveNewsTable(),
        symbols,
        startDate,
        endDate,
    QStringList{"trade_date", "publish_time", "pub_time", "created_at", "date"},
    QStringList{"symbol", "stock_code", "code", "ticker"},
        progressStart,
        progressSpan,
        progressLabel);
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
                                                   const QVariantMap& options,
                                                   int progressStart,
                                                   int progressSpan,
                                                   const QString& progressLabel) {
    Q_UNUSED(options)
    return fetchGenericTimeSeriesData(
        QStringLiteral("policy_data"),
        symbols,
        startDate,
        endDate,
    QStringList{"trade_date", "publish_time", "created_at", "date"},
    QStringList{"symbol", "stock_code", "code", "ticker"},
        progressStart,
        progressSpan,
        progressLabel);
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
                                                        const QVariantMap& options,
                                                        int progressStart,
                                                        int progressSpan,
                                                        const QString& progressLabel) {
    Q_UNUSED(options)
    return fetchGenericTimeSeriesData(
        QStringLiteral("alternative_data"),
        symbols,
        startDate,
        endDate,
    QStringList{"trade_date", "created_at", "publish_time", "date"},
    QStringList{"symbol", "stock_code", "code", "ticker"},
        progressStart,
        progressSpan,
        progressLabel);
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
                                                        const QVariantMap& options,
                                                        int progressStart,
                                                        int progressSpan,
                                                        const QString& progressLabel) {
    Q_UNUSED(options)
    return fetchGenericTimeSeriesData(
        QStringLiteral("derivatives_data"),
        symbols,
        startDate,
        endDate,
    QStringList{"trade_date", "created_at", "publish_time", "date"},
    QStringList{"symbol", "underlying_symbol", "stock_code", "code", "ticker"},
        progressStart,
        progressSpan,
        progressLabel);
}

QVariantList DataService::fetchGenericTimeSeriesData(const QString& tableName,
                                                    const QStringList& symbols,
                                                    const QString& startDate,
                                                    const QString& endDate,
                                                    const QStringList& dateColumns,
                                                    const QStringList& symbolColumns,
                                                    int progressStart,
                                                    int progressSpan,
                                                    const QString& progressLabel) {
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
    auto normalizeFetchedRows = [&dateColumn, &symbolColumn](QVariantList rows) -> QVariantList {
        if (dateColumn.isEmpty() && symbolColumn.isEmpty()) {
            return rows;
        }

        for (QVariant& rowValue : rows) {
            QVariantMap row = rowValue.toMap();
            if (row.isEmpty()) {
                continue;
            }

            if (!dateColumn.isEmpty()) {
                const QVariant resolvedDateValue = row.value(dateColumn);
                if (resolvedDateValue.isValid() && !resolvedDateValue.isNull()) {
                    const QString canonicalTradeDate = row.value(QStringLiteral("trade_date")).toString().trimmed();
                    if (canonicalTradeDate.isEmpty()) {
                        row.insert(QStringLiteral("trade_date"), resolvedDateValue);
                    }
                }
            }

            if (!symbolColumn.isEmpty()) {
                const QVariant resolvedSymbolValue = row.value(symbolColumn);
                if (resolvedSymbolValue.isValid() && !resolvedSymbolValue.isNull()) {
                    const QString canonicalSymbol = row.value(QStringLiteral("symbol")).toString().trimmed();
                    if (canonicalSymbol.isEmpty()) {
                        row.insert(QStringLiteral("symbol"), resolvedSymbolValue);
                    }
                }
            }
            rowValue = row;
        }

        return rows;
    };

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
        return normalizeFetchedRows(executeVariantQueryForFetch(sql, params));
    };

    if (!symbols.isEmpty() && !symbolColumn.isEmpty()) {
        const bool useBatches = symbols.size() > 1;
        if (useBatches) {
            const QString label = progressLabel.isEmpty()
                ? QStringLiteral("正在加载%1数据").arg(normalizedTable)
                : progressLabel;

            return collectBatchedQueryResults(this,
                                              symbols,
                                              100,
                                              progressStart,
                                              progressSpan,
                                              label,
                                              [&](const QStringList& batchSymbols) -> QVariantList {
                                                  return runQuery(QString("%1 IN (%2)").arg(symbolColumn, buildSymbolInClause(batchSymbols)));
                                              });
        }

        return executeSingleQueryWithProgress(this,
                                              progressStart,
                                              progressSpan,
                                              progressLabel.isEmpty()
                                                  ? QStringLiteral("正在加载%1数据").arg(normalizedTable)
                                                  : progressLabel,
                                              [&]() -> QVariantList {
                                                  return runQuery(QString("%1 IN (%2)").arg(symbolColumn, buildSymbolInClause(symbols)));
                                              });
    }

    return executeSingleQueryWithProgress(this,
                                          progressStart,
                                          progressSpan,
                                          progressLabel.isEmpty()
                                              ? QStringLiteral("正在加载%1数据").arg(normalizedTable)
                                              : progressLabel,
                                          [&]() -> QVariantList {
                                              return runQuery(QString());
                                          });
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

    if (!m_impl || !m_impl->database || tableName.trimmed().isEmpty()) {
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

    if (!m_impl || !m_impl->database || tableName.trimmed().isEmpty() || columnName.trimmed().isEmpty()) {
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
    static const QStringList newsTableCandidates{
        QStringLiteral("news_sentiment"),
        QStringLiteral("stock_news"),
        QStringLiteral("news_data"),
        QStringLiteral("news")
    };

    for (const QString& candidate : newsTableCandidates) {
        if (tableExists(candidate)) {
            return candidate;
        }
    }

    return QString();
}
   