#pragma once
#include <vector>
#include "Bar.h"

namespace domain::data {

class IMarketDataSource {
public:
    virtual ~IMarketDataSource() = default;

    virtual std::vector<domain::model::Bar> loadBars() = 0;
};

}
