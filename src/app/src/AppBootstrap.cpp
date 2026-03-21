#include "AppBootstrap.h"

// #include "InlineExecutor.h"
// #include "IExecutor.h"
#include "foundation.h"
#include "foundation/config/ConfigManager.hpp"
// 下面这些现在可以是空头文件或 forward declare
// #include "engine/Engine.h"

#include <iostream>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "VasAurora.hpp"

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
    
    // 按逆序清理
    m_vasAurora.reset();
    m_engine.reset();
    
    m_initialized = false;
    m_started = false;
    
    std::cout << "[AppBootstrap] Shutdown completed\n";
}

bool AppBootstrap::initConfiguration()
{
    std::cout << "[AppBootstrap] Initializing configuration...\n";
    
    try {
        std::string configDir = "./config";
        std::string profile = "development";
        
        foundation::config::ConfigManager::instance().initialize(profile, configDir);
        
        // 简单验证关键配置 - 通过实例方法调用
        auto& configManager = foundation::config::ConfigManager::instance();
        auto appName = configManager.get_app_config_string("app.name", "");
        if (appName.empty()) {
            std::cout << "[AppBootstrap] WARNING: Application name not configured\n";
        }
        
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
        
        // 检查FactorService是否已正确初始化
        // FactorService在instance()中自动初始化，这里可以添加简单的健康检查
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

FactorService* AppBootstrap::getFactorService()
{
    return FactorService::instance();
}