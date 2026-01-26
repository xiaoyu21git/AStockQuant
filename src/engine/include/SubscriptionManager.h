#pragma once
#include <unordered_map>
#include <vector>
#include <memory>
#include "EventSubscriber.h"
#include "foundation.h"
#include <mutex>
namespace engine {

    // 订阅管理器类
class SubscriptionManager {
    private:
        mutable std::mutex mutex_;
        std::unordered_map<Event::Type, std::vector<std::shared_ptr<EventSubscriber>>> subs_map_; // 存储订阅者

public:
    // 添加这一行即可
    SubscriptionManager() = default;
        // 禁止拷贝构造和拷贝赋值
    SubscriptionManager(const SubscriptionManager&) = delete;
    SubscriptionManager& operator=(const SubscriptionManager&) = delete;
        // 默认允许移动构造和移动赋值
        SubscriptionManager(SubscriptionManager&&) noexcept = default;
        SubscriptionManager& operator=(SubscriptionManager&&) noexcept = default;
        // 添加订阅者
        foundation::Uuid add_subscriber(std::shared_ptr<EventSubscriber> sub)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto t : sub->event_types()) {
                subs_map_[t].push_back(sub);
            }
            return sub->id();
    }

        // 移除订阅者
        bool remove_subscriber(foundation::Uuid id)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            bool removed = false;
            for (auto& [type, vec] : subs_map_) {
                auto it = std::remove_if(vec.begin(), vec.end(),
                    [&id](const std::shared_ptr<EventSubscriber>& s){ return s->id() == id; });
                    if (it != vec.end()) {
                        vec.erase(it, vec.end());
                        removed = true;
        }
    }
            return removed;
        }

        // 获取指定事件类型的订阅者
        std::vector<std::shared_ptr<EventSubscriber>> get_subscribers(Event::Type type) const{
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = subs_map_.find(type);
            if (it != subs_map_.end()) return it->second;
                return {};
            }
        };
}
