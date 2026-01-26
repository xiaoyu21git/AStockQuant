// domain/model/Trade.h
#pragma once
#include <string>
#include "foundation.h" // Timestamp

namespace domain::model {

struct Trade {
    std::string strategyName;
    bool isLong;
    double price;
    int quantity;
    foundation::utils::Timestamp timestamp;
};

struct PnL {
    double realized{0.0};
    double unrealized{0.0};
};

} // namespace domain::model
