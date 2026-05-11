#include "batch_technical_indicators.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <ta_libc.h>

namespace factor {

namespace {

double clampScore(double value)
{
    if (!std::isfinite(value)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return std::clamp(value, -1.0, 1.0);
}

void ensureTaLibInitialized()
{
    static std::once_flag onceFlag;
    std::call_once(onceFlag, []() {
        if (TA_Initialize() != TA_SUCCESS) {
            throw std::runtime_error("TA-Lib initialization failed");
        }
    });
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

std::unordered_map<std::string, double> buildScoreMap(const std::vector<std::string>& symbols,
                                                      const std::vector<double>& scores)
{
    std::unordered_map<std::string, double> result;
    const int rowCount = (std::min)(static_cast<int>(symbols.size()), static_cast<int>(scores.size()));
    for (int row = 0; row < rowCount; ++row) {
        const double score = scores[static_cast<size_t>(row)];
        if (std::isfinite(score)) {
            result.emplace(symbols[static_cast<size_t>(row)], score);
        }
    }
    return result;
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
    ensureTaLibInitialized();

    for (const auto& [symbol, closes] : allCloses) {
        if (closes.size() < static_cast<size_t>(resolvedPeriod + 1)) {
            continue;
        }

        std::vector<double> rsiValues(closes.size());
        int outBegIdx = 0;
        int outNbElement = 0;
        const TA_RetCode ret = TA_RSI(
            0,
            static_cast<int>(closes.size()) - 1,
            closes.data(),
            resolvedPeriod,
            &outBegIdx,
            &outNbElement,
            rsiValues.data());
        if (ret != TA_SUCCESS || outNbElement <= 0) {
            continue;
        }

        const double rsiValue = lastFiniteValue(std::vector<double>(rsiValues.begin(), rsiValues.begin() + outNbElement));
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
    ensureTaLibInitialized();

    for (const auto& [symbol, closes] : allCloses) {
        if (closes.size() < static_cast<size_t>(resolvedSlow + resolvedSignal)) {
            continue;
    }

        std::vector<double> macdValues(closes.size());
        std::vector<double> signalValues(closes.size());
        std::vector<double> histValues(closes.size());
        int outBegIdx = 0;
        int outNbElement = 0;
        const TA_RetCode ret = TA_MACD(
            0,
            static_cast<int>(closes.size()) - 1,
            closes.data(),
            resolvedFast,
            resolvedSlow,
            resolvedSignal,
            &outBegIdx,
            &outNbElement,
            macdValues.data(),
            signalValues.data(),
            histValues.data());
        if (ret != TA_SUCCESS || outNbElement <= 0) {
            continue;
        }

        const double histogramValue = lastFiniteValue(std::vector<double>(histValues.begin(), histValues.begin() + outNbElement));
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
    ensureTaLibInitialized();

    for (const auto& [symbol, closes] : allCloses) {
        if (closes.size() < static_cast<size_t>(resolvedPeriod)) {
            continue;
        }

        std::vector<double> maValues(closes.size());
        int outBegIdx = 0;
        int outNbElement = 0;
        const TA_RetCode ret = TA_MA(
            0,
            static_cast<int>(closes.size()) - 1,
            closes.data(),
            resolvedPeriod,
            TA_MAType_SMA,
            &outBegIdx,
            &outNbElement,
            maValues.data());
        if (ret != TA_SUCCESS || outNbElement <= 0) {
            continue;
        }

        const double maValue = lastFiniteValue(std::vector<double>(maValues.begin(), maValues.begin() + outNbElement));
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
    ensureTaLibInitialized();

    for (const auto& [symbol, closes] : allCloses) {
        if (closes.size() < static_cast<size_t>(resolvedPeriod)) {
            continue;
    }

        std::vector<double> emaValues(closes.size());
        int outBegIdx = 0;
        int outNbElement = 0;
        const TA_RetCode ret = TA_EMA(
            0,
            static_cast<int>(closes.size()) - 1,
            closes.data(),
            resolvedPeriod,
            &outBegIdx,
            &outNbElement,
            emaValues.data());
        if (ret != TA_SUCCESS || outNbElement <= 0) {
            continue;
        }

        const double emaValue = lastFiniteValue(std::vector<double>(emaValues.begin(), emaValues.begin() + outNbElement));
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
    ensureTaLibInitialized();

    for (const auto& [symbol, closes] : allCloses) {
        if (closes.size() < static_cast<size_t>(resolvedPeriod)) {
            continue;
        }

        std::vector<double> upperValues(closes.size());
        std::vector<double> middleValues(closes.size());
        std::vector<double> lowerValues(closes.size());
        int outBegIdx = 0;
        int outNbElement = 0;
        const TA_RetCode ret = TA_BBANDS(
            0,
            static_cast<int>(closes.size()) - 1,
            closes.data(),
            resolvedPeriod,
            stdMultiplier,
            stdMultiplier,
            TA_MAType_SMA,
            &outBegIdx,
            &outNbElement,
            upperValues.data(),
            middleValues.data(),
            lowerValues.data());
        if (ret != TA_SUCCESS || outNbElement <= 0) {
            continue;
        }

        const double upperValue = lastFiniteValue(std::vector<double>(upperValues.begin(), upperValues.begin() + outNbElement));
        const double middleValue = lastFiniteValue(std::vector<double>(middleValues.begin(), middleValues.begin() + outNbElement));
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
    ensureTaLibInitialized();

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

        std::vector<double> slowKValues(seriesLength);
        std::vector<double> slowDValues(seriesLength);
        int outBegIdx = 0;
        int outNbElement = 0;
        const TA_RetCode ret = TA_STOCH(
            0,
            static_cast<int>(seriesLength) - 1,
            highs.data(),
            lows.data(),
            closes.data(),
            resolvedWindow,
            resolvedKPeriod,
            TA_MAType_SMA,
            resolvedDPeriod,
            TA_MAType_SMA,
            &outBegIdx,
            &outNbElement,
            slowKValues.data(),
            slowDValues.data());
        if (ret != TA_SUCCESS || outNbElement <= 0) {
            continue;
        }

        const double slowK = lastFiniteValue(std::vector<double>(slowKValues.begin(), slowKValues.begin() + outNbElement));
        const double slowD = lastFiniteValue(std::vector<double>(slowDValues.begin(), slowDValues.begin() + outNbElement));
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
    ensureTaLibInitialized();

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

        std::vector<double> atrValues(seriesLength);
        int outBegIdx = 0;
        int outNbElement = 0;
        const TA_RetCode ret = TA_ATR(
            0,
            static_cast<int>(seriesLength) - 1,
            highs.data(),
            lows.data(),
            closes.data(),
            resolvedWindow,
            &outBegIdx,
            &outNbElement,
            atrValues.data());
        if (ret != TA_SUCCESS || outNbElement <= 0) {
            continue;
        }

        const double atrValue = lastFiniteValue(std::vector<double>(atrValues.begin(), atrValues.begin() + outNbElement));
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

    ensureTaLibInitialized();

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

        std::vector<double> volumeSums(seriesLength);
        std::vector<double> priceVolumeSums(seriesLength);
        int outBegIdx = 0;
        int outNbElement = 0;
        const int period = static_cast<int>(seriesLength);
        const TA_RetCode volumeRet = TA_SUM(
            0,
            static_cast<int>(seriesLength) - 1,
            volumes.data(),
            period,
            &outBegIdx,
            &outNbElement,
            volumeSums.data());
        if (volumeRet != TA_SUCCESS || outNbElement <= 0) {
            continue;
        }

        int priceOutBegIdx = 0;
        int priceOutNbElement = 0;
        const TA_RetCode priceRet = TA_SUM(
            0,
            static_cast<int>(seriesLength) - 1,
            priceVolumeSeries.data(),
            period,
            &priceOutBegIdx,
            &priceOutNbElement,
            priceVolumeSums.data());
        if (priceRet != TA_SUCCESS || priceOutNbElement <= 0) {
            continue;
        }

        const double volumeSum = lastFiniteValue(std::vector<double>(volumeSums.begin(), volumeSums.begin() + outNbElement));
        const double priceVolumeSum = lastFiniteValue(std::vector<double>(priceVolumeSums.begin(), priceVolumeSums.begin() + priceOutNbElement));
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
    ensureTaLibInitialized();

    for (const auto& [symbol, volumes] : allVolumes) {
        if (volumes.size() < static_cast<size_t>(resolvedPeriod)) {
            continue;
        }

        std::vector<double> meanValues(volumes.size());
        int outBegIdx = 0;
        int outNbElement = 0;
        const TA_RetCode ret = TA_MA(
            0,
            static_cast<int>(volumes.size()) - 1,
            volumes.data(),
            resolvedPeriod,
            TA_MAType_SMA,
            &outBegIdx,
            &outNbElement,
            meanValues.data());
        if (ret != TA_SUCCESS || outNbElement <= 0) {
            continue;
        }

        const double meanValue = lastFiniteValue(std::vector<double>(meanValues.begin(), meanValues.begin() + outNbElement));
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
    ensureTaLibInitialized();

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

        std::vector<double> obvValues(seriesLength);
        int outBegIdx = 0;
        int outNbElement = 0;
        const TA_RetCode ret = TA_OBV(
            0,
            static_cast<int>(seriesLength) - 1,
            closes.data(),
            volumes.data(),
            &outBegIdx,
            &outNbElement,
            obvValues.data());
        if (ret != TA_SUCCESS || outNbElement <= 0) {
            continue;
        }

        const double obvValue = lastFiniteValue(std::vector<double>(obvValues.begin(), obvValues.begin() + outNbElement));
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
    ensureTaLibInitialized();

    for (const auto& [symbol, values] : allValues) {
        if (values.size() < static_cast<size_t>(resolvedWindow)) {
            continue;
        }

        std::vector<double> meanValues(values.size());
        std::vector<double> stdDevValues(values.size());
        int outBegIdx = 0;
        int outNbElement = 0;
        const TA_RetCode meanRet = TA_MA(
            0,
            static_cast<int>(values.size()) - 1,
            values.data(),
            resolvedWindow,
            TA_MAType_SMA,
            &outBegIdx,
            &outNbElement,
            meanValues.data());
        if (meanRet != TA_SUCCESS || outNbElement <= 0) {
            continue;
        }

        int stdOutBegIdx = 0;
        int stdOutNbElement = 0;
        const TA_RetCode stdRet = TA_STDDEV(
            0,
            static_cast<int>(values.size()) - 1,
            values.data(),
            resolvedWindow,
            1.0,
            &stdOutBegIdx,
            &stdOutNbElement,
            stdDevValues.data());
        if (stdRet != TA_SUCCESS || stdOutNbElement <= 0) {
            continue;
        }

        const double meanValue = lastFiniteValue(std::vector<double>(meanValues.begin(), meanValues.begin() + outNbElement));
        const double stdValue = lastFiniteValue(std::vector<double>(stdDevValues.begin(), stdDevValues.begin() + stdOutNbElement));
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