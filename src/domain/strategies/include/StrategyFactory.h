#pragma once
#include <functional>
#include <memory>
#include <map>
#include <string>
#include "Strategy.h"

namespace domain {

class StrategyFactory {
public:
    using Creator = std::function<std::shared_ptr<Strategy>()>;

    static StrategyFactory& instance() {
        static StrategyFactory factory;
        return factory;
    }

    void registerStrategy(const std::string& name, Creator creator) {
        creators_[name] = creator;
    }

    std::shared_ptr<Strategy> create(const std::string& name) const {
        auto it = creators_.find(name);
        if (it != creators_.end()) {
            return it->second();
        }
        return nullptr;
    }

private:
    StrategyFactory() = default;
    std::map<std::string, Creator> creators_;
};

} // namespace domain
