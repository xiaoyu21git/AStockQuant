#pragma once
#include "Strategy.h"

namespace domain {

class FixedBarStrategy : public Strategy {
public:
    FixedBarStrategy(int buyBar, int sellBar);

    std::string name() const override;
    void onStart() override;
    void onFinish() override;
    StrategyAction onBar(const model::Bar& bar) override;

private:
    int m_buyBar;
    int m_sellBar;
    int m_index = 0;
    bool m_positionOpen = false;
};

} // namespace domain
