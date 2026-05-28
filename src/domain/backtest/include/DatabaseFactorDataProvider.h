#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include "StockDataProvider.h"

// 前向声明
class FactorService;

namespace domain::backtest {

class DatabaseFactorDataProvider : public FactorDataProvider {
public:
    explicit DatabaseFactorDataProvider(std::shared_ptr<FactorService> factorService,
                                        std::function<void()> rangeLoadStartedCallback = {});
    virtual ~DatabaseFactorDataProvider();
    
    // FactorDataProvider接口实现
    std::map<std::string, std::map<std::string, double>> getFactorValuesRange(
        const std::string& factorId,
        const std::string& startDate,
        const std::string& endDate) override;
    
private:
    std::shared_ptr<FactorService> factorService_;
    std::function<void()> rangeLoadStartedCallback_;
};

} // namespace domain::backtest