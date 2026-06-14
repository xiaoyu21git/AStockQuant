#pragma once

#include "OrderMapper.h"

#include <string>
#include <vector>

namespace domain::trading {

struct ConflictResult {
    bool hasConflict = false;
    std::string conflictingSymbol;
    OrderSide conflictingSide = OrderSide::Buy;
    std::string conflictingStatus;
    std::string conflictingOrderId;
    std::string message;
};

class OrderConflictDetector {
public:
    // 检测同标的反向未完成委托冲突
    [[nodiscard]] ConflictResult detect(const std::string& symbol,
                                         OrderSide requestedSide,
                                         const std::vector<TradeOrder>& pendingOrders) const;
};

} // namespace domain::trading