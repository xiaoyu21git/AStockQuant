#pragma once

#include <memory>
#include <string>
#include <QQmlApplicationEngine>
#include <QMetaObject>

namespace engine {
class EventBus;
}

namespace wang {
class VasAurora;
}

namespace app::facade {
class MarketDataFacade;
}

class IExecutor;
#if defined(ASTOCK_ENABLE_JUJIN_MARKET)
class JujinMarketConnector;
#endif

class AppBootstrap {
public:
    AppBootstrap();
    ~AppBootstrap();

    // 保持现有接口
    void init();
    void start();
    void shutdown();

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
    void scheduleDeferredStartupInitialization();
    void initializeDeferredUiServices();
    void initializeDeferredDomainServices();
    void initializeDeferredTradingServices();
#if defined(ASTOCK_ENABLE_JUJIN_MARKET)
    void shutdownOptionalConnectors();
    void reconcileOptionalConnectors();
    void scheduleOptionalConnectorReconcile();
#endif
    
private:
    bool m_initialized = false;
    bool m_started = false;
    std::string m_lastError;
    
    std::shared_ptr<IExecutor> executor_;
    std::shared_ptr<engine::EventBus> m_eventBus;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    std::unique_ptr<wang::VasAurora> m_vasAurora;
    bool m_deferredStartupScheduled = false;
    bool m_deferredUiServicesInitialized = false;
    bool m_deferredDomainServicesInitialized = false;
    bool m_deferredTradingServicesInitialized = false;
    QMetaObject::Connection m_tradingConfigurationChangedConnection;
#if defined(ASTOCK_ENABLE_JUJIN_MARKET)
    std::unique_ptr<JujinMarketConnector> m_jujinMarketConnector;
    bool m_optionalConnectorReconcilePending = false;
#endif
};
