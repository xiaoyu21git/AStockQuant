#include "batch_technical_indicators.h"

#include <ta_libc.h>


#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace factor {

namespace {

void ensureTaLibInitialized()
{
    static std::once_flag initFlag;
    static TA_RetCode initResult = TA_BAD_PARAM;
    std::call_once(initFlag, []() {
        initResult = TA_Initialize();
    });
    if (initResult != TA_SUCCESS) {
        throw std::runtime_error("TA-Lib initialization failed; supported technical indicators cannot fall back to local implementations.");
    }
}

double nanValue();

double taLastOutput(const std::vector<double>& output, int outBegIdx, int outNBElement)
{
    if (outBegIdx < 0 || outNBElement <= 0) {
        return nanValue();
    }
    const size_t lastIndex = static_cast<size_t>(outNBElement - 1);
    if (lastIndex >= output.size()) {
        return nanValue();
    }
    return output[lastIndex];
}

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

double meanTailValue(const std::vector<double>& values, size_t tailLength)
{
    if (values.empty() || tailLength == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const size_t start = values.size() > tailLength ? values.size() - tailLength : 0;
    std::vector<double> finiteTailValues;
    finiteTailValues.reserve(values.size() - start);
    for (size_t index = start; index < values.size(); ++index) {
        if (!std::isfinite(values[index])) {
            continue;
        }
        finiteTailValues.push_back(values[index]);
    }

    if (finiteTailValues.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    ensureTaLibInitialized();

    if (finiteTailValues.size() == 1) {
        return finiteTailValues.front();
    }

    std::vector<double> output(finiteTailValues.size(), nanValue());
    int outBegIdx = 0;
    int outNBElement = 0;
    const TA_RetCode ret = TA_SMA(0,
                                  static_cast<int>(finiteTailValues.size() - 1),
                                  finiteTailValues.data(),
                                  static_cast<int>(finiteTailValues.size()),
                                  &outBegIdx,
                                  &outNBElement,
                                  output.data());
    if (ret != TA_SUCCESS) {
        throw std::runtime_error("TA_SMA 计算尾部均值失败");
    }

    return taLastOutput(output, outBegIdx, outNBElement);
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

        std::vector<double> output(closes.size(), nanValue());
        int outBegIdx = 0;
        int outNBElement = 0;
        const TA_RetCode ret = TA_RSI(0,
                                      static_cast<int>(closes.size() - 1),
                                      closes.data(),
                                      resolvedPeriod,
                                      &outBegIdx,
                                      &outNBElement,
                                      output.data());
        if (ret != TA_SUCCESS) {
            continue;
        }

        const double rsiValue = taLastOutput(output, outBegIdx, outNBElement);
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

        std::vector<double> macdValues(closes.size(), nanValue());
        std::vector<double> signalValues(closes.size(), nanValue());
        std::vector<double> histogramValues(closes.size(), nanValue());
        int outBegIdx = 0;
        int outNBElement = 0;
        const TA_RetCode ret = TA_MACD(0,
                                       static_cast<int>(closes.size() - 1),
                                       closes.data(),
                                       resolvedFast,
                                       resolvedSlow,
                                       resolvedSignal,
                                       &outBegIdx,
                                       &outNBElement,
                                       macdValues.data(),
                                       signalValues.data(),
                                       histogramValues.data());
        if (ret != TA_SUCCESS) {
            continue;
        }

        const double histogramValue = taLastOutput(histogramValues, outBegIdx, outNBElement);
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

        std::vector<double> output(closes.size(), nanValue());
        int outBegIdx = 0;
        int outNBElement = 0;
        const TA_RetCode ret = TA_MA(0,
                                     static_cast<int>(closes.size() - 1),
                                     closes.data(),
                                     resolvedPeriod,
                                     TA_MAType_SMA,
                                     &outBegIdx,
                                     &outNBElement,
                                     output.data());
        if (ret != TA_SUCCESS) {
            continue;
        }

        const double maValue = taLastOutput(output, outBegIdx, outNBElement);
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

        std::vector<double> output(closes.size(), nanValue());
        int outBegIdx = 0;
        int outNBElement = 0;
        const TA_RetCode ret = TA_EMA(0,
                                      static_cast<int>(closes.size() - 1),
                                      closes.data(),
                                      resolvedPeriod,
                                      &outBegIdx,
                                      &outNBElement,
                                      output.data());
        if (ret != TA_SUCCESS) {
            continue;
        }

        const double emaValue = taLastOutput(output, outBegIdx, outNBElement);
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

        std::vector<double> upperValues(closes.size(), nanValue());
        std::vector<double> middleValues(closes.size(), nanValue());
        std::vector<double> lowerValues(closes.size(), nanValue());
        int outBegIdx = 0;
        int outNBElement = 0;
        const TA_RetCode ret = TA_BBANDS(0,
                                         static_cast<int>(closes.size() - 1),
                                         closes.data(),
                                         resolvedPeriod,
                                         stdMultiplier,
                                         stdMultiplier,
                                         TA_MAType_SMA,
                                         &outBegIdx,
                                         &outNBElement,
                                         upperValues.data(),
                                         middleValues.data(),
                                         lowerValues.data());
        if (ret != TA_SUCCESS) {
            continue;
        }

        const double upperValue = taLastOutput(upperValues, outBegIdx, outNBElement);
        const double middleValue = taLastOutput(middleValues, outBegIdx, outNBElement);
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

        std::vector<double> slowKValues(seriesLength, nanValue());
        std::vector<double> slowDValues(seriesLength, nanValue());
        int outBegIdx = 0;
        int outNBElement = 0;
        const TA_RetCode ret = TA_STOCH(0,
                                        static_cast<int>(seriesLength - 1),
                                        highs.data(),
                                        lows.data(),
                                        closes.data(),
                                        resolvedWindow,
                                        resolvedKPeriod,
                                        TA_MAType_SMA,
                                        resolvedDPeriod,
                                        TA_MAType_SMA,
                                        &outBegIdx,
                                        &outNBElement,
                                        slowKValues.data(),
                                        slowDValues.data());
        if (ret != TA_SUCCESS) {
            continue;
        }

        const double slowK = taLastOutput(slowKValues, outBegIdx, outNBElement);
        const double slowD = taLastOutput(slowDValues, outBegIdx, outNBElement);
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

        std::vector<double> output(seriesLength, nanValue());
        int outBegIdx = 0;
        int outNBElement = 0;
        const TA_RetCode ret = TA_ATR(0,
                                      static_cast<int>(seriesLength - 1),
                                      highs.data(),
                                      lows.data(),
                                      closes.data(),
                                      resolvedWindow,
                                      &outBegIdx,
                                      &outNBElement,
                                      output.data());
        if (ret != TA_SUCCESS) {
            continue;
        }

        const double atrValue = taLastOutput(output, outBegIdx, outNBElement);
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

        std::vector<double> volumeSumSeries(seriesLength, nanValue());
        std::vector<double> priceVolumeSumSeries(seriesLength, nanValue());
        int volumeBegIdx = 0;
        int volumeNBElement = 0;
        int priceBegIdx = 0;
        int priceNBElement = 0;
        const TA_RetCode volumeRet = TA_SUM(0,
                                            static_cast<int>(seriesLength - 1),
                                            volumes.data(),
                                            static_cast<int>(seriesLength),
                                            &volumeBegIdx,
                                            &volumeNBElement,
                                            volumeSumSeries.data());
        const TA_RetCode priceRet = TA_SUM(0,
                                           static_cast<int>(seriesLength - 1),
                                           priceVolumeSeries.data(),
                                           static_cast<int>(seriesLength),
                                           &priceBegIdx,
                                           &priceNBElement,
                                           priceVolumeSumSeries.data());
        if (volumeRet != TA_SUCCESS || priceRet != TA_SUCCESS) {
            continue;
        }

        const double volumeSum = taLastOutput(volumeSumSeries, volumeBegIdx, volumeNBElement);
        const double priceVolumeSum = taLastOutput(priceVolumeSumSeries, priceBegIdx, priceNBElement);
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

        std::vector<double> output(volumes.size(), nanValue());
        int outBegIdx = 0;
        int outNBElement = 0;
        const TA_RetCode ret = TA_SMA(0,
                                      static_cast<int>(volumes.size() - 1),
                                      volumes.data(),
                                      resolvedPeriod,
                                      &outBegIdx,
                                      &outNBElement,
                                      output.data());
        if (ret != TA_SUCCESS) {
            continue;
        }

        const double meanValue = taLastOutput(output, outBegIdx, outNBElement);
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

        std::vector<double> output(seriesLength, nanValue());
        int outBegIdx = 0;
        int outNBElement = 0;
        const TA_RetCode ret = TA_OBV(0,
                                      static_cast<int>(seriesLength - 1),
                                      closes.data(),
                                      volumes.data(),
                                      &outBegIdx,
                                      &outNBElement,
                                      output.data());
        if (ret != TA_SUCCESS) {
            continue;
        }

        const double obvValue = taLastOutput(output, outBegIdx, outNBElement);
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

        std::vector<double> meanOutput(values.size(), nanValue());
        std::vector<double> stdOutput(values.size(), nanValue());
        int meanBegIdx = 0;
        int meanNBElement = 0;
        int stdBegIdx = 0;
        int stdNBElement = 0;
        const TA_RetCode meanRet = TA_SMA(0,
                                          static_cast<int>(values.size() - 1),
                                          values.data(),
                                          resolvedWindow,
                                          &meanBegIdx,
                                          &meanNBElement,
                                          meanOutput.data());
        const TA_RetCode stdRet = TA_STDDEV(0,
                                            static_cast<int>(values.size() - 1),
                                            values.data(),
                                            resolvedWindow,
                                            1.0,
                                            &stdBegIdx,
                                            &stdNBElement,
                                            stdOutput.data());
        if (meanRet != TA_SUCCESS || stdRet != TA_SUCCESS) {
            continue;
        }

        const double meanValue = taLastOutput(meanOutput, meanBegIdx, meanNBElement);
        const double stdValue = taLastOutput(stdOutput, stdBegIdx, stdNBElement);
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