#include "Event.h"
using Timestamp = foundation::utils::Timestamp;
namespace engine {

// ===== 基础实现类（引擎内部使用） =====
class BasicEvent final : public Event {
public:
    BasicEvent(
        Type type,
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

    Type type() const override {
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

Event::Event(Type type, Timestamp timestamp, std::string source)
    : type_(type)
    , timestamp_(timestamp)
    , source_(std::move(source)) {}

// ===== 工厂方法 =====

std::unique_ptr<Event> Event::create(
    Type type,
    Timestamp timestamp,
    std::map<std::string, std::string> attributes
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

bool Event::get_attribute(const std::string& key, std::string& out) const {
    auto it = attributes().find(key);
    if (it == attributes().end()) {
        return false;
    }
    out = it->second;
    return true;
}

// ===== 类型转字符串 =====

const char* Event::type_to_string(Type type) {
    switch (type) {
    case Type::System:     return "System";
    case Type::MarketData: return "MarketData";
    case Type::News:       return "News";
    case Type::Signal:     return "Signal";
    case Type::Alert:      return "Alert";
    case Type::Warning:    return "Warning";
    default:               return "UserCustom";
    }
}

} // namespace engine
