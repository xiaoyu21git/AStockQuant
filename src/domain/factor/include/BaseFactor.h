#pragma once

#include <functional>
#include <memory>
#include <QString>
#include <QStringList>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include "foundation/json/json_facade.h"
#include "foundation/Utils/Uuid.h"
#include "DataAvailabilityChecker.h"
#include "HistoricalView.h"
#include "JsonFacadeHelpers.h"

namespace factor {

struct FactorInstanceInfo;

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
        
        return json;
    }
};

// 边界规则
struct BoundaryRules {
    int minDataPoints = 21;
    std::string handleNewStock = "exclude_if_lt_60d";  // exclude_if_lt_60d, include
    std::string handleSuspended = "forward_fill";      // forward_fill, exclude, set_null
    std::string handleDelisted = "keep_until_delist";  // keep_until_delist, exclude
    std::string handleOutliers = "winsorize_3sigma";   // winsorize_3sigma, exclude, keep
    
    foundation::json::JsonFacade toJson() const {
        auto json = foundation::json::JsonFacade::createObject();
        json.set("minDataPoints", json_helper::toJsonValue(minDataPoints));
        json.set("handleNewStock", json_helper::toJsonValue(handleNewStock));
        json.set("handleSuspended", json_helper::toJsonValue(handleSuspended));
        json.set("handleDelisted", json_helper::toJsonValue(handleDelisted));
        json.set("handleOutliers", json_helper::toJsonValue(handleOutliers));
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
};

struct CommonFactorParams {
    int lookbackPeriod = 252;
    bool laggedEnabled = false;
    std::string frequency = "daily";
    std::string standardization = "none";
    bool neutralizationEnabled = false;
};

struct CommonFactorRuntimeState {
    QString frequency{QStringLiteral("daily")};
    QString standardization{QStringLiteral("none")};
    QString effectiveDate;
    QString neutralizationMode{QStringLiteral("disabled")};
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
    
    // 检查数据可用性
    virtual DataStatus checkDataAvailability(const std::string& date) const;
    
    // 获取实例信息
    const std::string& getInstanceId() const { return instanceId_; }
    std::string getName() const { return name_; }
    std::string getDescription() const { return description_; }
    std::string getFactorType() const { return factorType_; }
    
    // 序列化/反序列化
    foundation::json::JsonFacade toJson() const;
    void fromJson(const foundation::json::JsonFacade& json);
    
protected:
    std::string instanceId_;
    std::string name_;
    std::string description_;
    std::string factorType_;
    
    DataRequirements dataRequirements_;
    BoundaryRules boundaryRules_;
    
    std::shared_ptr<DataAvailabilityChecker> dataChecker_;

    bool isHistoricalViewRuntime(const CalculationContext& context) const;
    CalculationResult createHistoricalViewRuntimeError(const CalculationContext& context,
                                                       const std::string& errorMsg) const;

    CalculationResult executeWithCommonParams(
        const CalculationContext& context,
        const CommonFactorParams& params,
        const QStringList& requiredFieldsForDateResolution,
        const std::function<void(const CommonFactorRuntimeState&, CalculationResult&)>& rawCalculator,
        const std::function<void(const CommonFactorRuntimeState&, CalculationResult&)>& preStandardizationProcessor,
        const std::function<void(const CommonFactorRuntimeState&, CalculationResult&)>& metadataAppender) const;

    static QString normalizeCommonFrequency(const std::string& frequency);
    static QString normalizeCommonStandardization(const std::string& standardization);
    static double calculatePercentileValue(std::vector<double> values, double quantile);
    static void applyCommonStandardization(std::unordered_map<std::string, double>& values,
                                           const QString& standardization);
    static void appendCommonMetadata(CalculationResult& result,
                                     const CommonFactorParams& params,
                                     const CommonFactorRuntimeState& runtime);
    
    // 边界规则处理
    virtual std::unordered_map<std::string, double> applyBoundaryRules(
        const std::unordered_map<std::string, double>& rawValues,
        const CalculationContext& context);
    
    // 异常值处理
    virtual std::unordered_map<std::string, double> handleOutliers(
        const std::unordered_map<std::string, double>& values);
    
    // 加载配置
    virtual void loadConfig(const foundation::json::JsonFacade& config);

private:
    QString resolveCommonEffectiveDate(const CalculationContext& context,
                                       const CommonFactorParams& params,
                                       const QStringList& requiredFieldsForDateResolution) const;
    bool applyCommonNeutralization(const CalculationContext& context,
                                   const CommonFactorParams& params,
                                   const CommonFactorRuntimeState& runtime,
                                   CalculationResult& result,
                                   QString& neutralizationMode) const;
};

} // namespace factor