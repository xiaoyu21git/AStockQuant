#pragma once

#include <functional>
#include <memory>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include "FactorMetricConfig.h"
#include "factor_enums.h"
#include "foundation/json/json_facade.h"
#include "foundation/Utils/Uuid.h"
#include "DataAvailabilityChecker.h"
#include "HistoricalView.h"
#include "JsonFacadeHelpers.h"

namespace factor {

struct FactorInstanceInfo;

enum class NewStockHandling {
    EXCLUDE_IF_LT_60D,
    INCLUDE
};

enum class SuspendedHandling {
    FORWARD_FILL,
    EXCLUDE,
    SET_NULL
};

enum class DelistedHandling {
    KEEP_UNTIL_DELIST,
    EXCLUDE
};

enum class OutlierHandling {
    WINSORIZE_3SIGMA,
    EXCLUDE,
    KEEP
};

template <typename EnumType>
inline EnumType requireNumericEnumValue(const foundation::json::JsonFacade& value,
                                        const char* fieldName,
                                        int minValue,
                                        int maxValue)
{
    if (!value.isNumber()) {
        throw std::runtime_error(std::string(fieldName) + " 不是枚举数值字段");
    }
    const int enumValue = value.asInt();
    if (enumValue < minValue || enumValue > maxValue) {
        throw std::runtime_error(std::string(fieldName) + " 不是有效的枚举值");
    }
    return static_cast<EnumType>(enumValue);
}

template <typename EnumType>
inline EnumType requireNumericEnumField(const foundation::json::JsonFacade& json,
                                        const char* fieldName,
                                        int minValue,
                                        int maxValue)
{
    if (!json.has(fieldName)) {
        throw std::runtime_error(std::string(fieldName) + " 字段缺失");
    }
    return requireNumericEnumValue<EnumType>(json.get(fieldName), fieldName, minValue, maxValue);
}

// 计算上下文
struct CalculationContext {
    std::string date;                          // 计算日期
    std::vector<std::string> symbols;          // 股票代码列表
    std::shared_ptr<HistoricalView> historicalView;  // 引擎裁剪后的历史视图
    
    CalculationContext() = default;
    
    CalculationContext(const std::string& d,
                      const std::vector<std::string>& s,
                      std::shared_ptr<HistoricalView> view)
        : date(d), symbols(s), historicalView(std::move(view)) {}
};

// 数据需求
struct DataRequirements {
    std::vector<std::string> requiredFields;
    std::vector<std::string> optionalFields;
    std::vector<std::string> alternativeFields;  // 替代字段（当主字段不可用时）
    SourceTable sourceTable{SourceTable::UNKNOWN};
    
    bool hasAlternative(const std::string& field) const {
        return std::find(alternativeFields.begin(), 
                        alternativeFields.end(), field) != alternativeFields.end();
    }
    
    foundation::json::JsonFacade toJson() const {
        auto json = foundation::json::JsonFacade::createObject();
        
        auto requiredArray = foundation::json::JsonFacade::createArray();
        for (const auto& field : requiredFields) {
            requiredArray.push_back(foundation::json::JsonFacade::createString(field));
        }
        json.set("required", requiredArray);
        
        auto optionalArray = foundation::json::JsonFacade::createArray();
        for (const auto& field : optionalFields) {
            optionalArray.push_back(foundation::json::JsonFacade::createString(field));
        }
        json.set("optional", optionalArray);
        
        auto alternativeArray = foundation::json::JsonFacade::createArray();
        for (const auto& field : alternativeFields) {
            alternativeArray.push_back(foundation::json::JsonFacade::createString(field));
        }
        json.set("alternative", alternativeArray);
        json.set("sourceTable", json_helper::toJsonValue(static_cast<int>(sourceTable)));
        
        return json;
    }
};

// 边界规则
struct BoundaryRules {
    int minDataPoints = 21;
    NewStockHandling handleNewStock = NewStockHandling::EXCLUDE_IF_LT_60D;
    SuspendedHandling handleSuspended = SuspendedHandling::FORWARD_FILL;
    DelistedHandling handleDelisted = DelistedHandling::KEEP_UNTIL_DELIST;
    OutlierHandling handleOutliers = OutlierHandling::WINSORIZE_3SIGMA;
    
    foundation::json::JsonFacade toJson() const {
        auto json = foundation::json::JsonFacade::createObject();
        json.set("minDataPoints", json_helper::toJsonValue(minDataPoints));
        json.set("handleNewStock", json_helper::toJsonValue(static_cast<int>(handleNewStock)));
        json.set("handleSuspended", json_helper::toJsonValue(static_cast<int>(handleSuspended)));
        json.set("handleDelisted", json_helper::toJsonValue(static_cast<int>(handleDelisted)));
        json.set("handleOutliers", json_helper::toJsonValue(static_cast<int>(handleOutliers)));
        return json;
    }
};

// 计算结果
struct CalculationResult {
    foundation::utils::Uuid calculationId;
    std::string date;
    std::unordered_map<std::string, double> values;  // symbol -> factor value
    DataStatus dataStatus;
    foundation::json::JsonFacade metadata;
    
    bool isEmpty() const { return values.empty(); }
    
    static CalculationResult createError(const std::string& errorMsg) {
        CalculationResult result;
        result.dataStatus.availability = DataAvailability::UNAVAILABLE;
        result.dataStatus.message = errorMsg;
        return result;
    }
    
    foundation::json::JsonFacade toJson() const {
        auto json = foundation::json::JsonFacade::createObject();
        json.set("calculation_id", json_helper::toJsonValue(calculationId.to_string()));
        json.set("trade_date", json_helper::toJsonValue(date));
        json.set("data_status", dataStatus.toJson());
        json.set("metadata", metadata);
        
        auto valuesJson = foundation::json::JsonFacade::createObject();
        for (const auto& [symbol, value] : values) {
            valuesJson.set(symbol, json_helper::toJsonValue(value));
        }
        json.set("values", valuesJson);
        
        return json;
    }

    static CalculationResult fromJson(const foundation::json::JsonFacade& json) {
        CalculationResult result;
        if (json.has("calculation_id")) {
            result.calculationId = foundation::utils::Uuid::from_string(json.get("calculation_id").asString());
        }
        if (json.has("trade_date")) {
            result.date = json.get("trade_date").asString();
        }
        if (json.has("data_status")) {
            result.dataStatus = DataStatus::fromJson(json.get("data_status"));
        }
        if (json.has("metadata")) {
            result.metadata = json.get("metadata");
        }
        if (json.has("values")) {
            const auto valuesJson = json.get("values");
            for (const std::string& key : valuesJson.keys()) {
                result.values.emplace(key, valuesJson.get(key).asDouble());
            }
        }
        return result;
    }
};

struct CommonRuntimeState {
    DataFrequency frequency{DataFrequency::Daily};
    StandardizationMethod standardization{StandardizationMethod::None};
    std::string effectiveDate;
    NeutralizationStatus neutralizationMode{NeutralizationStatus::Disabled};
};

enum class CommonFieldRequirementMode {
    AllFields,
    AnyField
};

// 因子基类
class BaseFactor {
public:
    BaseFactor();
    virtual ~BaseFactor() = default;
    
    // 禁止拷贝
    BaseFactor(const BaseFactor&) = delete;
    BaseFactor& operator=(const BaseFactor&) = delete;
    
    // 允许移动
    BaseFactor(BaseFactor&&) = default;
    BaseFactor& operator=(BaseFactor&&) = default;
    
    // 计算接口
    virtual CalculationResult calculate(const CalculationContext& context) = 0;
    
    // 批量计算（优化性能）
    virtual std::vector<CalculationResult> calculateBatch(
        const std::vector<CalculationContext>& contexts);
    
    // 数据需求
    virtual DataRequirements getDataRequirements() const = 0;
    
    // 边界规则
    virtual BoundaryRules getBoundaryRules() const = 0;

    // 历史回看天数（chunk 预热用）
    virtual int getLookbackDays() const = 0;

    // 中性化所需字段（所有因子通用，子类继承时天然包含）
    static std::vector<std::string> neutralizationFields() {
        return {"industry_code", "market_cap"};
    }
    
    // 检查数据可用性
    virtual DataStatus checkDataAvailability(const std::string& date) const;
    
    // 获取实例信息
    const foundation::utils::Uuid& getInstanceId() const { return instanceId_; }
    std::string getName() const { return name_; }
    std::string getDescription() const { return description_; }
    FactorType getFactorType() const { return factorType_; }
    
    // 序列化/反序列化
    foundation::json::JsonFacade toJson() const;
    void fromJson(const foundation::json::JsonFacade& json);
    
protected:
    foundation::utils::Uuid instanceId_;
    std::string name_;
    std::string description_;
    FactorType factorType_;
    
    DataRequirements dataRequirements_;
    BoundaryRules boundaryRules_;
    
    std::shared_ptr<DataAvailabilityChecker> dataChecker_;

    bool isHistoricalViewRuntime(const CalculationContext& context) const;
    CalculationResult createHistoricalViewRuntimeError(const CalculationContext& context,
                                                       const std::string& errorMsg) const;

    CalculationResult executeWithCommonParams(
        const CalculationContext& context,
        const CommonMetricParams& params,
        const std::function<std::string()>& effectiveDateResolver,
        const std::function<void(const CommonRuntimeState&, CalculationResult&)>& rawCalculator,
        const std::function<void(const CommonRuntimeState&, CalculationResult&)>& preStandardizationProcessor,
        const std::function<void(const CommonRuntimeState&, CalculationResult&)>& metadataAppender,
        const std::string& dataStatusMessage = "使用缓存数据集") const;

    std::string resolveCommonEffectiveDateForFields(
        const CalculationContext& context,
        const CommonMetricParams& params,
        const std::vector<std::string>& requiredFieldsForDateResolution,
        CommonFieldRequirementMode requirementMode) const;

    static CommonMetricParams buildCommonMetricParams(int lookbackWindow,
                                                      bool laggedEnabled,
                                                      DataFrequency frequency,
                                                      StandardizationMethod standardization,
                                                      bool neutralizationEnabled,
                                                      uint8_t lagPeriods = 1);

    static void appendRequiredField(DataRequirements& requirements,
                                    const std::string& field);
    static void appendHistoricalNeutralizationRequirements(
        DataRequirements& requirements,
        bool neutralizationEnabled,
        SourceTable nonNeutralizedSourceTable = SourceTable::UNKNOWN);
    static BoundaryRules buildBoundaryRules(
        int minDataPoints,
        OutlierHandling handleOutliers = OutlierHandling::KEEP);

    static double calculatePercentileValue(std::vector<double> values, double quantile);
    static void applyCommonStandardization(std::unordered_map<std::string, double>& values,
                                           StandardizationMethod standardization);
    static void appendCommonMetadata(CalculationResult& result,
                                     const CommonMetricParams& params,
                                     const CommonRuntimeState& runtime);
    
    // 边界规则处理
    virtual std::unordered_map<std::string, double> applyBoundaryRules(
        const std::unordered_map<std::string, double>& rawValues,
        const CalculationContext& context);
    
    // 异常值处理
    virtual std::unordered_map<std::string, double> handleOutliers(
        const std::unordered_map<std::string, double>& values);
    
    // 加载配置
    virtual void loadConfig(const foundation::json::JsonFacade& config);

    // ── 运行时辅助方法（原 ConfigurableFactorBase）──
    std::vector<std::string> effectiveSymbols(const CalculationContext& context) const;
    std::unordered_map<std::string, double> currentFieldCrossSection(
        const CalculationContext& context, const std::string& field) const;
    std::vector<double> seriesForField(
        const CalculationContext& context, const std::string& symbol,
        const std::string& field, int window) const;
    std::unordered_map<std::string, double> latestFinancialMetric(
        const CalculationContext& context, const std::string& field,
        const std::string& date) const;
    std::unordered_map<std::string, std::vector<double>> latestFinancialSeries(
        const CalculationContext& context, const std::string& field,
        const std::string& date, int limit) const;
    std::unordered_map<std::string, std::string> industryBySymbol(
        const CalculationContext& context) const;

private:
    bool applyCommonNeutralization(const CalculationContext& context,
                                   const CommonMetricParams& params,
                                   const CommonRuntimeState& runtime,
                                   CalculationResult& result,
                                   NeutralizationStatus& neutralizationMode) const;

    // latestFinancialSeries 的记忆化缓存（仅成长因子经此函数取财务序列）。
    // 财务按季度更新、回测每交易日都调，缓存后同一 (field,effectiveDate,limit) 只算一次、跨日复用。
    // key = field + "|" + effectiveDate + "|" + limit；per-instance，回测按日顺序执行、无需加锁。
    mutable std::unordered_map<std::string,
        std::unordered_map<std::string, std::vector<double>>> m_finSeriesCache;
};

// ── 框架公共字段名（中性化、边界规则、元数据）──
namespace field_names {
inline constexpr const char SYMBOL[] = "symbol";
inline constexpr const char TRADE_DATE[] = "trade_date";
inline constexpr const char DATA_SOURCE[] = "data_source";
inline constexpr const char INDUSTRY_CODE[] = "industry_code";
inline constexpr const char MARKET_CAP[] = "market_cap";
inline constexpr const char IS_SUSPENDED[] = "is_suspended";
inline constexpr const char LIMIT_UP[] = "limit_up";
inline constexpr const char LIMIT_DOWN[] = "limit_down";
}  // namespace field_names

} // namespace factor
