#pragma once

#include "BaseFactor.h"
#include "factor_enums.h"

#include <QString>
#include <unordered_set>

namespace factor {

class ConfigurableFactor : public BaseFactor {
public:
    struct Params {
        struct CustomVariableBinding {
            std::string name;
            std::string field;
            bool hasDefaultValue{false};
            double defaultValue{0.0};
        };

        FactorType configuredType{FactorType::UNKNOWN};
        std::string metric;
        LiquidityMetric liquidityMetric{LiquidityMetric::UNKNOWN};
        DividendMetric dividendMetric{DividendMetric::UNKNOWN};
        IndustryMetric industryMetricKind{IndustryMetric::UNKNOWN};
        SentimentMetric sentimentMetric{SentimentMetric::UNKNOWN};
        SentimentSource sentimentSource{SentimentSource::UNKNOWN};
        std::string expression;
        std::string sectorType;
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
        std::string macroFrequency = "auto";
        int macroWindow = 12;
        TechnicalPriceType priceType{TechnicalPriceType::CLOSE};
        std::vector<TechnicalIndicator> technicalIndicators;
        TechnicalPriceType technicalPriceType{TechnicalPriceType::UNKNOWN};
        std::string technicalCombinationMode = "equal_weight";
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
        std::string turnoverStabilityMetric = "turnover_rate";
        bool useVolume = false;
        std::string frequency = "daily";
        bool laggedEnabled = false;
        std::string standardization;
        bool neutralizationEnabled = false;
        std::vector<CustomVariableBinding> variables;
        std::vector<std::string> growthMetrics;
        std::vector<double> growthWeights;
        std::vector<std::string> dividendMetrics;
        int window = 20;
        int lookbackPeriod = 252;
        double minDividendYield = 0.0;

        void fromJson(const foundation::json::JsonFacade& json, FactorType factorType);
    };

    ConfigurableFactor();
    ~ConfigurableFactor() override = default;

    CalculationResult calculate(const CalculationContext& context) override;
    std::vector<CalculationResult> calculateBatch(const std::vector<CalculationContext>& contexts) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;

    static std::shared_ptr<ConfigurableFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

    friend class ConfigurableFactorTestAccess;

private:
    Params params_;

    void loadConfig(const foundation::json::JsonFacade& config) override;

    CalculationResult calculateGrowth(const CalculationContext& context) const;
    CalculationResult calculateLiquidity(const CalculationContext& context) const;
    CalculationResult calculateTechnical(const CalculationContext& context) const;
    CalculationResult calculateDividend(const CalculationContext& context) const;
    CalculationResult calculateMacro(const CalculationContext& context) const;
    CalculationResult calculateIndustry(const CalculationContext& context) const;
    CalculationResult calculateSentiment(const CalculationContext& context) const;
    CalculationResult calculateCustom(const CalculationContext& context) const;

    FactorType configuredFactorType() const;
    void loadConfig(const foundation::json::JsonFacade& config, FactorType factorType);
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
    const Params::CustomVariableBinding* findCustomVariableBinding(const QString& variableName) const;
    std::unordered_map<std::string, double> evaluateCustomExpression(
        const CalculationContext& context,
        const QString& expression,
        const std::vector<std::string>& symbols,
        QString* errorMessage) const;
};

} // namespace factor