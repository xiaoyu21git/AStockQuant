#include "FactorBacktestFacade.h"

namespace app::facade {

FactorBacktestFacade::FactorBacktestFacade() = default;
FactorBacktestFacade::~FactorBacktestFacade() = default;

bool FactorBacktestFacade::initialize() { return true; }
void FactorBacktestFacade::runBacktest(const std::string& factorId,
                                        const std::string& startDate,
                                        const std::string& endDate) {
    (void)factorId; (void)startDate; (void)endDate;
}

} // namespace app::facade