// thread/InlineExecutor.cpp
#include "foundation/thread/InlineExecutor.h"


namespace foundation {
namespace thread {

void InlineExecutor::post(std::function<void()> task) {
    if (task) {
        task();
    }
}

std::future<void> InlineExecutor::submit(std::function<void()> task) {
    auto promise = std::make_shared<std::promise<void>>();
    
    try {
        if (task) {
            task();
        }
        promise->set_value();
    } catch (...) {
        promise->set_exception(std::current_exception());
    }
    
    return promise->get_future();
}

bool InlineExecutor::isInExecutorThread() const {
    return true;
}

size_t InlineExecutor::getWorkerCount() const {
    return 1;
}

size_t InlineExecutor::getPendingTaskCount() const {
    return 0;
}

void InlineExecutor::wait_for_idle() {
    // 内联执行器总是空闲的
}

void InlineExecutor::start() {
    // 内联执行器总是运行的
}

void InlineExecutor::stop() {
    // 内联执行器不能被停止
}

bool InlineExecutor::isRunning() const {
    return true;
}

} // namespace thread
} // namespace foundation