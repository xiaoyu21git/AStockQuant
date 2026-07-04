#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"

namespace factor {

class QualityFactorTestAccess;

class QualityFactor : public BaseFactor {
public:
    static constexpr const char* F_ROE = "roe";
    static constexpr const char* F_ROA = "roa";
    static constexpr const char* F_NET_PROFIT = "net_profit";
    static constexpr const char* F_TOTAL_REVENUE = "total_revenue";
    static constexpr const char* F_TOTAL_ASSETS = "total_assets";
    static constexpr const char* F_TOTAL_LIABILITIES = "total_liabilities";
    static constexpr const char* F_EQUITY = "equity";
    static constexpr const char* F_EPS = "eps";
    static constexpr const char* F_BPS = "bps";
    static constexpr const char* F_PROFIT_MARGIN = "profit_margin";
    static constexpr const char* F_GROSS_MARGIN = "gross_margin";
    static constexpr const char* F_OPERATING_MARGIN = "operating_margin";
    static constexpr const char* F_DEBT_TO_EQUITY = "debt_to_equity";
    static constexpr const char* F_CURRENT_RATIO = "current_ratio";
    static constexpr const char* F_QUICK_RATIO = "quick_ratio";
    static constexpr const char* F_OPERATING_CASH_FLOW = "operating_cash_flow";
    static constexpr const char* F_DIVIDEND_YIELD = "dividend_yield";

    struct Params : CommonParams {
        QualityMetric metric = QualityMetric::ROE;
        double qualityThreshold = 0.1;
    };

    QualityFactor();
    ~QualityFactor() override = default;
    std::string resolveMetricColumn(QualityMetric metric);
    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;
    int getLookbackDays() const override { return params_.lookbackWindow; }

    static std::shared_ptr<QualityFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

private:
    friend class QualityFactorTestAccess;

    Params params_;

    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor