#include "Event/Event.h"
using Timestamp = foundation::utils::Timestamp;
namespace engine {

// ===== 基础实现类（引擎内部使用） =====
class BasicEvent final : public Event {
public:
    BasicEvent(
        Event_Core::Type type,
        Timestamp timestamp,
        std::string source,
        Attributes attributes
    )
        : Event(type, timestamp, std::move(source))
        , attributes_(std::move(attributes))
        , id_(foundation::utils::Uuid::generate())
    {}

    foundation::utils::Uuid id() const override {
        return id_;
    }

    Event_Core::Type type() const override {
        return type_;
    }

    Timestamp timestamp() const override {
        return timestamp_;
    }

    std::string source() const override {
        return source_;
    }

    const void* payload() const override {
        return nullptr;
    }

    std::string payload_type() const override {
        return {};
    }

    const Attributes& attributes() const override {
        return attributes_;
    }

    std::unique_ptr<Event> clone() const override {
        return std::make_unique<BasicEvent>(*this);
    }

private:
    Attributes attributes_;
    foundation::utils::Uuid id_;
};

// ===== Event 基类 =====

Event::Event(Event_Core::Type type, Timestamp timestamp, std::string source)
    : type_(type)
    , timestamp_(timestamp)
    , source_(std::move(source)) {}

// ===== 工厂方法 =====

std::unique_ptr<Event> Event::create(
    Event_Core::Type type,
    Timestamp timestamp,
    std::map<std::string, EventValue> attributes
) {
    return std::make_unique<BasicEvent>(
        type,
        timestamp,
        type_to_string(type),
        std::move(attributes)
    );
}

// ===== 属性访问便捷方法 =====

bool Event::has_attribute(const std::string& key) const {
    return attributes().find(key) != attributes().end();
}

// ===== 类型转字符串 =====

const char* Event::type_to_string(Event_Core::Type type) {
    switch (type) {
    case Event_Core::Type::SYSTEM :     return "System";
    case Event_Core::Type::MARKETDATA: return "MarketData";
    case Event_Core::Type::NEWS:       return "News";
    case Event_Core::Type::SIGNAL:     return "Signal";
    case Event_Core::Type::ALERT:      return "Alert";
    case Event_Core::Type::WARNING:    return "Warning";
    default:               return "UserCustom";
    }
}

} // namespace engine
