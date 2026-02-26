#pragma once

#include <memory>
#include <QQmlApplicationEngine>

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

private:
    void initExecutor();
    void initEngine();   // 先占位
    void initUI();       // 先占位

private:
    std::shared_ptr<IExecutor> executor_;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    std::unique_ptr<wang::VasAurora> m_vasAurora;
};
