#pragma once

#include <cstdint>
#include <string>

namespace domain::model {

struct Bar {
    std::string symbol;     // 标的代码
    std::int64_t timestamp; // Unix ms
    std::int64_t time; // 毫秒级时间戳
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
