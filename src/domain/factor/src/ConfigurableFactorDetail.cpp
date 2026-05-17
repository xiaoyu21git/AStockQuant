#include "domain/factor/include/ConfigurableFactorDetail.h"
#include "domain/factor/include/FactorNeutralizationUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <string_view>

namespace factor {
namespace configurable_factor_detail {

thread_local BatchComputationCache* activeBatchComputationCache = nullptr;

namespace {

const factor::bridge::FieldKey kAmihudIlliquidityField{"amihud_illiquidity"};
const factor::bridge::FieldKey kIndustrialAddedValueYoyField{"industrial_added_value_yoy"};
const factor::bridge::FieldKey kManufacturingPmiField{"manufacturing_pmi"};
const factor::bridge::FieldKey kGdpYoyField{"gdp_yoy"};
const factor::bridge::FieldKey kCpiYoyField{"cpi_yoy"};
const factor::bridge::FieldKey kPpiYoyField{"ppi_yoy"};
const factor::bridge::FieldKey kM2YoyField{"m2_yoy"};
const factor::bridge::FieldKey kSocialFinancingStockYoyField{"social_financing_stock_yoy"};
const factor::bridge::FieldKey kM1M2SpreadField{"m1_m2_spread"};
const factor::bridge::FieldKey kTenYearBondYieldField{"ten_year_bond_yield"};
const factor::bridge::FieldKey kShibor3MField{"shibor_3m"};
const factor::bridge::FieldKey kLpr1YField{"lpr_1y"};
const factor::bridge::FieldKey kReserveRequirementRatioField{"reserve_requirement_ratio"};
const factor::bridge::FieldKey kAaCreditSpreadField{"aa_credit_spread"};
const factor::bridge::FieldKey kVixProxyField{"vix_proxy"};
const factor::bridge::FieldKey kIndustryProsperityField{"industry_prosperity"};
const factor::bridge::FieldKey kIndustryMomentumField{"industry_momentum"};
const factor::bridge::FieldKey kIndustryConcentrationField{"industry_concentration"};

CommonIndicatorSpec buildCommonIndicatorSpec(SourceTable sourceTable, const factor::bridge::FieldKey* fieldKey)
{
    CommonIndicatorSpec spec;
    spec.sourceTable = sourceTable;
    spec.fieldKey = fieldKey;
    return spec;
}

CustomMetricDefinition buildMetricDefinition(uint32_t metricId,
                                            std::initializer_list<StandardField> dependencies = {},
                                            std::initializer_list<uint32_t> dependentMetricIds = {})
{
    CustomMetricDefinition definition;
    definition.metricId = metricId;
    definition.dependencies.assign(dependencies.begin(), dependencies.end());
    definition.dependentMetricIds.assign(dependentMetricIds.begin(), dependentMetricIds.end());
    return definition;
}

template <typename SpecType>
void assignMetricSpec(SpecType& spec,
                      uint32_t metricId,
                      SourceTable sourceTable,
                      const factor::bridge::FieldKey* fieldKey,
                      std::initializer_list<StandardField> dependencies = {},
                      std::initializer_list<uint32_t> dependentMetricIds = {})
{
    static_cast<CustomMetricDefinition&>(spec) = buildMetricDefinition(metricId, dependencies, dependentMetricIds);
    spec.common = buildCommonIndicatorSpec(sourceTable, fieldKey);
}

void assignTrimmedUtf8String(std::string& target, const QString& value)
{
    const auto utf8 = value.trimmed().toUtf8();
    target.assign(utf8.constData(), static_cast<size_t>(utf8.size()));
}

void buildBatchSeriesKey(std::string& key,
                         std::string_view date,
                         std::string_view field,
                         int window,
                         bool useBulkSymbols)
{
    key.clear();
    key.reserve(date.size() + field.size() + 32);
    key.append(date);
    key.append("|series|");
    key.append(field);
    key.push_back('|');
    key.append(std::to_string(window));
    key.push_back('|');
    key.append(useBulkSymbols ? "selected" : "all");
}

void applyConfigurableStandardizationImpl(StandardizationMethod standardization,
                                          std::unordered_map<std::string, double>& values)
{
    if (standardization == StandardizationMethod::None || values.empty()) {
        return;
    }

    std::vector<std::pair<std::string, double>> finiteValues;
    finiteValues.reserve(values.size());
    for (const auto& [symbol, value] : values) {
        if (std::isfinite(value)) {
            finiteValues.emplace_back(symbol, value);
        }
    }
    if (finiteValues.empty()) {
        return;
    }

    if (standardization == StandardizationMethod::Percentile || standardization == StandardizationMethod::Rank) {
        std::sort(finiteValues.begin(), finiteValues.end(), [](const auto& left, const auto& right) {
            return left.second < right.second;
        });
        if (finiteValues.size() == 1) {
            values[finiteValues.front().first] = 1.0;
            return;
        }
        for (size_t index = 0; index < finiteValues.size(); ++index) {
            values[finiteValues[index].first] = standardization == StandardizationMethod::Rank
                ? static_cast<double>(index + 1)
                : static_cast<double>(index) / static_cast<double>(finiteValues.size() - 1);
        }
        return;
    }

    std::vector<double> numbers;
    numbers.reserve(finiteValues.size());
    for (const auto& item : finiteValues) {
        numbers.push_back(item.second);
    }

    if (standardization == StandardizationMethod::ZScore) {
        const double mean = std::accumulate(numbers.begin(), numbers.end(), 0.0) / static_cast<double>(numbers.size());
        double variance = 0.0;
        for (double value : numbers) {
            const double delta = value - mean;
            variance += delta * delta;
        }
        const double stddev = std::sqrt(variance / static_cast<double>(numbers.size()));
        if (stddev <= 1e-12) {
            return;
        }
        for (auto& [symbol, value] : values) {
            if (std::isfinite(value)) {
                value = (value - mean) / stddev;
            }
        }
        return;
    }

    if (standardization == StandardizationMethod::MinMax) {
        const auto [minIt, maxIt] = std::minmax_element(numbers.begin(), numbers.end());
        const double range = *maxIt - *minIt;
        if (range <= 1e-12) {
            return;
        }
        for (auto& [symbol, value] : values) {
            if (std::isfinite(value)) {
                value = (value - *minIt) / range;
            }
        }
    }
}

template <typename EnumType>
foundation::json::JsonFacade uniqueEnumArrayJson(const std::vector<EnumType>& values, EnumType unknownValue)
{
    auto result = foundation::json::JsonFacade::createArray();
    std::unordered_set<int> seen;
    for (const EnumType value : values) {
        if (value == unknownValue) {
            continue;
        }
        const int intValue = static_cast<int>(value);
        if (seen.insert(intValue).second) {
            result.push_back(json_helper::toJsonValue(intValue));
        }
    }
    return result;
}

} // namespace

void applyConfigurableStandardization(StandardizationMethod standardization,
                                      std::unordered_map<std::string, double>& values)
{
    applyConfigurableStandardizationImpl(standardization, values);
}

QString configurableFrequencyText(DataFrequency frequency)
{
    switch (frequency) {
    case DataFrequency::Daily:
        return QStringLiteral("daily");
    case DataFrequency::Weekly:
        return QStringLiteral("weekly");
    case DataFrequency::Monthly:
        return QStringLiteral("monthly");
    case DataFrequency::Quarterly:
        return QStringLiteral("quarterly");
    case DataFrequency::Yearly:
        return QStringLiteral("yearly");
    default:
        return QStringLiteral("daily");
    }
}

QString configurableStandardizationText(StandardizationMethod standardization)
{
    switch (standardization) {
    case StandardizationMethod::None:
        return QStringLiteral("none");
    case StandardizationMethod::ZScore:
        return QStringLiteral("zscore");
    case StandardizationMethod::MinMax:
        return QStringLiteral("minmax");
    case StandardizationMethod::Rank:
        return QStringLiteral("rank");
    case StandardizationMethod::Percentile:
        return QStringLiteral("percentile");
    default:
        return QStringLiteral("none");
    }
}

QString configurableNeutralizationModeText(NeutralizationStatus neutralizationMode)
{
    switch (neutralizationMode) {
    case NeutralizationStatus::Disabled:
        return QStringLiteral("disabled");
    case NeutralizationStatus::Requested:
        return QStringLiteral("requested");
    case NeutralizationStatus::HistoricalViewCrossSectionIndustryMarketCap:
        return QStringLiteral("historical_view_cross_section_industry_size");
    case NeutralizationStatus::HistoricalViewFailed:
        return QStringLiteral("historical_view_neutralization_failed");
    default:
        return QStringLiteral("disabled");
    }
}

QString dividendMetricText(DividendMetric metric)
{
    switch (metric) {
    case DividendMetric::DIVIDEND_YIELD:
        return QStringLiteral("dividend_yield");
    case DividendMetric::PAYOUT_RATIO:
        return QStringLiteral("payout_ratio");
    case DividendMetric::DIVIDEND_STABILITY:
        return QStringLiteral("dividend_stability");
    default:
        return QString();
    }
}

QString industryMetricText(IndustryMetric metric)
{
    switch (metric) {
    case IndustryMetric::INDUSTRY_PROSPERITY:
        return QStringLiteral("industry_prosperity");
    case IndustryMetric::INDUSTRY_MOMENTUM:
        return QStringLiteral("industry_momentum");
    case IndustryMetric::INDUSTRY_CONCENTRATION:
        return QStringLiteral("industry_concentration");
    default:
        return QString();
    }
}

QString sectorTypeText(ConfigurableSectorType sectorType)
{
    switch (sectorType) {
    case ConfigurableSectorType::SW_L1:
        return QStringLiteral("sw_l1");
    case ConfigurableSectorType::SW_L2:
        return QStringLiteral("sw_l2");
    case ConfigurableSectorType::CITIC_L1:
        return QStringLiteral("citic_l1");
    case ConfigurableSectorType::CITIC_L2:
        return QStringLiteral("citic_l2");
    default:
        return QString();
    }
}

bool applyHistoricalViewIndustrySizeNeutralization(const CalculationContext& context,
                                                   std::unordered_map<std::string, double>& values,
                                                   QString* errorMessage)
{
    return neutralization::applyIndustrySizeNeutralization(context, values, errorMessage);
}

TechnicalPriceIndicatorSpec technicalPriceIndicatorSpec(TechnicalPriceType priceType)
{
    TechnicalPriceIndicatorSpec spec;
    spec.priceType = priceType;
    switch (priceType) {
    case TechnicalPriceType::OPEN:
        assignMetricSpec(spec,
                         1000u + static_cast<uint32_t>(priceType),
                         SourceTable::DAILY_BAR,
                         &factor::bridge::MarketBarFieldKeys::OPEN,
                         {StandardField::OpenPrice});
        return spec;
    case TechnicalPriceType::HIGH:
        assignMetricSpec(spec,
                         1000u + static_cast<uint32_t>(priceType),
                         SourceTable::DAILY_BAR,
                         &factor::bridge::MarketBarFieldKeys::HIGH,
                         {StandardField::HighPrice});
        return spec;
    case TechnicalPriceType::LOW:
        assignMetricSpec(spec,
                         1000u + static_cast<uint32_t>(priceType),
                         SourceTable::DAILY_BAR,
                         &factor::bridge::MarketBarFieldKeys::LOW,
                         {StandardField::LowPrice});
        return spec;
    case TechnicalPriceType::CLOSE:
        assignMetricSpec(spec,
                         1000u + static_cast<uint32_t>(priceType),
                         SourceTable::DAILY_BAR,
                         &factor::bridge::MarketBarFieldKeys::CLOSE,
                         {StandardField::ClosePrice});
        return spec;
    default:
        return spec;
    }
}

foundation::json::JsonFacade technicalIndicatorArrayJson(const std::vector<TechnicalIndicator>& indicators)
{
    return uniqueEnumArrayJson(indicators, TechnicalIndicator::UNKNOWN);
}

foundation::json::JsonFacade macroDimensionArrayJson(const std::vector<MacroDimension>& dimensions)
{
    return uniqueEnumArrayJson(dimensions, MacroDimension::UNKNOWN);
}

foundation::json::JsonFacade macroIndicatorArrayJson(const std::vector<MacroIndicator>& indicators)
{
    return uniqueEnumArrayJson(indicators, MacroIndicator::UNKNOWN);
}

foundation::json::JsonFacade growthMetricArrayJson(const std::vector<GrowthMetric>& metrics)
{
    return uniqueEnumArrayJson(metrics, GrowthMetric::UNKNOWN);
}

foundation::json::JsonFacade dividendMetricArrayJson(const std::vector<DividendMetric>& metrics)
{
    return uniqueEnumArrayJson(metrics, DividendMetric::UNKNOWN);
}

const factor::bridge::FieldKey* macroIndicatorFieldKey(MacroIndicator indicator)
{
    switch (indicator) {
    case MacroIndicator::INDUSTRIAL_ADDED_VALUE_YOY:
        return &kIndustrialAddedValueYoyField;
    case MacroIndicator::MANUFACTURING_PMI:
        return &kManufacturingPmiField;
    case MacroIndicator::GDP_YOY:
        return &kGdpYoyField;
    case MacroIndicator::CPI_YOY:
        return &kCpiYoyField;
    case MacroIndicator::PPI_YOY:
        return &kPpiYoyField;
    case MacroIndicator::M2_YOY:
        return &kM2YoyField;
    case MacroIndicator::SOCIAL_FINANCING_STOCK_YOY:
        return &kSocialFinancingStockYoyField;
    case MacroIndicator::M1_M2_SPREAD:
        return &kM1M2SpreadField;
    case MacroIndicator::TEN_YEAR_BOND_YIELD:
        return &kTenYearBondYieldField;
    case MacroIndicator::SHIBOR_3M:
        return &kShibor3MField;
    case MacroIndicator::LPR_1Y:
        return &kLpr1YField;
    case MacroIndicator::RESERVE_REQUIREMENT_RATIO:
        return &kReserveRequirementRatioField;
    case MacroIndicator::AA_CREDIT_SPREAD:
        return &kAaCreditSpreadField;
    case MacroIndicator::VIX_PROXY:
        return &kVixProxyField;
    default:
        return nullptr;
    }
}

MacroIndicatorSpec macroIndicatorSpec(MacroIndicator indicator)
{
    MacroIndicatorSpec spec;
    spec.indicator = indicator;
    static_cast<CustomMetricDefinition&>(spec) = buildMetricDefinition(7000u + static_cast<uint32_t>(indicator));
    spec.dimension = macroIndicatorDimension(indicator);
    spec.common = buildCommonIndicatorSpec(SourceTable::UNKNOWN, macroIndicatorFieldKey(indicator));
    switch (indicator) {
    case MacroIndicator::CPI_YOY:
    case MacroIndicator::PPI_YOY:
    case MacroIndicator::TEN_YEAR_BOND_YIELD:
    case MacroIndicator::SHIBOR_3M:
    case MacroIndicator::AA_CREDIT_SPREAD:
    case MacroIndicator::VIX_PROXY:
        spec.direction = -1.0;
        break;
    default:
        spec.direction = 1.0;
        break;
    }
    return spec;
}

int macroWindowScale(DataFrequency frequency)
{
    if (frequency == DataFrequency::Weekly) {
        return 5;
    }
    if (frequency == DataFrequency::Monthly) {
        return 21;
    }
    if (frequency == DataFrequency::Quarterly) {
        return 63;
    }
    return 1;
}

double indicatorWeightForDimension(MacroDimension dimension)
{
    switch (dimension) {
    case MacroDimension::GROWTH:
    case MacroDimension::CREDIT:
    case MacroDimension::POLICY:
        return 1.0;
    case MacroDimension::INFLATION:
    case MacroDimension::RATES:
    case MacroDimension::RISK_APPETITE:
        return 0.9;
    default:
        return 1.0;
    }
}

GrowthIndicatorSpec growthIndicatorSpec(GrowthMetric metric)
{
    GrowthIndicatorSpec spec;
    spec.metric = metric;
    switch (metric) {
    case GrowthMetric::REVENUE_GROWTH:
        assignMetricSpec(spec,
                         2000u + static_cast<uint32_t>(metric),
                         SourceTable::FINANCIAL_INDICATOR,
                         &factor::bridge::FinancialFieldKeys::TOTAL_REVENUE);
        return spec;
    case GrowthMetric::NET_PROFIT_GROWTH:
        assignMetricSpec(spec,
                         2000u + static_cast<uint32_t>(metric),
                         SourceTable::FINANCIAL_INDICATOR,
                         &factor::bridge::FinancialFieldKeys::NET_PROFIT);
        return spec;
    case GrowthMetric::DELTA_ROE:
        assignMetricSpec(spec,
                         2000u + static_cast<uint32_t>(metric),
                         SourceTable::FINANCIAL_INDICATOR,
                         &factor::bridge::FinancialFieldKeys::ROE,
                         {StandardField::ROE});
        return spec;
    case GrowthMetric::SUE:
        assignMetricSpec(spec,
                         2000u + static_cast<uint32_t>(metric),
                         SourceTable::FINANCIAL_INDICATOR,
                         &factor::bridge::FinancialFieldKeys::EPS);
        return spec;
    default:
        return spec;
    }
}

IndustryIndicatorSpec industryIndicatorSpec(IndustryMetric metric)
{
    IndustryIndicatorSpec spec;
    spec.metric = metric;
    switch (metric) {
    case IndustryMetric::INDUSTRY_PROSPERITY:
        assignMetricSpec(spec,
                         5000u + static_cast<uint32_t>(metric),
                         SourceTable::UNKNOWN,
                         &kIndustryProsperityField);
        return spec;
    case IndustryMetric::INDUSTRY_MOMENTUM:
        assignMetricSpec(spec,
                         5000u + static_cast<uint32_t>(metric),
                         SourceTable::UNKNOWN,
                         &kIndustryMomentumField);
        return spec;
    case IndustryMetric::INDUSTRY_CONCENTRATION:
        assignMetricSpec(spec,
                         5000u + static_cast<uint32_t>(metric),
                         SourceTable::UNKNOWN,
                         &kIndustryConcentrationField);
        return spec;
    default:
        return spec;
    }
}

LiquidityIndicatorSpec liquidityIndicatorSpec(LiquidityMetric metric)
{
    LiquidityIndicatorSpec spec;
    spec.metric = metric;
    switch (metric) {
    case LiquidityMetric::TURNOVER_RATE:
        assignMetricSpec(spec,
                         3000u + static_cast<uint32_t>(metric),
                         SourceTable::DAILY_BAR,
                         &factor::bridge::MarketBarFieldKeys::TURNOVER_RATE,
                         {StandardField::TurnoverRate});
        return spec;
    case LiquidityMetric::VOLUME:
        assignMetricSpec(spec,
                         3000u + static_cast<uint32_t>(metric),
                         SourceTable::DAILY_BAR,
                         &factor::bridge::MarketBarFieldKeys::VOLUME,
                         {StandardField::Volume});
        return spec;
    case LiquidityMetric::AMIHUD_ILLIQUIDITY:
        assignMetricSpec(spec,
                         3000u + static_cast<uint32_t>(metric),
                         SourceTable::DAILY_BAR,
                         &kAmihudIlliquidityField,
                         {StandardField::ClosePrice, StandardField::Volume});
        return spec;
    case LiquidityMetric::AMPLITUDE:
        assignMetricSpec(spec,
                         3000u + static_cast<uint32_t>(metric),
                         SourceTable::DAILY_BAR,
                         &factor::bridge::MarketBarFieldKeys::AMPLITUDE,
                         {StandardField::HighPrice, StandardField::LowPrice});
        return spec;
    default:
        return spec;
    }
}

DividendIndicatorSpec dividendIndicatorSpec(DividendMetric metric)
{
    DividendIndicatorSpec spec;
    spec.metric = metric;
    switch (metric) {
    case DividendMetric::DIVIDEND_YIELD:
        assignMetricSpec(spec,
                         4000u + static_cast<uint32_t>(metric),
                         SourceTable::FINANCIAL_INDICATOR,
                         &factor::bridge::FinancialFieldKeys::DIVIDEND_YIELD);
        return spec;
    case DividendMetric::PAYOUT_RATIO:
        assignMetricSpec(spec,
                         4000u + static_cast<uint32_t>(metric),
                         SourceTable::FINANCIAL_INDICATOR,
                         &factor::bridge::FinancialFieldKeys::PAYOUT_RATIO);
        return spec;
    case DividendMetric::DIVIDEND_STABILITY:
        assignMetricSpec(spec,
                         4000u + static_cast<uint32_t>(metric),
                         SourceTable::FINANCIAL_INDICATOR,
                         &factor::bridge::FinancialFieldKeys::DIVIDEND_STABILITY);
        return spec;
    default:
        return spec;
    }
}

SentimentIndicatorSpec sentimentIndicatorSpec(SentimentMetric metric)
{
    SentimentIndicatorSpec spec;
    spec.metric = metric;
    switch (metric) {
    case SentimentMetric::SENTIMENT_SCORE:
        assignMetricSpec(spec,
                         6000u + static_cast<uint32_t>(metric),
                         SourceTable::NEWS_SENTIMENT,
                         &factor::bridge::NewsFieldKeys::SENTIMENT_SCORE);
        return spec;
    case SentimentMetric::SOCIAL_SENTIMENT:
        assignMetricSpec(spec,
                         6000u + static_cast<uint32_t>(metric),
                         SourceTable::NEWS_SENTIMENT,
                         &factor::bridge::NewsFieldKeys::SOCIAL_SENTIMENT);
        return spec;
    case SentimentMetric::INVESTOR_SENTIMENT:
        assignMetricSpec(spec,
                         6000u + static_cast<uint32_t>(metric),
                         SourceTable::NEWS_SENTIMENT,
                         &factor::bridge::NewsFieldKeys::INVESTOR_SENTIMENT);
        return spec;
    case SentimentMetric::MARKET_SENTIMENT:
        assignMetricSpec(spec,
                         6000u + static_cast<uint32_t>(metric),
                         SourceTable::NEWS_SENTIMENT,
                         &factor::bridge::NewsFieldKeys::MARKET_SENTIMENT);
        return spec;
    default:
        return spec;
    }
}

double normalizeDividendYieldFloor(double rawValue)
{
    return rawValue > 1.0 ? rawValue / 100.0 : rawValue;
}

double sectorIndustryWeight(ConfigurableSectorType sectorType)
{
    if (sectorType == ConfigurableSectorType::SW_L2) {
        return 0.85;
    }
    if (sectorType == ConfigurableSectorType::CITIC_L1) {
        return 0.95;
    }
    if (sectorType == ConfigurableSectorType::CITIC_L2) {
        return 0.8;
    }
    return 1.0;
}

double safeMean(const std::vector<double>& values)
{
    if (values.empty()) {
        return 0.0;
    }
    return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}

double safeFiniteMean(const std::vector<double>& values)
{
    double sum = 0.0;
    int count = 0;
    for (double value : values) {
        if (!std::isfinite(value)) {
            continue;
        }
        sum += value;
        ++count;
    }
    if (count == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return sum / static_cast<double>(count);
}

double safeRatio(double numerator, double denominator)
{
    if (!std::isfinite(numerator) || !std::isfinite(denominator) || std::abs(denominator) < 1e-12) {
        return 0.0;
    }
    return numerator / denominator;
}

void buildBatchCrossSectionKey(std::string& key, const std::string& date, const QString& field)
{
    std::string fieldName;
    assignTrimmedUtf8String(fieldName, field);
    key.clear();
    key.reserve(date.size() + fieldName.size() + 8);
    key.append(date);
    key.append("|cross|");
    key.append(fieldName);
}

std::unordered_map<std::string, std::vector<double>> fetchBatchSeriesMap(
    const CalculationContext& context,
    const QString& field,
    int window)
{
    std::unordered_map<std::string, std::vector<double>> resolvedSeries;
    if (window <= 0 || !context.historicalView) {
        return resolvedSeries;
    }

    std::string fieldName;
    assignTrimmedUtf8String(fieldName, field);
    if (fieldName.empty()) {
        return resolvedSeries;
    }

    if (!context.historicalView->hasField(fieldName)) {
        return resolvedSeries;
    }

    const bool useBulkSymbols = !context.symbols.empty();
    std::string batchKey;
    buildBatchSeriesKey(batchKey, context.date, fieldName, window, useBulkSymbols);
    if (activeBatchComputationCache && activeBatchComputationCache->historicalView == context.historicalView) {
        const auto cacheIt = activeBatchComputationCache->seriesByKey.find(batchKey);
        if (cacheIt != activeBatchComputationCache->seriesByKey.end()) {
            return cacheIt->second;
        }
    }

    const std::vector<std::string> symbols = useBulkSymbols
        ? context.symbols
        : context.historicalView->getAvailableSymbols(context.date);
    if (symbols.empty()) {
        return resolvedSeries;
    }

    const auto batchValues = context.historicalView->getBatchTimeSeries(symbols, context.date, window, {fieldName});
    const auto fieldIt = batchValues.find(fieldName);
    if (fieldIt != batchValues.end()) {
        resolvedSeries = fieldIt->second;
    }
    if (activeBatchComputationCache && activeBatchComputationCache->historicalView == context.historicalView) {
        activeBatchComputationCache->seriesByKey[batchKey] = resolvedSeries;
    }

    return resolvedSeries;
}

SeriesMatrixBatch collectSeriesMatrix(
    const std::unordered_map<std::string, std::vector<double>>& seriesBySymbol,
    size_t minimumLength)
{
    SeriesMatrixBatch batch;
    if (seriesBySymbol.empty()) {
        return batch;
    }

    size_t commonLength = std::numeric_limits<size_t>::max();
    for (const auto& [symbol, values] : seriesBySymbol) {
        Q_UNUSED(symbol);
        if (values.size() < minimumLength) {
            continue;
        }
        commonLength = (std::min)(commonLength, values.size());
    }
    if (commonLength == std::numeric_limits<size_t>::max() || commonLength < minimumLength) {
        return batch;
    }

    batch.symbols.reserve(seriesBySymbol.size());
    for (const auto& [symbol, values] : seriesBySymbol) {
        if (values.size() >= commonLength) {
            batch.symbols.push_back(symbol);
        }
    }
    if (batch.symbols.empty()) {
        return batch;
    }

    batch.values.resize(static_cast<Eigen::Index>(batch.symbols.size()), static_cast<Eigen::Index>(commonLength));
    for (Eigen::Index row = 0; row < batch.values.rows(); ++row) {
        const auto& values = seriesBySymbol.at(batch.symbols[static_cast<size_t>(row)]);
        const size_t start = values.size() - commonLength;
        for (Eigen::Index col = 0; col < batch.values.cols(); ++col) {
            batch.values(row, col) = values[start + static_cast<size_t>(col)];
        }
    }

    return batch;
}

Eigen::VectorXd buildReturnVector(const std::vector<double>& values)
{
    if (values.size() < 2) {
        return {};
    }

    Eigen::VectorXd returns(static_cast<Eigen::Index>(values.size() - 1));
    for (size_t index = 1; index < values.size(); ++index) {
        const double previous = values[index - 1];
        const double current = values[index];
        if (!std::isfinite(previous) || !std::isfinite(current) || std::abs(previous) < 1e-12) {
            returns(static_cast<Eigen::Index>(index - 1)) = std::numeric_limits<double>::quiet_NaN();
            continue;
        }
        returns(static_cast<Eigen::Index>(index - 1)) = current / previous - 1.0;
    }
    return returns;
}

Eigen::MatrixXd buildReturnMatrix(const Eigen::MatrixXd& values)
{
    if (values.cols() < 2) {
        return {};
    }

    Eigen::MatrixXd returns(values.rows(), values.cols() - 1);
    for (Eigen::Index row = 0; row < values.rows(); ++row) {
        for (Eigen::Index col = 1; col < values.cols(); ++col) {
            const double previous = values(row, col - 1);
            const double current = values(row, col);
            if (!std::isfinite(previous) || !std::isfinite(current) || std::abs(previous) < 1e-12) {
                returns(row, col - 1) = std::numeric_limits<double>::quiet_NaN();
                continue;
            }
            returns(row, col - 1) = current / previous - 1.0;
        }
    }
    return returns;
}

Eigen::VectorXd batchCorrelate(const Eigen::MatrixXd& symbolReturns,
                               const Eigen::VectorXd& benchmarkReturns)
{
    const Eigen::Index columnCount = (std::min)(symbolReturns.cols(), benchmarkReturns.size());
    Eigen::VectorXd correlations(symbolReturns.rows());
    correlations.setConstant(std::numeric_limits<double>::quiet_NaN());
    if (columnCount < 2) {
        return correlations;
    }

    for (Eigen::Index row = 0; row < symbolReturns.rows(); ++row) {
        std::vector<double> symbolValues;
        std::vector<double> benchmarkValues;
        symbolValues.reserve(static_cast<size_t>(columnCount));
        benchmarkValues.reserve(static_cast<size_t>(columnCount));
        for (Eigen::Index col = 0; col < columnCount; ++col) {
            const double symbolValue = symbolReturns(row, col);
            const double benchmarkValue = benchmarkReturns(col);
            if (!std::isfinite(symbolValue) || !std::isfinite(benchmarkValue)) {
                continue;
            }
            symbolValues.push_back(symbolValue);
            benchmarkValues.push_back(benchmarkValue);
        }

        if (symbolValues.size() < 2) {
            continue;
        }

        const double symbolMean = safeMean(symbolValues);
        const double benchmarkMean = safeMean(benchmarkValues);
        double numerator = 0.0;
        double symbolVariance = 0.0;
        double benchmarkVariance = 0.0;
        for (size_t index = 0; index < symbolValues.size(); ++index) {
            const double symbolCentered = symbolValues[index] - symbolMean;
            const double benchmarkCentered = benchmarkValues[index] - benchmarkMean;
            numerator += symbolCentered * benchmarkCentered;
            symbolVariance += symbolCentered * symbolCentered;
            benchmarkVariance += benchmarkCentered * benchmarkCentered;
        }

        const double denominator = std::sqrt(symbolVariance * benchmarkVariance);
        if (denominator < 1e-12) {
            continue;
        }
        correlations(row) = numerator / denominator;
    }

    return correlations;
}

} // namespace configurable_factor_detail
} // namespace factor