#include "domain/factor/include/ConfigurableFactorDetail.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace factor {

using namespace configurable_factor_detail;

namespace {

LaggedDateMode resolvedLaggedDateMode(bool lagEnabled, bool laggedDateResolvedByProvider)
{
    if (!lagEnabled) {
        return LaggedDateMode::Disabled;
    }
    return laggedDateResolvedByProvider ? LaggedDateMode::ProviderScan : LaggedDateMode::AnchorDate;
}

} // namespace

CalculationResult ConfigurableFactorBase::calculateLiquidity(const CalculationContext& context) const
{
    QElapsedTimer elapsedTimer;
    elapsedTimer.start();

    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;
    result.dataStatus.availability = DataAvailability::AVAILABLE;
    result.dataStatus.coverage = 1.0;
    result.dataStatus.message = "使用缓存数据集";

    const CommonParams& common = commonParams_;
    const LiquidityParams& liquidity = liquidityParams();
    const LiquidityMetric metricKind = liquidity.liquidityMetric;
    const LiquidityIndicatorSpec metricSpec = liquidityIndicatorSpec(metricKind);
    const factor::bridge::FieldKey* metricKey = metricSpec.common.fieldKey;
    const QString metric = metricKey ? metricKey->toQString() : QString();
    if (metric.isEmpty()) {
        result.dataStatus = CalculationResult::createError("流动性因子缺少有效 metric 枚举").dataStatus;
        result.metadata.set("emptyReason", json_helper::toJsonValue("流动性因子缺少有效 metric 枚举"));
        return result;
    }
    const DataFrequency frequency = common.frequency;
    const StandardizationMethod standardization = common.standardization;
    const int window = (std::max)(1, static_cast<int>(common.window));
    bool laggedDateResolvedByProvider = false;
    CommonNeutralizationMode liquidityNeutralizationMode = common.neutralizationEnabled
        ? CommonNeutralizationMode::REQUESTED
        : CommonNeutralizationMode::DISABLED;
    auto resolvePreviousAvailableDate = [&](const QString& anchorDate, const QString& requiredField) {
        if (anchorDate.isEmpty()) {
            return QString::fromStdString(context.date);
        }

        const int maxOffset = (std::max)(45, static_cast<int>(common.lookbackWindow));
        const std::vector<std::string> symbols = context.symbols.empty()
            ? context.historicalView->getAvailableSymbols(context.date)
            : context.symbols;
        for (int offset = 1; offset <= maxOffset; ++offset) {
            const QString candidate = QDate::fromString(anchorDate, Qt::ISODate).addDays(-offset).toString(Qt::ISODate);
            CalculationContext candidateContext = context;
            candidateContext.date = candidate.toStdString();
            candidateContext.symbols = symbols;
            if (currentFieldCrossSection(candidateContext, requiredField).empty()) {
                continue;
            }
            laggedDateResolvedByProvider = true;
            return candidate;
        }

        return anchorDate;
    };

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

    if (common.lagEnabled) {
        QString requiredField = QString(factor::bridge::MarketBarFieldKeys::TURNOVER_RATE);
        switch (metricKind) {
        case LiquidityMetric::VOLUME:
            requiredField = QString(factor::bridge::MarketBarFieldKeys::VOLUME);
            break;
        case LiquidityMetric::AMPLITUDE:
            requiredField = QString(factor::bridge::MarketBarFieldKeys::AMPLITUDE);
            break;
        case LiquidityMetric::AMIHUD_ILLIQUIDITY:
            requiredField = QString(factor::bridge::MarketBarFieldKeys::CLOSE);
            break;
        case LiquidityMetric::TURNOVER_RATE:
        case LiquidityMetric::UNKNOWN:
            break;
        }
        effectiveDate = resolvePreviousAvailableDate(effectiveDate, requiredField);
    }

    CalculationContext effectiveContext = context;
    effectiveContext.date = effectiveDate.toStdString();

    const auto symbols = effectiveSymbols(effectiveContext);
    effectiveContext.symbols = symbols;
    const bool useLocalBatchCache = context.historicalView
        && (!activeBatchComputationCache || activeBatchComputationCache->historicalView != context.historicalView);

    auto calculateLiquidityBody = [&]() -> CalculationResult {
        size_t populatedSymbolCount = 0;
        const auto closesBySymbol = fetchBatchSeriesMap(effectiveContext, QString(factor::bridge::MarketBarFieldKeys::CLOSE), window + 1);
        const auto volumesBySymbol = fetchBatchSeriesMap(effectiveContext, QString(factor::bridge::MarketBarFieldKeys::VOLUME), window + 1);
        const QString metricField = metric;
        const auto metricBySymbol = fetchBatchSeriesMap(effectiveContext, metricField, window);

        const std::vector<std::string> activeSymbols = [&]() {
            std::vector<std::string> validSymbols;
            validSymbols.reserve(symbols.size());
            for (const auto& symbol : symbols) {
                if (metricKind == LiquidityMetric::AMIHUD_ILLIQUIDITY) {
                    const auto closeIt = closesBySymbol.find(symbol);
                    const auto volumeIt = volumesBySymbol.find(symbol);
                    if (closeIt == closesBySymbol.end() || volumeIt == volumesBySymbol.end()) {
                        continue;
                    }
                    if (closeIt->second.size() < 2 || volumeIt->second.size() < 2) {
                        continue;
                    }
                } else {
                    const auto metricIt = metricBySymbol.find(symbol);
                    if (metricIt == metricBySymbol.end() || metricIt->second.empty()) {
                        continue;
                    }
                }
                validSymbols.push_back(symbol);
            }
            return validSymbols;
        }();

        if (activeSymbols.empty()) {
            result.dataStatus = CalculationResult::createError("流动性因子没有可用价格或成交量数据").dataStatus;
            result.metadata.set("emptyReason", json_helper::toJsonValue("流动性因子没有可用价格或成交量数据"));
            result.metadata.set("laggedDateMode", json_helper::toJsonValue(static_cast<int>(resolvedLaggedDateMode(common.lagEnabled, laggedDateResolvedByProvider))));
            result.metadata.set("neutralizationMode", json_helper::toJsonValue(static_cast<int>(liquidityNeutralizationMode)));
            return result;
        }

        const auto findCommonLength = [&](const auto& seriesBySymbol) -> size_t {
            size_t commonLength = std::numeric_limits<size_t>::max();
            for (const auto& symbol : activeSymbols) {
                const auto seriesIt = seriesBySymbol.find(symbol);
                if (seriesIt == seriesBySymbol.end() || seriesIt->second.empty()) {
                    continue;
                }
                commonLength = (std::min)(commonLength, seriesIt->second.size());
            }
            return commonLength == std::numeric_limits<size_t>::max() ? 0 : commonLength;
        };

        const auto collectMatrix = [&](const auto& seriesBySymbol, size_t commonLength) {
            Eigen::MatrixXd matrix(static_cast<int>(activeSymbols.size()), static_cast<int>(commonLength));
            for (int row = 0; row < matrix.rows(); ++row) {
                const auto seriesIt = seriesBySymbol.find(activeSymbols[static_cast<size_t>(row)]);
                const auto& values = seriesIt->second;
                const size_t offset = values.size() - commonLength;
                for (int column = 0; column < matrix.cols(); ++column) {
                    matrix(row, column) = values[offset + static_cast<size_t>(column)];
                }
            }
            return matrix;
        };

        size_t commonLength = 0;
        if (metricKind == LiquidityMetric::AMIHUD_ILLIQUIDITY) {
            const size_t closeLength = findCommonLength(closesBySymbol);
            const size_t volumeLength = findCommonLength(volumesBySymbol);
            commonLength = (std::min)(closeLength, volumeLength);
        } else {
            commonLength = findCommonLength(metricBySymbol);
        }

        if (commonLength == 0 || (metric == QStringLiteral("amihud_illiquidity") && commonLength < 2)) {
            result.dataStatus = CalculationResult::createError("流动性因子没有可用价格或成交量数据").dataStatus;
            result.metadata.set("emptyReason", json_helper::toJsonValue("流动性因子没有可用价格或成交量数据"));
            result.metadata.set("laggedDateMode", json_helper::toJsonValue(static_cast<int>(resolvedLaggedDateMode(common.lagEnabled, laggedDateResolvedByProvider))));
            result.metadata.set("neutralizationMode", json_helper::toJsonValue(static_cast<int>(liquidityNeutralizationMode)));
            return result;
        }

        Eigen::VectorXd rawScores(static_cast<int>(activeSymbols.size()));
        rawScores.setConstant(std::numeric_limits<double>::quiet_NaN());

        if (metricKind == LiquidityMetric::VOLUME) {
            const Eigen::MatrixXd metricMatrix = collectMatrix(metricBySymbol, commonLength);
            rawScores = metricMatrix.rowwise().mean();
        } else if (metricKind == LiquidityMetric::AMPLITUDE) {
            const Eigen::MatrixXd metricMatrix = collectMatrix(metricBySymbol, commonLength);
            rawScores = -metricMatrix.rowwise().mean();
        } else if (metricKind == LiquidityMetric::AMIHUD_ILLIQUIDITY) {
            const Eigen::MatrixXd closeMatrix = collectMatrix(closesBySymbol, commonLength);
            const Eigen::MatrixXd volumeMatrix = collectMatrix(volumesBySymbol, commonLength);
            const Eigen::MatrixXd previousClose = closeMatrix.leftCols(static_cast<int>(commonLength) - 1);
            const Eigen::MatrixXd currentClose = closeMatrix.rightCols(static_cast<int>(commonLength) - 1);
            const Eigen::MatrixXd currentVolume = volumeMatrix.rightCols(static_cast<int>(commonLength) - 1);
            const Eigen::MatrixXd previousCloseAbs = previousClose.array().abs().matrix().unaryExpr([](double value) {
                return (std::max)(1e-12, value);
            });
            const Eigen::MatrixXd volumeSafe = currentVolume.array().abs().matrix().unaryExpr([](double value) {
                return (std::max)(1e-12, value);
            });
            const Eigen::MatrixXd ratioMatrix = (currentClose - previousClose).array().abs().matrix()
                .cwiseQuotient(previousCloseAbs)
                .cwiseQuotient(volumeSafe);

            for (int row = 0; row < ratioMatrix.rows(); ++row) {
                double sum = 0.0;
                int count = 0;
                for (int column = 0; column < ratioMatrix.cols(); ++column) {
                    const double ratio = ratioMatrix(row, column);
                    if (!std::isfinite(ratio)) {
                        continue;
                    }
                    sum += ratio;
                    ++count;
                }
                if (count > 0) {
                    rawScores(row) = -sum / static_cast<double>(count);
                }
            }
        } else {
            const Eigen::MatrixXd metricMatrix = collectMatrix(metricBySymbol, commonLength);
            rawScores = metricMatrix.rowwise().mean();
        }

        std::unordered_map<std::string, double> rawValueMap;
        rawValueMap.reserve(activeSymbols.size());
        for (int row = 0; row < rawScores.size(); ++row) {
            const double value = rawScores(row);
            if (!std::isfinite(value)) {
                continue;
            }
            rawValueMap[activeSymbols[static_cast<size_t>(row)]] = value;
        }

        if (rawValueMap.empty()) {
            result.dataStatus = CalculationResult::createError("流动性因子没有可用价格或成交量数据").dataStatus;
            result.metadata.set("emptyReason", json_helper::toJsonValue("流动性因子没有可用价格或成交量数据"));
            result.metadata.set("laggedDateMode", json_helper::toJsonValue(static_cast<int>(resolvedLaggedDateMode(common.lagEnabled, laggedDateResolvedByProvider))));
            result.metadata.set("neutralizationMode", json_helper::toJsonValue(static_cast<int>(liquidityNeutralizationMode)));
            return result;
        }

        if (common.neutralizationEnabled) {
            QString errorMessage;
            if (!applyHistoricalViewIndustrySizeNeutralization(effectiveContext, rawValueMap, &errorMessage)) {
                result.dataStatus = CalculationResult::createError(errorMessage.toStdString()).dataStatus;
                result.metadata.set("error", json_helper::toJsonValue(errorMessage.toStdString()));
                liquidityNeutralizationMode = CommonNeutralizationMode::HISTORICAL_VIEW_NEUTRALIZATION_FAILED;
                result.metadata.set("laggedDateMode", json_helper::toJsonValue(static_cast<int>(resolvedLaggedDateMode(common.lagEnabled, laggedDateResolvedByProvider))));
                result.metadata.set("neutralizationMode", json_helper::toJsonValue(static_cast<int>(liquidityNeutralizationMode)));
                return result;
            }
            liquidityNeutralizationMode = CommonNeutralizationMode::HISTORICAL_VIEW_CROSS_SECTION_INDUSTRY_SIZE;
        }

        applyConfigurableStandardization(standardization, rawValueMap);

        for (const auto& [symbol, score] : rawValueMap) {
            if (std::isfinite(score) && score != 0.0) {
                result.values[symbol] = score;
                ++populatedSymbolCount;
            }
        }

        result.metadata.set("metric", json_helper::toJsonValue(static_cast<int>(metricKind)));
        result.metadata.set("metricSourceTable", json_helper::toJsonValue(static_cast<int>(metricSpec.common.sourceTable)));
        result.metadata.set("window", json_helper::toJsonValue(window));
        result.metadata.set("effectiveDate", json_helper::toJsonValue(effectiveDate.toStdString()));
            result.metadata.set("frequency", json_helper::toJsonValue(static_cast<int>(frequency)));
        result.metadata.set("laggedEnabled", json_helper::toJsonValue(common.lagEnabled));
        result.metadata.set("laggedDateMode", json_helper::toJsonValue(static_cast<int>(resolvedLaggedDateMode(common.lagEnabled, laggedDateResolvedByProvider))));
        result.metadata.set("lookbackPeriod", json_helper::toJsonValue(common.lookbackWindow));
            result.metadata.set("standardization", json_helper::toJsonValue(static_cast<int>(standardization)));
        result.metadata.set("neutralizationEnabled", json_helper::toJsonValue(common.neutralizationEnabled));
        result.metadata.set("neutralizationMode", json_helper::toJsonValue(static_cast<int>(liquidityNeutralizationMode)));
        result.metadata.set("symbolCount", json_helper::toJsonValue(static_cast<int>(result.values.size())));

        const qint64 elapsedMs = elapsedTimer.elapsed();
        if (elapsedMs >= 300) {
            qDebug() << "ConfigurableFactorBase(liquidity): 计算耗时较长"
                     << "date=" << QString::fromStdString(context.date)
                     << "metric=" << metric
                     << "window=" << window
                     << "symbolCount=" << static_cast<int>(symbols.size())
                     << "resultCount=" << static_cast<int>(populatedSymbolCount)
                     << "usingHistoricalView=" << static_cast<bool>(context.historicalView)
                     << "elapsedMs=" << elapsedMs;
        }
        return result;
    };

    if (useLocalBatchCache) {
        BatchComputationCache cache;
        cache.historicalView = context.historicalView;
        BatchComputationCacheScope scope(cache);
        return calculateLiquidityBody();
    }

    return calculateLiquidityBody();
}

} // namespace factor