#include "domain/factor/include/BaseFactor.h"

#include "domain/factor/include/FactorNeutralizationUtils.h"

#include <QDate>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace factor {

BaseFactor::BaseFactor() 
    : instanceId_(foundation::utils::Uuid::generate_v4().to_string()) {
}

std::vector<CalculationResult> BaseFactor::calculateBatch(
    const std::vector<CalculationContext>& contexts) {
    
    std::vector<CalculationResult> results;
    results.reserve(contexts.size());
    
    for (const auto& context : contexts) {
        results.push_back(calculate(context));
    }
    
    return results;
}

DataStatus BaseFactor::checkDataAvailability(const std::string& date) const {
    if (!dataChecker_) {
        DataStatus status;
        status.availability = DataAvailability::UNAVAILABLE;
        status.message = "数据检查器未初始化";
        return status;
    }

    return dataChecker_->checkFactorData(instanceId_, date, date);
}

QString BaseFactor::normalizeCommonFrequency(const std::string& frequency)
{
    const QString normalized = QString::fromStdString(frequency).trimmed().toLower();
    if (normalized == QStringLiteral("weekly") || normalized == QString::fromUtf8("周频")) {
        return QStringLiteral("weekly");
    }
    if (normalized == QStringLiteral("monthly") || normalized == QString::fromUtf8("月频")) {
        return QStringLiteral("monthly");
    }
    if (normalized == QStringLiteral("quarterly") || normalized == QString::fromUtf8("季频")) {
        return QStringLiteral("quarterly");
    }
    if (normalized == QStringLiteral("annual") || normalized == QStringLiteral("yearly") || normalized == QString::fromUtf8("年频")) {
        return QStringLiteral("annual");
    }
    return QStringLiteral("daily");
}

QString BaseFactor::normalizeCommonStandardization(const std::string& standardization)
{
    const QString normalized = QString::fromStdString(standardization).trimmed().toLower();
    if (normalized == QStringLiteral("zscore") || normalized == QStringLiteral("z_score")
            || normalized == QStringLiteral("z-score") || normalized == QStringLiteral("z score")) {
        return QStringLiteral("zscore");
    }
    if (normalized == QStringLiteral("minmax") || normalized == QStringLiteral("min_max")
            || normalized == QStringLiteral("min-max") || normalized == QStringLiteral("min max")) {
        return QStringLiteral("minmax");
    }
    if (normalized == QStringLiteral("percentile") || normalized == QStringLiteral("rank")) {
        return QStringLiteral("percentile");
    }
    return QStringLiteral("none");
}

double BaseFactor::calculatePercentileValue(std::vector<double> values, double quantile)
{
    if (values.empty()) {
        return 0.0;
    }
    quantile = (std::max)(0.0, (std::min)(1.0, quantile));
    const double position = quantile * static_cast<double>(values.size() - 1);
    const size_t lower = static_cast<size_t>(std::floor(position));
    const size_t upper = static_cast<size_t>(std::ceil(position));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(lower), values.end());
    const double lowValue = values[lower];
    if (upper == lower) {
        return lowValue;
    }
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(upper), values.end());
    const double highValue = values[upper];
    return lowValue + (highValue - lowValue) * (position - static_cast<double>(lower));
}

void BaseFactor::applyCommonStandardization(std::unordered_map<std::string, double>& values,
                                            const QString& standardization)
{
    if (values.empty() || standardization == QStringLiteral("none")) {
        return;
    }

    if (standardization == QStringLiteral("percentile")) {
        std::vector<std::pair<std::string, double>> ranked(values.begin(), values.end());
        std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
            return left.second < right.second;
        });
        if (ranked.size() == 1) {
            values[ranked.front().first] = 1.0;
            return;
        }
        for (size_t index = 0; index < ranked.size(); ++index) {
            values[ranked[index].first] = static_cast<double>(index) / static_cast<double>(ranked.size() - 1);
        }
        return;
    }

    std::vector<double> finiteValues;
    finiteValues.reserve(values.size());
    for (const auto& entry : values) {
        if (std::isfinite(entry.second)) {
            finiteValues.push_back(entry.second);
        }
    }
    if (finiteValues.empty()) {
        return;
    }

    if (standardization == QStringLiteral("zscore")) {
        const double mean = std::accumulate(finiteValues.begin(), finiteValues.end(), 0.0)
            / static_cast<double>(finiteValues.size());
        double variance = 0.0;
        for (const double value : finiteValues) {
            const double delta = value - mean;
            variance += delta * delta;
        }
        const double stdev = std::sqrt(variance / static_cast<double>(finiteValues.size()));
        if (stdev > 1e-12) {
            for (auto& entry : values) {
                entry.second = (entry.second - mean) / stdev;
            }
        }
        return;
    }

    if (standardization == QStringLiteral("minmax")) {
        const auto [minIt, maxIt] = std::minmax_element(finiteValues.begin(), finiteValues.end());
        const double range = *maxIt - *minIt;
        if (range > 1e-12) {
            for (auto& entry : values) {
                entry.second = (entry.second - *minIt) / range;
            }
        }
    }
}

void BaseFactor::appendCommonMetadata(CalculationResult& result,
                                      const CommonFactorParams& params,
                                      const CommonFactorRuntimeState& runtime)
{
    result.metadata.set("effectiveDate", json_helper::toJsonValue(runtime.effectiveDate.toStdString()));
    result.metadata.set("frequency", json_helper::toJsonValue(runtime.frequency.toStdString()));
    result.metadata.set("laggedEnabled", json_helper::toJsonValue(params.laggedEnabled));
    result.metadata.set("lookbackPeriod", json_helper::toJsonValue(params.lookbackPeriod));
    result.metadata.set("standardization", json_helper::toJsonValue(runtime.standardization.toStdString()));
    result.metadata.set("neutralizationEnabled", json_helper::toJsonValue(params.neutralizationEnabled));
    result.metadata.set("neutralizationMode", json_helper::toJsonValue(runtime.neutralizationMode.toStdString()));
    result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
}

foundation::json::JsonFacade BaseFactor::toJson() const {
    auto json = foundation::json::JsonFacade::createObject();
    
    json.set("instance_id", json_helper::toJsonValue(instanceId_));
    json.set("name", json_helper::toJsonValue(name_));
    json.set("description", json_helper::toJsonValue(description_));
    json.set("factorType", json_helper::toJsonValue(factorType_));
    json.set("dataRequirements", dataRequirements_.toJson());
    json.set("boundaryRules", boundaryRules_.toJson());
    
    return json;
}

void BaseFactor::fromJson(const foundation::json::JsonFacade& json) {
    if (json.has("instance_id")) {
        const auto value = json.get("instance_id");
        if (!value.isString()) {
            throw std::runtime_error("instance_id 不是字符串字段");
        }
        instanceId_ = value.asString();
    }
    
    if (json.has("name")) {
        const auto value = json.get("name");
        if (!value.isString()) {
            throw std::runtime_error("name 不是字符串字段");
        }
        name_ = value.asString();
    }
    
    if (json.has("description")) {
        const auto value = json.get("description");
        if (!value.isString()) {
            throw std::runtime_error("description 不是字符串字段");
        }
        description_ = value.asString();
    }
    
    if (json.has("factorType")) {
        const auto value = json.get("factorType");
        if (!value.isString()) {
            throw std::runtime_error("factorType 不是字符串字段");
        }
        factorType_ = value.asString();
    }
    
    if (json.has("dataRequirements")) {
        auto dataReq = json.get("dataRequirements");
        if (dataReq.has("required")) {
            auto required = dataReq.get("required");
            for (size_t i = 0; i < required.size(); i++) {
                const auto item = required.at(i);
                if (!item.isString()) {
                    throw std::runtime_error("dataRequirements.required 不是字符串字段");
                }
                dataRequirements_.requiredFields.push_back(item.asString());
            }
        }
        
        if (dataReq.has("optional")) {
            auto optional = dataReq.get("optional");
            for (size_t i = 0; i < optional.size(); i++) {
                const auto item = optional.at(i);
                if (!item.isString()) {
                    throw std::runtime_error("dataRequirements.optional 不是字符串字段");
                }
                dataRequirements_.optionalFields.push_back(item.asString());
            }
        }
        
        if (dataReq.has("alternative")) {
            auto alternative = dataReq.get("alternative");
            for (size_t i = 0; i < alternative.size(); i++) {
                const auto item = alternative.at(i);
                if (!item.isString()) {
                    throw std::runtime_error("dataRequirements.alternative 不是字符串字段");
                }
                dataRequirements_.alternativeFields.push_back(item.asString());
            }
        }
    }
    
    if (json.has("boundaryRules")) {
        auto rules = json.get("boundaryRules");
        if (rules.has("minDataPoints")) {
            boundaryRules_.minDataPoints = rules.get("minDataPoints").asInt();
        }
        
        if (rules.has("handleNewStock")) {
            const auto value = rules.get("handleNewStock");
            if (!value.isString()) {
                throw std::runtime_error("boundaryRules.handleNewStock 不是字符串字段");
            }
            boundaryRules_.handleNewStock = value.asString();
        }
        
        if (rules.has("handleSuspended")) {
            const auto value = rules.get("handleSuspended");
            if (!value.isString()) {
                throw std::runtime_error("boundaryRules.handleSuspended 不是字符串字段");
            }
            boundaryRules_.handleSuspended = value.asString();
        }
        
        if (rules.has("handleDelisted")) {
            const auto value = rules.get("handleDelisted");
            if (!value.isString()) {
                throw std::runtime_error("boundaryRules.handleDelisted 不是字符串字段");
            }
            boundaryRules_.handleDelisted = value.asString();
        }
        
        if (rules.has("handleOutliers")) {
            const auto value = rules.get("handleOutliers");
            if (!value.isString()) {
                throw std::runtime_error("boundaryRules.handleOutliers 不是字符串字段");
            }
            boundaryRules_.handleOutliers = value.asString();
        }
    }
}

bool BaseFactor::isHistoricalViewRuntime(const CalculationContext& context) const {
    return static_cast<bool>(context.historicalView);
}

CalculationResult BaseFactor::createHistoricalViewRuntimeError(const CalculationContext& context,
                                                               const std::string& errorMsg) const {
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    result.dataStatus = CalculationResult::createError(errorMsg).dataStatus;
    result.metadata.set("error", json_helper::toJsonValue(errorMsg));
    return result;
}

CalculationResult BaseFactor::executeWithCommonParams(
    const CalculationContext& context,
    const CommonFactorParams& params,
    const QStringList& requiredFieldsForDateResolution,
    const std::function<void(const CommonFactorRuntimeState&, CalculationResult&)>& rawCalculator,
    const std::function<void(const CommonFactorRuntimeState&, CalculationResult&)>& preStandardizationProcessor,
    const std::function<void(const CommonFactorRuntimeState&, CalculationResult&)>& metadataAppender) const {

    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(
            context,
            QStringLiteral("已移除因子运行期数据库取数路径，请由引擎提供 HistoricalView").toStdString());
    }

    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = "使用缓存数据集";

    CommonFactorRuntimeState runtime;
    runtime.frequency = normalizeCommonFrequency(params.frequency);
    runtime.standardization = normalizeCommonStandardization(params.standardization);
    runtime.effectiveDate = resolveCommonEffectiveDate(context, params, requiredFieldsForDateResolution);
    runtime.neutralizationMode = params.neutralizationEnabled
        ? QStringLiteral("requested")
        : QStringLiteral("disabled");

    rawCalculator(runtime, result);

    if (result.dataStatus.isValid() && !result.values.empty()) {
        applyCommonNeutralization(context, params, runtime, result, runtime.neutralizationMode);
    }

    if (result.dataStatus.isValid() && !result.values.empty()) {
        preStandardizationProcessor(runtime, result);
    }

    if (result.dataStatus.isValid() && !result.values.empty()) {
        applyCommonStandardization(result.values, runtime.standardization);
    }

    appendCommonMetadata(result, params, runtime);
    metadataAppender(runtime, result);
    return result;
}

QString BaseFactor::resolveCommonEffectiveDate(const CalculationContext& context,
                                               const CommonFactorParams& params,
                                               const QStringList& requiredFieldsForDateResolution) const {
    QDate anchorDate = QDate::fromString(QString::fromStdString(context.date), Qt::ISODate);
    if (!anchorDate.isValid()) {
        return QString::fromStdString(context.date);
    }

    const QString frequency = normalizeCommonFrequency(params.frequency);
    if (frequency == QStringLiteral("weekly")) {
        const int shiftToPreviousFriday = anchorDate.dayOfWeek() >= 5 ? anchorDate.dayOfWeek() - 5 : anchorDate.dayOfWeek() + 2;
        anchorDate = anchorDate.addDays(-shiftToPreviousFriday);
    } else if (frequency == QStringLiteral("monthly")) {
        anchorDate = QDate(anchorDate.year(), anchorDate.month(), 1).addDays(-1);
    } else if (frequency == QStringLiteral("quarterly")) {
        const int quarter = (anchorDate.month() - 1) / 3;
        const int quarterStartMonth = quarter * 3 + 1;
        anchorDate = QDate(anchorDate.year(), quarterStartMonth, 1).addDays(-1);
    } else if (frequency == QStringLiteral("annual")) {
        anchorDate = QDate(anchorDate.year(), 1, 1).addDays(-1);
    }

    if (requiredFieldsForDateResolution.isEmpty()) {
        return anchorDate.toString(Qt::ISODate);
    }

    const std::vector<std::string> symbols = context.symbols.empty()
        ? context.historicalView->getAvailableSymbols(context.date)
        : context.symbols;
    const int maxOffset = (std::max)(0, params.lookbackPeriod);
    const int startOffset = params.laggedEnabled ? 1 : 0;
    for (int offset = startOffset; offset <= maxOffset; ++offset) {
        const QString candidate = anchorDate.addDays(-offset).toString(Qt::ISODate);
        bool hasAllFields = true;
        for (const QString& field : requiredFieldsForDateResolution) {
            if (field.isEmpty()) {
                hasAllFields = false;
                break;
            }
            if (context.historicalView->getCrossSection(candidate.toStdString(), field.toStdString(), symbols).empty()) {
                hasAllFields = false;
                break;
            }
        }
        if (hasAllFields) {
            return candidate;
        }
    }

    return anchorDate.toString(Qt::ISODate);
}

bool BaseFactor::applyCommonNeutralization(const CalculationContext& context,
                                           const CommonFactorParams& params,
                                           const CommonFactorRuntimeState& runtime,
                                           CalculationResult& result,
                                           QString& neutralizationMode) const {
    if (!params.neutralizationEnabled || result.values.empty()) {
        return true;
    }

    CalculationContext neutralizationContext = context;
    neutralizationContext.date = runtime.effectiveDate.toStdString();

    QString errorMessage;
    if (!factor::neutralization::applyIndustrySizeNeutralization(neutralizationContext, result.values, &errorMessage)) {
        result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
        result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
        neutralizationMode = QStringLiteral("historical_view_neutralization_failed");
        result.values.clear();
        return false;
    }

    neutralizationMode = QStringLiteral("historical_view_cross_section_industry_size");
    return true;
}

std::unordered_map<std::string, double> BaseFactor::applyBoundaryRules(
    const std::unordered_map<std::string, double>& rawValues,
    const CalculationContext& context) {
    
    // 简化实现：直接返回原始值
    // 实际实现需要根据boundaryRules_处理新股、停牌等
    return rawValues;
}

std::unordered_map<std::string, double> BaseFactor::handleOutliers(
    const std::unordered_map<std::string, double>& values) {
    
    if (values.empty() || boundaryRules_.handleOutliers == "keep") {
        return values;
    }
    
    if (boundaryRules_.handleOutliers == "exclude") {
        // 排除异常值：这里简化处理，实际需要计算统计量
        return values;
    }
    
    // winsorize处理
    if (boundaryRules_.handleOutliers == "winsorize_3sigma") {
        // 计算均值和标准差
        std::vector<double> valueList;
        for (const auto& [symbol, value] : values) {
            valueList.push_back(value);
        }
        
        double sum = std::accumulate(valueList.begin(), valueList.end(), 0.0);
        double mean = sum / valueList.size();
        
        double sq_sum = std::inner_product(valueList.begin(), valueList.end(), 
                                          valueList.begin(), 0.0);
        double stdev = std::sqrt(sq_sum / valueList.size() - mean * mean);
        
        double lower = mean - 3 * stdev;
        double upper = mean + 3 * stdev;
        
        std::unordered_map<std::string, double> winsorized;
        for (const auto& [symbol, value] : values) {
            double newValue = value;
            if (value < lower) newValue = lower;
            if (value > upper) newValue = upper;
            winsorized[symbol] = newValue;
        }
        
        return winsorized;
    }
    
    return values;
}

void BaseFactor::loadConfig(const foundation::json::JsonFacade& config) {
    dataRequirements_.requiredFields.clear();
    dataRequirements_.optionalFields.clear();
    dataRequirements_.alternativeFields.clear();

    // 解析配置
    if (config.has("dataRequirements")) {
        auto dataReq = config.get("dataRequirements");
        if (dataReq.has("required")) {
            auto required = dataReq.get("required");
            for (size_t i = 0; i < required.size(); i++) {
                const auto item = required.at(i);
                if (!item.isString()) {
                    throw std::runtime_error("dataRequirements.required 不是字符串字段");
                }
                dataRequirements_.requiredFields.push_back(item.asString());
            }
        }

        if (dataReq.has("optional")) {
            auto optional = dataReq.get("optional");
            for (size_t i = 0; i < optional.size(); i++) {
                const auto item = optional.at(i);
                if (!item.isString()) {
                    throw std::runtime_error("dataRequirements.optional 不是字符串字段");
                }
                dataRequirements_.optionalFields.push_back(item.asString());
            }
        }

        if (dataReq.has("alternative")) {
            auto alternative = dataReq.get("alternative");
            for (size_t i = 0; i < alternative.size(); i++) {
                const auto item = alternative.at(i);
                if (!item.isString()) {
                    throw std::runtime_error("dataRequirements.alternative 不是字符串字段");
                }
                dataRequirements_.alternativeFields.push_back(item.asString());
            }
        }
    }
    
    if (config.has("boundaryRules")) {
        auto rules = config.get("boundaryRules");
        if (rules.has("minDataPoints")) {
            boundaryRules_.minDataPoints = rules.get("minDataPoints").asInt();
        }

        if (rules.has("handleNewStock")) {
            const auto value = rules.get("handleNewStock");
            if (!value.isString()) {
                throw std::runtime_error("boundaryRules.handleNewStock 不是字符串字段");
            }
            boundaryRules_.handleNewStock = value.asString();
        }

        if (rules.has("handleSuspended")) {
            const auto value = rules.get("handleSuspended");
            if (!value.isString()) {
                throw std::runtime_error("boundaryRules.handleSuspended 不是字符串字段");
            }
            boundaryRules_.handleSuspended = value.asString();
        }

        if (rules.has("handleDelisted") || rules.has("handle_delisted")) {
            const auto value = rules.has("handleDelisted") ? rules.get("handleDelisted") : rules.get("handle_delisted");
            if (!value.isString()) {
                throw std::runtime_error("boundaryRules.handleDelisted 不是字符串字段");
            }
            boundaryRules_.handleDelisted = value.asString();
        }

        if (rules.has("handleOutliers") || rules.has("handle_outliers")) {
            const auto value = rules.has("handleOutliers") ? rules.get("handleOutliers") : rules.get("handle_outliers");
            if (!value.isString()) {
                throw std::runtime_error("boundaryRules.handleOutliers 不是字符串字段");
            }
            boundaryRules_.handleOutliers = value.asString();
        }
    }
}

} // namespace factor