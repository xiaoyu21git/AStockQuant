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

    void init();
    void start();
    void shutdown();

    // 获取FactorService实例
    FactorService* getFactorService();

private:
    void initExecutor();
    void initEngine();   // 先占位
    void initUI();       // 先占位
    void initServices(); // 初始化服务

private:
    std::shared_ptr<IExecutor> executor_;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    std::unique_ptr<wang::VasAurora> m_vasAurora;
    FactorService* m_factorService;  // 单例指针，不拥有所有权
};
