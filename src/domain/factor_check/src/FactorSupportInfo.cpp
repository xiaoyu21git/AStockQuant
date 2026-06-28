#include "factor_check/FactorSupportInfo.h"

#include <sstream>

namespace factor::check {

namespace {

std::string joinFields(const std::vector<FieldKey>& fields)
{
    std::ostringstream oss;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) {
            oss << "、";
        }
        oss << fields[i].value;
    }
    return oss.str();
}

} // namespace

SupportInfo FactorSupportInfoBuilder::makeRuntimeInitFailure(const FactorId& factorId,
                                                              const std::string& runtimeErrorDetail)
{
    SupportInfo info;
    info.factorId = factorId;
    info.category = SupportCategory::RuntimeInitFailed;
    info.reason = SupportReason::RuntimeInitFailure;
    info.runFailureCode = RunFailureCode::MissingExecutionModule;
    info.runtimeErrorDetail = runtimeErrorDetail;
    info.supported = false;
    return info;
}

SupportInfo FactorSupportInfoBuilder::makeInstanceMissing(const FactorId& factorId)
{
    SupportInfo info;
    info.factorId = factorId;
    info.category = SupportCategory::InstanceMissing;
    info.reason = SupportReason::MissingInstanceId;
    info.supported = false;
    return info;
}

SupportInfo FactorSupportInfoBuilder::makeInstanceCreateFailed(const FactorId& factorId,
                                                                const InstanceId& instanceId,
                                                                factor::FactorType runtimeType)
{
    SupportInfo info;
    info.factorId = factorId;
    info.instanceId = instanceId;
    info.runtimeType = runtimeType;
    info.category = SupportCategory::InstanceCreateFailed;
    info.reason = SupportReason::InstanceCreationFailure;
    info.supported = false;
    return info;
}

SupportInfo FactorSupportInfoBuilder::makeInvalidBacktestWindow(const FactorId& factorId,
                                                                 const InstanceId& instanceId,
                                                                 factor::FactorType runtimeType)
{
    SupportInfo info;
    info.factorId = factorId;
    info.instanceId = instanceId;
    info.runtimeType = runtimeType;
    info.category = SupportCategory::InvalidBacktestWindow;
    info.reason = SupportReason::InvalidWindow;
    info.runFailureCode = RunFailureCode::InvalidRunSpec;
    info.supported = false;
    return info;
}

SupportInfo FactorSupportInfoBuilder::makeRuntimeNotReady(const FactorId& factorId,
                                                          const std::string& detail)
{
    SupportInfo info;
    info.factorId = factorId;
    info.category = SupportCategory::RuntimeNotReady;
    info.reason = SupportReason::RuntimeNotReady;
    info.runFailureCode = RunFailureCode::MissingExecutionModule;
    info.runtimeErrorDetail = detail;
    info.supported = false;
    return info;
}

SupportInfo FactorSupportInfoBuilder::makeOutcomeBased(const OutcomeSupportRequest& request)
{
    SupportInfo info;
    info.factorId = request.factorId;
    info.instanceId = request.instanceId;
    info.runtimeType = request.runtimeType;
    info.sourceTable = request.sourceTable;
    info.useCacheMode = request.useCacheMode;
    info.availableTradeDateCount = request.availableTradeDateCount;
    info.requiredWarmupTradingDays = request.requiredWarmupTradingDays;
    info.requiredFields = request.requiredFields;
    info.missingFields = request.missingFields;
    info.emptyValueFields = request.emptyValueFields;
    info.supported = request.supported;

    switch (request.code) {
    case OutcomeCode::InvalidBacktestWindow:
        info.category = SupportCategory::InvalidBacktestWindow;
        info.reason = SupportReason::InvalidWindow;
        info.runFailureCode = RunFailureCode::InvalidRunSpec;
        break;
    case OutcomeCode::MissingCustomExpression:
        info.category = SupportCategory::MissingField;
        info.reason = SupportReason::MissingCustomExpression;
        break;
    case OutcomeCode::MissingRequiredFields:
        info.category = SupportCategory::MissingField;
        info.reason = SupportReason::MissingDeclaredFields;
        break;
    case OutcomeCode::DatasetMissing:
        info.category = SupportCategory::DatasetMissing;
        info.reason = SupportReason::MissingDataset;
        break;
    case OutcomeCode::DatasetInvalid:
        info.category = SupportCategory::DatasetInvalid;
        info.reason = SupportReason::InvalidDataset;
        break;
    case OutcomeCode::DatasetEmpty:
        info.category = SupportCategory::DatasetEmpty;
        info.reason = SupportReason::EmptyDataset;
        break;
    case OutcomeCode::MissingOrEmptyFields:
        info.category = SupportCategory::MissingField;
        info.reason = SupportReason::MissingOrEmptyFields;
        break;
    case OutcomeCode::InsufficientHistory:
        info.category = SupportCategory::InsufficientHistory;
        info.reason = SupportReason::InsufficientHistory;
        break;
    case OutcomeCode::RuntimeNotReady:
        info.category = SupportCategory::RuntimeNotReady;
        info.reason = SupportReason::RuntimeNotReady;
        info.runFailureCode = RunFailureCode::MissingExecutionModule;
        break;
    case OutcomeCode::Supported:
        info.category = SupportCategory::Supported;
        info.reason = SupportReason::FieldCheckPassed;
        break;
    }

    return info;
}

std::string FactorSupportInfoBuilder::categoryToken(SupportCategory category)
{
    switch (category) {
    case SupportCategory::Supported:
        return "supported";
    case SupportCategory::RuntimeInitFailed:
        return "runtime-init-failed";
    case SupportCategory::InstanceMissing:
        return "instance-missing";
    case SupportCategory::InstanceCreateFailed:
        return "instance-create-failed";
    case SupportCategory::InvalidBacktestWindow:
        return "invalid-backtest-window";
    case SupportCategory::MissingField:
        return "missing-field";
    case SupportCategory::DatasetMissing:
        return "dataset-missing";
    case SupportCategory::DatasetInvalid:
        return "dataset-invalid";
    case SupportCategory::DatasetEmpty:
        return "dataset-empty";
    case SupportCategory::InsufficientHistory:
        return "insufficient-history";
    case SupportCategory::RuntimeNotReady:
        return "runtime-not-ready";
    }
    return "missing-field";
}

std::string FactorSupportInfoBuilder::reasonMessage(const SupportInfo& info)
{
    switch (info.reason) {
    case SupportReason::FieldCheckPassed:
        return info.useCacheMode ? "字段与历史窗口检查通过" : "因子字段检查通过";
    case SupportReason::RuntimeInitFailure:
        return info.runtimeErrorDetail;
    case SupportReason::MissingInstanceId:
        return "未找到对应的因子实例 ID";
    case SupportReason::InstanceCreationFailure:
        return "因子实例创建失败，无法执行因子检查";
    case SupportReason::InvalidWindow:
        return "回测开始/结束日期必须同时提供，禁止使用默认兜底日期";
    case SupportReason::MissingCustomExpression:
        return "自定义因子必须显式提供 expression";
    case SupportReason::MissingDeclaredFields:
        return "因子未显式声明可用于回测检查的字段需求";
    case SupportReason::MissingDataset:
        return "未选择可用于因子回测检查的缓存集";
    case SupportReason::InvalidDataset:
        return "所选缓存集不存在或元数据无效";
    case SupportReason::EmptyDataset:
        return "所选缓存集没有可用于因子检查的数据";
    case SupportReason::MissingOrEmptyFields:
        if (info.missingFields.empty()) {
            return "缓存集字段存在但无有效非空值: " + joinFields(info.emptyValueFields);
        }
        if (info.emptyValueFields.empty()) {
            return "缓存集缺少因子检查所需字段: " + joinFields(info.missingFields);
        }
        return "缓存集缺少因子检查所需字段: " + joinFields(info.missingFields)
            + "；以下字段存在但无有效非空值: " + joinFields(info.emptyValueFields);
    case SupportReason::RuntimeNotReady:
        return info.runtimeErrorDetail;
    case SupportReason::InsufficientHistory: {
        std::ostringstream oss;
        oss << "缓存集仅覆盖 " << info.availableTradeDateCount << " 个交易日，低于该因子所需的 "
            << info.requiredWarmupTradingDays << " 个交易日";
        return oss.str();
    }
    }

    return {};
}

} // namespace factor::check
