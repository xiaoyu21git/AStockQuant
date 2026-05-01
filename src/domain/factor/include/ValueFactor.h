#pragma once

#include "BaseFactor.h"

#include <QString>
#include <vector>

namespace astock {
namespace database {
class QtMySQLDatabase;
}
}

namespace factor {

class ValueFactor : public BaseFactor {
public:
    struct Params {
        std::vector<std::string> valuationMetrics{"bp", "ep"};
        std::string valuationType{"bp"};
        bool usePercentile = false;
        bool industryNeutral = false;
        int lookbackPeriod = 252;
        bool laggedEnabled = true;
        std::string frequency = "daily";
        std::string standardization = "none";
        double bpWeight = 25.0;
        double epWeight = 25.0;
        double dividendYieldWeight = 25.0;
        double cfPWeight = 25.0;

        void fromJson(const foundation::json::JsonFacade& json) {
            valuationMetrics.clear();
            if (json.has("valuationMetrics")) {
                const auto metrics = json.get("valuationMetrics");
                if (metrics.isArray() && metrics.size() > 0) {
                    for (size_t index = 0; index < metrics.size(); ++index) {
                        const QString metric = QString::fromStdString(metrics.at(index).asString()).trimmed().toLower();
                        if (!metric.isEmpty()) {
                            const std::string normalized = metric.toStdString();
                            if (std::find(valuationMetrics.begin(), valuationMetrics.end(), normalized) == valuationMetrics.end()) {
                                valuationMetrics.push_back(normalized);
                            }
                        }
                    }
                } else if (metrics.isString()) {
                    const QString metric = QString::fromStdString(metrics.asString()).trimmed().toLower();
                    if (!metric.isEmpty()) {
                        valuationMetrics.push_back(metric.toStdString());
                    }
                }
            }
            if (valuationMetrics.empty() && json.has("valuationType")) {
                const QString metric = QString::fromStdString(json.get("valuationType").asString()).trimmed().toLower();
                if (!metric.isEmpty()) {
                    valuationMetrics.push_back(metric.toStdString());
                }
            }
            if (valuationMetrics.empty()) {
                valuationMetrics = {"bp", "ep"};
            }
            valuationType = valuationMetrics.front();
            if (json.has("lookback_period")) lookbackPeriod = json.get("lookback_period").asInt();
            if (json.has("lookbackPeriod")) lookbackPeriod = json.get("lookbackPeriod").asInt();
            if (json.has("lagged_enabled")) laggedEnabled = json.get("lagged_enabled").asBool();
            if (json.has("laggedEnabled")) laggedEnabled = json.get("laggedEnabled").asBool();
            if (json.has("frequency")) frequency = json.get("frequency").asString();
            if (json.has("standardization")) standardization = json.get("standardization").asString();
            if (json.has("use_percentile")) usePercentile = json.get("use_percentile").asBool();
            if (json.has("usePercentile")) usePercentile = json.get("usePercentile").asBool();
            if (json.has("industry_neutral")) industryNeutral = json.get("industry_neutral").asBool();
            if (json.has("industryNeutral")) industryNeutral = json.get("industryNeutral").asBool();
            if (json.has("neutralizationEnabled")) industryNeutral = json.get("neutralizationEnabled").asBool();
            if (json.has("bpWeight")) bpWeight = json.get("bpWeight").asDouble();
            if (json.has("epWeight")) epWeight = json.get("epWeight").asDouble();
            if (json.has("dividendYieldWeight")) dividendYieldWeight = json.get("dividendYieldWeight").asDouble();
            if (json.has("cfPWeight")) cfPWeight = json.get("cfPWeight").asDouble();
        }
    };

    ValueFactor();
    ~ValueFactor() override = default;

    void initializeFromDatabase(const std::string& instanceId) override;
    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;

    static std::shared_ptr<ValueFactor> create(
        const std::string& instanceId,
        std::shared_ptr<astock::database::QtMySQLDatabase> db,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

    friend class ValueFactorTestAccess;

private:
    Params params_;

    QString selectedColumn() const;
    double scoreFromRawValue(double rawValue) const;
    void loadConfig(const foundation::json::JsonFacade& config) override;
};

} // namespace factor