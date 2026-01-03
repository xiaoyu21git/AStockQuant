#pragma once
#include <cstdint>

namespace domain {

enum class OrderSide
{
    Buy,
    Sell
};

enum class OrderType
{
    Market,
    Limit
};

struct Order
{
    std::int64_t id;
    OrderSide side;
    OrderType type;
    double price;
    int quantity;
};

}
