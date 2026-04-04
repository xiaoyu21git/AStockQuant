#include "AppBootstrap.h"

// #include "InlineExecutor.h"
// #include "IExecutor.h"
#include "foundation.h"
#include "foundation/config/ConfigManager.hpp"
#include "Event/EventBus.hpp"
#include "GlobalEventBusRegistry.h"
// 下面这些现在可以是空头文件或 forward declare
// #include "engine/Engine.h"

#include <iostream>
#include <cstdio>
#include <mutex>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtGlobal>
#include "VasAurora.hpp"
#include "../../ui/bridge/include/MarketDataService.h"
#include "../../ui/bridge/include/PositionAccountService.h"
#include "../../ui/bridge/include/RiskMonitorService.h"
#include "../../ui/bridge/include/StrategyService.h"
#include "../../ui/bridge/include/TradeExecutionService.h"
#include "../../ui/bridge/include/TradingConnectionConfigService.h"
#if defined(ASTOCK_ENABLE_JUJIN_MARKET)
#include "JujinMarketConnector.h"
#endif

namespace {

struct RuntimeDirectories {
    QString baseDir;
    QString configDir;
    QString logsDir;
    QString filesDir;
    QString logFilePath;
};

std::mutex qtMessageHandlerMutex;
QtMessageHandler previousQtMessageHandler = nullptr;
QString qtMessageLogFilePath;
bool qtMessageHandlerInstalled = false;

QString qtMessageTypeLabel(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }

    return QStringLiteral("LOG");
}

void qtMessageFileHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    QString sanitizedMessage = message;
    sanitizedMessage.replace(QChar('\r'), QStringLiteral("\\r"));
    sanitizedMessage.replace(QChar('\n'), QStringLiteral("\\n"));

    QString formatted = QStringLiteral("%1 [QT][%2]")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
             qtMessageTypeLabel(type));

    if (context.category && context.category[0] != '\0') {
        formatted += QStringLiteral(" [%1]").arg(QString::fromUtf8(context.category));
    }

    formatted += QStringLiteral(" %1").arg(sanitizedMessage);

    if (context.file && context.file[0] != '\0') {
        formatted += QStringLiteral(" (%1:%2)")
            .arg(QString::fromUtf8(context.file))
            .arg(context.line);
    }

    {
        std::lock_guard<std::mutex> lock(qtMessageHandlerMutex);
        if (!qtMessageLogFilePath.isEmpty()) {
            QFile logFile(qtMessageLogFilePath);
            if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                const QByteArray utf8 = formatted.toUtf8();
                logFile.write(utf8);
                logFile.write("\n", 1);
            }
        }
    }

    if (previousQtMessageHandler) {
        previousQtMessageHandler(type, context, message);
        return;
    }

    const QByteArray local8Bit = formatted.toLocal8Bit();
    FILE* stream = (type == QtDebugMsg || type == QtInfoMsg) ? stdout : stderr;
    std::fprintf(stream, "%s\n", local8Bit.constData());
    std::fflush(stream);
}

void installQtMessageFileLogger(const QString& logFilePath)
{
    std::lock_guard<std::mutex> lock(qtMessageHandlerMutex);
    qtMessageLogFilePath = logFilePath;
    if (qtMessageHandlerInstalled) {
        return;
    }

    previousQtMessageHandler = qInstallMessageHandler(qtMessageFileHandler);
    qtMessageHandlerInstalled = true;
}

RuntimeDirectories runtimeDirectories()
{
    const QString baseDir = QCoreApplication::applicationDirPath();
    const QDir dir(baseDir);

    return RuntimeDirectories{
        baseDir,
        dir.filePath(QStringLiteral("config")),
        dir.filePath(QStringLiteral("logs")),
        dir.filePath(QStringLiteral("files")),
        dir.filePath(QStringLiteral("logs/astockquantapp.log"))
    };
}

bool ensureDirectoryExists(const QString& path, const char* label)
{
    const QDir dir(path);
    if (dir.exists()) {
        return true;
    }

    if (QDir().mkpath(path)) {
        return true;
    }

    std::cerr << "[AppBootstrap] ERROR: Failed to create " << label
              << " directory: " << path.toStdString() << "\n";
    return false;
}

bool ensureRuntimeDirectoriesReady(const RuntimeDirectories& directories)
{
    return ensureDirectoryExists(directories.configDir, "config")
        && ensureDirectoryExists(directories.logsDir, "logs")
        && ensureDirectoryExists(directories.filesDir, "files");
}

} // namespace

AppBootstrap::AppBootstrap() = default;
AppBootstrap::~AppBootstrap() = default;

void AppBootstrap::init()
{
    std::cout << "[AppBootstrap] Starting initialization...\n";
    
    // 重置状态
    m_initialized = false;
    m_lastError.clear();
    
    // 阶段1: 配置初始化
    if (!initConfiguration()) {
        m_lastError = "Configuration initialization failed";
        std::cerr << "[AppBootstrap] ERROR: " << m_lastError << "\n";
        return;
    }
    
    // 阶段2: 服务初始化
    if (!initServices()) {
        m_lastError = "Services initialization failed";
        std::cerr << "[AppBootstrap] ERROR: " << m_lastError << "\n";
        return;
    }
    
    // 阶段3: 数据库初始化（如果需要）
    if (!initDatabase()) {
        // 数据库初始化失败可能不是致命的，记录警告
        std::cout << "[AppBootstrap] WARNING: Database initialization failed\n";
    }
    
    m_initialized = true;
    std::cout << "[AppBootstrap] Initialization completed successfully\n";
}

void AppBootstrap::start()
{
    if (!m_initialized) {
        m_lastError = "Cannot start: application not initialized";
        std::cerr << "[AppBootstrap] ERROR: " << m_lastError << "\n";
        return;
    }
    
    std::cout << "[AppBootstrap] Starting UI...\n";
    
    if (!initQmlEngine()) {
        m_lastError = "QML engine initialization failed";
        std::cerr << "[AppBootstrap] ERROR: " << m_lastError << "\n";
        return;
    }
    
    m_started = true;
    std::cout << "[AppBootstrap] UI started successfully\n";
}

void AppBootstrap::shutdown()
{
    std::cout << "[AppBootstrap] Shutting down...\n";

#if defined(ASTOCK_ENABLE_JUJIN_MARKET)
    shutdownOptionalConnectors();
#endif

    if (m_eventBus) {
        engine::register_engine_event_bus(nullptr);
        m_eventBus->stop(true, 1000);
        m_eventBus.reset();
    }
    
    // 按逆序清理
    m_vasAurora.reset();
    m_engine.reset();
    foundation::Foundation::instance().shutdown();
    
    m_initialized = false;
    m_started = false;
    
    std::cout << "[AppBootstrap] Shutdown completed\n";
}

bool AppBootstrap::initConfiguration()
{
    std::cout << "[AppBootstrap] Initializing configuration...\n";
    
    try {
        const RuntimeDirectories directories = runtimeDirectories();
        if (!ensureRuntimeDirectoriesReady(directories)) {
            return false;
        }

        installQtMessageFileLogger(directories.logFilePath);

        const std::string configDir = directories.configDir.toStdString();
        std::string profile = "development";

        foundation::Config foundationConfig;
        foundationConfig.profile = profile;
        foundationConfig.config_dir = configDir;
        foundationConfig.enable_console_log = true;
        foundationConfig.enable_file_log = true;
        foundationConfig.log_file = directories.logFilePath.toStdString();

        if (!foundation::Foundation::instance().initialize(foundationConfig)) {
            std::cerr << "[AppBootstrap] ERROR: Foundation initialization failed\n";
            return false;
        }
        
        foundation::config::ConfigManager::instance().initialize(profile, configDir);
        
        // 简单验证关键配置 - 通过实例方法调用
        auto& configManager = foundation::config::ConfigManager::instance();
        auto appName = configManager.get_app_config_string("app.name", "");
        if (appName.empty()) {
            std::cout << "[AppBootstrap] WARNING: Application name not configured\n";
        }
        
        std::cout << "[AppBootstrap] Runtime directories ready: "
                  << directories.configDir.toStdString() << ", "
                  << directories.logsDir.toStdString() << ", "
                  << directories.filesDir.toStdString() << "\n";
        std::cout << "[AppBootstrap] Configuration initialized\n";
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[AppBootstrap] Configuration error: " << e.what() << "\n";
        return false;
    }
}

bool AppBootstrap::initServices()
{
    std::cout << "[AppBootstrap] Initializing services...\n";
    
    try {
        if (!engine::get_engine_event_bus()) {
            m_eventBus = engine::EventBus::create();
            if (!m_eventBus) {
                std::cerr << "[AppBootstrap] ERROR: Failed to create application EventBus\n";
                return false;
            }

            if (!m_eventBus->start()) {
                std::cerr << "[AppBootstrap] ERROR: Failed to start application EventBus\n";
                return false;
            }

            engine::register_engine_event_bus(m_eventBus.get());
            std::cout << "[AppBootstrap] Application EventBus initialized\n";
        }

        // 初始化执行器（当前为空，保留占位）
        // executor_ = std::make_shared<InlineExecutor>();
        
        // 初始化引擎（当前为空，保留占位）
        // engine_ = std::make_unique<Engine>(executor_);
        
        // 初始化FactorService
        m_factorService = FactorService::instance();
        if (!m_factorService) {
            std::cerr << "[AppBootstrap] ERROR: Failed to get FactorService instance\n";
            return false;
        }
        
        // 初始化StrategyService - 单例模式，不需要保存指针
        StrategyService* strategyService = StrategyService::instance();
        if (!strategyService) {
            std::cerr << "[AppBootstrap] ERROR: Failed to get StrategyService instance\n";
            return false;
        }

        MarketDataService* marketDataService = MarketDataService::instance();
        if (!marketDataService) {
            std::cerr << "[AppBootstrap] ERROR: Failed to get MarketDataService instance\n";
            return false;
        }
        marketDataService->initialize();
        
        // 自动初始化StrategyService
        std::cout << "[AppBootstrap] Initializing StrategyService...\n";
        strategyService->initialize();

        RiskMonitorService* riskMonitorService = RiskMonitorService::instance();
        if (!riskMonitorService) {
            std::cerr << "[AppBootstrap] ERROR: Failed to get RiskMonitorService instance\n";
            return false;
        }
        riskMonitorService->initialize();

        TradeExecutionService* tradeExecutionService = TradeExecutionService::instance();
        if (!tradeExecutionService) {
            std::cerr << "[AppBootstrap] ERROR: Failed to get TradeExecutionService instance\n";
            return false;
        }
        tradeExecutionService->initialize();

        PositionAccountService* positionAccountService = PositionAccountService::instance();
        if (!positionAccountService) {
            std::cerr << "[AppBootstrap] ERROR: Failed to get PositionAccountService instance\n";
            return false;
        }
        positionAccountService->initialize();

        TradingConnectionConfigService* tradingConnectionConfigService = TradingConnectionConfigService::instance();
        if (!tradingConnectionConfigService) {
            std::cerr << "[AppBootstrap] ERROR: Failed to get TradingConnectionConfigService instance\n";
            return false;
        }
        tradingConnectionConfigService->initialize();

#if defined(ASTOCK_ENABLE_JUJIN_MARKET)
        m_jujinMarketConnector = std::make_unique<JujinMarketConnector>();
        if (m_jujinMarketConnector->isEnabledByEnvironment()) {
            if (!m_jujinMarketConnector->start()) {
                std::cerr << "[AppBootstrap] WARNING: Jujin market connector failed: "
                          << m_jujinMarketConnector->lastError() << "\n";
            }
        } else {
            std::cout << "[AppBootstrap] Jujin market connector disabled by environment\n";
        }
#endif
        
        std::cout << "[AppBootstrap] Services initialized\n";
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[AppBootstrap] Services error: " << e.what() << "\n";
        return false;
    }
}

bool AppBootstrap::initDatabase()
{
    std::cout << "[AppBootstrap] Initializing database...\n";
    
    // 这里可以添加数据库连接初始化
    // 当前项目可能通过FactorService间接初始化数据库
    // 如果需要显式初始化，可以在这里添加
    
    std::cout << "[AppBootstrap] Database initialization skipped (handled by services)\n";
    return true; // 暂时返回成功，数据库初始化可能不是必需的
}

bool AppBootstrap::initQmlEngine()
{
    std::cout << "[AppBootstrap] Initializing QML engine...\n";
    
    try {
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_vasAurora = std::make_unique<wang::VasAurora>(m_engine.get());
        
        std::cout << "[AppBootstrap] QML engine initialized\n";
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[AppBootstrap] QML engine error: " << e.what() << "\n";
        return false;
    }
}

#if defined(ASTOCK_ENABLE_JUJIN_MARKET)
void AppBootstrap::shutdownOptionalConnectors()
{
    if (m_jujinMarketConnector) {
        m_jujinMarketConnector->stop();
        m_jujinMarketConnector.reset();
    }
}
#endif

FactorService* AppBootstrap::getFactorService()
{
    return FactorService::instance();
}
