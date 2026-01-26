
#pragma once
#include <thread>
#include <atomic>
#include <condition_variable>
#include "EventQueue.h"
#include "EventDispatcher.h"
#include "SubscriptionManager.h"
#include "DispatchPolicy.h"
#include "foundation/thread/ThreadExit.h"
namespace engine {

class DispatchWorker {
private:
    std::thread worker_thread_;
    std::atomic<bool> stop_flag_{false};
    std::condition_variable cv_;
    std::mutex cv_mutex_;
    CThreadExit m_WorkExit;
     // 改为智能指针管理生命周期
    std::shared_ptr<EventQueue> queue_;
    std::shared_ptr<SubscriptionManager> subs_mgr_;
    std::shared_ptr<EventDispatcher> dispatcher_;
    std::shared_ptr<DispatchStrategy> strategy_;
    //void run_loop_internal();
    //static void workThread( void* param);
    int WorkLoop();
public:
    DispatchWorker(std::shared_ptr<EventQueue> queue, std::shared_ptr<SubscriptionManager> subs, std::shared_ptr<EventDispatcher> disp,std::shared_ptr<DispatchStrategy> strat);
    ~DispatchWorker();
    void run_loop();   // 外部启动线程
    void stop();       // 安全停止线程
    void notify();     // 新事件通知
};

} // namespace engine

