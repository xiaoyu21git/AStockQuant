#pragma once

#include "ConfigurableFactor.h"
#include "FactorMetricConfig.h"
#include "ui/bridge/include/DataFetchFieldContractUtils.h"

#include <Eigen/Dense>

#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define ASTOCK_CONFIGURABLE_GROWTH_BASIC_SERIES_POINTS 2
#define ASTOCK_CONFIGURABLE_GROWTH_SUE_SEASONAL_LAG 4
#define ASTOCK_CONFIGURABLE_GROWTH_SUE_HISTORY_COUNT 3
#define ASTOCK_CONFIGURABLE_GROWTH_SUE_SERIES_POINTS \
    (ASTOCK_CONFIGURABLE_GROWTH_SUE_SEASONAL_LAG + ASTOCK_CONFIGURABLE_GROWTH_SUE_HISTORY_COUNT + 1)

namespace factor {
namespace configurable_factor_detail {

struct BatchComputationCache
{
    std::shared_ptr<HistoricalView> historicalView;
    std::unordered_map<std::string, std::unordered_map<std::string, double>> crossSectionsByKey;
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> seriesByKey;
};

extern thread_local BatchComputationCache* activeBatchComputationCache;

class BatchComputationCacheScope
{
public:
    explicit BatchComputationCacheScope(BatchComputationCache& cache)
        : previous_(activeBatchComputationCache)
    {
        activeBatchComputationCache = &cache;
    }

    ~BatchComputationCacheScope()
    {
        activeBatchComputationCache = previous_;
    }

private:
    BatchComputationCache* previous_ = nullptr;
};

struct FieldSourceSpec
{
    SourceTable sourceTable{SourceTable::UNKNOWN};
    const factor::bridge::FieldKey* fieldKey{nullptr};

    bool isValid() const
    {
        return sourceTable != SourceTable::UNKNOWN && fieldKey != nullptr;
    }
};

struct CommonIndicatorSpec
{
    SourceTable sourceTable{SourceTable::UNKNOWN};
    const factor::bridge::FieldKey* fieldKey{nullptr};

    bool hasField() const
    {
        return fieldKey != nullptr;
    }

    bool hasResolvedSource() const
    {
        return sourceTable != SourceTable::UNKNOWN && fieldKey != nullptr;
    }
};

struct TechnicalPriceIndicatorSpec : CustomMetricDefinition
{
    TechnicalPriceType priceType{TechnicalPriceType::UNKNOWN};
    CommonIndicatorSpec common;
};

struct MacroIndicatorSpec : CustomMetricDefinition
{
    MacroIndicator indicator{MacroIndicator::UNKNOWN};
    CommonIndicatorSpec common;
    MacroDimension dimension{MacroDimension::UNKNOWN};
    double direction = 1.0;
};

struct GrowthIndicatorSpec : CustomMetricDefinition
{
    GrowthMetric metric{GrowthMetric::UNKNOWN};
    CommonIndicatorSpec common;
};

struct LiquidityIndicatorSpec : CustomMetricDefinition
{
    LiquidityMetric metric{LiquidityMetric::UNKNOWN};
    CommonIndicatorSpec common;
};

struct DividendIndicatorSpec : CustomMetricDefinition
{
    DividendMetric metric{DividendMetric::UNKNOWN};
    CommonIndicatorSpec common;
};

struct IndustryIndicatorSpec : CustomMetricDefinition
{
    IndustryMetric metric{IndustryMetric::UNKNOWN};
    CommonIndicatorSpec common;
};

struct SentimentIndicatorSpec : CustomMetricDefinition
{
    SentimentMetric metric{SentimentMetric::UNKNOWN};
    CommonIndicatorSpec common;
};

struct SeriesMatrixBatch
{
    std::vector<std::string> symbols;
    Eigen::MatrixXd values;
};

enum class TechnicalConfigMode : uint8_t {
    MissingTechnicalIndicators,
    TechnicalIndicators
};

enum class DividendConfigMode : uint8_t {
    DividendMetric,
    DividendMetrics
};

enum class ConfiguredMode : uint8_t {
    Configured
};

enum class ConfigurableDataMode : uint8_t {
    Direct,
    DirectCrossSection,
    BatchCrossSection,
    FinancialSeriesDirect
};

enum class MacroComputationMode : uint8_t {
    ProxySensitivity
};

enum class LaggedDateMode : uint8_t {
    Disabled,
    AnchorDate,
    ProviderScan
};

void applyConfigurableStandardization(StandardizationMethod standardization,
                                      std::unordered_map<std::string, double>& values);
bool applyHistoricalViewIndustrySizeNeutralization(const CalculationContext& context,
                                                   std::unordered_map<std::string, double>& values,
                                                   std::string* errorMessage);
TechnicalPriceIndicatorSpec technicalPriceIndicatorSpec(TechnicalPriceType priceType);
foundation::json::JsonFacade technicalIndicatorArrayJson(const std::vector<TechnicalIndicator>& indicators);
foundation::json::JsonFacade macroDimensionArrayJson(const std::vector<MacroDimension>& dimensions);
foundation::json::JsonFacade macroIndicatorArrayJson(const std::vector<MacroIndicator>& indicators);
foundation::json::JsonFacade growthMetricArrayJson(const std::vector<GrowthMetric>& metrics);
foundation::json::JsonFacade dividendMetricArrayJson(const std::vector<DividendMetric>& metrics);
MacroIndicatorSpec macroIndicatorSpec(MacroIndicator indicator);
int macroWindowScale(DataFrequency frequency);
double indicatorWeightForDimension(MacroDimension dimension);
GrowthIndicatorSpec growthIndicatorSpec(GrowthMetric metric);
IndustryIndicatorSpec industryIndicatorSpec(IndustryMetric metric);
LiquidityIndicatorSpec liquidityIndicatorSpec(LiquidityMetric metric);
DividendIndicatorSpec dividendIndicatorSpec(DividendMetric metric);
SentimentIndicatorSpec sentimentIndicatorSpec(SentimentMetric metric);
double normalizeDividendYieldFloor(double rawValue);
double sectorIndustryWeight(ConfigurableSectorType sectorType);
double safeMean(const std::vector<double>& values);
double safeFiniteMean(const std::vector<double>& values);
double safeRatio(double numerator, double denominator);
void buildBatchCrossSectionKey(std::string& key, const std::string& date, const std::string& field);
std::unordered_map<std::string, std::vector<double>> fetchBatchSeriesMap(
    const CalculationContext& context,
    const std::string& field,
    int window);
SeriesMatrixBatch collectSeriesMatrix(
    const std::unordered_map<std::string, std::vector<double>>& seriesBySymbol,
    size_t minimumLength);
Eigen::VectorXd buildReturnVector(const std::vector<double>& values);
Eigen::MatrixXd buildReturnMatrix(const Eigen::MatrixXd& values);
Eigen::VectorXd batchCorrelate(const Eigen::MatrixXd& symbolReturns,
                               const Eigen::VectorXd& benchmarkReturns);

} // namespace configurable_factor_detail
} // namespace factor