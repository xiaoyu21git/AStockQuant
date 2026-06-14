#pragma once

namespace domain::strategy {

class IOrderListener {
public:
    virtual ~IOrderListener() = default;
};

} // namespace domain::strategy