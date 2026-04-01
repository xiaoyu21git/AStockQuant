#pragma once

#include "BaseFactor.h"

#include <QString>
#include <unordered_set>

namespace astock {
namespace database {
class QtMySQLDatabase;
}
}

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

        std::string configuredType;
        std::string metric;
        std::string timeframe = "daily";
        std::string indicatorType;
        std::string sentimentSource;
        std::string expression;
        std::string sectorType;
        std::string macroFactor;
        std::vector<CustomVariableBinding> variables;
        int window = 20;
        int lookbackPeriod = 252;
        double minDividendYield = 0.0;
        double sentimentWeight = 0.3;

        void fromJson(const foundation::json::JsonFacade& json);
    };

    ConfigurableFactor();
    ~ConfigurableFactor() override = default;

    void initializeFromDatabase(const std::string& instanceId) override;
    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;

    static std::shared_ptr<ConfigurableFactor> create(
        const std::string& instanceId,
        std::shared_ptr<astock::database::QtMySQLDatabase> db,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

private:
    Params params_;

    void loadConfig(const foundation::json::JsonFacade& config) override;

    CalculationResult calculateGrowth(const CalculationContext& context) const;
    CalculationResult calculateLiquidity(const CalculationContext& context) const;
    CalculationResult calculateTechnical(const CalculationContext& context) const;
    CalculationResult calculateDividend(const CalculationContext& context) const;
    CalculationResult calculateMacroSector(const CalculationContext& context) const;
    CalculationResult calculateSentiment(const CalculationContext& context) const;
    CalculationResult calculateCustom(const CalculationContext& context) const;

    QString normalizedType() const;
    QString normalizedMetric() const;
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