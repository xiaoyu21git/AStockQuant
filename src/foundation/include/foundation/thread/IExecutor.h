#pragma once
#include <functional>
namespace foundation {
namespace thread {
class IExecutor {
public:
    virtual ~IExecutor() = default;

    // 提交任务执行
    virtual void post(std::function<void()> task) = 0;

    // 是否在 executor 所在线程
    virtual bool isInExecutorThread() const = 0;
};
}
}