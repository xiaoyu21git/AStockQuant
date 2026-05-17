#include "batch_technical_indicators.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace factor {

namespace {

constexpr double kEpsilon = 1e-12;

double clampScore(double value)
{
    if (!std::isfinite(value)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return std::clamp(value, -1.0, 1.0);
}

double nanValue()
{
    return std::numeric_limits<double>::quiet_NaN();
}

double lastFiniteValue(const std::vector<double>& values)
{
    for (auto it = values.rbegin(); it != values.rend(); ++it) {
        if (std::isfinite(*it)) {
            return *it;
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double meanTailValue(const std::vector<double>& values, size_t tailLength)
{
    if (values.empty() || tailLength == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const size_t start = values.size() > tailLength ? values.size() - tailLength : 0;
    double sum = 0.0;
    size_t count = 0;
    for (size_t index = start; index < values.size(); ++index) {
        if (!std::isfinite(values[index])) {
            continue;
        }
        sum += values[index];
        ++count;
    }

    if (count == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    return sum / static_cast<double>(count);
}

double safeScale(double value)
{
    return (std::max)(1e-6, std::abs(value));
}

bool allFinite(const std::vector<double>& values, size_t begin, size_t end)
{
    for (size_t index = begin; index < end; ++index) {
        if (!std::isfinite(values[index])) {
            return false;
        }
    }
    return true;
}

double computeSimpleMean(const std::vector<double>& values, size_t begin, size_t end)
{
    if (begin >= end || end > values.size() || !allFinite(values, begin, end)) {
        return nanValue();
    }

    double sum = 0.0;
    for (size_t index = begin; index < end; ++index) {
        sum += values[index];
    }
    return sum / static_cast<double>(end - begin);
}

double computeSimpleMeanLast(const std::vector<double>& values, size_t period)
{
    if (period == 0 || values.size() < period) {
        return nanValue();
    }
    return computeSimpleMean(values, values.size() - period, values.size());
}

double computeSumLast(const std::vector<double>& values, size_t period)
{
    if (period == 0 || values.size() < period || !allFinite(values, values.size() - period, values.size())) {
        return nanValue();
    }

    double sum = 0.0;
    for (size_t index = values.size() - period; index < values.size(); ++index) {
        sum += values[index];
    }
    return sum;
}

std::vector<double> buildSimpleMovingAverageSeries(const std::vector<double>& values, size_t period)
{
    std::vector<double> result(values.size(), nanValue());
    if (period == 0 || values.size() < period) {
        return result;
    }

    for (size_t end = period; end <= values.size(); ++end) {
        const double mean = computeSimpleMean(values, end - period, end);
        result[end - 1] = mean;
    }
    return result;
}

std::vector<double> buildExponentialMovingAverageSeries(const std::vector<double>& values, size_t period)
{
    std::vector<double> result(values.size(), nanValue());
    if (period == 0 || values.size() < period) {
        return result;
    }

    const double seed = computeSimpleMean(values, 0, period);
    if (!std::isfinite(seed)) {
        return result;
    }

    const double alpha = 2.0 / (static_cast<double>(period) + 1.0);
    double ema = seed;
    result[period - 1] = ema;
    for (size_t index = period; index < values.size(); ++index) {
        if (!std::isfinite(values[index])) {
            return std::vector<double>(values.size(), nanValue());
        }
        ema = (values[index] - ema) * alpha + ema;
        result[index] = ema;
    }
    return result;
}

double computeExponentialMovingAverageLast(const std::vector<double>& values, size_t period)
{
    return lastFiniteValue(buildExponentialMovingAverageSeries(values, period));
}

double computeRsiLast(const std::vector<double>& closes, size_t period)
{
    if (period == 0 || closes.size() < period + 1 || !allFinite(closes, 0, closes.size())) {
        return nanValue();
    }

    double averageGain = 0.0;
    double averageLoss = 0.0;
    for (size_t index = 1; index <= period; ++index) {
        const double change = closes[index] - closes[index - 1];
        if (change > 0.0) {
            averageGain += change;
        } else {
            averageLoss -= change;
        }
    }
    averageGain /= static_cast<double>(period);
    averageLoss /= static_cast<double>(period);

    for (size_t index = period + 1; index < closes.size(); ++index) {
        const double change = closes[index] - closes[index - 1];
        const double gain = change > 0.0 ? change : 0.0;
        const double loss = change < 0.0 ? -change : 0.0;
        averageGain = ((averageGain * (static_cast<double>(period) - 1.0)) + gain) / static_cast<double>(period);
        averageLoss = ((averageLoss * (static_cast<double>(period) - 1.0)) + loss) / static_cast<double>(period);
    }

    if (averageLoss <= kEpsilon) {
        return averageGain <= kEpsilon ? 50.0 : 100.0;
    }

    const double relativeStrength = averageGain / averageLoss;
    return 100.0 - (100.0 / (1.0 + relativeStrength));
}

double computeMacdHistogramLast(const std::vector<double>& closes, size_t fastPeriod, size_t slowPeriod, size_t signalPeriod)
{
    if (closes.size() < slowPeriod || !allFinite(closes, 0, closes.size())) {
        return nanValue();
    }

    const auto fastSeries = buildExponentialMovingAverageSeries(closes, fastPeriod);
    const auto slowSeries = buildExponentialMovingAverageSeries(closes, slowPeriod);
    std::vector<double> macdValues;
    macdValues.reserve(closes.size());
    for (size_t index = 0; index < closes.size(); ++index) {
        if (std::isfinite(fastSeries[index]) && std::isfinite(slowSeries[index])) {
            macdValues.push_back(fastSeries[index] - slowSeries[index]);
        }
    }

    if (macdValues.size() < signalPeriod) {
        return nanValue();
    }

    const double signalValue = computeExponentialMovingAverageLast(macdValues, signalPeriod);
    if (!std::isfinite(signalValue)) {
        return nanValue();
    }
    return macdValues.back() - signalValue;
}

std::pair<double, double> computeBollingerLast(const std::vector<double>& closes, size_t period, double stdMultiplier)
{
    if (closes.size() < period) {
        return {nanValue(), nanValue()};
    }

    const size_t begin = closes.size() - period;
    const double middle = computeSimpleMean(closes, begin, closes.size());
    if (!std::isfinite(middle)) {
        return {nanValue(), nanValue()};
    }

    double variance = 0.0;
    for (size_t index = begin; index < closes.size(); ++index) {
        const double diff = closes[index] - middle;
        variance += diff * diff;
    }
    variance /= static_cast<double>(period);
    const double standardDeviation = std::sqrt((std::max)(0.0, variance));
    return {middle + stdMultiplier * standardDeviation, middle};
}

std::pair<double, double> computeStochasticLast(const std::vector<double>& highs,
                                                const std::vector<double>& lows,
                                                const std::vector<double>& closes,
                                                size_t window,
                                                size_t kPeriod,
                                                size_t dPeriod)
{
    const size_t seriesLength = (std::min)({highs.size(), lows.size(), closes.size()});
    if (seriesLength < window || !allFinite(highs, 0, seriesLength) || !allFinite(lows, 0, seriesLength)
        || !allFinite(closes, 0, seriesLength)) {
        return {nanValue(), nanValue()};
    }

    std::vector<double> rawKValues;
    rawKValues.reserve(seriesLength - window + 1);
    for (size_t end = window - 1; end < seriesLength; ++end) {
        const size_t begin = end + 1 - window;
        const auto highestIt = std::max_element(highs.begin() + static_cast<std::ptrdiff_t>(begin),
                                                highs.begin() + static_cast<std::ptrdiff_t>(end + 1));
        const auto lowestIt = std::min_element(lows.begin() + static_cast<std::ptrdiff_t>(begin),
                                               lows.begin() + static_cast<std::ptrdiff_t>(end + 1));
        const double highestHigh = *highestIt;
        const double lowestLow = *lowestIt;
        const double range = highestHigh - lowestLow;
        rawKValues.push_back(range <= kEpsilon ? 50.0 : 100.0 * (closes[end] - lowestLow) / range);
    }

    const auto slowKSeries = buildSimpleMovingAverageSeries(rawKValues, kPeriod);
    std::vector<double> compactSlowK;
    compactSlowK.reserve(slowKSeries.size());
    for (double value : slowKSeries) {
        if (std::isfinite(value)) {
            compactSlowK.push_back(value);
        }
    }

    if (compactSlowK.size() < dPeriod) {
        return {nanValue(), nanValue()};
    }

    const double slowK = compactSlowK.back();
    const double slowD = computeSimpleMeanLast(compactSlowK, dPeriod);
    return {slowK, slowD};
}

double computeAtrLast(const std::vector<double>& highs,
                      const std::vector<double>& lows,
                      const std::vector<double>& closes,
                      size_t period)
{
    const size_t seriesLength = (std::min)({highs.size(), lows.size(), closes.size()});
    if (seriesLength < period + 1 || !allFinite(highs, 0, seriesLength) || !allFinite(lows, 0, seriesLength)
        || !allFinite(closes, 0, seriesLength)) {
        return nanValue();
    }

    std::vector<double> trueRanges(seriesLength);
    trueRanges[0] = highs[0] - lows[0];
    for (size_t index = 1; index < seriesLength; ++index) {
        const double highLow = highs[index] - lows[index];
        const double highClose = std::abs(highs[index] - closes[index - 1]);
        const double lowClose = std::abs(lows[index] - closes[index - 1]);
        trueRanges[index] = (std::max)({highLow, highClose, lowClose});
    }

    double atr = computeSimpleMean(trueRanges, 1, period + 1);
    if (!std::isfinite(atr)) {
        return nanValue();
    }

    for (size_t index = period + 1; index < seriesLength; ++index) {
        atr = ((atr * (static_cast<double>(period) - 1.0)) + trueRanges[index]) / static_cast<double>(period);
    }
    return atr;
}

double computeObvLast(const std::vector<double>& closes, const std::vector<double>& volumes)
{
    const size_t seriesLength = (std::min)(closes.size(), volumes.size());
    if (seriesLength < 2 || !allFinite(closes, 0, seriesLength) || !allFinite(volumes, 0, seriesLength)) {
        return nanValue();
    }

    double obv = 0.0;
    for (size_t index = 1; index < seriesLength; ++index) {
        if (closes[index] > closes[index - 1]) {
            obv += volumes[index];
        } else if (closes[index] < closes[index - 1]) {
            obv -= volumes[index];
        }
    }
    return obv;
}

double computeStdDevLast(const std::vector<double>& values, size_t period)
{
    if (period == 0 || values.size() < period) {
        return nanValue();
    }

    const size_t begin = values.size() - period;
    const double mean = computeSimpleMean(values, begin, values.size());
    if (!std::isfinite(mean)) {
        return nanValue();
    }

    double variance = 0.0;
    for (size_t index = begin; index < values.size(); ++index) {
        const double diff = values[index] - mean;
        variance += diff * diff;
    }
    variance /= static_cast<double>(period);
    return std::sqrt((std::max)(0.0, variance));
}

} // namespace

std::unordered_map<std::string, double> batchCalculateRsi(
    const std::unordered_map<std::string, std::vector<double>>& allCloses,
    int period)
{
    std::unordered_map<std::string, double> results;
    if (allCloses.empty()) {
        return results;
    }

    const int resolvedPeriod = (std::max)(2, period);

    for (const auto& [symbol, closes] : allCloses) {
        if (closes.size() < static_cast<size_t>(resolvedPeriod + 1)) {
            continue;
        }

        const double rsiValue = computeRsiLast(closes, static_cast<size_t>(resolvedPeriod));
        if (!std::isfinite(rsiValue)) {
            continue;
        }

        results.emplace(symbol, clampScore((rsiValue - 50.0) / 50.0));
    }

    return results;
}

std::unordered_map<std::string, double> batchCalculateMacd(
    const std::unordered_map<std::string, std::vector<double>>& allCloses,
    int fast,
    int slow,
    int signal)
{
    std::unordered_map<std::string, double> results;
    if (allCloses.empty()) {
        return results;
    }

    const int resolvedFast = (std::max)(2, fast);
    const int resolvedSlow = (std::max)(resolvedFast + 1, slow);
    const int resolvedSignal = (std::max)(2, signal);

    for (const auto& [symbol, closes] : allCloses) {
        if (closes.size() < static_cast<size_t>(resolvedSlow + resolvedSignal)) {
            continue;
        }

        const double histogramValue = computeMacdHistogramLast(closes,
                                                               static_cast<size_t>(resolvedFast),
                                                               static_cast<size_t>(resolvedSlow),
                                                               static_cast<size_t>(resolvedSignal));
        const double closeValue = closes.back();
        if (!std::isfinite(histogramValue) || !std::isfinite(closeValue)) {
            continue;
        }

        results.emplace(symbol, clampScore(std::tanh(histogramValue / (std::max)(1e-6, std::abs(closeValue)))));
    }

    return results;
}

std::unordered_map<std::string, double> batchCalculateMa(
    const std::unordered_map<std::string, std::vector<double>>& allCloses,
    int period)
{
    std::unordered_map<std::string, double> results;
    if (allCloses.empty()) {
        return results;
    }

    const int resolvedPeriod = (std::max)(2, period);

    for (const auto& [symbol, closes] : allCloses) {
        if (closes.size() < static_cast<size_t>(resolvedPeriod)) {
            continue;
        }

        const double maValue = computeSimpleMeanLast(closes, static_cast<size_t>(resolvedPeriod));
        const double closeValue = closes.back();
        if (!std::isfinite(maValue) || !std::isfinite(closeValue)) {
            continue;
        }

        results.emplace(symbol, clampScore(std::tanh((closeValue - maValue) / (std::max)(1e-6, std::abs(maValue)))));
    }

    return results;
}

std::unordered_map<std::string, double> batchCalculateEma(
    const std::unordered_map<std::string, std::vector<double>>& allCloses,
    int period)
{
    std::unordered_map<std::string, double> results;
    if (allCloses.empty()) {
        return results;
    }

    const int resolvedPeriod = (std::max)(2, period);

    for (const auto& [symbol, closes] : allCloses) {
        if (closes.size() < static_cast<size_t>(resolvedPeriod)) {
            continue;
        }

        const double emaValue = computeExponentialMovingAverageLast(closes, static_cast<size_t>(resolvedPeriod));
        const double closeValue = closes.back();
        if (!std::isfinite(emaValue) || !std::isfinite(closeValue)) {
            continue;
        }

        results.emplace(symbol, clampScore(std::tanh((closeValue - emaValue) / (std::max)(1e-6, std::abs(emaValue)))));
    }

    return results;
}

std::unordered_map<std::string, double> batchCalculateBoll(
    const std::unordered_map<std::string, std::vector<double>>& allCloses,
    int period,
    double stdMultiplier)
{
    std::unordered_map<std::string, double> results;
    if (allCloses.empty()) {
        return results;
    }

    const int resolvedPeriod = (std::max)(2, period);

    for (const auto& [symbol, closes] : allCloses) {
        if (closes.size() < static_cast<size_t>(resolvedPeriod)) {
            continue;
        }

        const auto [upperValue, middleValue] = computeBollingerLast(closes,
                                                                    static_cast<size_t>(resolvedPeriod),
                                                                    stdMultiplier);
        const double closeValue = closes.back();
        if (!std::isfinite(upperValue) || !std::isfinite(middleValue) || !std::isfinite(closeValue)) {
            continue;
        }

        results.emplace(symbol, clampScore(std::tanh((closeValue - middleValue) / (std::max)(1e-6, std::abs(upperValue - middleValue)))));
    }

    return results;
}

std::unordered_map<std::string, double> batchCalculateKdj(
    const std::unordered_map<std::string, std::vector<double>>& allHighs,
    const std::unordered_map<std::string, std::vector<double>>& allLows,
    const std::unordered_map<std::string, std::vector<double>>& allCloses,
    int window,
    int kPeriod,
    int dPeriod)
{
    std::unordered_map<std::string, double> results;
    if (allHighs.empty() || allLows.empty() || allCloses.empty()) {
        return results;
    }

    const int resolvedWindow = (std::max)(2, window);
    const int resolvedKPeriod = (std::max)(2, kPeriod);
    const int resolvedDPeriod = (std::max)(2, dPeriod);

    for (const auto& [symbol, highs] : allHighs) {
        const auto lowIt = allLows.find(symbol);
        const auto closeIt = allCloses.find(symbol);
        if (lowIt == allLows.end() || closeIt == allCloses.end()) {
            continue;
        }

        const auto& lows = lowIt->second;
        const auto& closes = closeIt->second;
        const size_t seriesLength = (std::min)({highs.size(), lows.size(), closes.size()});
        if (seriesLength < static_cast<size_t>(resolvedWindow)) {
            continue;
        }

        const auto [slowK, slowD] = computeStochasticLast(highs,
                                                          lows,
                                                          closes,
                                                          static_cast<size_t>(resolvedWindow),
                                                          static_cast<size_t>(resolvedKPeriod),
                                                          static_cast<size_t>(resolvedDPeriod));
        if (!std::isfinite(slowK) || !std::isfinite(slowD)) {
            continue;
        }

        const double jValue = 3.0 * slowK - 2.0 * slowD;
        results.emplace(symbol, clampScore((jValue - 50.0) / 50.0));
    }

    return results;
}

std::unordered_map<std::string, double> batchCalculateAtr(
    const std::unordered_map<std::string, std::vector<double>>& allHighs,
    const std::unordered_map<std::string, std::vector<double>>& allLows,
    const std::unordered_map<std::string, std::vector<double>>& allCloses,
    int window)
{
    std::unordered_map<std::string, double> results;
    if (allHighs.empty() || allLows.empty() || allCloses.empty()) {
        return results;
    }

    const int resolvedWindow = (std::max)(2, window);

    for (const auto& [symbol, highs] : allHighs) {
        const auto lowIt = allLows.find(symbol);
        const auto closeIt = allCloses.find(symbol);
        if (lowIt == allLows.end() || closeIt == allCloses.end()) {
            continue;
        }

        const auto& lows = lowIt->second;
        const auto& closes = closeIt->second;
        const size_t seriesLength = (std::min)({highs.size(), lows.size(), closes.size()});
        if (seriesLength < static_cast<size_t>(resolvedWindow + 1)) {
            continue;
        }

        const double atrValue = computeAtrLast(highs, lows, closes, static_cast<size_t>(resolvedWindow));
        const double closeValue = closes.back();
        if (!std::isfinite(atrValue) || !std::isfinite(closeValue)) {
            continue;
        }

        results.emplace(symbol, clampScore(-atrValue / (std::max)(1e-6, std::abs(closeValue))));
    }

    return results;
}

std::unordered_map<std::string, double> batchCalculateVwap(
    const std::unordered_map<std::string, std::vector<double>>& allCloses,
    const std::unordered_map<std::string, std::vector<double>>& allVolumes)
{
    std::unordered_map<std::string, double> results;
    if (allCloses.empty() || allVolumes.empty()) {
        return results;
    }

    for (const auto& [symbol, closes] : allCloses) {
        const auto volumeIt = allVolumes.find(symbol);
        if (volumeIt == allVolumes.end()) {
            continue;
        }

        const auto& volumes = volumeIt->second;
        const size_t seriesLength = (std::min)(closes.size(), volumes.size());
        if (seriesLength < 2) {
            continue;
        }

        std::vector<double> priceVolumeSeries(seriesLength);
        for (size_t index = 0; index < seriesLength; ++index) {
            priceVolumeSeries[index] = closes[index] * volumes[index];
        }

        const int period = static_cast<int>(seriesLength);
        const double volumeSum = computeSumLast(volumes, static_cast<size_t>(period));
        const double priceVolumeSum = computeSumLast(priceVolumeSeries, static_cast<size_t>(period));
        const double closeValue = closes.back();
        if (!std::isfinite(volumeSum) || !std::isfinite(priceVolumeSum) || volumeSum <= 1e-12 || !std::isfinite(closeValue)) {
            continue;
        }

        const double vwap = priceVolumeSum / volumeSum;
        results.emplace(symbol, clampScore(std::tanh((closeValue - vwap) / (std::max)(1e-6, std::abs(vwap)))));
    }

    return results;
}

std::unordered_map<std::string, double> batchCalculateVolumeRatio(
    const std::unordered_map<std::string, std::vector<double>>& allVolumes,
    int period)
{
    std::unordered_map<std::string, double> results;
    if (allVolumes.empty()) {
        return results;
    }

    const int resolvedPeriod = (std::max)(2, period);

    for (const auto& [symbol, volumes] : allVolumes) {
        if (volumes.size() < static_cast<size_t>(resolvedPeriod)) {
            continue;
        }

        const double meanValue = computeSimpleMeanLast(volumes, static_cast<size_t>(resolvedPeriod));
        const double lastValue = volumes.back();
        if (!std::isfinite(meanValue) || !std::isfinite(lastValue)) {
            continue;
        }

        results.emplace(symbol, clampScore(std::tanh((lastValue - meanValue) / (std::max)(1e-6, std::abs(meanValue)))));
    }

    return results;
}

std::unordered_map<std::string, double> batchCalculateObv(
    const std::unordered_map<std::string, std::vector<double>>& allCloses,
    const std::unordered_map<std::string, std::vector<double>>& allVolumes,
    int period)
{
    std::unordered_map<std::string, double> results;
    if (allCloses.empty() || allVolumes.empty()) {
        return results;
    }

    const int resolvedPeriod = (std::max)(2, period);

    for (const auto& [symbol, closes] : allCloses) {
        const auto volumeIt = allVolumes.find(symbol);
        if (volumeIt == allVolumes.end()) {
            continue;
        }

        const auto& volumes = volumeIt->second;
        const size_t seriesLength = (std::min)(closes.size(), volumes.size());
        if (seriesLength < 2) {
            continue;
        }

        const double obvValue = computeObvLast(closes, volumes);
        if (!std::isfinite(obvValue)) {
            continue;
        }

        const size_t tailLength = (std::min)(static_cast<size_t>(resolvedPeriod + 1), seriesLength);
        const double averageVolume = meanTailValue(volumes, tailLength);
        if (!std::isfinite(averageVolume)) {
            continue;
        }

        results.emplace(symbol, clampScore(std::tanh(obvValue / (std::max)(1e-6, std::abs(averageVolume) * static_cast<double>(tailLength)))));
    }

    return results;
}

std::unordered_map<std::string, double> batchCalculateTurnoverStability(
    const std::unordered_map<std::string, std::vector<double>>& allValues,
    int window)
{
    std::unordered_map<std::string, double> results;
    if (allValues.empty()) {
        return results;
    }

    const int resolvedWindow = (std::max)(2, window);

    for (const auto& [symbol, values] : allValues) {
        if (values.size() < static_cast<size_t>(resolvedWindow)) {
            continue;
        }

        const double meanValue = computeSimpleMeanLast(values, static_cast<size_t>(resolvedWindow));
        const double stdValue = computeStdDevLast(values, static_cast<size_t>(resolvedWindow));
        if (!std::isfinite(meanValue) || !std::isfinite(stdValue)) {
            continue;
        }

        const double coefficient = std::abs(meanValue) <= 1e-12 ? std::numeric_limits<double>::infinity() : stdValue / std::abs(meanValue);
        const double normalized = 1.0 - std::clamp(coefficient, 0.0, 2.0) / 2.0;
        results.emplace(symbol, clampScore(normalized * 2.0 - 1.0));
    }

    return results;
}

} // namespace factor