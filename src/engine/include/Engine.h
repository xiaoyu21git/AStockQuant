#pragma once
#include <memory>
#include <functional>
#include <vector>
#include "IExecutor.h"

class Engine {
public:
    explicit Engine(std::shared_ptr<IExecutor> executor);
    ~Engine();

    void start();
    void stop();
    void runOnce(); // 执行一次调度

    // 添加任务
    void postTask(std::function<void()> task);

private:
    std::shared_ptr<IExecutor> executor_;
    std::vector<std::function<void()>> pendingTasks_;
};
