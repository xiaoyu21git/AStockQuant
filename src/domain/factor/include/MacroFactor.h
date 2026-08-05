#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"
#include "factor_enums.h"

#include <string>
#include <vector>

namespace factor {

class MacroFactor final : public BaseFactor {
public:
    struct Params : CommonParams {
        std::string benchmarkSymbol = "000300.SH";
        std::vector<MacroDimension> macroDimensions;
        std::vector<MacroIndicator> macroIndicators;
        DataFrequency macroFrequency{DataFrequency::Daily};
        int macroWindow = 12;
        TechnicalPriceType priceType{TechnicalPriceType::CLOSE};

        void fromJson(const foundation::json::JsonFacade& json);
    };

    MacroFactor();

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;
    int getLookbackDays() const override { return params_.lookbackWindow; }

    static std::shared_ptr<MacroFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

    static constexpr const char* FIELD_INDUSTRIAL_ADDED_VALUE_YOY = "industrial_added_value_yoy";
    static constexpr const char* FIELD_MANUFACTURING_PMI = "manufacturing_pmi";
    static constexpr const char* FIELD_GDP_YOY = "gdp_yoy";
    static constexpr const char* FIELD_CPI_YOY = "cpi_yoy";
    static constexpr const char* FIELD_PPI_YOY = "ppi_yoy";
    static constexpr const char* FIELD_M2_YOY = "m2_yoy";
    static constexpr const char* FIELD_SOCIAL_FINANCING_STOCK_YOY = "social_financing_stock_yoy";
    static constexpr const char* FIELD_M1_M2_SPREAD = "m1_m2_spread";
    static constexpr const char* FIELD_TEN_YEAR_BOND_YIELD = "ten_year_bond_yield";
    static constexpr const char* FIELD_SHIBOR_3M = "shibor_3m";
    static constexpr const char* FIELD_LPR_1Y = "lpr_1y";
    static constexpr const char* FIELD_RESERVE_REQUIREMENT_RATIO = "reserve_requirement_ratio";
    static constexpr const char* FIELD_AA_CREDIT_SPREAD = "aa_credit_spread";
    static constexpr const char* FIELD_VIX_PROXY = "vix_proxy";

private:
    Params params_;

    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor