#include "domain/factor/include/ConfigurableFactorDetail.h"

#include <algorithm>

namespace factor {

using namespace configurable_factor_detail;

CalculationResult ConfigurableFactorBase::calculateMacro(const CalculationContext& context) const
{
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    const CommonParams& common = commonParams_;
    const MacroParams& macro = macroParams();
    const QString benchmarkSymbol = QString::fromStdString(macro.benchmarkSymbol).trimmed().isEmpty()
        ? QStringLiteral("000300.SH")
        : QString::fromStdString(macro.benchmarkSymbol).trimmed();
    const TechnicalPriceIndicatorSpec priceIndicator = technicalPriceIndicatorSpec(macro.priceType);
    const factor::bridge::FieldKey* priceFieldKey = priceIndicator.common.fieldKey;
    const QString priceField = priceFieldKey ? priceFieldKey->toQString() : QString();
    const DataFrequency frequency = macro.macroFrequency;
    const StandardizationMethod standardization = common.standardization;
    if (!priceIndicator.common.hasResolvedSource() || priceIndicator.common.sourceTable != SourceTable::DAILY_BAR) {
        result.dataStatus = CalculationResult::createError("宏观因子包含不支持的 priceType 配置").dataStatus;
        result.metadata.set("emptyReason", json_helper::toJsonValue("宏观因子包含不支持的 priceType 配置"));
        return result;
    }
    const int baseWindow = macro.macroWindow > 0 ? macro.macroWindow : (common.lookbackWindow > 0 ? common.lookbackWindow : common.window);
    const int resolvedWindow = (std::max)(3, baseWindow) * macroWindowScale(frequency);

    std::vector<MacroDimension> selectedDimensions;
    const ConfiguredMode macroResolvedConfigMode = ConfiguredMode::Configured;
    for (MacroDimension dimension : macro.macroDimensions) {
        if (dimension != MacroDimension::UNKNOWN
                && std::find(selectedDimensions.begin(), selectedDimensions.end(), dimension) == selectedDimensions.end()) {
            selectedDimensions.push_back(dimension);
        }
    }
    std::vector<MacroIndicator> selectedIndicators;
    for (MacroIndicator indicator : macro.macroIndicators) {
        if (indicator != MacroIndicator::UNKNOWN
                && std::find(selectedIndicators.begin(), selectedIndicators.end(), indicator) == selectedIndicators.end()) {
            selectedIndicators.push_back(indicator);
        }
    }
    std::vector<MacroIndicator> dimensionScopedIndicators;
    for (MacroIndicator indicator : selectedIndicators) {
        const MacroIndicatorSpec spec = macroIndicatorSpec(indicator);
        if (std::find(selectedDimensions.begin(), selectedDimensions.end(), spec.dimension) != selectedDimensions.end()
                && std::find(dimensionScopedIndicators.begin(), dimensionScopedIndicators.end(), indicator) == dimensionScopedIndicators.end()) {
            dimensionScopedIndicators.push_back(indicator);
        }
    }
    selectedIndicators = dimensionScopedIndicators;
    if (selectedDimensions.empty() || selectedIndicators.empty()) {
        result.dataStatus = CalculationResult::createError("宏观因子必须显式提供 macroDimensions 和 macroIndicators").dataStatus;
        result.metadata.set("error", json_helper::toJsonValue("宏观因子必须显式提供 macroDimensions 和 macroIndicators"));
        return result;
    }

    std::vector<std::string> benchmarkFields;
    std::unordered_set<std::string> seenBenchmarkFields;
    benchmarkFields.reserve(static_cast<size_t>(selectedIndicators.size()));
    for (MacroIndicator indicator : selectedIndicators) {
        const MacroIndicatorSpec spec = macroIndicatorSpec(indicator);
        if (!spec.common.hasField()) {
            continue;
        }
        const std::string fieldName = spec.common.fieldKey->c_str();
        if (!fieldName.empty() && seenBenchmarkFields.insert(fieldName).second) {
            benchmarkFields.push_back(fieldName);
        }
    }

    const auto symbols = effectiveSymbols(context);
    if (symbols.empty() || selectedIndicators.empty()) {
        result.dataStatus = CalculationResult::createError("宏观因子缺少可用标的或驱动指标").dataStatus;
        result.metadata.set("error", json_helper::toJsonValue("宏观因子缺少可用标的或驱动指标"));
        return result;
    }

    CalculationContext effectiveContext = context;
    effectiveContext.symbols = symbols;

    const bool useLocalBatchCache = context.historicalView
        && (!activeBatchComputationCache || activeBatchComputationCache->historicalView != context.historicalView);

    auto calculateMacroBody = [&]() -> CalculationResult {
        auto appendCommonMetadata = [&](const QString& effectiveDate, CommonNeutralizationMode neutralizationMode) {
            result.metadata.set("macroConfigMode", json_helper::toJsonValue(static_cast<int>(macroResolvedConfigMode)));
            result.metadata.set("macroDimensions", macroDimensionArrayJson(selectedDimensions));
            result.metadata.set("macroIndicators", macroIndicatorArrayJson(selectedIndicators));
            result.metadata.set("macroFrequency", json_helper::toJsonValue(static_cast<int>(frequency)));
            result.metadata.set("macroWindow", json_helper::toJsonValue(baseWindow));
            result.metadata.set("macroMode", json_helper::toJsonValue(static_cast<int>(MacroComputationMode::ProxySensitivity)));
            result.metadata.set("benchmarkSymbol", json_helper::toJsonValue(benchmarkSymbol.toStdString()));
            result.metadata.set("effectiveDate", json_helper::toJsonValue(effectiveDate.toStdString()));
            result.metadata.set("frequency", json_helper::toJsonValue(static_cast<int>(frequency)));
            result.metadata.set("lookbackPeriod", json_helper::toJsonValue(common.lookbackWindow));
            result.metadata.set("laggedEnabled", json_helper::toJsonValue(common.lagEnabled));
            result.metadata.set("standardization", json_helper::toJsonValue(static_cast<int>(standardization)));
            result.metadata.set("neutralizationEnabled", json_helper::toJsonValue(common.neutralizationEnabled));
            result.metadata.set("neutralizationMode", json_helper::toJsonValue(static_cast<int>(neutralizationMode)));
            result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));
        };

        auto resolveMacroEffectiveDate = [&]() {
            QString effectiveDate = QString::fromStdString(context.date);
            QDate anchorDate = QDate::fromString(effectiveDate, Qt::ISODate);
            if (anchorDate.isValid()) {
                if (frequency == DataFrequency::Weekly) {
                    const int shiftToPreviousFriday = anchorDate.dayOfWeek() >= 5 ? anchorDate.dayOfWeek() - 5 : anchorDate.dayOfWeek() + 2;
                    anchorDate = anchorDate.addDays(-shiftToPreviousFriday);
                } else if (frequency == DataFrequency::Monthly) {
                    anchorDate = QDate(anchorDate.year(), anchorDate.month(), 1).addDays(-1);
                } else if (frequency == DataFrequency::Quarterly) {
                    const int quarterStartMonth = ((anchorDate.month() - 1) / 3) * 3 + 1;
                    anchorDate = QDate(anchorDate.year(), quarterStartMonth, 1).addDays(-1);
                }
                effectiveDate = anchorDate.toString(Qt::ISODate);
            }

            const int maxOffset = (std::max)(0, static_cast<int>(common.lookbackWindow));
            const int startOffset = common.lagEnabled ? (std::max)(1, static_cast<int>(common.lagPeriods)) : 0;
            const std::vector<std::string> benchmarkSymbols{benchmarkSymbol.toStdString()};
            for (int offset = startOffset; offset <= maxOffset; ++offset) {
                const QString candidate = anchorDate.isValid()
                    ? anchorDate.addDays(-offset).toString(Qt::ISODate)
                    : effectiveDate;
                if (context.historicalView->getCrossSection(candidate.toStdString(), priceField.toStdString(), symbols).empty()) {
                    continue;
                }

                bool hasBenchmarkSeries = false;
                for (const auto& fieldName : benchmarkFields) {
                    if (!context.historicalView->getCrossSection(candidate.toStdString(), fieldName, benchmarkSymbols).empty()) {
                        hasBenchmarkSeries = true;
                        break;
                    }
                }
                if (hasBenchmarkSeries) {
                    return candidate;
                }
            }

            return effectiveDate;
        };

        const QString effectiveDate = resolveMacroEffectiveDate();
        effectiveContext.date = effectiveDate.toStdString();
        CommonNeutralizationMode macroNeutralizationMode = common.neutralizationEnabled
            ? CommonNeutralizationMode::REQUESTED
            : CommonNeutralizationMode::DISABLED;

        std::unordered_map<std::string, double> weightedScores;
        std::unordered_map<std::string, int> scoreCounts;
        const auto priceSeriesBySymbol = fetchBatchSeriesMap(effectiveContext, priceField, resolvedWindow + 1);
        const SeriesMatrixBatch priceSeriesBatch = collectSeriesMatrix(priceSeriesBySymbol, 2);
        const Eigen::MatrixXd allSymbolReturns = buildReturnMatrix(priceSeriesBatch.values);
        std::unordered_map<std::string, std::vector<double>> benchmarkSeriesByField;
        if (context.historicalView && !benchmarkFields.empty()) {
            const auto benchmarkBatchValues = context.historicalView->getBatchTimeSeries(
                {benchmarkSymbol.toStdString()},
                effectiveDate.toStdString(),
                resolvedWindow + 1,
                benchmarkFields);

            for (const std::string& fieldName : benchmarkFields) {
                const auto fieldIt = benchmarkBatchValues.find(fieldName);
                if (fieldIt == benchmarkBatchValues.end()) {
                    continue;
                }

                const auto symbolIt = fieldIt->second.find(benchmarkSymbol.toStdString());
                if (symbolIt != fieldIt->second.end()) {
                    benchmarkSeriesByField.emplace(fieldName, symbolIt->second);
                }
            }
        }

        for (MacroIndicator indicator : selectedIndicators) {
            const MacroIndicatorSpec spec = macroIndicatorSpec(indicator);
            const double dimensionWeight = indicatorWeightForDimension(spec.dimension);

            std::vector<double> benchmarkSeries;
            if (spec.common.hasField()) {
                const auto benchmarkIt = benchmarkSeriesByField.find(spec.common.fieldKey->c_str());
                if (benchmarkIt != benchmarkSeriesByField.end() && benchmarkIt->second.size() >= 2) {
                    benchmarkSeries = benchmarkIt->second;
                }
            }

            if (benchmarkSeries.size() < 2) {
                continue;
            }

            const Eigen::VectorXd benchmarkReturns = buildReturnVector(benchmarkSeries);
            if (benchmarkReturns.size() < 2 || allSymbolReturns.cols() < 2 || priceSeriesBatch.symbols.empty()) {
                continue;
            }

            const Eigen::VectorXd correlations = batchCorrelate(allSymbolReturns, benchmarkReturns);
            for (int row = 0; row < correlations.size(); ++row) {
                const double correlation = correlations(row);
                if (!std::isfinite(correlation)) {
                    continue;
                }

                const std::string& symbol = priceSeriesBatch.symbols[static_cast<size_t>(row)];
                weightedScores[symbol] += correlation * spec.direction * dimensionWeight;
                scoreCounts[symbol] += 1;
            }
        }

        if (weightedScores.empty()) {
            result.dataStatus = CalculationResult::createError("宏观因子缺少可用代理数据").dataStatus;
            result.metadata.set("error", json_helper::toJsonValue("宏观因子缺少可用代理数据"));
            appendCommonMetadata(effectiveDate, macroNeutralizationMode);
            return result;
        }

        for (const auto& [symbol, weightedScore] : weightedScores) {
            const int count = scoreCounts[symbol];
            if (count <= 0) {
                continue;
            }
            result.values[symbol] = std::clamp(std::tanh(weightedScore / static_cast<double>(count)), -1.0, 1.0);
        }

        if (common.neutralizationEnabled) {
            QString errorMessage;
            if (!applyHistoricalViewIndustrySizeNeutralization(effectiveContext, result.values, &errorMessage)) {
                result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                result.values.clear();
                macroNeutralizationMode = CommonNeutralizationMode::HISTORICAL_VIEW_NEUTRALIZATION_FAILED;
                appendCommonMetadata(effectiveDate, macroNeutralizationMode);
                return result;
            }
            macroNeutralizationMode = CommonNeutralizationMode::HISTORICAL_VIEW_CROSS_SECTION_INDUSTRY_SIZE;
        }

        applyConfigurableStandardization(standardization, result.values);

        const double coverage = static_cast<double>(result.values.size()) / static_cast<double>((std::max)(size_t(1), symbols.size()));
        result.dataStatus.availability = result.values.size() == symbols.size() ? DataAvailability::AVAILABLE : DataAvailability::PARTIAL;
        result.dataStatus.coverage = coverage;
        result.dataStatus.message = "使用宏观代理敏感度运行时";
        appendCommonMetadata(effectiveDate, macroNeutralizationMode);
        return result;
    };

    if (useLocalBatchCache) {
        BatchComputationCache cache;
        cache.historicalView = context.historicalView;
        BatchComputationCacheScope scope(cache);
        return calculateMacroBody();
    }

    return calculateMacroBody();
}

} // namespace factor