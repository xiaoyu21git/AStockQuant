#include "batch_technical_indicators.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace factor {

namespace {

struct SingleSeriesBatch
{
    std::vector<std::string> symbols;
    Eigen::MatrixXd values;
};

struct PairSeriesBatch
{
    std::vector<std::string> symbols;
    Eigen::MatrixXd first;
    Eigen::MatrixXd second;
};

struct TripleSeriesBatch
{
    std::vector<std::string> symbols;
    Eigen::MatrixXd first;
    Eigen::MatrixXd second;
    Eigen::MatrixXd third;
};

double clampScore(double value)
{
    if (!std::isfinite(value)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return std::clamp(value, -1.0, 1.0);
}

std::unordered_map<std::string, double> buildScoreMap(const std::vector<std::string>& symbols,
                                                      const Eigen::VectorXd& scores)
{
    std::unordered_map<std::string, double> result;
    const int rowCount = (std::min)(static_cast<int>(symbols.size()), static_cast<int>(scores.size()));
    for (int row = 0; row < rowCount; ++row) {
        const double score = scores(row);
        if (std::isfinite(score)) {
            result.emplace(symbols[static_cast<size_t>(row)], score);
        }
    }
    return result;
}

void applyEma(Eigen::MatrixXd& series, double alpha)
{
    if (series.cols() < 2) {
        return;
    }

    const double beta = 1.0 - alpha;
    for (int column = 1; column < series.cols(); ++column) {
        series.col(column) = alpha * series.col(column) + beta * series.col(column - 1);
    }
}

SingleSeriesBatch collectSingleSeries(const std::unordered_map<std::string, std::vector<double>>& input,
                                      size_t minimumLength)
{
    SingleSeriesBatch batch;
    size_t commonLength = std::numeric_limits<size_t>::max();

    for (const auto& [symbol, values] : input) {
        if (values.size() < minimumLength) {
            continue;
        }
        batch.symbols.push_back(symbol);
        commonLength = (std::min)(commonLength, values.size());
    }

    if (batch.symbols.empty() || commonLength == std::numeric_limits<size_t>::max()) {
        return batch;
    }

    batch.values.resize(static_cast<int>(batch.symbols.size()), static_cast<int>(commonLength));
    for (int row = 0; row < batch.values.rows(); ++row) {
        const auto& values = input.at(batch.symbols[static_cast<size_t>(row)]);
        const size_t offset = values.size() - commonLength;
        for (int column = 0; column < batch.values.cols(); ++column) {
            batch.values(row, column) = values[offset + static_cast<size_t>(column)];
        }
    }

    return batch;
}

PairSeriesBatch collectPairSeries(const std::unordered_map<std::string, std::vector<double>>& firstInput,
                                  const std::unordered_map<std::string, std::vector<double>>& secondInput,
                                  size_t minimumLength)
{
    PairSeriesBatch batch;
    size_t commonLength = std::numeric_limits<size_t>::max();

    for (const auto& [symbol, firstValues] : firstInput) {
        const auto secondIt = secondInput.find(symbol);
        if (secondIt == secondInput.end()) {
            continue;
        }

        const auto& secondValues = secondIt->second;
        if (firstValues.size() < minimumLength || secondValues.size() < minimumLength) {
            continue;
        }

        batch.symbols.push_back(symbol);
        commonLength = (std::min)({commonLength, firstValues.size(), secondValues.size()});
    }

    if (batch.symbols.empty() || commonLength == std::numeric_limits<size_t>::max()) {
        return batch;
    }

    batch.first.resize(static_cast<int>(batch.symbols.size()), static_cast<int>(commonLength));
    batch.second.resize(static_cast<int>(batch.symbols.size()), static_cast<int>(commonLength));

    for (int row = 0; row < batch.first.rows(); ++row) {
        const auto& symbol = batch.symbols[static_cast<size_t>(row)];
        const auto& firstValues = firstInput.at(symbol);
        const auto& secondValues = secondInput.at(symbol);
        const size_t firstOffset = firstValues.size() - commonLength;
        const size_t secondOffset = secondValues.size() - commonLength;
        for (int column = 0; column < batch.first.cols(); ++column) {
            batch.first(row, column) = firstValues[firstOffset + static_cast<size_t>(column)];
            batch.second(row, column) = secondValues[secondOffset + static_cast<size_t>(column)];
        }
    }

    return batch;
}

TripleSeriesBatch collectTripleSeries(const std::unordered_map<std::string, std::vector<double>>& firstInput,
                                      const std::unordered_map<std::string, std::vector<double>>& secondInput,
                                      const std::unordered_map<std::string, std::vector<double>>& thirdInput,
                                      size_t minimumLength)
{
    TripleSeriesBatch batch;
    size_t commonLength = std::numeric_limits<size_t>::max();

    for (const auto& [symbol, firstValues] : firstInput) {
        const auto secondIt = secondInput.find(symbol);
        if (secondIt == secondInput.end()) {
            continue;
        }
        const auto thirdIt = thirdInput.find(symbol);
        if (thirdIt == thirdInput.end()) {
            continue;
        }

        const auto& secondValues = secondIt->second;
        const auto& thirdValues = thirdIt->second;
        if (firstValues.size() < minimumLength || secondValues.size() < minimumLength || thirdValues.size() < minimumLength) {
            continue;
        }

        batch.symbols.push_back(symbol);
        commonLength = (std::min)({commonLength, firstValues.size(), secondValues.size(), thirdValues.size()});
    }

    if (batch.symbols.empty() || commonLength == std::numeric_limits<size_t>::max()) {
        return batch;
    }

    batch.first.resize(static_cast<int>(batch.symbols.size()), static_cast<int>(commonLength));
    batch.second.resize(static_cast<int>(batch.symbols.size()), static_cast<int>(commonLength));
    batch.third.resize(static_cast<int>(batch.symbols.size()), static_cast<int>(commonLength));

    for (int row = 0; row < batch.first.rows(); ++row) {
        const auto& symbol = batch.symbols[static_cast<size_t>(row)];
        const auto& firstValues = firstInput.at(symbol);
        const auto& secondValues = secondInput.at(symbol);
        const auto& thirdValues = thirdInput.at(symbol);
        const size_t firstOffset = firstValues.size() - commonLength;
        const size_t secondOffset = secondValues.size() - commonLength;
        const size_t thirdOffset = thirdValues.size() - commonLength;
        for (int column = 0; column < batch.first.cols(); ++column) {
            const size_t index = static_cast<size_t>(column);
            batch.first(row, column) = firstValues[firstOffset + index];
            batch.second(row, column) = secondValues[secondOffset + index];
            batch.third(row, column) = thirdValues[thirdOffset + index];
        }
    }

    return batch;
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
    const SingleSeriesBatch batch = collectSingleSeries(allCloses, 2);
    if (batch.symbols.empty() || batch.values.cols() < 2) {
        return results;
    }

    const int totalColumns = static_cast<int>(batch.values.cols());
    const int diffColumns = totalColumns - 1;
    const int windowColumns = (std::min<int>)(resolvedPeriod, diffColumns);
    const Eigen::MatrixXd diffs = batch.values.rightCols(diffColumns) - batch.values.leftCols(diffColumns);
    const Eigen::MatrixXd gains = diffs.cwiseMax(0.0);
    const Eigen::MatrixXd losses = (-diffs).cwiseMax(0.0);
    const Eigen::VectorXd avgGain = gains.rightCols(windowColumns).rowwise().mean();
    const Eigen::VectorXd avgLoss = losses.rightCols(windowColumns).rowwise().mean();

    Eigen::VectorXd scores(batch.values.rows());
    for (int row = 0; row < scores.size(); ++row) {
        const double gain = avgGain(row);
        const double loss = avgLoss(row);
        double score = std::numeric_limits<double>::quiet_NaN();
        if (std::isfinite(gain) && std::isfinite(loss)) {
            const double rsi = loss <= 1e-12
                ? 100.0
                : 100.0 - (100.0 / (1.0 + gain / loss));
            score = std::clamp((rsi - 50.0) / 50.0, -1.0, 1.0);
        }
        scores(row) = score;
    }

    return buildScoreMap(batch.symbols, scores);
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
    const SingleSeriesBatch batch = collectSingleSeries(allCloses, 2);
    if (batch.symbols.empty() || batch.values.cols() < 2) {
        return results;
    }

    const double fastAlpha = 2.0 / (static_cast<double>(resolvedFast) + 1.0);
    const double slowAlpha = 2.0 / (static_cast<double>(resolvedSlow) + 1.0);
    const double signalAlpha = 2.0 / (static_cast<double>(resolvedSignal) + 1.0);

    Eigen::MatrixXd fastEma = batch.values;
    Eigen::MatrixXd slowEma = batch.values;
    applyEma(fastEma, fastAlpha);
    applyEma(slowEma, slowAlpha);

    Eigen::MatrixXd macdLine = fastEma - slowEma;
    Eigen::MatrixXd signalLine = macdLine;
    applyEma(signalLine, signalAlpha);

    const Eigen::VectorXd histogram = macdLine.col(macdLine.cols() - 1) - signalLine.col(signalLine.cols() - 1);
    const Eigen::VectorXd scale = batch.values.col(batch.values.cols() - 1).cwiseAbs().cwiseMax(1e-6);

    Eigen::VectorXd scores(batch.values.rows());
    for (int row = 0; row < scores.size(); ++row) {
        const double histogramValue = histogram(row);
        const double scaleValue = scale(row);
        scores(row) = std::isfinite(histogramValue) && std::isfinite(scaleValue)
            ? std::clamp(std::tanh(histogramValue / scaleValue), -1.0, 1.0)
            : std::numeric_limits<double>::quiet_NaN();
    }

    return buildScoreMap(batch.symbols, scores);
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
    const SingleSeriesBatch batch = collectSingleSeries(allCloses, 2);
    if (batch.symbols.empty() || batch.values.cols() < 2) {
        return results;
    }

    const int windowColumns = (std::min<int>)(resolvedPeriod, static_cast<int>(batch.values.cols()));
    const Eigen::MatrixXd tail = batch.values.rightCols(windowColumns);
    const Eigen::VectorXd mean = tail.rowwise().mean();
    const Eigen::VectorXd last = batch.values.col(batch.values.cols() - 1);

    Eigen::VectorXd scores(batch.values.rows());
    for (int row = 0; row < scores.size(); ++row) {
        const double meanValue = mean(row);
        const double lastValue = last(row);
        scores(row) = (std::isfinite(meanValue) && std::isfinite(lastValue))
            ? std::clamp(std::tanh((lastValue - meanValue) / (std::max)(1e-6, std::abs(meanValue))), -1.0, 1.0)
            : std::numeric_limits<double>::quiet_NaN();
    }

    return buildScoreMap(batch.symbols, scores);
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
    const SingleSeriesBatch batch = collectSingleSeries(allCloses, 2);
    if (batch.symbols.empty() || batch.values.cols() < 2) {
        return results;
    }

    const double alpha = 2.0 / (static_cast<double>(resolvedPeriod) + 1.0);
    Eigen::MatrixXd ema = batch.values;
    applyEma(ema, alpha);

    const Eigen::VectorXd last = batch.values.col(batch.values.cols() - 1);
    const Eigen::VectorXd lastEma = ema.col(ema.cols() - 1);

    Eigen::VectorXd scores(batch.values.rows());
    for (int row = 0; row < scores.size(); ++row) {
        const double closeValue = last(row);
        const double emaValue = lastEma(row);
        scores(row) = (std::isfinite(closeValue) && std::isfinite(emaValue))
            ? std::clamp(std::tanh((closeValue - emaValue) / (std::max)(1e-6, std::abs(emaValue))), -1.0, 1.0)
            : std::numeric_limits<double>::quiet_NaN();
    }

    return buildScoreMap(batch.symbols, scores);
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
    const SingleSeriesBatch batch = collectSingleSeries(allCloses, 2);
    if (batch.symbols.empty() || batch.values.cols() < 2) {
        return results;
    }

    const int windowColumns = (std::min<int>)(resolvedPeriod, static_cast<int>(batch.values.cols()));
    const Eigen::MatrixXd tail = batch.values.rightCols(windowColumns);
    const Eigen::VectorXd mean = tail.rowwise().mean();
    Eigen::MatrixXd centered = tail;
    centered.colwise() -= mean;
    const Eigen::VectorXd variance = centered.array().square().rowwise().mean();
    const Eigen::VectorXd deviation = variance.array().sqrt();
    const Eigen::VectorXd last = batch.values.col(batch.values.cols() - 1);

    Eigen::VectorXd scores(batch.values.rows());
    for (int row = 0; row < scores.size(); ++row) {
        const double meanValue = mean(row);
        const double deviationValue = deviation(row);
        const double lastValue = last(row);
        if (!std::isfinite(meanValue) || !std::isfinite(deviationValue) || !std::isfinite(lastValue)) {
            scores(row) = std::numeric_limits<double>::quiet_NaN();
            continue;
        }

        const double scale = (std::max)(1e-6, deviationValue * (std::max)(1.0, stdMultiplier));
        scores(row) = std::clamp(std::tanh((lastValue - meanValue) / scale), -1.0, 1.0);
    }

    return buildScoreMap(batch.symbols, scores);
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
    const TripleSeriesBatch batch = collectTripleSeries(allHighs, allLows, allCloses, 2);
    if (batch.symbols.empty() || batch.first.cols() < 2) {
        return results;
    }

    const int windowColumns = (std::min<int>)(resolvedWindow, static_cast<int>(batch.first.cols()));
    const Eigen::MatrixXd highsTail = batch.first.rightCols(windowColumns);
    const Eigen::MatrixXd lowsTail = batch.second.rightCols(windowColumns);
    const Eigen::MatrixXd closesTail = batch.third.rightCols(windowColumns);
    const Eigen::VectorXd highestHigh = highsTail.rowwise().maxCoeff();
    const Eigen::VectorXd lowestLow = lowsTail.rowwise().minCoeff();
    const Eigen::VectorXd lastClose = closesTail.col(closesTail.cols() - 1);

    const double kAlpha = 1.0 / static_cast<double>((std::max)(2, kPeriod));
    const double dAlpha = 1.0 / static_cast<double>((std::max)(2, dPeriod));

    Eigen::VectorXd scores(batch.first.rows());
    for (int row = 0; row < scores.size(); ++row) {
        const double high = highestHigh(row);
        const double low = lowestLow(row);
        const double close = lastClose(row);
        if (!std::isfinite(high) || !std::isfinite(low) || !std::isfinite(close) || high <= low) {
            scores(row) = std::numeric_limits<double>::quiet_NaN();
            continue;
        }

        const double rsv = 100.0 * (close - low) / (high - low);
        const double kValue = 50.0 + (rsv - 50.0) * kAlpha;
        const double dValue = 50.0 + (kValue - 50.0) * dAlpha;
        const double jValue = 3.0 * kValue - 2.0 * dValue;
        scores(row) = std::clamp((jValue - 50.0) / 50.0, -1.0, 1.0);
    }

    return buildScoreMap(batch.symbols, scores);
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
    const TripleSeriesBatch batch = collectTripleSeries(allHighs, allLows, allCloses, 2);
    if (batch.symbols.empty() || batch.first.cols() < 2) {
        return results;
    }

    const int windowColumns = (std::min<int>)(resolvedWindow + 1, static_cast<int>(batch.first.cols()));
    if (windowColumns < 2) {
        return results;
    }

    const Eigen::MatrixXd highsTail = batch.first.rightCols(windowColumns);
    const Eigen::MatrixXd lowsTail = batch.second.rightCols(windowColumns);
    const Eigen::MatrixXd closesTail = batch.third.rightCols(windowColumns);

    const Eigen::MatrixXd currentHighs = highsTail.rightCols(windowColumns - 1);
    const Eigen::MatrixXd currentLows = lowsTail.rightCols(windowColumns - 1);
    const Eigen::MatrixXd previousCloses = closesTail.leftCols(windowColumns - 1);

    const Eigen::MatrixXd range1 = currentHighs - currentLows;
    const Eigen::MatrixXd range2 = (currentHighs - previousCloses).array().abs().matrix();
    const Eigen::MatrixXd range3 = (currentLows - previousCloses).array().abs().matrix();

    const Eigen::MatrixXd trueRanges = range1.cwiseMax(range2).cwiseMax(range3);
    const Eigen::VectorXd atr = trueRanges.rowwise().mean();
    const Eigen::VectorXd lastClose = closesTail.col(closesTail.cols() - 1);

    Eigen::VectorXd scores(batch.first.rows());
    for (int row = 0; row < scores.size(); ++row) {
        const double atrValue = atr(row);
        const double closeValue = lastClose(row);
        scores(row) = (std::isfinite(atrValue) && std::isfinite(closeValue))
            ? std::clamp(-atrValue / (std::max)(1e-6, std::abs(closeValue)), -1.0, 1.0)
            : std::numeric_limits<double>::quiet_NaN();
    }

    return buildScoreMap(batch.symbols, scores);
}

std::unordered_map<std::string, double> batchCalculateVwap(
    const std::unordered_map<std::string, std::vector<double>>& allCloses,
    const std::unordered_map<std::string, std::vector<double>>& allVolumes)
{
    std::unordered_map<std::string, double> results;
    if (allCloses.empty() || allVolumes.empty()) {
        return results;
    }

    const PairSeriesBatch batch = collectPairSeries(allCloses, allVolumes, 2);
    if (batch.symbols.empty() || batch.first.cols() < 2) {
        return results;
    }

    const Eigen::VectorXd volumeSum = batch.second.rowwise().sum();
    const Eigen::VectorXd priceVolumeSum = (batch.first.array() * batch.second.array()).rowwise().sum();
    const Eigen::VectorXd lastClose = batch.first.col(batch.first.cols() - 1);

    Eigen::VectorXd scores(batch.first.rows());
    for (int row = 0; row < scores.size(); ++row) {
        const double volume = volumeSum(row);
        const double numerator = priceVolumeSum(row);
        const double closeValue = lastClose(row);
        if (!std::isfinite(volume) || !std::isfinite(numerator) || volume <= 1e-12 || !std::isfinite(closeValue)) {
            scores(row) = std::numeric_limits<double>::quiet_NaN();
            continue;
        }

        const double vwap = numerator / volume;
        scores(row) = std::clamp(std::tanh((closeValue - vwap) / (std::max)(1e-6, std::abs(vwap))), -1.0, 1.0);
    }

    return buildScoreMap(batch.symbols, scores);
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
    const SingleSeriesBatch batch = collectSingleSeries(allVolumes, 2);
    if (batch.symbols.empty() || batch.values.cols() < 2) {
        return results;
    }

    const int windowColumns = (std::min<int>)(resolvedPeriod, static_cast<int>(batch.values.cols()));
    const Eigen::MatrixXd tail = batch.values.rightCols(windowColumns);
    const Eigen::VectorXd mean = tail.rowwise().mean();
    const Eigen::VectorXd last = batch.values.col(batch.values.cols() - 1);

    Eigen::VectorXd scores(batch.values.rows());
    for (int row = 0; row < scores.size(); ++row) {
        const double meanValue = mean(row);
        const double lastValue = last(row);
        scores(row) = (std::isfinite(meanValue) && std::isfinite(lastValue))
            ? std::clamp(std::tanh((lastValue - meanValue) / (std::max)(1e-6, std::abs(meanValue))), -1.0, 1.0)
            : std::numeric_limits<double>::quiet_NaN();
    }

    return buildScoreMap(batch.symbols, scores);
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
    const PairSeriesBatch batch = collectPairSeries(allCloses, allVolumes, static_cast<size_t>(resolvedPeriod + 1));
    if (batch.symbols.empty() || batch.first.cols() < 2) {
        return results;
    }

    const int windowColumns = (std::min<int>)(resolvedPeriod + 1, batch.first.cols());
    const Eigen::MatrixXd closeTail = batch.first.rightCols(windowColumns);
    const Eigen::MatrixXd volumeTail = batch.second.rightCols(windowColumns);
    const int diffColumns = closeTail.cols() - 1;
    const Eigen::MatrixXd closeDiffs = closeTail.rightCols(diffColumns) - closeTail.leftCols(diffColumns);
    const Eigen::MatrixXd signedDirection = closeDiffs.unaryExpr([](double delta) {
        if (delta > 0.0) {
            return 1.0;
        }
        if (delta < 0.0) {
            return -1.0;
        }
        return 0.0;
    });
    const Eigen::MatrixXd signedVolumes = signedDirection.cwiseProduct(volumeTail.rightCols(diffColumns));
    const Eigen::VectorXd obv = signedVolumes.rowwise().sum();
    const Eigen::VectorXd averageVolume = volumeTail.rowwise().mean();
    const Eigen::VectorXd scale = averageVolume.array() * static_cast<double>(volumeTail.cols());

    Eigen::VectorXd scores(batch.first.rows());
    for (int row = 0; row < scores.size(); ++row) {
        const double obvValue = obv(row);
        const double scaleValue = scale(row);
        scores(row) = (std::isfinite(obvValue) && std::isfinite(scaleValue))
            ? std::clamp(std::tanh(obvValue / (std::max)(1e-6, std::abs(scaleValue))), -1.0, 1.0)
            : std::numeric_limits<double>::quiet_NaN();
    }

    return buildScoreMap(batch.symbols, scores);
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
    const SingleSeriesBatch batch = collectSingleSeries(allValues, static_cast<size_t>(resolvedWindow));
    if (batch.symbols.empty() || batch.values.cols() < 2) {
        return results;
    }

    const int windowColumns = (std::min<int>)(resolvedWindow, batch.values.cols());
    const Eigen::MatrixXd turnoverWindow = batch.values.rightCols(windowColumns);
    const Eigen::VectorXd mean = turnoverWindow.rowwise().mean();
    Eigen::MatrixXd centered = turnoverWindow;
    centered.colwise() -= mean;
    const Eigen::VectorXd variance = centered.array().square().rowwise().mean();
    const Eigen::VectorXd stdDev = variance.array().sqrt();

    Eigen::VectorXd scores(turnoverWindow.rows());
    for (int row = 0; row < scores.size(); ++row) {
        const double meanValue = mean(row);
        const double stdValue = stdDev(row);
        if (!std::isfinite(meanValue) || !std::isfinite(stdValue)) {
            scores(row) = std::numeric_limits<double>::quiet_NaN();
            continue;
        }

        const double coefficient = std::abs(meanValue) <= 1e-12 ? std::numeric_limits<double>::infinity() : stdValue / std::abs(meanValue);
        const double normalized = 1.0 - std::clamp(coefficient, 0.0, 2.0) / 2.0;
        scores(row) = std::clamp(normalized * 2.0 - 1.0, -1.0, 1.0);
    }

    return buildScoreMap(batch.symbols, scores);
}

} // namespace factor