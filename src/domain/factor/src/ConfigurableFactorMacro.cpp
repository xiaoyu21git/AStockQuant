#include "domain/factor/include/ConfigurableFactorDetail.h"

#include <algorithm>

namespace factor {

using namespace configurable_factor_detail;

CalculationResult ConfigurableFactorBase::calculateMacro(const CalculationContext& context) const
{
    const CommonParams& common = commonParams_;
    const MacroParams& macro = macroParams();
    const QString benchmarkSymbol = QString::fromStdString(macro.benchmarkSymbol).trimmed().isEmpty()
        ? QStringLiteral("000300.SH")
        : QString::fromStdString(macro.benchmarkSymbol).trimmed();
    const TechnicalPriceIndicatorSpec priceIndicator = technicalPriceIndicatorSpec(macro.priceType);
    const factor::bridge::FieldKey* priceFieldKey = priceIndicator.common.fieldKey;
    const QString priceField = priceFieldKey ? priceFieldKey->toQString() : QString();
    const DataFrequency frequency = macro.macroFrequency;
    if (!priceIndicator.common.hasResolvedSource() || priceIndicator.common.sourceTable != SourceTable::DAILY_BAR) {
        return createHistoricalViewRuntimeError(context, "宏观因子包含不支持的 priceType 配置");
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
        return createHistoricalViewRuntimeError(context, "宏观因子必须显式提供 macroDimensions 和 macroIndicators");
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
        return createHistoricalViewRuntimeError(context, "宏观因子缺少可用标的或驱动指标");
    }

    CalculationContext effectiveContext = context;
    effectiveContext.symbols = symbols;

    const bool useLocalBatchCache = context.historicalView
        && (!activeBatchComputationCache || activeBatchComputationCache->historicalView != context.historicalView);

    CommonMetricParams macroRuntimeParams = common;
    macroRuntimeParams.frequency = frequency;

    auto calculateMacroBody = [&]() -> CalculationResult {
        return executeWithCommonParams(
            context,
            macroRuntimeParams,
            [&]() {
                QString resolvedDate = QString::fromStdString(context.date);
                QDate anchorDate = QDate::fromString(resolvedDate, Qt::ISODate);
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
                    resolvedDate = anchorDate.toString(Qt::ISODate);
                }

                const int maxOffset = (std::max)(0, static_cast<int>(common.lookbackWindow));
                const int startOffset = common.lagEnabled ? (std::max)(1, static_cast<int>(common.lagPeriods)) : 0;
                const std::vector<std::string> benchmarkSymbols{benchmarkSymbol.toStdString()};
                for (int offset = startOffset; offset <= maxOffset; ++offset) {
                    const QString candidate = anchorDate.isValid()
                        ? anchorDate.addDays(-offset).toString(Qt::ISODate)
                        : resolvedDate;
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

                return resolvedDate;
            },
            [this, &context, &effectiveContext, &priceField, resolvedWindow, &benchmarkFields, &benchmarkSymbol, &selectedIndicators](const CommonRuntimeState& runtime, CalculationResult& result) {
                effectiveContext.date = runtime.effectiveDate.toStdString();

                std::unordered_map<std::string, double> weightedScores;
                std::unordered_map<std::string, int> scoreCounts;
                const auto priceSeriesBySymbol = fetchBatchSeriesMap(effectiveContext, priceField, resolvedWindow + 1);
                const SeriesMatrixBatch priceSeriesBatch = collectSeriesMatrix(priceSeriesBySymbol, 2);
                const Eigen::MatrixXd allSymbolReturns = buildReturnMatrix(priceSeriesBatch.values);
                std::unordered_map<std::string, std::vector<double>> benchmarkSeriesByField;
                if (context.historicalView && !benchmarkFields.empty()) {
                    const auto benchmarkBatchValues = context.historicalView->getBatchTimeSeries(
                        {benchmarkSymbol.toStdString()},
                        runtime.effectiveDate.toStdString(),
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
                    return;
                }

                for (const auto& [symbol, weightedScore] : weightedScores) {
                    const int count = scoreCounts[symbol];
                    if (count <= 0) {
                        continue;
                    }
                    result.values[symbol] = std::clamp(std::tanh(weightedScore / static_cast<double>(count)), -1.0, 1.0);
                }
            },
            [](const CommonRuntimeState&, CalculationResult&) {},
            [&](const CommonRuntimeState&, CalculationResult& result) {
                const double coverage = static_cast<double>(result.values.size()) / static_cast<double>((std::max)(size_t(1), symbols.size()));
                result.dataStatus.availability = result.values.size() == symbols.size() ? DataAvailability::AVAILABLE : DataAvailability::PARTIAL;
                result.dataStatus.coverage = coverage;
                result.dataStatus.message = "使用宏观代理敏感度运行时";
                result.metadata.set("macroConfigMode", json_helper::toJsonValue(static_cast<int>(macroResolvedConfigMode)));
                result.metadata.set("macroDimensions", macroDimensionArrayJson(selectedDimensions));
                result.metadata.set("macroIndicators", macroIndicatorArrayJson(selectedIndicators));
                result.metadata.set("macroFrequency", json_helper::toJsonValue(static_cast<int>(frequency)));
                result.metadata.set("macroWindow", json_helper::toJsonValue(baseWindow));
                result.metadata.set("macroMode", json_helper::toJsonValue(static_cast<int>(MacroComputationMode::ProxySensitivity)));
                result.metadata.set("benchmarkSymbol", json_helper::toJsonValue(benchmarkSymbol.toStdString()));
            },
            QStringLiteral("使用宏观代理敏感度运行时"));
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