#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include "DatabaseConnectionManager.h"
#include "StrategyBacktestController.h"
#include "database/ConnectionPool.h"
#include "database/StrategyRepository.h"
#include "foundation.h"

namespace {

QString defaultStrategyId()
{
    return QStringLiteral("STR_400868_425440_9403");
}

QVariantMap asMap(const QVariant& value)
{
    return value.canConvert<QVariantMap>() ? value.toMap() : QVariantMap{};
}

QVariantList asList(const QVariant& value)
{
    return value.canConvert<QVariantList>() ? value.toList() : QVariantList{};
}

void mergeConfiguredValues(QVariantMap& target, const QVariantMap& source)
{
    for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
        const QVariant& value = it.value();
        if (!value.isValid() || value.isNull()) {
            continue;
        }
        if (value.typeId() == QMetaType::QString && value.toString().trimmed().isEmpty()) {
            continue;
        }
        target.insert(it.key(), value);
    }
}

QVariant firstConfiguredValue(const QVariantMap& map, const QStringList& keys)
{
    for (const QString& key : keys) {
        if (!map.contains(key)) {
            continue;
        }
        const QVariant value = map.value(key);
        if (!value.isValid() || value.isNull()) {
            continue;
        }
        if (value.typeId() == QMetaType::QString && value.toString().trimmed().isEmpty()) {
            continue;
        }
        return value;
    }
    return {};
}

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

QVariantMap loadBacktestHistoryEntry(const QVariantMap& strategy)
{
    const QVariantMap parameters = asMap(strategy.value(QStringLiteral("parameters")));
    QVariantMap performance = asMap(parameters.value(QStringLiteral("performance_metrics")));
    if (performance.isEmpty()) {
        performance = asMap(strategy.value(QStringLiteral("performance_metrics")));
    }

    QVariantMap historyEntry = asMap(performance.value(QStringLiteral("backtestHistoryEntry")));
    if (historyEntry.isEmpty()) {
        historyEntry = asMap(performance.value(QStringLiteral("latestBacktest")));
    }
    return historyEntry;
}

QVariantMap loadRawStrategyParameters(const QString& strategyId)
{
    astock::database::ConnectionGuard connectionGuard;
    QSqlDatabase& db = connectionGuard.get();
    if (!db.isValid() || !db.isOpen()) {
        return {};
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT CAST(parameters AS CHAR) FROM strategy WHERE strategy_id = ?"));
    query.addBindValue(strategyId);
    if (!query.exec()) {
        QTextStream(stderr) << "查询 strategy.parameters 失败: " << query.lastError().text() << '\n';
        return {};
    }
    if (!query.next()) {
        return {};
    }

    const QString parametersJson = query.value(0).toString().trimmed();
    if (parametersJson.isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(parametersJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QTextStream(stderr) << "解析 strategy.parameters 失败: " << parseError.errorString() << '\n';
        return {};
    }

    return document.object().toVariantMap();
}

QVariantList resolveHistorySymbolPool(const QVariantMap& historyEntry)
{
    const QStringList candidateKeys{
        QStringLiteral("backtest_symbol_pool"),
        QStringLiteral("backtestSymbolPool"),
        QStringLiteral("symbol_pool"),
        QStringLiteral("symbolPool"),
        QStringLiteral("selectedSymbols")
    };

    for (const QString& key : candidateKeys) {
        const QVariant value = historyEntry.value(key);
        const QVariantList list = asList(value);
        if (!list.isEmpty()) {
            return list;
        }
    }

    return {};
}

int resolveDatasetId(const QVariantMap& historyEntry, const QVariantMap& runtimeParameters)
{
    bool ok = false;
    int datasetId = firstConfiguredValue(historyEntry, {QStringLiteral("selectedDatasetId"), QStringLiteral("datasetId")}).toInt(&ok);
    if (ok) {
        return datasetId;
    }
    datasetId = firstConfiguredValue(runtimeParameters, {QStringLiteral("selectedDatasetId"), QStringLiteral("datasetId")}).toInt(&ok);
    return ok ? datasetId : -1;
}

QVariantMap buildStrategyPayload(const QVariantMap& strategy, const QVariantMap& historyEntry)
{
    QVariantMap payload = strategy;
    QVariantMap parameters = asMap(payload.value(QStringLiteral("parameters")));
    const QVariantMap runtimeParameters = asMap(historyEntry.value(QStringLiteral("runtimeParameters")));

    mergeConfiguredValues(parameters, runtimeParameters);

    const QString universeType = historyEntry.value(QStringLiteral("universeType")).toString().trimmed();
    const QString indexSymbol = historyEntry.value(QStringLiteral("indexSymbol")).toString().trimmed();
    if (!universeType.isEmpty()) {
        parameters.insert(QStringLiteral("universeType"), universeType);
        payload.insert(QStringLiteral("universeType"), universeType);
    }
    if (!indexSymbol.isEmpty()) {
        parameters.insert(QStringLiteral("indexSymbol"), indexSymbol);
        parameters.insert(QStringLiteral("universeId"), indexSymbol);
        payload.insert(QStringLiteral("indexSymbol"), indexSymbol);
        payload.insert(QStringLiteral("universeId"), indexSymbol);
    }

    const QVariantList symbolPool = resolveHistorySymbolPool(historyEntry);
    if (!symbolPool.isEmpty()) {
        parameters.insert(QStringLiteral("symbol_pool"), symbolPool);
        parameters.insert(QStringLiteral("symbolPool"), symbolPool);
        payload.insert(QStringLiteral("symbol_pool"), symbolPool);
        payload.insert(QStringLiteral("symbolPool"), symbolPool);
    }

    payload.insert(QStringLiteral("parameters"), parameters);
    mergeConfiguredValues(payload, runtimeParameters);
    return payload;
}

double readMetric(const QVariantMap& result, const QString& topLevelKey, const QString& nestedKey)
{
    bool ok = false;
    const double topLevel = result.value(topLevelKey).toDouble(&ok);
    if (ok) {
        return topLevel;
    }

    const QVariantMap performance = asMap(result.value(QStringLiteral("performance")));
    const double nested = performance.value(nestedKey).toDouble(&ok);
    return ok ? nested : 0.0;
}

int readTradeCount(const QVariantMap& result)
{
    bool ok = false;
    const int topLevel = result.value(QStringLiteral("totalTrades")).toInt(&ok);
    if (ok) {
        return topLevel;
    }

    const QVariantMap trades = asMap(result.value(QStringLiteral("trades")));
    const int nested = trades.value(QStringLiteral("totalTrades")).toInt(&ok);
    return ok ? nested : 0;
}

QString defaultOutputPath(const QString& repoRoot, const QString& strategyId)
{
    const QString outputDir = QDir(repoRoot).filePath(QStringLiteral("build/tests/manual_backtest_runs"));
    QDir().mkpath(outputDir);
    return QDir(outputDir).filePath(QStringLiteral("%1_%2.json")
        .arg(strategyId, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))));
}

bool writeJsonFile(const QString& outputPath, const QVariantMap& payload)
{
    QDir().mkpath(QFileInfo(outputPath).absolutePath());
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromVariant(payload).object().isEmpty()
        ? QJsonDocument(QJsonObject::fromVariantMap(payload))
        : QJsonDocument(QJsonObject::fromVariantMap(payload));
    file.write(document.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

void printSummary(const QVariantMap& result, const QString& outputPath)
{
    const double totalReturn = readMetric(result, QStringLiteral("totalReturn"), QStringLiteral("totalReturn"));
    const double annualizedReturn = readMetric(result, QStringLiteral("annualizedReturn"), QStringLiteral("annualizedReturn"));
    const double maxDrawdown = readMetric(result, QStringLiteral("maxDrawdown"), QStringLiteral("maxDrawdown"));
    const double sharpeRatio = readMetric(result, QStringLiteral("sharpeRatio"), QStringLiteral("sharpeRatio"));
    const int totalTrades = readTradeCount(result);
    const QVariantMap ruleSummary = asMap(result.value(QStringLiteral("ruleTemplateSummary")));
    const int entryBlockCount = ruleSummary.value(QStringLiteral("entryBlockCount")).toInt();
    const int forcedExitCount = ruleSummary.value(QStringLiteral("forcedExitCount")).toInt();

    QTextStream stream(stdout);
    stream << "SUMMARY\n";
    stream << "totalTrades=" << totalTrades << '\n';
    stream << "totalReturn=" << QString::number(totalReturn, 'f', 6) << '\n';
    stream << "annualizedReturn=" << QString::number(annualizedReturn, 'f', 6) << '\n';
    stream << "maxDrawdown=" << QString::number(maxDrawdown, 'f', 6) << '\n';
    stream << "sharpeRatio=" << QString::number(sharpeRatio, 'f', 6) << '\n';
    stream << "entryBlockCount=" << entryBlockCount << '\n';
    stream << "forcedExitCount=" << forcedExitCount << '\n';
    stream << "outputPath=" << QDir::toNativeSeparators(outputPath) << '\n';

    const QVariantList latestGroupDecisions = asList(ruleSummary.value(QStringLiteral("latestGroupDecisions")));
    if (!latestGroupDecisions.isEmpty()) {
        stream << "latestGroupDecision="
               << QString::fromUtf8(QJsonDocument::fromVariant(latestGroupDecisions.first()).toJson(QJsonDocument::Compact)).trimmed()
               << '\n';
    }
    stream.flush();
}

}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("run_strategy_backtest_once"));

    const QStringList arguments = QCoreApplication::arguments();
    const QString strategyId = arguments.size() > 1 ? arguments.at(1).trimmed() : defaultStrategyId();
    const QString repoRoot = detectRepoRoot();
    QDir::setCurrent(repoRoot);

    QTextStream stream(stdout);
    stream << "repoRoot=" << QDir::toNativeSeparators(repoRoot) << '\n';
    stream << "strategyId=" << strategyId << '\n';
    stream.flush();

    foundation::Config foundationConfig;
    foundationConfig.config_dir = QDir(repoRoot).filePath(QStringLiteral("config")).toStdString();
    foundationConfig.enable_console_log = true;
    foundationConfig.enable_file_log = false;
    foundationConfig.thread_pool_size = 4;
    if (!foundation::Foundation::instance().initialize(foundationConfig)) {
        QTextStream(stderr) << "Foundation 初始化失败\n";
        return 8;
    }

    if (!astock::database::DatabaseConnectionManager::instance().initialize()) {
        QTextStream(stderr) << "数据库初始化失败\n";
        return 2;
    }

    astock::database::StrategyRepository repository;
    QVariantMap strategy = repository.findById(strategyId);
    const QVariantMap rawParameters = loadRawStrategyParameters(strategyId);
    if (strategy.isEmpty() && rawParameters.isEmpty()) {
        QTextStream(stderr) << "未找到策略: " << strategyId << '\n';
        return 3;
    }
    if (!rawParameters.isEmpty()) {
        strategy.insert(QStringLiteral("parameters"), rawParameters);
    }

    const QVariantMap historyEntry = loadBacktestHistoryEntry(strategy);
    if (historyEntry.isEmpty()) {
        const QVariantMap parameters = asMap(strategy.value(QStringLiteral("parameters")));
        const QVariantMap nestedPerformance = asMap(parameters.value(QStringLiteral("performance_metrics")));
        const QVariantMap rootPerformance = asMap(strategy.value(QStringLiteral("performance_metrics")));
        QTextStream(stderr) << "调试 parameters.keys=" << parameters.keys().join(',') << '\n';
        QTextStream(stderr) << "调试 nestedPerformance.keys=" << nestedPerformance.keys().join(',') << '\n';
        QTextStream(stderr) << "调试 rootPerformance.keys=" << rootPerformance.keys().join(',') << '\n';
        QTextStream(stderr) << "策略缺少 performance_metrics.backtestHistoryEntry，无法复用上次回测上下文\n";
        return 4;
    }

    const QVariantMap runtimeParameters = asMap(historyEntry.value(QStringLiteral("runtimeParameters")));
    const QVariantMap strategyPayload = buildStrategyPayload(strategy, historyEntry);
    const QVariantList selectedSymbols = resolveHistorySymbolPool(historyEntry);
    const QString startDate = historyEntry.value(QStringLiteral("startDate")).toString().trimmed();
    const QString endDate = historyEntry.value(QStringLiteral("endDate")).toString().trimmed();
    const QString dataSourceMode = historyEntry.value(QStringLiteral("dataSourceMode"), QStringLiteral("raw")).toString().trimmed();
    const int datasetId = resolveDatasetId(historyEntry, runtimeParameters);
    const double initialCapital = runtimeParameters.value(QStringLiteral("initialCapital")).toDouble();
    const QString outputPath = arguments.size() > 2 ? arguments.at(2).trimmed() : defaultOutputPath(repoRoot, strategyId);

    StrategyBacktestController controller;
    controller.setSelectedStrategyId(strategyId);
    controller.setStrategyParams(strategyPayload);
    if (initialCapital > 0.0) {
        controller.setInitialCapital(initialCapital);
    }
    if (!dataSourceMode.isEmpty()) {
        controller.setDataSourceMode(dataSourceMode);
    }
    if (datasetId >= 0) {
        controller.setSelectedDatasetId(datasetId);
    }

    QObject::connect(&controller, &StrategyBacktestController::backtestProgress,
                     [&stream](int progress, const QString& status) {
        stream << "progress=" << progress << " status=" << status << '\n';
        stream.flush();
    });

    QObject::connect(&controller, &StrategyBacktestController::backtestCompleted,
                     [&app, outputPath](const QVariantMap& result) {
        if (!writeJsonFile(outputPath, result)) {
            QTextStream(stderr) << "回测完成，但结果写文件失败: " << QDir::toNativeSeparators(outputPath) << '\n';
            app.exit(5);
            return;
        }

        printSummary(result, outputPath);
        app.exit(0);
    });

    QObject::connect(&controller, &StrategyBacktestController::backtestFailed,
                     [&app](const QString& error) {
        QTextStream(stderr) << "回测失败: " << error << '\n';
        app.exit(6);
    });

    QTimer::singleShot(45 * 60 * 1000, &app, [&app]() {
        QTextStream(stderr) << "回测超时退出\n";
        app.exit(7);
    });

    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        foundation::Foundation::instance().shutdown();
    });

    controller.startStrategyBacktest(strategyId, strategyPayload, selectedSymbols, startDate, endDate);
    return app.exec();
}