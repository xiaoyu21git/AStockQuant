// domain/model/Position.h
#pragma once

namespace domain::model {

struct Position {
    bool hasPosition{false};
    double entryPrice{0.0};
    int quantity{0};

    void open(double price, int qty) {
        hasPosition = true;
        entryPrice = price;
        quantity = qty;
    }

    void close() {
        hasPosition = false;
        entryPrice = 0.0;
        quantity = 0;
    }
};

}
