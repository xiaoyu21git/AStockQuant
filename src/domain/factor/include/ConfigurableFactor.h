#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"
#include "factor_enums.h"

#include <QString>
#include <unordered_set>
#include <variant>

namespace factor {

class ConfigurableFactorBase : public BaseFactor {
public:
    struct CustomVariableBinding {
        std::string name;
        std::string field;
        bool hasDefaultValue{false};
        double defaultValue{0.0};
    };

    struct CommonParams : CommonMetricParams {
        uint16_t window = 20;

        void fromJson(const foundation::json::JsonFacade& json);
    };

    struct GrowthParams {
        std::vector<GrowthMetric> growthMetrics;
        std::vector<double> growthWeights;

        void fromJson(const foundation::json::JsonFacade& json);
    };

    struct LiquidityParams {
        LiquidityMetric liquidityMetric{LiquidityMetric::UNKNOWN};

        void fromJson(const foundation::json::JsonFacade& json);
    };

    struct DividendParams {
        DividendMetric dividendMetric{DividendMetric::UNKNOWN};
        std::vector<DividendMetric> dividendMetrics;
        double minDividendYield = 0.0;

        void fromJson(const foundation::json::JsonFacade& json);
    };

    struct TechnicalParams {
        std::vector<TechnicalIndicator> technicalIndicators;
        TechnicalPriceType technicalPriceType{TechnicalPriceType::UNKNOWN};
        TechnicalCombinationMode technicalCombinationMode{TechnicalCombinationMode::EqualWeight};
        int rsiWindow = 14;
        int maWindow = 20;
        int emaWindow = 20;
        int bollWindow = 20;
        double bollStdDev = 2.0;
        int kdjWindow = 9;
        int kdjKPeriod = 3;
        int kdjDPeriod = 3;
        int atrWindow = 14;
        int macdFastPeriod = 12;
        int macdSlowPeriod = 26;
        int macdSignalPeriod = 9;
        int obvWindow = 20;
        int vwapWindow = 20;
        int volumeRatioWindow = 20;
        int turnoverStabilityWindow = 60;
        LiquidityMetric turnoverStabilityMetric{LiquidityMetric::TURNOVER_RATE};
        bool useVolume = false;

        void fromJson(const foundation::json::JsonFacade& json);
    };

    struct MacroParams {
        std::string benchmarkSymbol = "000300.SH";
        std::vector<MacroDimension> macroDimensions = {
            MacroDimension::GROWTH,
            MacroDimension::INFLATION,
            MacroDimension::CREDIT,
            MacroDimension::RATES,
            MacroDimension::POLICY,
            MacroDimension::RISK_APPETITE
        };
        std::vector<MacroIndicator> macroIndicators = {
            MacroIndicator::INDUSTRIAL_ADDED_VALUE_YOY,
            MacroIndicator::CPI_YOY,
            MacroIndicator::M2_YOY,
            MacroIndicator::TEN_YEAR_BOND_YIELD,
            MacroIndicator::LPR_1Y,
            MacroIndicator::AA_CREDIT_SPREAD
        };
        DataFrequency macroFrequency{DataFrequency::Daily};
        int macroWindow = 12;
        TechnicalPriceType priceType{TechnicalPriceType::CLOSE};

        void fromJson(const foundation::json::JsonFacade& json);
    };

    struct IndustryParams {
        IndustryMetric industryMetricKind{IndustryMetric::UNKNOWN};
        ConfigurableSectorType sectorType{ConfigurableSectorType::SW_L1};

        void fromJson(const foundation::json::JsonFacade& json);
    };

    struct SentimentParams {
        SentimentMetric sentimentMetric{SentimentMetric::UNKNOWN};
        SentimentSource sentimentSource{SentimentSource::UNKNOWN};

        void fromJson(const foundation::json::JsonFacade& json);
    };

    struct CustomParams {
        std::string expression;
        std::vector<CustomVariableBinding> variables;

        void fromJson(const foundation::json::JsonFacade& json);
    };

    using SpecificParams = std::variant<
        GrowthParams,
        LiquidityParams,
        TechnicalParams,
        DividendParams,
        MacroParams,
        IndustryParams,
        SentimentParams,
        CustomParams>;

    explicit ConfigurableFactorBase(FactorType factorType);
    ~ConfigurableFactorBase() override = default;

    std::vector<CalculationResult> calculateBatch(const std::vector<CalculationContext>& contexts) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;

    friend class ConfigurableFactorTestAccess;

protected:
    CommonParams commonParams_;
    SpecificParams specificParams_;

    void loadConfig(const foundation::json::JsonFacade& config) override;

    CalculationResult calculateGrowth(const CalculationContext& context) const;
    CalculationResult calculateLiquidity(const CalculationContext& context) const;
    CalculationResult calculateTechnical(const CalculationContext& context) const;
    CalculationResult calculateDividend(const CalculationContext& context) const;
    CalculationResult calculateMacro(const CalculationContext& context) const;
    CalculationResult calculateIndustry(const CalculationContext& context) const;
    CalculationResult calculateSentiment(const CalculationContext& context) const;
    CalculationResult calculateCustom(const CalculationContext& context) const;

private:
    FactorType configuredFactorType() const;
    const GrowthParams& growthParams() const;
    const LiquidityParams& liquidityParams() const;
    const TechnicalParams& technicalParams() const;
    const DividendParams& dividendParams() const;
    const MacroParams& macroParams() const;
    const IndustryParams& industryParams() const;
    const SentimentParams& sentimentParams() const;
    const CustomParams& customParams() const;
    std::vector<std::string> effectiveSymbols(const CalculationContext& context) const;

    std::unordered_map<std::string, double> currentFieldCrossSection(
        const CalculationContext& context,
        const QString& field) const;
    std::vector<double> seriesForField(
        const CalculationContext& context,
        const std::string& symbol,
        const QString& field,
        int window) const;
    std::unordered_map<std::string, double> latestFinancialMetric(
        const CalculationContext& context,
        const QString& field,
        const QString& date) const;
    std::unordered_map<std::string, std::vector<double>> latestFinancialSeries(
        const CalculationContext& context,
        const QString& field,
        const QString& date,
        int limit) const;
    std::unordered_map<std::string, QString> industryBySymbol(
        const CalculationContext& context) const;
    const CustomVariableBinding* findCustomVariableBinding(const QString& variableName) const;
    std::unordered_map<std::string, double> evaluateCustomExpression(
        const CalculationContext& context,
        const QString& expression,
        const std::vector<std::string>& symbols,
        QString* errorMessage) const;
};

} // namespace factor