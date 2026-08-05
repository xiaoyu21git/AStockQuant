#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"
#include "factor_enums.h"

#include <string>
#include <vector>
#include <unordered_map>

namespace factor {

class ValueFactor : public BaseFactor {
public:
    static constexpr const char* F_OPEN = "open";
    static constexpr const char* F_HIGH = "high";
    static constexpr const char* F_LOW = "low";
    static constexpr const char* F_CLOSE = "close";
    static constexpr const char* F_PRE_CLOSE = "pre_close";
    static constexpr const char* F_VOLUME = "volume";
    static constexpr const char* F_TURNOVER = "turnover";
    static constexpr const char* F_CHANGE_PCT = "change_pct";
    static constexpr const char* F_AMPLITUDE = "amplitude";
    static constexpr const char* F_TURNOVER_RATE = "turnover_rate";
    static constexpr const char* F_PRE_ADJ_FACTOR = "pre_adjust_factor";
    static constexpr const char* F_POST_ADJ_FACTOR = "post_adjust_factor";
    static constexpr const char* F_MARKET_CAP = "market_cap";
    static constexpr const char* F_CIRCULATING_MARKET_CAP = "circulating_market_cap";
    static constexpr const char* F_TOTAL_MARKET_CAP = "total_market_cap";
    static constexpr const char* F_PE_RATIO = "pe_ratio";
    static constexpr const char* F_PB_RATIO = "pb_ratio";
    static constexpr const char* F_EPS = "eps";
    static constexpr const char* F_BPS = "bps";
    static constexpr const char* F_ROE = "roe";
    static constexpr const char* F_ROA = "roa";
    static constexpr const char* F_NET_PROFIT = "net_profit";
    static constexpr const char* F_TOTAL_REVENUE = "total_revenue";
    static constexpr const char* F_TOTAL_ASSETS = "total_assets";
    static constexpr const char* F_TOTAL_LIABILITIES = "total_liabilities";
    static constexpr const char* F_EQUITY = "equity";
    static constexpr const char* F_PROFIT_MARGIN = "profit_margin";
    static constexpr const char* F_GROSS_MARGIN = "gross_margin";
    static constexpr const char* F_OPERATING_MARGIN = "operating_margin";
    static constexpr const char* F_DEBT_TO_EQUITY = "debt_to_equity";
    static constexpr const char* F_CURRENT_RATIO = "current_ratio";
    static constexpr const char* F_QUICK_RATIO = "quick_ratio";
    static constexpr const char* F_OPERATING_CASH_FLOW = "operating_cash_flow";
    static constexpr const char* F_DIVIDEND_YIELD = "dividend_yield";

    struct Params : CommonParams {
        std::vector<ValuationMetric> valuationMetrics{ValuationMetric::BP, ValuationMetric::EP};
        Params() { lagEnabled = true; }
        double bpWeight = 25.0;
        double epWeight = 25.0;
        double dividendYieldWeight = 25.0;
        double cfPWeight = 25.0;
    };

    ValueFactor();
    ~ValueFactor() override = default;

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;
    int getLookbackDays() const override { return params_.lookbackWindow; }

    static std::shared_ptr<ValueFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

    friend class ValueFactorTestAccess;

private:
    struct MetricContribution {
        double weight{0.0};
        std::unordered_map<std::string, double> scores;
        int rawSampleCount{0};
        int invalidSampleCount{0};
    };

    Params params_;

    void loadConfig(const foundation::json::JsonFacade& config) override;

    static void appendUniqueField(std::vector<std::string>& fields, const std::string& field);
    static double calculatePercentileValueLocal(std::vector<double> values, double quantile);
    static std::string valuationMetricField(ValuationMetric metric);
    static std::vector<ValuationMetric> selectedMetricsFromParams(const Params& params);
    static double valuationMetricWeight(const Params& params, ValuationMetric metric);
    static double scoreFromMetricRawValue(ValuationMetric metric, double rawValue);
    static std::vector<std::string> collectDateResolutionFields(const std::vector<ValuationMetric>& metrics);
    static MetricContribution computeCFPContribution(const CalculationContext& context,
                                                                        const CommonRuntimeState& runtime,
                                                      double weight);
    static MetricContribution computeStandardContribution(const CalculationContext& context,
                                                                            const CommonRuntimeState& runtime,
                                                         ValuationMetric metric,
                                                         double weight);
    static void winsorizeTopBottom5Percent(std::unordered_map<std::string, double>& values);
};

} // namespace factor