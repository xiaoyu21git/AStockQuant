#pragma once
#include "BaseInterface.h"
#include "foundation.h"
#include <functional>
#include <map>
#include <string>
#include <memory>

namespace engine {

class Event {
public:
    // 事件类型枚举
    enum class Type {
        System = 0,
        MarketData,
        News,
        Signal,
        Alert,
        Warning,
        UserCustom = 1000
    };

    using Attributes = std::map<std::string, std::string>;

    /**
     * @brief 构造函数
     */
    Event(Type type, Timestamp timestamp, std::string source);

    virtual ~Event() = default;

    // ===== 原有接口（不动） =====
    virtual foundation::Uuid id() const = 0;
    virtual Type type() const = 0;
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
        Type type,
        Timestamp timestamp,
        std::map<std::string, std::string> attributes
    );

    // ===== 已声明但未实现的能力 =====
    static const char* type_to_string(Type type);

protected:
    Type type_;
    Timestamp timestamp_;
    std::string source_;
};

} // namespace engine
namespace std {
    template<>
    struct hash<engine::Event::Type> {
        size_t operator()(const engine::Event::Type& type) const noexcept {
            using Underlying = underlying_type_t<engine::Event::Type>;
            return hash<Underlying>{}(static_cast<Underlying>(type));
        }
    };
}