#pragma once

#include "ConfigurableFactor.h"
#include "factor_enums.h"

#include <QString>
#include <QStringList>
#include <vector>
#include <unordered_map>

namespace factor {

class ValueFactor : public BaseFactor {
public:
    struct Params : ConfigurableFactorBase::CommonParams {
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

    static void appendUniqueField(QStringList& fields, const QString& field);
    static double calculatePercentileValueLocal(std::vector<double> values, double quantile);
    static QString valuationMetricField(ValuationMetric metric);
    static std::vector<ValuationMetric> selectedMetricsFromParams(const Params& params);
    static double valuationMetricWeight(const Params& params, ValuationMetric metric);
    static double scoreFromMetricRawValue(ValuationMetric metric, double rawValue);
    static QStringList collectDateResolutionFields(const std::vector<ValuationMetric>& metrics);
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