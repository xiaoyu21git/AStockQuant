#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QTextStream>
#include <QTimer>
#include <QtGlobal>

#include <iostream>
#include <memory>

#include "Event/EventBus.hpp"
#include "GlobalEventBusRegistry.h"
#include "JujinMarketConnector.h"
#include "foundation.h"
#include "foundation/config/ConfigManager.hpp"

#include "../ui/bridge/include/MarketDataBridge.h"
#include "../ui/bridge/include/TradingBridges.h"
#include "../ui/bridge/include/TradingConnectionConfigService.h"
#include "../ui/bridge/include/TradingRuntimeStatusService.h"

namespace {

struct ProbeOptions {
    QString symbol = QStringLiteral("600000.SH");
    QString side = QStringLiteral("BUY");
    QString orderType = QStringLiteral("LIMIT");
    double price = 0.01;
    qint64 quantity = 100;
    int submitDelayMs = 1200;
    int timeoutMs = 8000;
    bool noSubmit = false;
};

QString compactJson(const QVariant& value)
{
    return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
}

void printLine(const QString& line)
{
    QTextStream(stdout) << line << Qt::endl;
}

void printValue(const QString& label, const QVariant& value)
{
    printLine(QStringLiteral("[tradeprobe] %1 %2").arg(label, compactJson(value)));
}

QString trimmedConfigValue(const QVariantMap& configuration, const QString& key)
{
    return configuration.value(key).toString().trimmed();
}

QString resolveRuntimeStrategyId(const QVariantMap& configuration)
{
    for (const QString& key : {QStringLiteral("accountRuntimeStrategyId"),
                               QStringLiteral("gmStrategyId"),
                               QStringLiteral("runtimeStrategyId"),
                               QStringLiteral("strategyId")}) {
        const QString value = trimmedConfigValue(configuration, key);
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QString resolveBusinessStrategyId(const QVariantMap& configuration)
{
    for (const QString& key : {QStringLiteral("boundStrategyId"),
                               QStringLiteral("strategyId"),
                               QStringLiteral("accountRuntimeStrategyId")}) {
        const QString value = trimmedConfigValue(configuration, key);
        if (!value.isEmpty()) {
            return value;
        }
    }
    return QStringLiteral("manual_test");
}

QString resolveBusinessStrategyName(const QVariantMap& configuration, const QString& accountId)
{
    for (const QString& key : {QStringLiteral("boundStrategyName"),
                               QStringLiteral("strategyName")}) {
        const QString value = trimmedConfigValue(configuration, key);
        if (!value.isEmpty()) {
            return value;
        }
    }

    if (!accountId.isEmpty()) {
        return QStringLiteral("Account Runtime Probe %1").arg(accountId);
    }

    return QStringLiteral("Account Runtime Probe");
}

bool isTerminalOrderStatus(const QString& status)
{
    const QString normalized = status.trimmed().toUpper();
    return normalized == QStringLiteral("FILLED")
        || normalized == QStringLiteral("REJECTED")
        || normalized == QStringLiteral("CANCELLED")
        || normalized == QStringLiteral("EXPIRED")
        || normalized == QStringLiteral("FAILED");
}

bool ensureDirectory(const QString& path)
{
    return QDir(path).exists() || QDir().mkpath(path);
}

bool initializeRuntimeEnvironment()
{
    const QString baseDir = QCoreApplication::applicationDirPath();
    const QString configDir = QDir(baseDir).filePath(QStringLiteral("config"));
    const QString logsDir = QDir(baseDir).filePath(QStringLiteral("logs"));
    const QString filesDir = QDir(baseDir).filePath(QStringLiteral("files"));

    if (!ensureDirectory(configDir) || !ensureDirectory(logsDir) || !ensureDirectory(filesDir)) {
        std::cerr << "[tradeprobe] failed to prepare runtime directories" << std::endl;
        return false;
    }

    foundation::Config config;
    config.profile = "development";
    config.config_dir = configDir.toStdString();
    config.enable_console_log = true;
    config.enable_file_log = true;
    config.log_file = QDir(logsDir).filePath(QStringLiteral("astockquant_tradeprobe.log")).toStdString();

    if (!foundation::Foundation::instance().initialize(config)) {
        std::cerr << "[tradeprobe] foundation initialization failed" << std::endl;
        return false;
    }

    foundation::config::ConfigManager::instance().initialize(config.profile, config.config_dir);
    return true;
}

void shutdownRuntimeEnvironment(std::unique_ptr<engine::EventBus>& ownedBus)
{
    if (ownedBus) {
        engine::register_engine_event_bus(nullptr);
        ownedBus->stop(true, 1000);
        ownedBus.reset();
    }

    foundation::Foundation::instance().shutdown();
}

ProbeOptions parseOptions(QCoreApplication& app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AStock account-runtime trade probe"));
    parser.addHelpOption();

    QCommandLineOption symbolOption({QStringLiteral("s"), QStringLiteral("symbol")},
                                    QStringLiteral("委托证券代码"),
                                    QStringLiteral("symbol"),
                                    QStringLiteral("600000.SH"));
    QCommandLineOption sideOption({QStringLiteral("side")},
                                  QStringLiteral("方向 BUY/SELL"),
                                  QStringLiteral("side"),
                                  QStringLiteral("BUY"));
    QCommandLineOption priceOption({QStringLiteral("p"), QStringLiteral("price")},
                                   QStringLiteral("价格"),
                                   QStringLiteral("price"),
                                   QStringLiteral("0.01"));
    QCommandLineOption quantityOption({QStringLiteral("q"), QStringLiteral("quantity")},
                                      QStringLiteral("数量"),
                                      QStringLiteral("quantity"),
                                      QStringLiteral("100"));
    QCommandLineOption orderTypeOption({QStringLiteral("t"), QStringLiteral("order-type")},
                                       QStringLiteral("委托类型"),
                                       QStringLiteral("orderType"),
                                       QStringLiteral("LIMIT"));
    QCommandLineOption submitDelayOption({QStringLiteral("submit-delay-ms")},
                                         QStringLiteral("下单前等待毫秒数"),
                                         QStringLiteral("ms"),
                                         QStringLiteral("1200"));
    QCommandLineOption timeoutOption({QStringLiteral("timeout-ms")},
                                     QStringLiteral("整体超时毫秒数"),
                                     QStringLiteral("ms"),
                                     QStringLiteral("8000"));
    QCommandLineOption noSubmitOption({QStringLiteral("no-submit")},
                                      QStringLiteral("只检查账户级 runtime 与快照，不发送测试委托"));

    parser.addOption(symbolOption);
    parser.addOption(sideOption);
    parser.addOption(priceOption);
    parser.addOption(quantityOption);
    parser.addOption(orderTypeOption);
    parser.addOption(submitDelayOption);
    parser.addOption(timeoutOption);
    parser.addOption(noSubmitOption);
    parser.process(app);

    ProbeOptions options;
    options.symbol = parser.value(symbolOption).trimmed().toUpper();
    options.side = parser.value(sideOption).trimmed().toUpper();
    options.orderType = parser.value(orderTypeOption).trimmed().toUpper();
    options.price = parser.value(priceOption).toDouble();
    options.quantity = parser.value(quantityOption).toLongLong();
    options.submitDelayMs = qMax(0, parser.value(submitDelayOption).toInt());
    options.timeoutMs = qMax(1000, parser.value(timeoutOption).toInt());
    options.noSubmit = parser.isSet(noSubmitOption);
    return options;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("astockquant_tradeprobe"));

    const ProbeOptions options = parseOptions(app);
    if (!initializeRuntimeEnvironment()) {
        return 1;
    }

    std::unique_ptr<engine::EventBus> ownedBus;
    if (!engine::get_engine_event_bus()) {
        ownedBus = engine::EventBus::create();
        if (!ownedBus || !ownedBus->start()) {
            std::cerr << "[tradeprobe] failed to initialize EventBus" << std::endl;
            shutdownRuntimeEnvironment(ownedBus);
            return 1;
        }
        engine::register_engine_event_bus(ownedBus.get());
    }

    auto* configService = TradingConnectionConfigService::instance();
    auto* runtimeStatusService = TradingRuntimeStatusService::instance();
    auto* positionService = PositionAccountService::instance();
    auto* marketDataService = MarketDataService::instance();
    auto* tradeService = TradeExecutionService::instance();

    configService->initialize();
    configService->refreshClientProcessStatus();
    runtimeStatusService->initialize();
    positionService->initialize();
    marketDataService->initialize();
    tradeService->initialize();

    const QVariantMap tradingConfiguration = configService->currentConfiguration();
    const QString accountId = trimmedConfigValue(tradingConfiguration, QStringLiteral("accountId"));
    const QString runtimeStrategyId = resolveRuntimeStrategyId(tradingConfiguration);
    const QString businessStrategyId = resolveBusinessStrategyId(tradingConfiguration);
    const QString businessStrategyName = resolveBusinessStrategyName(tradingConfiguration, accountId);
    const bool shouldSubmit = !options.noSubmit && tradingConfiguration.value(QStringLiteral("enabled")).toBool();

    QVariantMap summaryConfiguration;
    for (const QString& key : {QStringLiteral("enabled"),
                               QStringLiteral("readOnly"),
                               QStringLiteral("clientProcessRunning"),
                               QStringLiteral("accountId"),
                               QStringLiteral("accountRuntimeStrategyId"),
                               QStringLiteral("boundStrategyId"),
                               QStringLiteral("gmStrategyId"),
                               QStringLiteral("runtimeStrategyId")}) {
        if (tradingConfiguration.contains(key)) {
            summaryConfiguration.insert(key, tradingConfiguration.value(key));
        }
    }

    printValue(QStringLiteral("config"), summaryConfiguration);
    printLine(QStringLiteral("[tradeprobe] probeIdentity businessStrategyId=%1 runtimeStrategyId=%2 submit=%3")
                  .arg(businessStrategyId,
                       runtimeStrategyId,
                       shouldSubmit ? QStringLiteral("true") : QStringLiteral("false")));

    QString lastSessionJson;
    QString lastAccountJson;
    QString lastPositionsJson;
    QString lastBridgeStatus;
    QString lastSubscriptionStatus;
    QString lastSnapshotState;
    QString lastAccountSessionJson;
    bool sessionObserved = false;
    bool snapshotObserved = false;
    bool orderTerminal = !shouldSubmit;
    int exitCode = 0;

    const auto printBridgeState = [&]() {
        const QString currentStatus = QStringLiteral("ready=%1 message=%2")
            .arg(tradeService->isLiveBridgeReady() ? QStringLiteral("true") : QStringLiteral("false"),
                 tradeService->liveBridgeStatusMessage());
        if (currentStatus != lastBridgeStatus) {
            lastBridgeStatus = currentStatus;
            printLine(QStringLiteral("[tradeprobe] bridge %1").arg(currentStatus));
        }
    };

    const auto printRuntimeState = [&]() {
        const QVariantList allSessions = runtimeStatusService->sessionSnapshots();
        const QString sessionsJson = compactJson(allSessions);
        if (sessionsJson != lastSessionJson) {
            lastSessionJson = sessionsJson;
            printValue(QStringLiteral("sessions"), allSessions);
        }

        if (!accountId.isEmpty()) {
            const QVariantMap accountSession = runtimeStatusService->sessionSnapshotForAccount(accountId);
            const QString accountSessionJson = compactJson(accountSession);
            if (accountSessionJson != lastAccountSessionJson) {
                lastAccountSessionJson = accountSessionJson;
                printValue(QStringLiteral("accountSession"), accountSession);
            }
            if (!accountSession.isEmpty()
                && accountSession.value(QStringLiteral("connected")).toBool()
                && accountSession.value(QStringLiteral("isRunning")).toBool()) {
                sessionObserved = true;
            }
        }
    };

    const auto printSnapshotState = [&]() {
        const QVariantMap accountSnapshot = positionService->accountSnapshot();
        const bool initialSnapshotLoaded = positionService->initialSnapshotLoaded();
        const QString currentAccountJson = compactJson(accountSnapshot);
        if (currentAccountJson != lastAccountJson) {
            lastAccountJson = currentAccountJson;
            printValue(QStringLiteral("accountSnapshot"), accountSnapshot);
        }

        const QVariantList positions = positionService->positions();
        const QString currentPositionsJson = compactJson(positions);
        if (currentPositionsJson != lastPositionsJson) {
            lastPositionsJson = currentPositionsJson;
            printValue(QStringLiteral("positions"), positions);
        }

        const QString snapshotAccountId = accountSnapshot.value(QStringLiteral("accountId")).toString().trimmed();
        const bool accountMatches = accountId.isEmpty() || snapshotAccountId == accountId;
        const QString snapshotState = QStringLiteral("initialSnapshotLoaded=%1 accountId=%2 positions=%3")
            .arg(initialSnapshotLoaded ? QStringLiteral("true") : QStringLiteral("false"),
                 snapshotAccountId,
                 QString::number(positions.size()));
        if (snapshotState != lastSnapshotState) {
            lastSnapshotState = snapshotState;
            printLine(QStringLiteral("[tradeprobe] snapshotState %1").arg(snapshotState));
        }

        if (initialSnapshotLoaded && accountMatches) {
            snapshotObserved = true;
        }
    };

    const auto maybeQuit = [&]() {
        const bool snapshotReady = snapshotObserved;
        if (orderTerminal && sessionObserved && snapshotReady) {
            QTimer::singleShot(150, &app, &QCoreApplication::quit);
        }
    };

    QObject::connect(configService, &TradingConnectionConfigService::errorOccurred, &app, [](const QString& message) {
        printLine(QStringLiteral("[tradeprobe] configError %1").arg(message));
    });
    QObject::connect(positionService, &PositionAccountService::errorOccurred, &app, [](const QString& message) {
        printLine(QStringLiteral("[tradeprobe] snapshotError %1").arg(message));
    });
    QObject::connect(positionService, &PositionAccountService::accountSnapshotChanged, &app, [&]() {
        printSnapshotState();
        maybeQuit();
    });
    QObject::connect(positionService, &PositionAccountService::positionsChanged, &app, [&]() {
        printSnapshotState();
        maybeQuit();
    });
    QObject::connect(runtimeStatusService, &TradingRuntimeStatusService::sessionSnapshotsChanged, &app, [&]() {
        printRuntimeState();
        maybeQuit();
    });
    QObject::connect(marketDataService, &MarketDataService::runtimeSubscriptionStatusChanged, &app, [&]() {
        const QString currentStatus = QStringLiteral("count=%1 limit=%2")
            .arg(marketDataService->runtimeSubscriptionCount())
            .arg(marketDataService->runtimeSubscriptionLimit());
        if (currentStatus != lastSubscriptionStatus) {
            lastSubscriptionStatus = currentStatus;
            printLine(QStringLiteral("[tradeprobe] marketSubscriptions %1").arg(currentStatus));
        }
    });
    QObject::connect(tradeService, &TradeExecutionService::lastErrorMessageChanged, &app, [&]() {
        const QString message = tradeService->lastErrorMessage().trimmed();
        if (!message.isEmpty()) {
            printLine(QStringLiteral("[tradeprobe] tradeError %1").arg(message));
        }
    });
    QObject::connect(tradeService, &TradeExecutionService::orderRequestPublished, &app, [](const QVariantMap& request) {
        printValue(QStringLiteral("orderRequest"), request);
    });
    QObject::connect(tradeService, &TradeExecutionService::orderStatusPublished, &app, [&](const QVariantMap& status) {
        printValue(QStringLiteral("orderStatus"), status);
        if (isTerminalOrderStatus(status.value(QStringLiteral("status")).toString())) {
            orderTerminal = true;
            maybeQuit();
        }
    });
    QObject::connect(tradeService, &TradeExecutionService::tradeFillPublished, &app, [](const QVariantMap& fill) {
        printValue(QStringLiteral("tradeFill"), fill);
    });

    JujinMarketConnector connector;
    if (tradingConfiguration.value(QStringLiteral("enabled")).toBool() && connector.isEnabledByEnvironment()) {
        const bool started = connector.start();
        printLine(QStringLiteral("[tradeprobe] connector started=%1 lastError=%2")
                      .arg(started ? QStringLiteral("true") : QStringLiteral("false"),
                           QString::fromStdString(connector.lastError())));
        if (!started) {
            exitCode = 1;
        }
    } else {
        printLine(QStringLiteral("[tradeprobe] connector skipped enabled=%1 environmentReady=%2")
                      .arg(tradingConfiguration.value(QStringLiteral("enabled")).toBool() ? QStringLiteral("true") : QStringLiteral("false"),
                           connector.isEnabledByEnvironment() ? QStringLiteral("true") : QStringLiteral("false")));
    }

    if (!options.symbol.isEmpty()) {
        marketDataService->ensureWatchSymbol(options.symbol);
    }

    printBridgeState();
    printRuntimeState();
    printSnapshotState();

    QTimer pollTimer;
    pollTimer.setInterval(500);
    QObject::connect(&pollTimer, &QTimer::timeout, &app, [&]() {
        configService->refreshClientProcessStatus();
        runtimeStatusService->refresh();
        printRuntimeState();
        printBridgeState();
        printSnapshotState();
        if (!positionService->initialSnapshotLoaded()) {
            positionService->requestInitialSnapshot();
        }
    });
    pollTimer.start();

    QTimer::singleShot(250, &app, [&]() {
        runtimeStatusService->refresh();
        positionService->requestInitialSnapshot();
    });

    if (shouldSubmit) {
        QTimer::singleShot(options.submitDelayMs, &app, [&]() {
            QVariantMap request;
            request.insert(QStringLiteral("symbol"), options.symbol);
            request.insert(QStringLiteral("side"), options.side);
            request.insert(QStringLiteral("price"), options.price);
            request.insert(QStringLiteral("quantity"), options.quantity);
            request.insert(QStringLiteral("orderType"), options.orderType);
            request.insert(QStringLiteral("strategyId"), businessStrategyId);
            request.insert(QStringLiteral("strategyName"), businessStrategyName);
            if (!runtimeStrategyId.isEmpty()) {
                request.insert(QStringLiteral("runtimeStrategyId"), runtimeStrategyId);
            }

            const bool submitted = tradeService->submitBridgeOrder(request);
            printLine(QStringLiteral("[tradeprobe] submitBridgeOrder returned=%1 runtimeStrategyId=%2")
                          .arg(submitted ? QStringLiteral("true") : QStringLiteral("false"),
                               runtimeStrategyId));
            if (!submitted) {
                orderTerminal = true;
                maybeQuit();
            }
        });
    } else {
        printLine(QStringLiteral("[tradeprobe] submit skipped"));
    }

    QTimer::singleShot(options.timeoutMs, &app, [&]() {
        if (!sessionObserved || (!orderTerminal && shouldSubmit)) {
            exitCode = 2;
        }
        printLine(QStringLiteral("[tradeprobe] timeout reached after %1ms")
                      .arg(options.timeoutMs));
        app.quit();
    });

    const int appResult = app.exec();
    connector.stop();
    shutdownRuntimeEnvironment(ownedBus);
    return appResult != 0 ? appResult : exitCode;
}