#pragma once
// EventBridgePoller — 轮询 live.event_bridge 桥接表, 将 Python 事件发布到 C++ EventBus
//
// 运行方式: AppBootstrap 启动时创建, 独立线程每 60s 轮询一次
// 职责: SELECT unconsumed rows → EventFormat → C++ EventBus.publish() → UPDATE consumed=true

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace astock::infrastructure::database {

class EventBridgePoller {
public:
    static EventBridgePoller& instance();

    void start();
    void stop();
    bool isRunning() const { return m_running.load(); }

private:
    EventBridgePoller() = default;
    ~EventBridgePoller();

    void pollLoop();

    std::unique_ptr<std::thread> m_thread;
    std::atomic<bool> m_running{false};
    int m_pollCount = 0;
    int m_totalPublished = 0;
};

} // namespace astock::infrastructure::database
