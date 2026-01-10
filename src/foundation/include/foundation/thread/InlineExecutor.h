// IExecutor.h
#pragma once
#include <functional>
#include <future>
#include <memory>

namespace foundation {
namespace thread {

class InlineExecutor {
public:
    virtual ~InlineExecutor() = default;
    
    virtual void post(std::function<void()> task) = 0;
    
    // 非模板版本
    virtual std::future<void> submit(std::function<void()> task) = 0;
    
    virtual bool isInExecutorThread() const = 0;
    
    virtual size_t getWorkerCount() const = 0;
    virtual size_t getPendingTaskCount() const = 0;
    virtual void wait_for_idle() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
};

using IExecutorPtr = std::shared_ptr<InlineExecutor>;

} // namespace thread
} // namespace foundation