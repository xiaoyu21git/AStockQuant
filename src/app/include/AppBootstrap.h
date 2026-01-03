#pragma once

#include <memory>

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
};
