#pragma once
#include "Event.h"
#include <string>

namespace domain {

enum class StrategyEventType {
    Started,
    Finished,
    SignalGenerated,
    PositionOpened,
    PositionClosed,
};


class StrategyEvent : public engine::Event {
public:
    StrategyEvent(
        domain::StrategyEventType type,
        foundation::utils::Timestamp ts,
        const std::string& strategyName,
        const std::string& message
    ): engine::Event(mapStrategyEventTypeToEngineType(type), ts, "Strategy")
    , strategyType_(type)
    , strategyName_(strategyName)
    , message_(message)
    {
        attributes_["strategyName"] = strategyName_;
        attributes_["message"] = message_;
    }
    inline engine::Event::Type mapStrategyEventTypeToEngineType(StrategyEventType t) {
        switch (t) {
            case StrategyEventType::Started:         return engine::Event::Type::UserCustom;
            case StrategyEventType::Finished:        return engine::Event::Type::UserCustom;
            case StrategyEventType::SignalGenerated: return engine::Event::Type::Signal;
            case StrategyEventType::PositionOpened:  return engine::Event::Type::UserCustom;
            case StrategyEventType::PositionClosed:  return engine::Event::Type::UserCustom;
            default:                                return engine::Event::Type::UserCustom;
        }
    }
    // ===== 实现纯虚接口 =====
    foundation::utils::Uuid id() const override { return id_; } // 生成或传入 UUID
    Type type() const override { return type_; }
    Timestamp timestamp() const override { return timestamp_; }
    std::string source() const override { return source_; }
    const void* payload() const override { return static_cast<const void*>(&message_); }
    std::string payload_type() const override { return "StrategyEvent"; }

    std::unique_ptr<engine::Event> clone() const override {
        return std::make_unique<StrategyEvent>(*this);
    }

    const Attributes& attributes() const override { return attributes_; }

    // 事件特有字段
    std::string strategyName() const { return strategyName_; }
    std::string message() const { return message_; }

private:
    foundation::utils::Uuid id_; // 可在构造时生成
    std::string strategyName_;
    std::string message_;
    Attributes attributes_;
    StrategyEventType strategyType_;
};

} // namespace domain
