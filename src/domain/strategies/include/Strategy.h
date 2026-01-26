#pragma once
#include "Bar.h"
#include "StrategyAction.h"
#include <string>
#include <memory>
#include "Event.h"
#include "EventBus.h"
#include "StrategyEvent.h"

namespace engine {
class EventBus;
}

namespace domain {

class Strategy {
public:
    virtual ~Strategy() = default;

    virtual void onStart() {
       emitEvent (domain::StrategyEventType::Started, "strategy started");
    }
    virtual void onFinish(){
        emitEvent(domain::StrategyEventType::Finished, "strategy finished");
    }
    virtual StrategyAction onBar(const domain::model::Bar& bar) = 0;

    virtual std::string name() const = 0;

    void setEventBus(engine::EventBus* bus) {
        eventBus_ = bus;
    }
protected:
    void emitEvent(StrategyEventType type, const std::string& msg) {
        auto timestamp = foundation::timestamp_now(); // 假设有获取当前时间的方法
    auto evt = std::make_unique<domain::StrategyEvent>(
        type, // 需要转换成 engine::Event::Type
        timestamp,
        name(),   // strategyName   
        msg
    );
    if (eventBus_)
    {
       eventBus_->publish(std::move(evt));
    }
    
    }
private:
    engine::EventBus* eventBus_;
};

} // namespace domain
