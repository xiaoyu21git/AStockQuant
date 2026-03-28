#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "FactorBacktestService.h"

// 前向声明
class FactorService;

namespace domain::backtest {

class DatabaseFactorDataProvider : public FactorDataProvider {
public:
    explicit DatabaseFactorDataProvider(std::shared_ptr<FactorService> factorService);
    virtual ~DatabaseFactorDataProvider();
    
    // FactorDataProvider接口实现
    std::map<std::string, std::map<std::string, double>> getFactorValuesRange(
        const std::string& factorId,
        const std::string& startDate,
        const std::string& endDate) override;
    
private:
    std::shared_ptr<FactorService> factorService_;
};

} // namespace domain::backtest