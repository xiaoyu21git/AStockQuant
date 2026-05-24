#pragma once

#include <memory>
#include <string>

namespace factor {

class BaseFactor;
struct FactorInstanceInfo;

class IFactorResolver {
public:
    virtual ~IFactorResolver() = default;

    virtual std::shared_ptr<BaseFactor> createIsolated(const std::string& instanceId) = 0;
    virtual FactorInstanceInfo getInfo(const std::string& instanceId) = 0;
};

} // namespace factor