#pragma once

#include <cstdint>
#include <string>

namespace domain::model {

struct Tick {
    std::string symbol;
    std::int64_t timestamp;

    double price;
    double volume;
};

} // namespace domain::model
