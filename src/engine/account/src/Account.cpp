#include "Account.h"

namespace engine {

// 构造函数
Account::Account(double initialCash)
    : cash_(initialCash),
      realizedPnL_(0.0),
      position_(0),
      lastPrice_(0.0)
{
}

// 开多仓
bool Account::openLong(const domain::model::Bar& bar, int quantity)
{
    const double cost = bar.close * quantity;
    if (cash_ < cost) {
        return false;
    }

    cash_ -= cost;
    position_ += quantity;

    // 关键：记录建仓价格
    lastPrice_ = bar.close;

    return true;
}

// 平多仓
bool Account::closeLong(const domain::model::Bar& bar)
{
    if (position_ <= 0) {
        return false;
    }

    const double pnl = (bar.close - lastPrice_) * position_;

    cash_ += bar.close * position_;
    realizedPnL_ += pnl;

    position_ = 0;
    lastPrice_ = 0.0;

    return true;
}

// // getters
// double Account::cash() const
// {
//     return cash_;
// }

// double Account::realizedPnL() const
// {
//     return realizedPnL_;
// }

// int Account::position() const
// {
//     return position_;
// }

// // 设置最新价格（用于浮动盈亏）
// void Account::setLastPrice(double price)
// {
//     lastPrice_ = price;
// }

} // namespace engine
