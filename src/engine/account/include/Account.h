#pragma once
#include "Bar.h"

namespace engine {

class Account {
public:
    explicit Account(double initialCash);

    bool openLong(const domain::model::Bar& bar, int quantity);

    bool closeLong(const domain::model::Bar& bar);

    double cash() const { return cash_; }
    double realizedPnL() const { return realizedPnL_; }
    int position() const { return position_; }

    void setLastPrice(double price) { lastPrice_ = price; }

private:
    double cash_;
    double realizedPnL_;
    int position_;
    double lastPrice_ = 0.0;
};

} // namespace engine
