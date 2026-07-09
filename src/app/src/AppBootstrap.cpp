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
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QIcon>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QtGlobal>
#include "VasAurora.hpp"

#include "../../domain/strategy/include/RiskEvaluator.h"
#include "../../domain/strategy/include/RiskManager.h"
#include "../../domain/strategy/include/StrategyManager.h"
#include "../../engine/include/GmSessionEngine.h"
#include "../../engine/include/TradeEngine.h"
#include "../../engine/include/AccountEngine.h"
#include "../../engine/include/OrderManager.h"
#include "../../ui/bridge/include/MarketDataBridge.h"
#include "database/NativePgConnectionPool.h"
#include "database/PostMarketSyncService.h"
#include "../../../domain/strategy/include/EventRiskSubscriber.h"
// MarketDataFacade/SymbolMapper/MarketDataRepository 已移除，行情桥接改用预留接口
#if defined(ASTOCK_ENABLE_JUJIN_MARKET)
#include "JujinMarketConnector.h"
#endif

namespace {

struct RuntimeDirectories {
    QString baseDir;
    QString configDir;
    QString logsDir;
    QString filesDir;
    QString cacheDir;
    QString tempDir;
    QString logFilePath;
};

RuntimeDirectories runtimeDirectories()
{
    const QString baseDir = QCoreApplication::applicationDirPath();
    const QDir dir(baseDir);

    return RuntimeDirectories{
        baseDir,
        dir.filePath(QStringLiteral("config")),
        dir.filePath(QStringLiteral("logs")),
        dir.filePath(QStringLiteral("files")),
        dir.filePath(QStringLiteral("cache")),
        dir.filePath(QStringLiteral("temp")),
        dir.filePath(QStringLiteral("logs/astockquantapp.log"))
    };
}

bool ensureDirectoryExists(const QString& path, const char* label);

void configureProcessTempDirectory(const RuntimeDirectories& directories)
{
    if (!ensureDirectoryExists(directories.tempDir, "temp")) {
        return;
    }

    const QString qmlCacheDir = QDir(directories.cacheDir).filePath(QStringLiteral("qmlcache"));
    if (!ensureDirectoryExists(qmlCacheDir, "qmlcache")) {
        return;
    }

    const QByteArray tempDirUtf8 = directories.tempDir.toUtf8();
    const QByteArray qmlCacheDirUtf8 = qmlCacheDir.toUtf8();
    qputenv("TEMP", tempDirUtf8);
    qputenv("TMP", tempDirUtf8);
    qputenv("TMPDIR", tempDirUtf8);
    qputenv("QML_DISK_CACHE_PATH", qmlCacheDirUtf8);
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

    INTERNAL_ERROR_STREAM << "[AppBootstrap] ERROR: Failed to create " << label
                         << " directory: " << path.toStdString();
    return false;
}

bool ensureRuntimeDirectoriesReady(const RuntimeDirectories& directories)
{
    return ensureDirectoryExists(directories.configDir, "config")
        && ensureDirectoryExists(directories.logsDir, "logs")
    && ensureDirectoryExists(directories.filesDir, "files")
    && ensureDirectoryExists(directories.cacheDir, "cache")
    && ensureDirectoryExists(directories.tempDir, "temp");
}

void applyRootWindowIcon(QQmlApplicationEngine* engine)
{
    if (!engine) {
        return;
    }

    const QIcon icon(QStringLiteral(":/resources/icons/app.ico"));
    if (icon.isNull()) {
        return;
    }

    const QList<QObject*> rootObjects = engine->rootObjects();
    for (QObject* object : rootObjects) {
        if (auto* window = qobject_cast<QQuickWindow*>(object)) {
            window->setIcon(icon);
        }
    }
}

} // namespace

AppBootstrap::AppBootstrap() = default;
AppBootstrap::~AppBootstrap() = default;

void AppBootstrap::init()
{
    INTERNAL_INFO_STREAM << "[AppBootstrap] Starting initialization...";
    
    // 重置状态
    m_initialized = false;
    m_lastError.clear();
    
    // 阶段1: 配置初始化
    if (!initConfiguration()) {
        m_lastError = "Configuration initialization failed";
        INTERNAL_ERROR_STREAM << "[AppBootstrap] ERROR: " << m_lastError;
        return;
    }
    
    // 阶段2: 服务初始化
    if (!initServices()) {
        m_lastError = "Services initialization failed";
        INTERNAL_ERROR_STREAM << "[AppBootstrap] ERROR: " << m_lastError;
        return;
    }
    
    // 阶段3: 数据库初始化（如果需要）
    if (!initDatabase()) {
        // 数据库初始化失败可能不是致命的，记录警告
        INTERNAL_INFO_STREAM << "[AppBootstrap] WARNING: Database initialization failed";
    }
    
    m_initialized = true;
    INTERNAL_INFO_STREAM << "[AppBootstrap] Initialization completed successfully";
}

void AppBootstrap::start()
{
    if (!m_initialized) {
        m_lastError = "Cannot start: application not initialized";
        INTERNAL_ERROR_STREAM << "[AppBootstrap] ERROR: " << m_lastError;
        return;
    }
    
    INTERNAL_INFO_STREAM << "[AppBootstrap] Starting UI...";
    
    if (!initQmlEngine()) {
        m_lastError = "QML engine initialization failed";
        INTERNAL_ERROR_STREAM << "[AppBootstrap] ERROR: " << m_lastError;
        return;
    }
    
    m_started = true;
    scheduleDeferredStartupInitialization();
    INTERNAL_INFO_STREAM << "[AppBootstrap] UI started successfully";
}

void AppBootstrap::shutdown()
{
    INTERNAL_INFO_STREAM << "[AppBootstrap] Shutting down...";

    // 先停所有策略引擎 (线程安全退出)
    domain::strategy::StrategyManager::instance().stopAll();

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

    INTERNAL_INFO_STREAM << "[AppBootstrap] Shutdown completed";
}

bool AppBootstrap::initConfiguration()
{
    INTERNAL_INFO_STREAM << "[AppBootstrap] Initializing configuration...";
    
    try {
        const RuntimeDirectories directories = runtimeDirectories();
        if (!ensureRuntimeDirectoriesReady(directories)) {
            return false;
        }

        configureProcessTempDirectory(directories);

        const std::string configDir = directories.configDir.toStdString();
        std::string profile = "development";

        foundation::Config foundationConfig;
        foundationConfig.profile = profile;
        foundationConfig.config_dir = configDir;
        foundationConfig.enable_console_log = true;
        foundationConfig.enable_file_log = true;
        foundationConfig.log_file = directories.logFilePath.toStdString();

        if (!foundation::Foundation::instance().initialize(foundationConfig)) {
            INTERNAL_ERROR_STREAM << "[AppBootstrap] ERROR: Foundation initialization failed";
            return false;
        }
        
        foundation::config::ConfigManager::instance().initialize(profile, configDir);

        // ── 统一预加载所有命名配置文件（一次性加载，避免各模块惰性调用的时序问题）──
        {
            auto& cfg = foundation::config::ConfigManager::instance();
            using CF = foundation::config::ConfigFile;
            for (auto f : {CF::TradingConnection, CF::RiskConfig, CF::Jujin}) {
                auto node = cfg.loadConfigFile(f);
                if (!node || node->isEmpty()) {
                    INTERNAL_WARN_STREAM << "[AppBootstrap] 配置文件未就绪: "
                                         << static_cast<int>(f);
                }
            }

            // ── 同步风控 + 费率配置到 TradingSystem ──
            auto riskNode = cfg.loadConfigFile(CF::RiskConfig);
            if (riskNode && !riskNode->isEmpty()) {
                using namespace domain::strategy;
                RiskConfig c = RiskConfig::defaults();
                c.stopLossPercent        = riskNode->getPath("stopLossPercent",'.').asDouble(c.stopLossPercent);
                c.takeProfitPercent      = riskNode->getPath("takeProfitPercent",'.').asDouble(c.takeProfitPercent);
                c.maxDrawdownLimitPercent = riskNode->getPath("maxDrawdownLimitPercent",'.').asDouble(c.maxDrawdownLimitPercent);
                c.maxDailyLossPercent    = riskNode->getPath("maxDailyLossPercent",'.').asDouble(c.maxDailyLossPercent);
                c.breakerLevel1Percent   = riskNode->getPath("breakerLevel1Percent",'.').asDouble(c.breakerLevel1Percent);
                c.breakerLevel2Percent   = riskNode->getPath("breakerLevel2Percent",'.').asDouble(c.breakerLevel2Percent);
                c.breakerLevel3Percent   = riskNode->getPath("breakerLevel3Percent",'.').asDouble(c.breakerLevel3Percent);
                c.maxPositionPercent     = riskNode->getPath("maxPositionPercent",'.').asDouble(c.maxPositionPercent);
                c.maxTotalExposurePercent = riskNode->getPath("maxTotalExposurePercent",'.').asDouble(c.maxTotalExposurePercent);
                c.orderSizeLimitWan      = riskNode->getPath("orderSizeLimitWan",'.').asDouble(c.orderSizeLimitWan);
                c.slippageLimitPercent   = riskNode->getPath("slippageLimitPercent",'.').asDouble(c.slippageLimitPercent);
                c.turnoverLimitWan       = riskNode->getPath("turnoverLimitWan",'.').asDouble(c.turnoverLimitWan);
                c.commissionRate         = riskNode->getPath("commissionRate",'.').asDouble(c.commissionRate);
                c.minCommission          = riskNode->getPath("minCommission",'.').asDouble(c.minCommission);
                c.stampTaxRate           = riskNode->getPath("stampTaxRate",'.').asDouble(c.stampTaxRate);
                domain::strategy::RiskManager::instance().setRiskConfig(c);
                INTERNAL_INFO_STREAM << "[AppBootstrap] 风控+费率配置已同步";
            }
        }
        
        // 简单验证关键配置 - 通过实例方法调用
        auto& configManager = foundation::config::ConfigManager::instance();
        auto appName = configManager.get_app_config_string("app.name", "");
        if (appName.empty()) {
            INTERNAL_INFO_STREAM << "[AppBootstrap] WARNING: Application name not configured";
        }
        
        INTERNAL_INFO_STREAM << "[AppBootstrap] Runtime directories ready: "
                             << directories.configDir.toStdString() << ", "
                             << directories.logsDir.toStdString() << ", "
                             << directories.filesDir.toStdString() << ", "
                             << directories.cacheDir.toStdString() << ", "
                             << directories.tempDir.toStdString();
        INTERNAL_INFO_STREAM << "[AppBootstrap] Configuration initialized";
        return true;
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[AppBootstrap] Configuration error: " << e.what();
        return false;
    }
}

bool AppBootstrap::initServices()
{
    INTERNAL_INFO_STREAM << "[AppBootstrap] Initializing services...";
    
    try {
        if (!engine::get_engine_event_bus()) {
            m_eventBus = engine::EventBus::create();
            if (!m_eventBus) {
                INTERNAL_ERROR_STREAM << "[AppBootstrap] ERROR: Failed to create application EventBus";
                return false;
            }

            if (!m_eventBus->start()) {
                INTERNAL_ERROR_STREAM << "[AppBootstrap] ERROR: Failed to start application EventBus";
                return false;
            }

            engine::register_engine_event_bus(m_eventBus.get());
            INTERNAL_INFO_STREAM << "[AppBootstrap] Application EventBus initialized";

            // 启动事件风控订阅器（订阅 news.* 事件，动态调整风控参数）
            domain::strategy::EventRiskSubscriber::instance().start();
        }

        // ── GmSessionEngine（唯一 gmsdk 连接）在 QML 之前初始化 ──
        {
            std::string token, accountId, strategyId;
            auto cfg = foundation::config::ConfigManager::instance()
                .loadConfigFile(foundation::config::ConfigFile::TradingConnection);
            if (cfg && !cfg->isNull()) {
                token      = cfg->has("token")      ? cfg->get("token").asString()      : "";
                accountId  = cfg->has("accountId")  ? cfg->get("accountId").asString()  : "";
                strategyId = cfg->has("gmStrategyId")? cfg->get("gmStrategyId").asString(): "";
            }
            if (!token.empty()) {
                // AccountEngine 先订阅 EventBus，再启动 GmSessionEngine（避开 on_init 事件丢失）
                engine::AccountEngine::instance();
                auto& sdk = engine::GmSessionEngine::instance();
                if (sdk.initialize(token, strategyId.empty() ? accountId : strategyId)) {
                    auto* s = sdk.strategy();
                    engine::TradeEngine::instance().initialize(s);
                    engine::AccountEngine::instance().initialize(s);
                    engine::OrderManager::instance().initialize(s);
                    INTERNAL_INFO_STREAM << "[AppBootstrap] GmSessionEngine initialized";
                }
            }
        }

        INTERNAL_INFO_STREAM << "[AppBootstrap] Services initialized";
        return true;

    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[AppBootstrap] Services error: " << e.what();
        return false;
    }
}

bool AppBootstrap::initDatabase()
{
    INTERNAL_INFO_STREAM << "[AppBootstrap] Initializing database...";

    // 预热数据库连接池（NativePgConnectionPool 自动惰性初始化）
    auto conn = astock::database::NativePgConnectionPool::instance().getConnection();
    if (conn) {
        INTERNAL_INFO_STREAM << "[AppBootstrap] Native PG pool warmed up";
    }

    return true;
}

bool AppBootstrap::initQmlEngine()
{
    INTERNAL_INFO_STREAM << "[AppBootstrap] Initializing QML engine...";

    try {
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_vasAurora = std::make_unique<wang::VasAurora>(m_engine.get());
        applyRootWindowIcon(m_engine.get());

        INTERNAL_INFO_STREAM << "[AppBootstrap] QML engine initialized";
        return true;

    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[AppBootstrap] QML engine error: " << e.what();
        return false;
    }
}

void AppBootstrap::scheduleDeferredStartupInitialization()
{
    if (m_deferredStartupScheduled) {
        return;
    }

    QObject* context = QCoreApplication::instance();
    if (!context) {
        initializeDeferredUiServices();
        return;
    }

    m_deferredStartupScheduled = true;
    QTimer::singleShot(0, context, [this]() {
        initializeDeferredUiServices();
    });
}

void AppBootstrap::initializeDeferredUiServices()
{
    if (m_deferredUiServicesInitialized) {
        return;
    }

#if defined(ASTOCK_ENABLE_JUJIN_MARKET)
    scheduleOptionalConnectorReconcile();
#endif

    INTERNAL_INFO_STREAM << "[AppBootstrap] Deferred startup: MarketDataBridge uses stub interface";

    m_deferredUiServicesInitialized = true;

    if (QObject* context = QCoreApplication::instance()) {
        QTimer::singleShot(0, context, [this]() {
            initializeDeferredDomainServices();
        });
    } else {
        initializeDeferredDomainServices();
    }
}

void AppBootstrap::initializeDeferredDomainServices()
{
    if (m_deferredDomainServicesInitialized) {
        return;
    }

    // 启动盘后数据同步服务
    astock::infrastructure::database::PostMarketSyncService::instance().start();

    initializeDeferredTradingServices();

    INTERNAL_INFO_STREAM << "[AppBootstrap] Deferred startup phase 2/3: domain & trading services initialized";

    m_deferredDomainServicesInitialized = true;
}

void AppBootstrap::initializeDeferredTradingServices()
{
    if (m_deferredTradingServicesInitialized) {
        return;
    }

    // ── 交易系统由 TradeExecutionBridge::ensureInitialized 延迟初始化 ──
    // (需 QML 加载配置后，通过 isLiveBridgeReady() 触发掘金网关连接)

    INTERNAL_INFO_STREAM << "[AppBootstrap] Deferred startup phase 3/3: ready";

    m_deferredTradingServicesInitialized = true;
}

#if defined(ASTOCK_ENABLE_JUJIN_MARKET)
void AppBootstrap::scheduleOptionalConnectorReconcile()
{
    reconcileOptionalConnectors();
}

void AppBootstrap::reconcileOptionalConnectors()
{
    if (!m_jujinMarketConnector) {
        m_jujinMarketConnector = std::make_unique<JujinMarketConnector>();
    }

    INTERNAL_INFO_STREAM << "[AppBootstrap] JMC 启动中...";
    if (m_jujinMarketConnector->isEnabledByEnvironment()) {
        if (!m_jujinMarketConnector->start()) {
            INTERNAL_ERROR_STREAM << "[AppBootstrap] JMC 启动失败: " << m_jujinMarketConnector->lastError();
        } else {
            INTERNAL_INFO_STREAM << "[AppBootstrap] JMC 启动成功";
        }
        return;
    }

    INTERNAL_INFO_STREAM << "[AppBootstrap] Jujin market connector disabled by environment";
    m_jujinMarketConnector->stop();
}

void AppBootstrap::shutdownOptionalConnectors()
{
    if (m_jujinMarketConnector) {
        m_jujinMarketConnector->stop();
        m_jujinMarketConnector.reset();
    }
}
#endif

