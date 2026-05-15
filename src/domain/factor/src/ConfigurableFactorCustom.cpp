#include "domain/factor/include/ConfigurableFactorDetail.h"
#include "domain/factor/include/CustomExpressionUtils.h"

#include <algorithm>
#include <cmath>

namespace factor {

using namespace configurable_factor_detail;

std::unordered_map<std::string, double> ConfigurableFactorBase::evaluateCustomExpression(
    const CalculationContext& context,
    const QString& expression,
    const std::vector<std::string>& symbols,
    QString* errorMessage) const
{
    std::unordered_map<std::string, double> results;
    const QString resolvedExpression = expression.trimmed();
    if (resolvedExpression.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("自定义因子必须显式提供 expression");
        }
        return results;
    }
    QString parseError;
    const QStringList rpn = factor::custom_expression::toRpn(resolvedExpression.toLower(), &parseError);
    if (rpn.isEmpty()) {
        if (errorMessage) {
            *errorMessage = parseError;
        }
        return results;
    }

    const QStringList variables = factor::custom_expression::extractVariables(resolvedExpression.toLower());
    std::unordered_map<std::string, QString> sourceFieldByVariable;
    std::vector<std::string> batchFields;
    std::unordered_set<std::string> seenBatchFields;
    for (const QString& variable : variables) {
        const auto* binding = findCustomVariableBinding(variable);
        QString sourceField = variable;
        if (binding) {
            sourceField = QString::fromStdString(binding->field).trimmed();
            if (sourceField.isEmpty()) {
                if (errorMessage && errorMessage->isEmpty()) {
                    *errorMessage = QStringLiteral("自定义表达式变量 %1 缺少 sourceField 配置").arg(variable);
                }
                return results;
            }
        }
        const std::string variableKey = variable.toStdString();
        sourceFieldByVariable[variableKey] = sourceField;
        const std::string fieldKey = sourceField.toStdString();
        if (!fieldKey.empty() && seenBatchFields.insert(fieldKey).second) {
            batchFields.push_back(fieldKey);
        }
    }

    std::unordered_map<std::string, std::unordered_map<std::string, double>> batchCrossSections;
    if (context.historicalView && !batchFields.empty()) {
        batchCrossSections = context.historicalView->getBatchCrossSections(context.date, symbols, batchFields);
        if (activeBatchComputationCache && activeBatchComputationCache->historicalView == context.historicalView) {
            for (const auto& [fieldName, symbolValues] : batchCrossSections) {
                std::string batchKey;
                buildBatchCrossSectionKey(batchKey, context.date, QString::fromStdString(fieldName));
                activeBatchComputationCache->crossSectionsByKey[batchKey] = symbolValues;
            }
        }
    }

    for (const auto& symbol : symbols) {
        std::unordered_map<std::string, double> variableMap;
        bool missingVariable = false;
        for (const QString& variable : variables) {
            const auto* binding = findCustomVariableBinding(variable);
            const std::string variableKey = variable.toStdString();
            const auto sourceFieldIt = sourceFieldByVariable.find(variableKey);
            if (sourceFieldIt == sourceFieldByVariable.end()) {
                Q_UNUSED(binding);
                if (errorMessage && errorMessage->isEmpty()) {
                    *errorMessage = QStringLiteral("自定义表达式变量 %1 缺少字段映射").arg(variable);
                }
                missingVariable = true;
                break;
            }

            const auto fieldIt = batchCrossSections.find(sourceFieldIt->second.toStdString());
            if (fieldIt == batchCrossSections.end()) {
                if (errorMessage && errorMessage->isEmpty()) {
                    *errorMessage = QStringLiteral("自定义表达式变量 %1 缺少可用字段数据").arg(variable);
                }
                missingVariable = true;
                break;
            }
            const auto valueIt = fieldIt->second.find(symbol);
            if (valueIt == fieldIt->second.end()) {
                if (errorMessage && errorMessage->isEmpty()) {
                    *errorMessage = QStringLiteral("自定义表达式变量 %1 缺少符号数据").arg(variable);
                }
                missingVariable = true;
                break;
            }
            variableMap[variableKey] = valueIt->second;
        }
        if (missingVariable) {
            continue;
        }

        QString evalError;
        const auto evaluated = factor::custom_expression::evaluateRpn(rpn, variableMap, &evalError);
        if (!evaluated.has_value() || !std::isfinite(*evaluated)) {
            if (errorMessage && errorMessage->isEmpty()) {
                *errorMessage = evalError;
            }
            continue;
        }
        results[symbol] = *evaluated;
    }
    if (results.empty() && errorMessage && errorMessage->isEmpty()) {
        *errorMessage = QStringLiteral("自定义表达式没有产生有效结果");
    }
    return results;
}

CalculationResult ConfigurableFactorBase::calculateCustom(const CalculationContext& context) const
{
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    const CommonParams& common = commonParams_;
    const CustomParams& custom = customParams();
    const DataFrequency frequency = common.frequency;
    const StandardizationMethod standardization = common.standardization;
    const auto symbols = effectiveSymbols(context);
    const ConfiguredMode customResolvedExpressionMode = ConfiguredMode::Configured;
    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = "使用自定义表达式";

    const bool useLocalBatchCache = context.historicalView
        && (!activeBatchComputationCache || activeBatchComputationCache->historicalView != context.historicalView);

    auto calculateCustomBody = [&]() -> CalculationResult {
        auto resolveCustomEffectiveDate = [&]() {
            QString effectiveDate = QString::fromStdString(context.date);
            QDate anchorDate = QDate::fromString(effectiveDate, Qt::ISODate);
            if (anchorDate.isValid()) {
                if (frequency == DataFrequency::Weekly) {
                    const int shiftToPreviousFriday = anchorDate.dayOfWeek() >= 5 ? anchorDate.dayOfWeek() - 5 : anchorDate.dayOfWeek() + 2;
                    anchorDate = anchorDate.addDays(-shiftToPreviousFriday);
                } else if (frequency == DataFrequency::Monthly) {
                    anchorDate = QDate(anchorDate.year(), anchorDate.month(), 1).addDays(-1);
                }
                effectiveDate = anchorDate.toString(Qt::ISODate);
            }

            const int maxOffset = (std::max)(0, static_cast<int>(common.lookbackWindow));
            const int startOffset = common.lagEnabled ? (std::max)(1, static_cast<int>(common.lagPeriods)) : 0;
            for (int offset = startOffset; offset <= maxOffset; ++offset) {
                const QString candidate = anchorDate.isValid()
                    ? anchorDate.addDays(-offset).toString(Qt::ISODate)
                    : effectiveDate;
                CalculationContext candidateContext = context;
                candidateContext.date = candidate.toStdString();
                candidateContext.symbols = symbols;
                QString candidateError;
                const auto candidateValues = evaluateCustomExpression(candidateContext,
                                                                     QString::fromStdString(custom.expression),
                                                                     symbols,
                                                                     &candidateError);
                if (!candidateValues.empty()) {
                    return candidate;
                }
            }

            return effectiveDate;
        };

        const QString effectiveDate = resolveCustomEffectiveDate();
        CalculationContext effectiveContext = context;
        effectiveContext.date = effectiveDate.toStdString();
        effectiveContext.symbols = symbols;
        CommonNeutralizationMode neutralizationMode = common.neutralizationEnabled
            ? CommonNeutralizationMode::REQUESTED
            : CommonNeutralizationMode::DISABLED;

        const auto appendCommonMetadata = [&](CommonNeutralizationMode resolvedNeutralizationMode) {
            result.metadata.set("effectiveDate", json_helper::toJsonValue(effectiveDate.toStdString()));
            result.metadata.set("frequency", json_helper::toJsonValue(static_cast<int>(frequency)));
            result.metadata.set("lookbackPeriod", json_helper::toJsonValue(common.lookbackWindow));
            result.metadata.set("laggedEnabled", json_helper::toJsonValue(common.lagEnabled));
            result.metadata.set("standardization", json_helper::toJsonValue(static_cast<int>(standardization)));
            result.metadata.set("neutralizationEnabled", json_helper::toJsonValue(common.neutralizationEnabled));
            result.metadata.set("neutralizationMode", json_helper::toJsonValue(static_cast<int>(resolvedNeutralizationMode)));
        };

        QString errorMessage;
        result.values = evaluateCustomExpression(effectiveContext,
                                                 QString::fromStdString(custom.expression),
                                                 symbols,
                                                 &errorMessage);

        if (result.values.empty()) {
            result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
            result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
            appendCommonMetadata(neutralizationMode);
        } else if (!errorMessage.isEmpty()) {
            result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
            result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
            appendCommonMetadata(neutralizationMode);
        } else {
            if (common.neutralizationEnabled) {
                QString neutralizationError;
                if (!applyHistoricalViewIndustrySizeNeutralization(effectiveContext, result.values, &neutralizationError)) {
                    result.dataStatus = CalculationResult::createError(neutralizationError.toStdString()).dataStatus;
                    result.metadata.set("error", json_helper::toJsonValue(neutralizationError.toStdString()));
                    result.values.clear();
                    neutralizationMode = CommonNeutralizationMode::HISTORICAL_VIEW_NEUTRALIZATION_FAILED;
                    appendCommonMetadata(neutralizationMode);
                    result.metadata.set("expression", json_helper::toJsonValue(custom.expression));
                    result.metadata.set("customExpressionMode", json_helper::toJsonValue(static_cast<int>(customResolvedExpressionMode)));
                    result.metadata.set("variableCount", json_helper::toJsonValue(static_cast<int>(custom.variables.size())));
                    result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
                    return result;
                }
                neutralizationMode = CommonNeutralizationMode::HISTORICAL_VIEW_CROSS_SECTION_INDUSTRY_SIZE;
            }

            applyConfigurableStandardization(standardization, result.values);
            const double coverage = static_cast<double>(result.values.size()) / static_cast<double>((std::max)(size_t(1), symbols.size()));
            result.dataStatus.availability = result.values.size() == symbols.size() ? DataAvailability::AVAILABLE : DataAvailability::PARTIAL;
            result.dataStatus.coverage = coverage;
            appendCommonMetadata(neutralizationMode);
        }
        result.metadata.set("expression", json_helper::toJsonValue(custom.expression));
        result.metadata.set("customExpressionMode", json_helper::toJsonValue(static_cast<int>(customResolvedExpressionMode)));
        result.metadata.set("variableCount", json_helper::toJsonValue(static_cast<int>(custom.variables.size())));
        result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
        return result;
    };

    if (useLocalBatchCache) {
        BatchComputationCache cache;
        cache.historicalView = context.historicalView;
        BatchComputationCacheScope scope(cache);
        return calculateCustomBody();
    }

    return calculateCustomBody();
}

} // namespace factor