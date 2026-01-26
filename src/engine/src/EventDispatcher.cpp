#include "EventDispatcher.h"

namespace engine {

void EventDispatcher::dispatch(const std::vector<std::unique_ptr<Event>>& events, const SubscriptionManager& subs_mgr) {
    for (const auto& event : events) {
        if (!event) continue;
        
        try {
            auto event_copy = event->clone();
            auto sp_event = std::shared_ptr<Event>(std::move(event_copy));
            notify_subscribers(sp_event, subs_mgr);
        } catch (const std::exception& e) {
            std::cerr << "[EventDispatcher] 事件分发异常: " << e.what() << std::endl;
            // 继续处理下一个事件
        } catch (...) {
            std::cerr << "[EventDispatcher] 事件分发未知异常" << std::endl;
            // 继续处理下一个事件
        }
    }
}

void EventDispatcher::notify_subscribers(const std::shared_ptr<Event>& event, const SubscriptionManager& subs_mgr) {
     auto subscribers = subs_mgr.get_subscribers(event->type());
    
    for (auto& sub : subscribers) {
        if (!sub) continue;
        
        try {
            // 为每个订阅者克隆事件
            auto event_clone = event->clone();
            sub->notify(std::move(event_clone));
            
        } catch (const std::exception& e) {
            std::cerr << "[EventDispatcher] 订阅者通知异常: " << e.what() << std::endl;
            // 关键：继续通知其他订阅者
            continue;
            
        } catch (...) {
            std::cerr << "[EventDispatcher] 订阅者通知未知异常" << std::endl;
            // 关键：继续通知其他订阅者
            continue;
        }
    }
}

} // namespace engine
