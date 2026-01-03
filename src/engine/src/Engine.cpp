#include "Engine.h"
#include <iostream>

Engine::Engine(std::shared_ptr<IExecutor> executor)
    : executor_(executor)
{
}

Engine::~Engine() = default;

void Engine::start()
{
    std::cout << "[Engine] start\n";
}

void Engine::stop()
{
    std::cout << "[Engine] stop\n";
}

void Engine::postTask(std::function<void()> task)
{
    pendingTasks_.push_back(std::move(task));
}

void Engine::runOnce()
{
    std::cout << "[Engine] runOnce\n";

    // 复制当前任务列表，清空 pendingTasks_
    auto tasks = std::move(pendingTasks_);
    pendingTasks_.clear();

    // 执行每个任务，通过 executor
    for (auto& task : tasks) {
        executor_->post(task);
    }
}
