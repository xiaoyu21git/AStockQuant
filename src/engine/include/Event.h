#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

// 事件类型
struct Event {
    std::string name;   // 事件名称
    std::string data;   // 简单数据载体，可扩展为任何结构
};

// 回调函数类型
using EventCallback = std::function<void(const Event&)>;

class EventBus {
public:
    void subscribe(const std::string& eventName, EventCallback cb) {
        subscribers_[eventName].push_back(std::move(cb));
    }

    void publish(const Event& event) {
        auto it = subscribers_.find(event.name);
        if (it != subscribers_.end()) {
            for (auto& cb : it->second) {
                cb(event);
            }
        }
    }

private:
    std::unordered_map<std::string, std::vector<EventCallback>> subscribers_;
};

} // namespace engine
