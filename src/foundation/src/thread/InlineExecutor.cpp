// thread/InlineExecutor.cpp
#include "InlineExecutor.h"
#include <stdexcept>

void InlineExecutor::post(std::function<void()> task) {
    if (!task) {
        throw std::invalid_argument("Task cannot be null");
    }
    
    try {
        task();
    } catch (...) {
        // 内联执行器直接抛出异常
        throw;
    }
}

bool InlineExecutor::isInExecutorThread() const {
    return true; // 总是在调用线程中执行
}