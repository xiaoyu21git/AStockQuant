#pragma once
// PythonEventBridge — 嵌入式Python金融事件感知调度器
// App启动时初始化Python解释器, 加载astock_engine.events.scheduler

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace app {

class PythonEventBridge {
public:
    static PythonEventBridge& instance();

    /// @brief 启动嵌入式Python解释器并运行事件感知调度器
    /// 路径从可执行文件位置自动推导
    bool start();

    void stop();
    bool isRunning() const { return m_running.load(); }

private:
    PythonEventBridge() = default;
    ~PythonEventBridge();

    static void schedulerThread(const std::string& eventsPath,
                                const std::string& binPath);

    std::unique_ptr<std::thread> m_thread;
    std::atomic<bool> m_running{false};
};

} // namespace app
