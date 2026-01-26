#include "AppBootstrap.h"

// #include "InlineExecutor.h"
// #include "IExecutor.h"
#include "foundation.h"
#include "foundation/config/ConfigManager.hpp"
// 下面这些现在可以是空头文件或 forward declare
// #include "engine/Engine.h"

#include <iostream>
#include "VasAurora.hpp"
AppBootstrap::AppBootstrap() = default;
AppBootstrap::~AppBootstrap() = default;

void AppBootstrap::init()
{
    std::cout << "[App] init\n";
    // 1. 确保配置目录存在（可选）
    std::string configDir = "./config";
    std::string profile = "development";

    // 2. 初始化配置管理器
    foundation::config::ConfigManager::instance().initialize(profile, configDir);

    // // 3. 之后的模块可以直接获取配置
    // auto appName = foundation::config::ConfigManager::get_app_config_string("app.name", "defaultApp");
    // auto port = foundation::config::ConfigManager::get_app_config_int("server.port", 8080);
    initExecutor();
    initEngine();
}

void AppBootstrap::start()
{
    
    std::cout << "[App] start\n";
    std::make_unique<wang::VasAurora>(new QQmlApplicationEngine());
    // engine_->start();  // 未来
}

void AppBootstrap::shutdown()
{
    std::cout << "[App] shutdown\n";
    // engine_->stop();   // 未来
}

void AppBootstrap::initExecutor()
{
    // 现在改用线程池
    //executor_ = std::make_shared<ThreadPoolExecutor>(4); // 4 个线程
    // executor_ = std::make_shared<InlineExecutor>();
}

void AppBootstrap::initEngine()
{
    // engine_ = std::make_unique<Engine>(executor_);
}
