#pragma once
#include <vector>
#include "Bar.h"
#include "Strategy.h"

#include "BacktestResult.h"



namespace engine {

class BacktestEngine
{
public:
    BacktestResult run(
        domain::Strategy& strategy,
        const std::vector<domain::model::Bar>& bars
    );

private:
    double m_entryPrice = 0.0;
};

}
