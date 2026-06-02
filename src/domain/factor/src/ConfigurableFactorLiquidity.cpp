#include "domain/factor/include/ConfigurableFactorDetail.h"

#include <ta_libc.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>

namespace factor {

using namespace configurable_factor_detail;

namespace {

constexpr int kMonthsPerYear = 12;
constexpr int kFridayIndex = 5;
constexpr int kIsoWeekLength = 7;

bool parseIsoDate(const std::string& text, std::tm& out)
{
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        return false;
    }

    try {
        const int year = std::stoi(text.substr(0, 4));
        const int month = std::stoi(text.substr(5, 2));
        const int day = std::stoi(text.substr(8, 2));
        if (month < 1 || month > kMonthsPerYear || day < 1 || day > 31) {
            return false;
        }

        std::tm candidate = {};
        candidate.tm_year = year - 1900;
        candidate.tm_mon = month - 1;
        candidate.tm_mday = day;
        candidate.tm_isdst = -1;
        if (std::mktime(&candidate) == -1) {
            return false;
        }
        out = candidate;
        return true;
    } catch (...) {
        return false;
    }
}

std::string formatIsoDate(const std::tm& value)
{
    char buffer[11] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &value);
    return std::string(buffer);
}

std::tm addDays(const std::tm& base, int dayOffset)
{
    std::tm shifted = base;
    shifted.tm_mday += dayOffset;
    shifted.tm_isdst = -1;
    std::mktime(&shifted);
    return shifted;
}

int isoDayOfWeek(const std::tm& value)
{
    const int wday = value.tm_wday;
    return ((wday + 6) % kIsoWeekLength) + 1;
}

LaggedDateMode resolvedLaggedDateMode(bool lagEnabled, bool laggedDateResolvedByProvider)
{
    if (!lagEnabled) {
        return LaggedDateMode::Disabled;
    }
    return laggedDateResolvedByProvider ? LaggedDateMode::ProviderScan : LaggedDateMode::AnchorDate;
}

} // namespace


void ensureTaLibInitialized()
{
    static std::once_flag initFlag;
    static TA_RetCode initResult = TA_BAD_PARAM;
    std::call_once(initFlag, []() {
        initResult = TA_Initialize();
    });
    if (initResult != TA_SUCCESS) {
        throw std::runtime_error("TA-Lib initialization failed for liquidity factor");
    }
}

double taLastOutput(const std::vector<double>& output, int outBegIdx, int outNBElement)
{
    if (outBegIdx < 0 || outNBElement <= 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const size_t lastIndex = static_cast<size_t>(outNBElement - 1);
    if (lastIndex >= output.size()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return output[lastIndex];
}
CalculationResult ConfigurableFactorBase::calculateLiquidity(const CalculationContext& context) const
{
    const auto startTime = std::chrono::steady_clock::now();

    const CommonParams& common = commonParams_;
    const LiquidityParams& liquidity = liquidityParams();
    const LiquidityMetric metricKind = liquidity.liquidityMetric;
    const LiquidityIndicatorSpec metricSpec = liquidityIndicatorSpec(metricKind);
    const factor::bridge::FieldKey* metricKey = metricSpec.common.fieldKey;
    const std::string metric = metricKey ? std::string(metricKey->c_str()) : std::string();
    if (metric.empty()) {
        return createHistoricalViewRuntimeError(context, "流动性因子缺少有效 metric 枚举");
    }
    const DataFrequency frequency = common.frequency;
    const int window = (std::max)(1, static_cast<int>(common.window));
    bool laggedDateResolvedByProvider = false;
    auto resolvePreviousAvailableDate = [&](const std::string& anchorDate, const std::string& requiredField) {
        if (anchorDate.empty()) {
            return context.date;
        }

        std::tm parsedAnchor = {};
        if (!parseIsoDate(anchorDate, parsedAnchor)) {
            return anchorDate;
        }

        const int maxOffset = (std::max)(45, static_cast<int>(common.lookbackWindow));
        const std::vector<std::string> symbols = context.symbols.empty()
            ? context.historicalView->getAvailableSymbols(context.date)
            : context.symbols;
        for (int offset = 1; offset <= maxOffset; ++offset) {
            const std::string candidate = formatIsoDate(addDays(parsedAnchor, -offset));
            CalculationContext candidateContext = context;
            candidateContext.date = candidate;
            candidateContext.symbols = symbols;
            if (currentFieldCrossSection(candidateContext, requiredField).empty()) {
                continue;
            }
            laggedDateResolvedByProvider = true;
            return candidate;
        }

        return anchorDate;
    };

    std::string effectiveDate = context.date;
    std::tm anchorDate = {};
    if (parseIsoDate(effectiveDate, anchorDate)) {
        if (frequency == DataFrequency::Weekly) {
            const int dayOfWeek = isoDayOfWeek(anchorDate);
            const int shiftToPreviousFriday = dayOfWeek >= kFridayIndex ? dayOfWeek - kFridayIndex : dayOfWeek + 2;
            anchorDate = addDays(anchorDate, -shiftToPreviousFriday);
        } else if (frequency == DataFrequency::Monthly) {
            std::tm monthStart = anchorDate;
            monthStart.tm_mday = 1;
            monthStart.tm_isdst = -1;
            std::mktime(&monthStart);
            anchorDate = addDays(monthStart, -1);
        }
        effectiveDate = formatIsoDate(anchorDate);
    }

    const auto symbols = effectiveSymbols(context);
    const bool useLocalBatchCache = context.historicalView
        && (!activeBatchComputationCache || activeBatchComputationCache->historicalView != context.historicalView);

    auto executeLiquidityBody = [&]() -> CalculationResult {
        return executeWithCommonParams(
            context,
            common,
            [&]() {
                std::string resolvedDate = effectiveDate;
                if (common.lagEnabled) {
                    std::string requiredField = std::string(factor::bridge::MarketBarFieldKeys::TURNOVER_RATE.c_str());
                    switch (metricKind) {
                    case LiquidityMetric::VOLUME:
                        requiredField = std::string(factor::bridge::MarketBarFieldKeys::VOLUME.c_str());
                        break;
                    case LiquidityMetric::AMPLITUDE:
                        requiredField = std::string(factor::bridge::MarketBarFieldKeys::AMPLITUDE.c_str());
                        break;
                    case LiquidityMetric::AMIHUD_ILLIQUIDITY:
                        requiredField = std::string(factor::bridge::MarketBarFieldKeys::CLOSE.c_str());
                        break;
                    case LiquidityMetric::TURNOVER_RATE:
                    case LiquidityMetric::UNKNOWN:
                        break;
                    }
                    resolvedDate = resolvePreviousAvailableDate(resolvedDate, requiredField);
                }
                return resolvedDate;
            },
            [this, &context, &metric, metricKind, window, &symbols](const CommonRuntimeState& runtime, CalculationResult& result) {
                CalculationContext effectiveContext = context;
                effectiveContext.date = runtime.effectiveDate;
                effectiveContext.symbols = symbols;

                const auto closesBySymbol = fetchBatchSeriesMap(effectiveContext, std::string(factor::bridge::MarketBarFieldKeys::CLOSE.c_str()), window + 1);
                const auto volumesBySymbol = fetchBatchSeriesMap(effectiveContext, std::string(factor::bridge::MarketBarFieldKeys::VOLUME.c_str()), window + 1);
                const auto metricBySymbol = fetchBatchSeriesMap(effectiveContext, metric, window);

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
                    result.metadata.set("emptyReason", json_helper::toJsonValue("流动性因子没有可用价格或成交量数据"));
                    return;
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

                if (commonLength == 0 || (metric == "amihud_illiquidity" && commonLength < 2)) {
                    result.metadata.set("emptyReason", json_helper::toJsonValue("流动性因子没有可用价格或成交量数据"));
                    return;
                }

                Eigen::VectorXd rawScores(static_cast<int>(activeSymbols.size()));
                rawScores.setConstant(std::numeric_limits<double>::quiet_NaN());

                if (metricKind == LiquidityMetric::VOLUME) {
                    ensureTaLibInitialized();
                    for (int row = 0; row < rawScores.size(); ++row) {
                        const auto seriesIt = metricBySymbol.find(activeSymbols[static_cast<size_t>(row)]);
                        if (seriesIt == metricBySymbol.end()) {
                            continue;
                        }

                        const auto& values = seriesIt->second;
                        const size_t offset = values.size() - commonLength;
                        std::vector<double> trailing(values.begin() + static_cast<std::ptrdiff_t>(offset), values.end());
                        if (trailing.empty()) {
                            continue;
                        }
                        if (trailing.size() == 1) {
                            rawScores(row) = trailing.front();
                            continue;
                        }

                        std::vector<double> output(trailing.size(), std::numeric_limits<double>::quiet_NaN());
                        int outBegIdx = 0;
                        int outNBElement = 0;
                        const TA_RetCode ret = TA_SMA(0,
                                                      static_cast<int>(trailing.size() - 1),
                                                      trailing.data(),
                                                      static_cast<int>(trailing.size()),
                                                      &outBegIdx,
                                                      &outNBElement,
                                                      output.data());
                        if (ret != TA_SUCCESS) {
                            continue;
                        }

                        rawScores(row) = taLastOutput(output, outBegIdx, outNBElement);
                    }
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

                for (int row = 0; row < rawScores.size(); ++row) {
                    const double value = rawScores(row);
                    if (!std::isfinite(value)) {
                        continue;
                    }
                    result.values[activeSymbols[static_cast<size_t>(row)]] = value;
                }

                if (result.values.empty()) {
                    result.metadata.set("emptyReason", json_helper::toJsonValue("流动性因子没有可用价格或成交量数据"));
                }
            },
            [](const CommonRuntimeState&, CalculationResult&) {},
            [&](const CommonRuntimeState&, CalculationResult& result) {
                result.metadata.set("metric", json_helper::toJsonValue(static_cast<int>(metricKind)));
                result.metadata.set("metricSourceTable", json_helper::toJsonValue(static_cast<int>(metricSpec.common.sourceTable)));
                result.metadata.set("window", json_helper::toJsonValue(window));
                result.metadata.set("laggedDateMode", json_helper::toJsonValue(static_cast<int>(resolvedLaggedDateMode(common.lagEnabled, laggedDateResolvedByProvider))));
            });
    };

    auto finalizeLiquidityResult = [&](CalculationResult result) {
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime)
                                   .count();
        (void)elapsedMs;
        return result;
    };

    if (useLocalBatchCache) {
        BatchComputationCache cache;
        cache.historicalView = context.historicalView;
        BatchComputationCacheScope scope(cache);
        return finalizeLiquidityResult(executeLiquidityBody());
    }

    return finalizeLiquidityResult(executeLiquidityBody());
}

} // namespace factor