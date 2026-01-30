#pragma once
#include "BaseInterface.h"
#include <functional>
#include <map>
#include <string>
#include <memory>
#include "foundation.h"
#include "topic/EventType.hpp"
using Timestamp = foundation::Timestamp;
namespace engine {

class Event {
public:

    using Attributes = std::map<std::string, std::string>;

    /**
     * @brief 构造函数
     */
    Event(Event_Core::Type type, foundation::utils::Timestamp timestamp, std::string source);

    virtual ~Event() = default;

    // ===== 原有接口（不动） =====
    virtual foundation::utils::Uuid id() const = 0;
    virtual Event_Core::Type type() const = 0;
    virtual Timestamp timestamp() const = 0;
    virtual std::string source() const = 0;
    virtual const void* payload() const = 0;
    virtual std::string payload_type() const = 0;
    virtual std::unique_ptr<Event> clone() const = 0;

    // ===== ✅ 新增：事件属性（Condition 使用） =====
    virtual const Attributes& attributes() const = 0;

    bool has_attribute(const std::string& key) const;
    bool get_attribute(const std::string& key, std::string& out) const;

    // ===== ✅ 工厂方法（你要求的三个参数） =====
    static std::unique_ptr<Event> create(
        Event_Core::Type type,
        foundation::utils::Timestamp timestamp,
        std::map<std::string, std::string> attributes
    );
    // ============================================================================
// 辅助函数
// ============================================================================

// 添加Event类型转字符串的辅助函数
static std::string event_type_to_string(Event_Core::Type type) {
    switch (type) {
        case Event_Core::Type::SYSTEM: return "System";
        case Event_Core::Type::MARKETDATA: return "MarketData";
        case Event_Core::Type::NEWS: return "News";
        case Event_Core::Type::SIGNAL: return "Signal";
        case Event_Core::Type::ALERT: return "Alert";
        case Event_Core::Type::WARNING: return "Warning";
        case Event_Core::Type::TRADING: return "Trading";
        case Event_Core::Type::BACKTEST : return "Backtest";
        case Event_Core::Type::RISK : return "Risk";
        default:
            if (static_cast<uint32_t>(type) >= 1000) {
                return "UserCustom[" + std::to_string(static_cast<uint32_t>(type)) + "]";
            }
            return "Unknown[" + std::to_string(static_cast<uint32_t>(type)) + "]";
    }
}

    // ===== 已声明但未实现的能力 =====
    static const char* type_to_string(Event_Core::Type type);

protected:
    Event_Core::Type type_;
    foundation::utils::Timestamp timestamp_;
    std::string source_;
};

} // namespace engine
namespace std {
    template<>
    struct hash<engine::Event_Core::Type> {
        size_t operator()(const engine::Event_Core::Type& type) const noexcept {
            using Underlying = underlying_type_t<engine::Event_Core::Type>;
            return hash<Underlying>{}(static_cast<Underlying>(type));
        }
    };
}