#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace foundation::json { class JsonFacade; }

namespace factor {

enum class DataFrequency : uint8_t {
    Daily,
    Weekly,
    Monthly,
    Quarterly,
    Yearly
};

enum class StandardizationMethod : uint8_t {
    None,
    ZScore,
    MinMax,
    Rank,
    Percentile
};

enum class NeutralizationMethod : uint8_t {
    None,
    Industry,
    MarketCap,
    IndustryMarketCap
};

enum class NeutralizationStatus : uint8_t {
    Disabled,
    Requested,
    HistoricalViewCrossSectionIndustryMarketCap,
    HistoricalViewFailed
};

enum class LagMode : uint8_t {
    None,
    SinglePeriod,
    MultiPeriod,
    Adaptive
};

enum class TechnicalCombinationMode : uint8_t {
    EqualWeight,
    NormalizedAverage
};

enum class ConfigurableSectorType : uint8_t {
    SW_L1,
    SW_L2,
    CITIC_L1,
    CITIC_L2,
    Unknown
};

enum class StandardField : uint16_t {
    ClosePrice,
    OpenPrice,
    HighPrice,
    LowPrice,
    Volume,
    TurnoverRate,
    MarketCap,
    PE_TTM,
    PB,
    PS_TTM,
    ROE,
    ROA
};

struct CommonMetricParams {
    DataFrequency frequency{DataFrequency::Daily};
    uint16_t lookbackWindow{252};
    StandardizationMethod standardization{StandardizationMethod::None};
    bool neutralizationEnabled{false};
    NeutralizationMethod neutralizationMethod{NeutralizationMethod::None};
    bool lagEnabled{false};
    LagMode lagMode{LagMode::None};
    uint8_t lagPeriods{1};
};

struct CustomMetricDefinition {
    uint32_t metricId{0};
    std::vector<StandardField> dependencies;
    std::vector<uint32_t> dependentMetricIds;
};

struct CommonParams : CommonMetricParams {
    uint16_t window = 20;

    void fromJson(const foundation::json::JsonFacade& json);
};

struct FactorMetricConfig {
    CommonMetricParams commonParams;
    std::vector<CustomMetricDefinition> customMetrics;
};

} // namespace factor
