#pragma once

namespace domain {

struct Position
{
    int quantity = 0;
    double avgPrice = 0.0;

    bool empty() const { return quantity == 0; }
};

}
