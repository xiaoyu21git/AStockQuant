#pragma once
#include <cstdint>
#include <string>

namespace domain::model {

struct Bar {
    std::string symbol;
    std::int64_t time;
    double open;
    double high;
    double low;
    double close;
    double volume;

    bool isValid() const noexcept {
        return high >= low && volume >= 0.0;
    }
};

} // namespace domain::model