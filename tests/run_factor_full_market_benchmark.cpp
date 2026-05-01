#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <future>
#include <memory>

#include "cache/include/cache_facade.h"
#include "DatabaseConnectionManager.h"
#include "foundation.h"
#include "domain/factor/include/DataAvailabilityChecker.h"
#include "domain/factor/include/FactorBacktestExecutor.h"
#include "domain/factor/include/FactorCacheManager.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include "FactorBacktestWarmupUtils.h"

namespace {

struct BenchmarkOptions {
    QString instanceId;
    int years = 5;
    int daysLimit = 0;
    int forwardDays = 1;
    int rebalanceDays = 1;
    int numGroups = 10;
    int threads = 8;
    int symbolLimit = 0;
    double transactionCost = 0.001;
    double slippageRate = 0.0;
    double riskFreeRate = 0.0;
    double maxHours = 2.0;
    QString benchmarkSymbol = QStringLiteral("000300.SH");
    QString outputPath;
    bool disableDateParallelism = false;
    bool forceColdRun = false;
};

QString detectRepoRoot()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 8; ++depth) {
        if (dir.exists(QStringLiteral("CMakeLists.txt")) && dir.exists(QStringLiteral("src"))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QDir::currentPath();
}

QStringList fetchActiveStockSymbols(const std::shared_ptr<astock::database::QtMySQLDatabase>& database)
{
    QStringList symbols;
    if (!database) {
        return symbols;
    }

    const auto result = database->executeQuery(
        QStringLiteral("SELECT symbol FROM symbol_info WHERE asset_class = 'STOCK' AND status = 'ACTIVE' ORDER BY symbol"),
        {});

    symbols.reserve(static_cast<int>(result.rowCount()));
    for (size_t index = 0; index < result.rowCount(); ++index) {
        const QString symbol = result.getRow(index).getString(QStringLiteral("symbol")).trimmed().toUpper();
        if (!symbol.isEmpty()) {
            symbols.push_back(symbol);
        }
    }
    return symbols;
}

QStringList fetchCoveredStockSymbols(const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                                    const QString& startDate,
                                    const QString& endDate,
                                    int limit)
{
    QStringList symbols;
    if (!database || startDate.trimmed().isEmpty() || endDate.trimmed().isEmpty()) {
        return symbols;
    }

    QString sql = QStringLiteral(
        "SELECT db.symbol AS symbol, COUNT(*) AS row_count "
        "FROM daily_bar db "
        "JOIN symbol_info si ON si.symbol = db.symbol "
        "WHERE si.asset_class = 'STOCK' AND si.status = 'ACTIVE' "
        "AND db.trade_date BETWEEN :start_date AND :end_date "
        "AND db.close > 0 "
        "GROUP BY db.symbol "
        "HAVING COUNT(*) >= ("
        "    SELECT COUNT(DISTINCT trade_date) "
        "    FROM daily_bar "
        "    WHERE trade_date BETWEEN :start_date AND :end_date"
        ") "
        "ORDER BY row_count DESC, db.symbol ASC");
    if (limit > 0) {
        sql += QStringLiteral(" LIMIT %1").arg(limit);
    }

    const auto result = database->executeQuery(
        sql,
        {{QStringLiteral(":start_date"), startDate},
         {QStringLiteral(":end_date"), endDate}});

    symbols.reserve(static_cast<int>(result.rowCount()));
    for (size_t index = 0; index < result.rowCount(); ++index) {
        const QString symbol = result.getRow(index).getString(QStringLiteral("symbol")).trimmed().toUpper();
        if (!symbol.isEmpty()) {
            symbols.push_back(symbol);
        }
    }
    return symbols;
}

QString fetchLatestTradeDate(const std::shared_ptr<astock::database::QtMySQLDatabase>& database)
{
    if (!database) {
        return {};
    }

    const auto result = database->executeQuery(
        QStringLiteral("SELECT MAX(trade_date) AS trade_date FROM daily_bar WHERE trade_date IS NOT NULL"),
        {});
    if (result.isEmpty()) {
        return {};
    }

    return result.getRow(0).getString(QStringLiteral("trade_date")).trimmed();
}

QString fetchBenchmarkInstanceId(const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                                 const std::shared_ptr<factor::DataAvailabilityChecker>& dataChecker,
                                 const QString& requestedInstanceId,
                                 const QString& startDate,
                                 const QString& endDate)
{
    if (!requestedInstanceId.trimmed().isEmpty()) {
        return requestedInstanceId.trimmed();
    }

    (void)dataChecker;
    (void)startDate;
    (void)endDate;

    if (!database) {
        return {};
    }

    const auto result = database->executeQuery(
        QStringLiteral(
            "SELECT fi.instance_id AS instance_id, "
            "LOWER(COALESCE(JSON_UNQUOTE(JSON_EXTRACT(fi.full_config, '$.factorType')), "
            "              JSON_UNQUOTE(JSON_EXTRACT(fi.full_config, '$.factor_type')), '')) AS factor_type, "
            "COALESCE(JSON_UNQUOTE(JSON_EXTRACT(fi.full_config, '$.displayName')), "
            "         JSON_UNQUOTE(JSON_EXTRACT(fi.full_config, '$.factorName')), '') AS display_name "
            "FROM factor_instance fi "
            "WHERE fi.status = 'ACTIVE' "
            "ORDER BY fi.created_at DESC "
            "LIMIT 100"),
        {});

    if (result.isEmpty()) {
        return {};
    }

    QString fallbackInstanceId;
    const QStringList preferredTypes = {
        QStringLiteral("technical"),
        QStringLiteral("size"),
        QStringLiteral("value"),
        QStringLiteral("liquidity"),
        QStringLiteral("quality"),
        QStringLiteral("momentum"),
        QStringLiteral("dividend"),
        QStringLiteral("low_volatility"),
        QStringLiteral("industry"),
        QStringLiteral("growth"),
        QStringLiteral("macro"),
        QStringLiteral("sentiment"),
        QStringLiteral("custom")
    };

    for (const QString& preferredType : preferredTypes) {
        for (size_t index = 0; index < result.rowCount(); ++index) {
            const auto row = result.getRow(index);
            const QString instanceId = row.getString(QStringLiteral("instance_id")).trimmed();
            const QString factorType = row.getString(QStringLiteral("factor_type")).trimmed().toLower();
            if (instanceId.isEmpty() || factorType.isEmpty()) {
                continue;
            }

            if (fallbackInstanceId.isEmpty()) {
                fallbackInstanceId = instanceId;
            }

            if (factorType != preferredType) {
                continue;
            }

            return instanceId;
        }
    }

    return fallbackInstanceId;
}

QStringList loadHistoricalTradeDates(const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                                     const QString& anchorStartDate,
                                     const QStringList& stockCodes)
{
    QStringList tradeDates;
    if (!database || anchorStartDate.trimmed().isEmpty()) {
        return tradeDates;
    }

    const QString sql = QStringLiteral(
        "SELECT DISTINCT trade_date FROM daily_bar "
        "WHERE trade_date < :anchorStartDate AND close > 0 "
        "ORDER BY trade_date ASC");

    const auto result = database->executeQuery(
        sql,
        {{QStringLiteral(":anchorStartDate"), anchorStartDate}});
    tradeDates.reserve(static_cast<int>(result.rowCount()));
    for (size_t rowIndex = 0; rowIndex < result.rowCount(); ++rowIndex) {
        const QString tradeDate = result.getRow(rowIndex).getString(QStringLiteral("trade_date")).trimmed();
        if (!tradeDate.isEmpty()) {
            tradeDates.append(tradeDate);
        }
    }
    tradeDates.removeDuplicates();
    return tradeDates;
}

struct WarmupRequirement {
    int minDataPoints = 0;
    int skipRecent = 0;
};

WarmupRequirement loadWarmupRequirementFromConfigText(const QString& configText)
{
    WarmupRequirement requirement;
    if (configText.trimmed().isEmpty()) {
        return requirement;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(configText.toUtf8());
    if (!doc.isObject()) {
        return requirement;
    }

    const QJsonObject config = doc.object();
    const QJsonObject calculation = config.value(QStringLiteral("calculation")).toObject();
    const QJsonObject boundaryRules = config.value(QStringLiteral("boundary_rules")).toObject();

    requirement.minDataPoints = boundaryRules.value(QStringLiteral("min_data_points")).toInt(
        calculation.value(QStringLiteral("window")).toInt(
            calculation.value(QStringLiteral("lookback_period")).toInt(
                calculation.value(QStringLiteral("lookbackPeriod")).toInt(0))));
    requirement.skipRecent = calculation.value(QStringLiteral("skip_recent")).toInt(
        calculation.value(QStringLiteral("skipRecent")).toInt(0));
    return requirement;
}

QString resolveBenchmarkStartDate(const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                                 const QString& requestedStartDate,
                                 const QStringList& stockCodes,
                                 const QString& instanceId)
{
    if (!database || requestedStartDate.trimmed().isEmpty() || instanceId.trimmed().isEmpty()) {
        return requestedStartDate;
    }

    qDebug() << "[benchmark] step=resolveBenchmarkStartDate.loadInstanceConfig begin";
    const auto instanceInfo = database->executeQuery(
        QStringLiteral("SELECT CAST(full_config AS CHAR) AS full_config FROM factor_instance WHERE instance_id = :instance_id LIMIT 1"),
        {{QStringLiteral(":instance_id"), instanceId}});
    qDebug() << "[benchmark] step=resolveBenchmarkStartDate.loadInstanceConfig end";
    if (instanceInfo.isEmpty()) {
        return requestedStartDate;
    }

    const QString fullConfig = instanceInfo.getRow(0).getString(QStringLiteral("full_config")).trimmed();
    const WarmupRequirement requirement = loadWarmupRequirementFromConfigText(fullConfig);
    const int requiredTradingDays = factor::warmup::requiredWarmupTradingDays(requirement.minDataPoints, requirement.skipRecent);
    if (requiredTradingDays <= 0) {
        return requestedStartDate;
    }

    const QDate anchorStartDate = QDate::fromString(requestedStartDate, QStringLiteral("yyyy-MM-dd"));
    if (!anchorStartDate.isValid()) {
        return requestedStartDate;
    }

    const int lookbackCalendarDays = factor::warmup::fallbackWarmupCalendarLookbackDays(requiredTradingDays);
    return anchorStartDate.addDays(-lookbackCalendarDays).toString(QStringLiteral("yyyy-MM-dd"));
}

int countTradeDates(const std::shared_ptr<astock::database::QtMySQLDatabase>& database,
                    const QString& startDate,
                    const QString& endDate)
{
    if (!database) {
        return 0;
    }

    const auto result = database->executeQuery(
        QStringLiteral("SELECT COUNT(DISTINCT trade_date) AS trade_date_count FROM daily_bar WHERE trade_date BETWEEN :start_date AND :end_date"),
        {{QStringLiteral(":start_date"), startDate},
         {QStringLiteral(":end_date"), endDate}});

    if (result.isEmpty()) {
        return 0;
    }

    return result.getRow(0).getInt(QStringLiteral("trade_date_count"));
}

QString defaultOutputPath(const QString& repoRoot, const QString& instanceId)
{
    const QString outputDir = QDir(repoRoot).filePath(QStringLiteral("build/tests/full_market_benchmark"));
    QDir().mkpath(outputDir);
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString fileName = QStringLiteral("%1_%2.json").arg(instanceId, timestamp);
    return QDir(outputDir).filePath(fileName);
}

bool writeJsonFile(const QString& filePath, const QVariantMap& payload)
{
    const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
    QDir().mkpath(QFileInfo(absolutePath).absolutePath());

    QFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromVariant(payload);
    file.write(document.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool readJsonFile(const QString& filePath, QVariantMap& payload)
{
    QFile file(filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject()) {
        return false;
    }

    payload = document.object().toVariantMap();
    return true;
}

QString buildBenchmarkCacheSignature(const QString& instanceId,
                                     const QString& startDate,
                                     const QString& endDate,
                                     int forwardDays,
                                     int rebalanceDays,
                                     int numGroups,
                                     double transactionCost,
                                     double slippageRate,
                                     double riskFreeRate,
                                     const QString& benchmarkSymbol,
                                     bool dateParallelism,
                                     int tradeDateCount,
                                     const QStringList& activeSymbols)
{
    const QByteArray activeSymbolBytes = activeSymbols.join(QStringLiteral("|")).toUtf8();
    const QString activeSymbolFingerprint = QString::fromUtf8(
        QCryptographicHash::hash(activeSymbolBytes, QCryptographicHash::Sha256).toHex());

    const QStringList parts = {
        instanceId,
        startDate,
        endDate,
        QString::number(forwardDays),
        QString::number(rebalanceDays),
        QString::number(numGroups),
        QString::number(transactionCost, 'f', 8),
        QString::number(slippageRate, 'f', 8),
        QString::number(riskFreeRate, 'f', 8),
        benchmarkSymbol,
        dateParallelism ? QStringLiteral("1") : QStringLiteral("0"),
        QString::number(tradeDateCount),
        activeSymbolFingerprint
    };

    return QString::fromUtf8(QCryptographicHash::hash(parts.join(QStringLiteral("|")).toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString benchmarkCachePath(const QString& repoRoot, const QString& cacheSignature)
{
    const QString cacheDir = QDir(repoRoot).filePath(QStringLiteral("build/tests/full_market_benchmark/cache"));
    QDir().mkpath(cacheDir);
    return QDir(cacheDir).filePath(cacheSignature + QStringLiteral(".json"));
}

QString sanitizeCacheComponent(const QString& value)
{
    QString sanitized;
    sanitized.reserve(value.size());
    for (const QChar ch : value.trimmed()) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-')) {
            sanitized.append(ch);
        } else {
            sanitized.append(QLatin1Char('_'));
        }
    }
    if (sanitized.isEmpty()) {
        return QStringLiteral("unknown");
    }
    return sanitized;
}

QString benchmarkStableCachePath(const QString& repoRoot,
                                 const QString& instanceId,
                                 const QString& startDate,
                                 const QString& endDate,
                                 int forwardDays,
                                 int rebalanceDays,
                                 int numGroups,
                                 const QString& benchmarkSymbol,
                                 bool dateParallelism)
{
    const QString cacheDir = QDir(repoRoot).filePath(QStringLiteral("build/tests/full_market_benchmark/cache"));
    QDir().mkpath(cacheDir);

    const QString fileName = QStringLiteral("stable__%1__%2__%3__f%4__r%5__g%6__%7__dp%8.json")
        .arg(sanitizeCacheComponent(instanceId),
             sanitizeCacheComponent(startDate),
             sanitizeCacheComponent(endDate),
             QString::number(forwardDays),
             QString::number(rebalanceDays),
             QString::number(numGroups),
             sanitizeCacheComponent(benchmarkSymbol),
             dateParallelism ? QStringLiteral("1") : QStringLiteral("0"));

    return QDir(cacheDir).filePath(fileName);
}

void printLine(const QString& line)
{
    QTextStream(stdout) << line << Qt::endl;
}

void printKeyValue(const QString& key, const QVariant& value)
{
    printLine(QStringLiteral("[benchmark] %1=%2").arg(key, value.toString()));
}

BenchmarkOptions parseOptions(QCoreApplication& app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AStock factor full-market benchmark"));
    parser.addHelpOption();

    QCommandLineOption instanceOption({QStringLiteral("i"), QStringLiteral("instance-id")},
                                      QStringLiteral("Factor instance id to benchmark"),
                                      QStringLiteral("instanceId"));
    QCommandLineOption yearsOption({QStringLiteral("y"), QStringLiteral("years")},
                                   QStringLiteral("Benchmark window in years"),
                                   QStringLiteral("years"),
                                   QStringLiteral("5"));
    QCommandLineOption daysLimitOption({QStringLiteral("days-limit")},
                                       QStringLiteral("Limit the benchmark window to the last N days"),
                                       QStringLiteral("days"),
                                       QStringLiteral("0"));
    QCommandLineOption forwardDaysOption({QStringLiteral("f"), QStringLiteral("forward-days")},
                                         QStringLiteral("Forward return days"),
                                         QStringLiteral("days"),
                                         QStringLiteral("1"));
    QCommandLineOption rebalanceDaysOption({QStringLiteral("r"), QStringLiteral("rebalance-days")},
                                           QStringLiteral("Rebalance interval in trading days"),
                                           QStringLiteral("days"),
                                           QStringLiteral("1"));
    QCommandLineOption groupsOption({QStringLiteral("g"), QStringLiteral("groups")},
                                    QStringLiteral("Number of groups"),
                                    QStringLiteral("count"),
                                    QStringLiteral("10"));
    QCommandLineOption threadsOption({QStringLiteral("t"), QStringLiteral("threads")},
                                     QStringLiteral("Thread pool size"),
                                     QStringLiteral("count"),
                                     QStringLiteral("8"));
    QCommandLineOption symbolLimitOption({QStringLiteral("symbol-limit")},
                                         QStringLiteral("Limit active stock symbols for a shorter benchmark slice"),
                                         QStringLiteral("count"),
                                         QStringLiteral("0"));
    QCommandLineOption transactionCostOption({QStringLiteral("transaction-cost")},
                                             QStringLiteral("Transaction cost"),
                                             QStringLiteral("ratio"),
                                             QStringLiteral("0.001"));
    QCommandLineOption slippageOption({QStringLiteral("slippage")},
                                      QStringLiteral("Slippage rate"),
                                      QStringLiteral("ratio"),
                                      QStringLiteral("0.0"));
    QCommandLineOption benchmarkSymbolOption({QStringLiteral("benchmark-symbol")},
                                             QStringLiteral("Benchmark symbol"),
                                             QStringLiteral("symbol"),
                                             QStringLiteral("000300.SH"));
    QCommandLineOption maxHoursOption({QStringLiteral("max-hours")},
                                      QStringLiteral("Maximum allowed wall-clock hours"),
                                      QStringLiteral("hours"),
                                      QStringLiteral("2.0"));
    QCommandLineOption outputOption({QStringLiteral("o"), QStringLiteral("output")},
                                    QStringLiteral("Output JSON file"),
                                    QStringLiteral("path"));
    QCommandLineOption disableDateParallelismOption(QStringLiteral("disable-date-parallelism"),
                                                    QStringLiteral("Disable date-level parallelism"));
    QCommandLineOption forceColdRunOption(QStringLiteral("cold-run"),
                                          QStringLiteral("Bypass benchmark summary cache and disable factor cache backend"));

    parser.addOption(instanceOption);
    parser.addOption(yearsOption);
    parser.addOption(daysLimitOption);
    parser.addOption(forwardDaysOption);
    parser.addOption(rebalanceDaysOption);
    parser.addOption(groupsOption);
    parser.addOption(threadsOption);
    parser.addOption(symbolLimitOption);
    parser.addOption(transactionCostOption);
    parser.addOption(slippageOption);
    parser.addOption(benchmarkSymbolOption);
    parser.addOption(maxHoursOption);
    parser.addOption(outputOption);
    parser.addOption(disableDateParallelismOption);
    parser.addOption(forceColdRunOption);
    parser.process(app);

    BenchmarkOptions options;
    options.instanceId = parser.value(instanceOption).trimmed();
    options.years = qMax(1, parser.value(yearsOption).toInt());
    options.daysLimit = qMax(0, parser.value(daysLimitOption).toInt());
    options.forwardDays = qMax(1, parser.value(forwardDaysOption).toInt());
    options.rebalanceDays = qMax(1, parser.value(rebalanceDaysOption).toInt());
    options.numGroups = qMax(2, parser.value(groupsOption).toInt());
    options.threads = qMax(1, parser.value(threadsOption).toInt());
    options.symbolLimit = qMax(0, parser.value(symbolLimitOption).toInt());
    options.transactionCost = parser.value(transactionCostOption).toDouble();
    options.slippageRate = parser.value(slippageOption).toDouble();
    options.benchmarkSymbol = parser.value(benchmarkSymbolOption).trimmed().isEmpty()
        ? QStringLiteral("000300.SH")
        : parser.value(benchmarkSymbolOption).trimmed().toUpper();
    options.maxHours = qMax(0.1, parser.value(maxHoursOption).toDouble());
    options.outputPath = parser.value(outputOption).trimmed();
    options.disableDateParallelism = parser.isSet(disableDateParallelismOption);
    options.forceColdRun = parser.isSet(forceColdRunOption);
    return options;
}

void populateActiveStocks(factor::BacktestConfig& config, const QStringList& symbols)
{
    config.allowedStockCodes.reserve(static_cast<size_t>(symbols.size()));
    for (const QString& symbol : symbols) {
        const QString normalized = symbol.trimmed().toUpper();
        if (!normalized.isEmpty()) {
            config.allowedStockCodes.push_back(normalized.toStdString());
        }
    }
}

[[noreturn]] void terminateBenchmarkSuccess()
{
    QTextStream(stdout).flush();
    std::quick_exit(0);
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("astockquant_factor_full_market_benchmark"));

    const BenchmarkOptions options = parseOptions(app);
    const QString repoRoot = detectRepoRoot();
    QDir::setCurrent(repoRoot);

    printKeyValue(QStringLiteral("repoRoot"), repoRoot);

    foundation::Config foundationConfig;
    foundationConfig.profile = "development";
    foundationConfig.config_dir = QDir(repoRoot).filePath(QStringLiteral("config")).toStdString();
    foundationConfig.enable_console_log = true;
    foundationConfig.enable_file_log = false;
    foundationConfig.thread_pool_size = static_cast<size_t>(options.threads);
    if (!foundation::Foundation::instance().initialize(foundationConfig)) {
        printLine(QStringLiteral("[benchmark] foundation_initialize_failed"));
        return 1;
    }

    if (!astock::database::DatabaseConnectionManager::instance().initialize()) {
        printLine(QStringLiteral("[benchmark] database_initialize_failed"));
        return 2;
    }

    auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        printLine(QStringLiteral("[benchmark] database_unavailable"));
        return 3;
    }

    auto dataChecker = std::make_shared<factor::DataAvailabilityChecker>(database);

    printLine(QStringLiteral("[benchmark] step=fetchLatestTradeDate begin"));
    const QString latestTradeDate = fetchLatestTradeDate(database);
    printLine(QStringLiteral("[benchmark] step=fetchLatestTradeDate end"));
    if (latestTradeDate.isEmpty()) {
        printLine(QStringLiteral("[benchmark] latest_trade_date_not_found"));
        return 4;
    }

    const QDate endDate = QDate::fromString(latestTradeDate, QStringLiteral("yyyy-MM-dd"));
    if (!endDate.isValid()) {
        printLine(QStringLiteral("[benchmark] latest_trade_date_invalid=") + latestTradeDate);
        return 5;
    }

    const QDate requestedStartDate = options.daysLimit > 0 ? endDate.addDays(-options.daysLimit) : endDate.addYears(-options.years);
    QDate startDate = requestedStartDate;
    if (options.daysLimit > 0) {
        startDate = requestedStartDate;
    }
    if (!startDate.isValid()) {
        printLine(QStringLiteral("[benchmark] start_date_invalid"));
        return 6;
    }

    printLine(QStringLiteral("[benchmark] step=fetchActiveStockSymbols begin"));
    const QStringList activeSymbols = fetchActiveStockSymbols(database);
    printLine(QStringLiteral("[benchmark] step=fetchActiveStockSymbols end"));
    if (activeSymbols.isEmpty()) {
        printLine(QStringLiteral("[benchmark] active_stock_universe_empty"));
        return 7;
    }

    QStringList benchmarkSymbols = activeSymbols;

    printLine(QStringLiteral("[benchmark] step=fetchBenchmarkInstanceId begin"));
    const QString instanceId = fetchBenchmarkInstanceId(
        database,
        dataChecker,
        options.instanceId,
        startDate.toString(QStringLiteral("yyyy-MM-dd")),
        endDate.toString(QStringLiteral("yyyy-MM-dd")));
    printLine(QStringLiteral("[benchmark] step=fetchBenchmarkInstanceId end"));
    if (instanceId.isEmpty()) {
        printLine(QStringLiteral("[benchmark] benchmark_instance_not_found"));
        return 8;
    }

    printLine(QStringLiteral("[benchmark] step=resolveBenchmarkStartDate begin"));
    const QString effectiveStartDate = resolveBenchmarkStartDate(
        database,
        startDate.toString(QStringLiteral("yyyy-MM-dd")),
        benchmarkSymbols,
        instanceId);
    printLine(QStringLiteral("[benchmark] step=resolveBenchmarkStartDate end"));
    if (!effectiveStartDate.isEmpty()) {
        const QDate resolvedStartDate = QDate::fromString(effectiveStartDate, QStringLiteral("yyyy-MM-dd"));
        if (resolvedStartDate.isValid()) {
            startDate = resolvedStartDate;
        }
    }

    benchmarkSymbols = activeSymbols;
    if (options.symbolLimit > 0 && benchmarkSymbols.size() > options.symbolLimit) {
        benchmarkSymbols = benchmarkSymbols.mid(0, options.symbolLimit);
    }

    printLine(QStringLiteral("[benchmark] step=countTradeDates begin"));
    const int tradeDateCount = countTradeDates(
        database,
        startDate.toString(QStringLiteral("yyyy-MM-dd")),
        endDate.toString(QStringLiteral("yyyy-MM-dd")));
    printLine(QStringLiteral("[benchmark] step=countTradeDates end"));

    QString benchmarkFactorType;
    QString benchmarkFactorName;
    {
        printLine(QStringLiteral("[benchmark] step=loadBenchmarkFactorConfig begin"));
        const auto instanceInfo = database->executeQuery(
            QStringLiteral("SELECT full_config FROM factor_instance WHERE instance_id = :instance_id LIMIT 1"),
            {{QStringLiteral(":instance_id"), instanceId}});
        printLine(QStringLiteral("[benchmark] step=loadBenchmarkFactorConfig end"));
        if (!instanceInfo.isEmpty()) {
            const QString fullConfig = instanceInfo.getRow(0).getString(QStringLiteral("full_config")).trimmed();
            const QJsonDocument document = QJsonDocument::fromJson(fullConfig.toUtf8());
            if (document.isObject()) {
                const QJsonObject config = document.object();
                benchmarkFactorType = config.value(QStringLiteral("factorType")).toString().trimmed();
                benchmarkFactorName = config.value(QStringLiteral("displayName")).toString().trimmed();
            }
        }
    }

    factor::BacktestConfig config;
    config.instanceId = instanceId.toStdString();
    config.startDate = startDate.toString(QStringLiteral("yyyy-MM-dd")).toStdString();
    config.endDate = endDate.toString(QStringLiteral("yyyy-MM-dd")).toStdString();
    config.forwardDays = options.forwardDays;
    config.rebalanceDays = options.rebalanceDays;
    config.numGroups = options.numGroups;
    config.transactionCost = options.transactionCost;
    config.slippageRate = options.slippageRate;
    config.riskFreeRate = 0.0;
    config.benchmarkSymbol = options.benchmarkSymbol.toStdString();
    config.enableDateParallelism = !options.disableDateParallelism;
    config.datasetId = -1;
    populateActiveStocks(config, benchmarkSymbols);

    printKeyValue(QStringLiteral("instanceId"), instanceId);
    if (!benchmarkFactorType.isEmpty()) {
        printKeyValue(QStringLiteral("benchmarkFactorType"), benchmarkFactorType);
    }
    if (!benchmarkFactorName.isEmpty()) {
        printKeyValue(QStringLiteral("benchmarkFactorName"), benchmarkFactorName);
    }
    printKeyValue(QStringLiteral("startDate"), config.startDate.c_str());
    printKeyValue(QStringLiteral("requestedStartDate"), requestedStartDate.toString(QStringLiteral("yyyy-MM-dd")));
    printKeyValue(QStringLiteral("endDate"), config.endDate.c_str());
    printKeyValue(QStringLiteral("daysLimit"), options.daysLimit);
    printKeyValue(QStringLiteral("activeSymbolCount"), activeSymbols.size());
    printKeyValue(QStringLiteral("benchmarkSymbolCount"), benchmarkSymbols.size());
    printKeyValue(QStringLiteral("benchmarkSymbol"), options.benchmarkSymbol);
    printKeyValue(QStringLiteral("symbolLimit"), options.symbolLimit);
    printKeyValue(QStringLiteral("dateParallelism"), config.enableDateParallelism ? QStringLiteral("enabled") : QStringLiteral("disabled"));
    printKeyValue(QStringLiteral("coldRun"), options.forceColdRun ? QStringLiteral("true") : QStringLiteral("false"));

    const QString cacheSignature = buildBenchmarkCacheSignature(
        instanceId,
        QString::fromStdString(config.startDate),
        QString::fromStdString(config.endDate),
        config.forwardDays,
        config.rebalanceDays,
        config.numGroups,
        config.transactionCost,
        config.slippageRate,
        config.riskFreeRate,
        options.benchmarkSymbol,
        config.enableDateParallelism,
        tradeDateCount,
        benchmarkSymbols);
    const QString benchmarkCacheFile = benchmarkCachePath(repoRoot, cacheSignature);
    const QString stableBenchmarkCacheFile = benchmarkStableCachePath(
        repoRoot,
        instanceId,
        QString::fromStdString(config.startDate),
        QString::fromStdString(config.endDate),
        config.forwardDays,
        config.rebalanceDays,
        config.numGroups,
        options.benchmarkSymbol,
        config.enableDateParallelism);

    const QString outputPath = options.outputPath.isEmpty()
        ? defaultOutputPath(repoRoot, instanceId)
        : (QDir::isAbsolutePath(options.outputPath) ? options.outputPath : QDir(repoRoot).filePath(options.outputPath));

    printKeyValue(QStringLiteral("cacheSignature"), cacheSignature);
    printKeyValue(QStringLiteral("benchmarkCacheFile"), QDir::toNativeSeparators(benchmarkCacheFile));
    printKeyValue(QStringLiteral("stableBenchmarkCacheFile"), QDir::toNativeSeparators(stableBenchmarkCacheFile));
    printKeyValue(QStringLiteral("benchmarkCacheExists"), QFileInfo::exists(benchmarkCacheFile) ? QStringLiteral("true") : QStringLiteral("false"));

    QVariantMap cachedSummary;
    const bool cacheLoaded = !options.forceColdRun && readJsonFile(benchmarkCacheFile, cachedSummary);
    const bool stableCacheLoaded = !options.forceColdRun && !cacheLoaded && stableBenchmarkCacheFile != benchmarkCacheFile
        ? readJsonFile(stableBenchmarkCacheFile, cachedSummary)
        : false;
    const QString loadedCacheFile = cacheLoaded
        ? benchmarkCacheFile
        : (stableCacheLoaded ? stableBenchmarkCacheFile : QString());
    printKeyValue(QStringLiteral("benchmarkCacheLoaded"), (cacheLoaded || stableCacheLoaded) ? QStringLiteral("true") : QStringLiteral("false"));
    printKeyValue(QStringLiteral("benchmarkCacheBypassed"), options.forceColdRun ? QStringLiteral("true") : QStringLiteral("false"));
    if (cacheLoaded || stableCacheLoaded) {
        printKeyValue(QStringLiteral("benchmarkCacheSource"), QDir::toNativeSeparators(loadedCacheFile));
    }
    if ((cacheLoaded || stableCacheLoaded)
        && cachedSummary.value(QStringLiteral("status")).toString() == QStringLiteral("SUCCESS")
        && cachedSummary.value(QStringLiteral("instanceId")).toString() == instanceId
        && cachedSummary.value(QStringLiteral("startDate")).toString() == QString::fromStdString(config.startDate)
        && cachedSummary.value(QStringLiteral("endDate")).toString() == QString::fromStdString(config.endDate)
        && cachedSummary.value(QStringLiteral("forwardDays")).toInt() == config.forwardDays
        && cachedSummary.value(QStringLiteral("rebalanceDays")).toInt() == config.rebalanceDays
        && cachedSummary.value(QStringLiteral("numGroups")).toInt() == config.numGroups
        && cachedSummary.value(QStringLiteral("benchmarkSymbol")).toString() == options.benchmarkSymbol)
    {
        cachedSummary.insert(QStringLiteral("activeSymbolCount"), activeSymbols.size());
        cachedSummary.insert(QStringLiteral("tradeDateCount"), tradeDateCount);
        cachedSummary.insert(QStringLiteral("cacheHit"), true);
        cachedSummary.insert(QStringLiteral("cachePath"), QDir::toNativeSeparators(loadedCacheFile));

        if (!writeJsonFile(outputPath, cachedSummary)) {
            printKeyValue(QStringLiteral("outputWriteFailed"), outputPath);
        } else {
            printKeyValue(QStringLiteral("outputPath"), QDir::toNativeSeparators(outputPath));
        }

        if (loadedCacheFile == benchmarkCacheFile && stableBenchmarkCacheFile != benchmarkCacheFile) {
            if (writeJsonFile(stableBenchmarkCacheFile, cachedSummary)) {
                printKeyValue(QStringLiteral("stableCachePath"), QDir::toNativeSeparators(stableBenchmarkCacheFile));
            } else {
                printKeyValue(QStringLiteral("stableCacheWriteFailed"), stableBenchmarkCacheFile);
            }
        }

        if (loadedCacheFile == stableBenchmarkCacheFile && stableBenchmarkCacheFile != benchmarkCacheFile) {
            if (writeJsonFile(benchmarkCacheFile, cachedSummary)) {
                printKeyValue(QStringLiteral("cachePath"), QDir::toNativeSeparators(benchmarkCacheFile));
            } else {
                printKeyValue(QStringLiteral("cacheWriteFailed"), benchmarkCacheFile);
            }
        }

        printKeyValue(QStringLiteral("cacheHit"), QStringLiteral("true"));
        printKeyValue(QStringLiteral("cachePath"), QDir::toNativeSeparators(loadedCacheFile));
        printKeyValue(QStringLiteral("resultStatus"), cachedSummary.value(QStringLiteral("status")).toString());
        printKeyValue(QStringLiteral("wallClockMs"), cachedSummary.value(QStringLiteral("wallClockMs")));
        printKeyValue(QStringLiteral("executorExecutionMs"), cachedSummary.value(QStringLiteral("executorExecutionMs")));
        printKeyValue(QStringLiteral("withinThreshold"), cachedSummary.value(QStringLiteral("withinThreshold")).toBool() ? QStringLiteral("true") : QStringLiteral("false"));
        printKeyValue(QStringLiteral("annualReturn"), QString::number(cachedSummary.value(QStringLiteral("annualReturn")).toDouble(), 'f', 6));
        printKeyValue(QStringLiteral("benchmarkAnnualReturn"), QString::number(cachedSummary.value(QStringLiteral("benchmarkAnnualReturn")).toDouble(), 'f', 6));
        printKeyValue(QStringLiteral("excessAnnualReturn"), QString::number(cachedSummary.value(QStringLiteral("excessAnnualReturn")).toDouble(), 'f', 6));
        printKeyValue(QStringLiteral("sharpeRatio"), QString::number(cachedSummary.value(QStringLiteral("sharpeRatio")).toDouble(), 'f', 6));
        printKeyValue(QStringLiteral("maxDrawdown"), QString::number(cachedSummary.value(QStringLiteral("maxDrawdown")).toDouble(), 'f', 6));
        printKeyValue(QStringLiteral("winRate"), QString::number(cachedSummary.value(QStringLiteral("winRate")).toDouble(), 'f', 6));
        printKeyValue(QStringLiteral("profitFactor"), QString::number(cachedSummary.value(QStringLiteral("profitFactor")).toDouble(), 'f', 6));
        printKeyValue(QStringLiteral("turnoverRate"), QString::number(cachedSummary.value(QStringLiteral("turnoverRate")).toDouble(), 'f', 6));
        printKeyValue(QStringLiteral("dataCoverage"), QString::number(cachedSummary.value(QStringLiteral("dataCoverage")).toDouble(), 'f', 6));
        terminateBenchmarkSuccess();
    }

    auto instanceManager = std::make_shared<factor::FactorInstanceManager>(database, dataChecker);
    auto threadPool = std::make_shared<foundation::thread::ThreadPoolExecutor>(static_cast<size_t>(options.threads));

    auto& cacheFacade = AStockQuantEngine::Cache::CacheFacade::getInstance();
    if (!cacheFacade.isEnabled()) {
        AStockQuantEngine::Cache::CacheConfig cacheConfig;
        cacheConfig.enabled = true;
        cacheConfig.localCache.enabled = true;
        cacheConfig.redisCache.enabled = true;
        cacheFacade.initialize(cacheConfig);
    }

    auto cacheManager = std::make_shared<factor::FactorCacheManager>();
    cacheManager->setCacheFacade(
        std::shared_ptr<AStockQuantEngine::Cache::CacheFacade>(&cacheFacade, [](AStockQuantEngine::Cache::CacheFacade*) {})
    );

    factor::FactorBacktestExecutor executor(instanceManager, threadPool, cacheManager);
    QElapsedTimer wallClock;
    wallClock.start();
    const qint64 maxAllowedMs = static_cast<qint64>(options.maxHours * 3600.0 * 1000.0);
    factor::FactorBacktestExecutor::ExecutionHandle executionHandle = executor.executeTrackedAsync(config);
    factor::BacktestResult result;
    bool timedOut = false;

    while (true) {
        if (executionHandle.future.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready) {
            result = executionHandle.future.get();
            break;
        }

        const qint64 elapsedMs = wallClock.elapsed();
        if (elapsedMs > maxAllowedMs) {
            timedOut = true;
            executor.cancel(executionHandle.taskId);
            result = executionHandle.future.get();
            break;
        }
    }

    const qint64 wallClockMs = wallClock.elapsed();
    const bool withinThreshold = wallClockMs <= maxAllowedMs;

    QVariantMap summary;
    summary.insert(QStringLiteral("repoRoot"), repoRoot);
    summary.insert(QStringLiteral("instanceId"), instanceId);
    summary.insert(QStringLiteral("startDate"), QString::fromStdString(result.config.startDate));
    summary.insert(QStringLiteral("requestedStartDate"), requestedStartDate.toString(QStringLiteral("yyyy-MM-dd")));
    summary.insert(QStringLiteral("endDate"), QString::fromStdString(result.config.endDate));
    summary.insert(QStringLiteral("tradeDateCount"), tradeDateCount);
    summary.insert(QStringLiteral("activeSymbolCount"), activeSymbols.size());
    summary.insert(QStringLiteral("wallClockMs"), static_cast<qlonglong>(wallClockMs));
    summary.insert(QStringLiteral("executorExecutionMs"), result.executionTimeMs);
    summary.insert(QStringLiteral("maxAllowedMs"), static_cast<qlonglong>(maxAllowedMs));
    summary.insert(QStringLiteral("withinThreshold"), withinThreshold);
    summary.insert(QStringLiteral("timedOut"), timedOut);
    summary.insert(QStringLiteral("status"), QString::fromStdString(result.status));
    summary.insert(QStringLiteral("errorMessage"), QString::fromStdString(result.errorMessage));
    summary.insert(QStringLiteral("annualReturn"), result.annualReturn);
    summary.insert(QStringLiteral("benchmarkAnnualReturn"), result.benchmarkAnnualReturn);
    summary.insert(QStringLiteral("excessAnnualReturn"), result.excessAnnualReturn);
    summary.insert(QStringLiteral("sharpeRatio"), result.sharpeRatio);
    summary.insert(QStringLiteral("maxDrawdown"), result.maxDrawdown);
    summary.insert(QStringLiteral("winRate"), result.winRate);
    summary.insert(QStringLiteral("profitFactor"), result.profitFactor);
    summary.insert(QStringLiteral("turnoverRate"), result.turnoverRate);
    summary.insert(QStringLiteral("dataCoverage"), result.dataCoverage);
    summary.insert(QStringLiteral("benchmarkSymbol"), QString::fromStdString(result.config.benchmarkSymbol));
    summary.insert(QStringLiteral("forwardDays"), result.config.forwardDays);
    summary.insert(QStringLiteral("rebalanceDays"), result.config.rebalanceDays);
    summary.insert(QStringLiteral("numGroups"), result.config.numGroups);
    summary.insert(QStringLiteral("dateParallelism"), result.config.enableDateParallelism);
    summary.insert(QStringLiteral("cacheHit"), false);
    summary.insert(QStringLiteral("cachePath"), QDir::toNativeSeparators(benchmarkCacheFile));

    if (!writeJsonFile(outputPath, summary)) {
        printKeyValue(QStringLiteral("outputWriteFailed"), outputPath);
    } else {
        printKeyValue(QStringLiteral("outputPath"), QDir::toNativeSeparators(outputPath));
    }

    if (result.status == "SUCCESS") {
        if (writeJsonFile(benchmarkCacheFile, summary)) {
            printKeyValue(QStringLiteral("cachePath"), QDir::toNativeSeparators(benchmarkCacheFile));
        } else {
            printKeyValue(QStringLiteral("cacheWriteFailed"), benchmarkCacheFile);
        }
        if (stableBenchmarkCacheFile != benchmarkCacheFile) {
            if (writeJsonFile(stableBenchmarkCacheFile, summary)) {
                printKeyValue(QStringLiteral("stableCachePath"), QDir::toNativeSeparators(stableBenchmarkCacheFile));
            } else {
                printKeyValue(QStringLiteral("stableCacheWriteFailed"), stableBenchmarkCacheFile);
            }
        }
    }

    printKeyValue(QStringLiteral("resultStatus"), QString::fromStdString(result.status));
    printKeyValue(QStringLiteral("wallClockMs"), wallClockMs);
    printKeyValue(QStringLiteral("executorExecutionMs"), result.executionTimeMs);
    printKeyValue(QStringLiteral("withinThreshold"), withinThreshold ? QStringLiteral("true") : QStringLiteral("false"));
    printKeyValue(QStringLiteral("timedOut"), timedOut ? QStringLiteral("true") : QStringLiteral("false"));
    printKeyValue(QStringLiteral("annualReturn"), QString::number(result.annualReturn, 'f', 6));
    printKeyValue(QStringLiteral("benchmarkAnnualReturn"), QString::number(result.benchmarkAnnualReturn, 'f', 6));
    printKeyValue(QStringLiteral("excessAnnualReturn"), QString::number(result.excessAnnualReturn, 'f', 6));
    printKeyValue(QStringLiteral("sharpeRatio"), QString::number(result.sharpeRatio, 'f', 6));
    printKeyValue(QStringLiteral("maxDrawdown"), QString::number(result.maxDrawdown, 'f', 6));
    printKeyValue(QStringLiteral("winRate"), QString::number(result.winRate, 'f', 6));
    printKeyValue(QStringLiteral("profitFactor"), QString::number(result.profitFactor, 'f', 6));
    printKeyValue(QStringLiteral("turnoverRate"), QString::number(result.turnoverRate, 'f', 6));
    printKeyValue(QStringLiteral("dataCoverage"), QString::number(result.dataCoverage, 'f', 6));

    if (result.status != "SUCCESS") {
        printKeyValue(QStringLiteral("errorMessage"), QString::fromStdString(result.errorMessage));
        return 9;
    }

    if (!withinThreshold) {
        printLine(QStringLiteral("[benchmark] threshold_exceeded"));
        return 10;
    }

    printKeyValue(QStringLiteral("exitMode"), QStringLiteral("quick_exit"));
    terminateBenchmarkSuccess();
}
