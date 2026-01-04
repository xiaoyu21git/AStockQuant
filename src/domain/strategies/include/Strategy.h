#pragma once
#include "Bar.h"
#include "StrategyAction.h"
#include <string>
#include <memory>
#include "Event.h"
namespace engine {
class EventBus;
}

namespace domain {

class Strategy {
public:
    virtual ~Strategy() = default;

    virtual void onStart() {}
    virtual StrategyAction onBar(const domain::model::Bar& bar) = 0;
    virtual void onFinish() {}

    virtual std::string name() const = 0;

    void setEventBus(std::shared_ptr<engine::EventBus> bus) {
        eventBus_ = bus;
    }
    void emitEvent(const std::string& eventName, const std::string& data = "") {
        if (eventBus_) eventBus_->publish({eventName, data});
    }
protected:
   

private:
    std::shared_ptr<engine::EventBus> eventBus_;
};

} // namespace domain
