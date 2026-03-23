#pragma once

#include <memory>
#include <QQmlApplicationEngine>
#include "../../ui/bridge/include/FactorService.h"

namespace wang {
class VasAurora;
}

class IExecutor;

class AppBootstrap {
public:
    AppBootstrap();
    ~AppBootstrap();

    // 保持现有接口
    void init();
    void start();
    void shutdown();

    // 保持向后兼容
    FactorService* getFactorService();

    // 新增：简单状态查询
    bool isInitialized() const { return m_initialized; }
    bool isStarted() const { return m_started; }
    const std::string& lastError() const { return m_lastError; }

private:
    // 拆分为清晰的私有方法
    bool initConfiguration();
    bool initServices();
    bool initDatabase();  // 如果需要
    bool initQmlEngine();
    
private:
    bool m_initialized = false;
    bool m_started = false;
    std::string m_lastError;
    
    std::shared_ptr<IExecutor> executor_;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    std::unique_ptr<wang::VasAurora> m_vasAurora;
    FactorService* m_factorService = nullptr;  // 单例指针，不拥有所有权
};
