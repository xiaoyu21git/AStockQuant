#include "domain/factor/include/FactorBacktestExecutor.h"
#include "domain/factor/include/FactorBacktestCachedBarUtils.h"
#include "domain/factor/include/FactorBacktestGroupingUtils.h"
#include "domain/factor/include/FactorBacktestIcUtils.h"

#include "infrastructure/include/database/QtMySQLDatabase.h"

#include <QDate>
#include <QDebug>
#include <QElapsedTimer>
#include <QString>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <future>
#include <set>
#include <limits>
#include <map>
#include <sstream>
#include <numeric>
#include <unordered_map>
#include <stdexcept>
#include <QDateTime>

namespace factor {

namespace {

using Row = astock::database::QueryResultRow;

class CachedRowFactorDataProvider final : public FactorDataProvider {
public:
    explicit CachedRowFactorDataProvider(const std::vector<factor::CachedMarketBar>& rows) {
        for (const auto& row : rows) {
            rowsByDate_[row.tradeDate][row.symbol] = row;
            auto& symbolRows = rowsBySymbol_[row.symbol];
            symbolRows.push_back(row);
            availableFields_.insert("close");
            for (const auto& [field, value] : row.numericFields) {
                (void)value;
                availableFields_.insert(field);
            }
        }

        for (auto& [symbol, symbolRows] : rowsBySymbol_) {
            std::sort(symbolRows.begin(), symbolRows.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.tradeDate < rhs.tradeDate;
            });
        }
    }

    bool hasField(const std::string& field) const override {
        return availableFields_.find(field) != availableFields_.end();
    }

    std::optional<double> getValue(const std::string& symbol,
                                   const std::string& date,
                                   const std::string& field) const override {
        auto dateIt = rowsByDate_.find(date);
        if (dateIt == rowsByDate_.end()) {
            return std::nullopt;
        }

        auto symbolIt = dateIt->second.find(symbol);
        if (symbolIt == dateIt->second.end()) {
            return std::nullopt;
        }

        return extractFieldValue(symbolIt->second, field);
    }

    std::vector<FactorDataPoint> getSeries(const std::string& symbol,
                                           const std::string& startDate,
                                           const std::string& endDate,
                                           const std::string& field) const override {
        std::vector<FactorDataPoint> series;
        auto symbolIt = rowsBySymbol_.find(symbol);
        if (symbolIt == rowsBySymbol_.end()) {
            return series;
        }

        for (const auto& row : symbolIt->second) {
            if (row.tradeDate < startDate || row.tradeDate > endDate) {
                continue;
            }

            const auto value = extractFieldValue(row, field);
            if (!value.has_value()) {
                continue;
            }

            series.push_back(FactorDataPoint{row.tradeDate, *value});
        }
        return series;
    }

    std::vector<std::string> getAvailableSymbols(const std::string& date) const override {
        std::vector<std::string> symbols;
        auto dateIt = rowsByDate_.find(date);
        if (dateIt == rowsByDate_.end()) {
            return symbols;
        }

        symbols.reserve(dateIt->second.size());
        for (const auto& [symbol, row] : dateIt->second) {
            (void)row;
            symbols.push_back(symbol);
        }
        std::sort(symbols.begin(), symbols.end());
        return symbols;
    }

    std::unordered_map<std::string, double> getCrossSection(const std::string& date,
                                                            const std::string& field,
                                                            const std::vector<std::string>& symbols = {}) const override {
        std::unordered_map<std::string, double> values;
        auto dateIt = rowsByDate_.find(date);
        if (dateIt == rowsByDate_.end()) {
            return values;
        }

        if (symbols.empty()) {
            values.reserve(dateIt->second.size());
            for (const auto& [symbol, row] : dateIt->second) {
                const auto value = extractFieldValue(row, field);
                if (value.has_value()) {
                    values.emplace(symbol, *value);
                }
            }
            return values;
        }

        values.reserve(symbols.size());
        for (const auto& symbol : symbols) {
            auto symbolIt = dateIt->second.find(symbol);
            if (symbolIt == dateIt->second.end()) {
                continue;
            }

            const auto value = extractFieldValue(symbolIt->second, field);
            if (value.has_value()) {
                values.emplace(symbol, *value);
            }
        }

        return values;
    }

private:
    static std::optional<double> extractFieldValue(const factor::CachedMarketBar& row, const std::string& field) {
        if (field == "close") {
            return std::isfinite(row.close) && row.close > 0.0 ? std::optional<double>(row.close) : std::nullopt;
        }

        auto fieldIt = row.numericFields.find(field);
        if (fieldIt == row.numericFields.end() || !std::isfinite(fieldIt->second)) {
            return std::nullopt;
        }
        return fieldIt->second;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, factor::CachedMarketBar>> rowsByDate_;
    std::unordered_map<std::string, std::vector<factor::CachedMarketBar>> rowsBySymbol_;
    std::unordered_set<std::string> availableFields_;
};

std::map<QString, QVariant> makeNamedParams(std::initializer_list<std::pair<QString, QVariant>> values)
{
    std::map<QString, QVariant> params;
    for (const auto& [key, value] : values) {
        params.emplace(key, value);
    }
    return params;
}

double annualizationFactorForPeriods(int forwardDays)
{
    return 252.0 / static_cast<double>((std::max)(1, forwardDays));
}

std::string buildRiskCacheSignature(const BacktestConfig& config)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6)
           << "sl" << config.stopLossRate
           << "_tp" << config.takeProfitRate
           << "_dd" << config.maxDrawdownLimit
           << "_dl" << config.maxDailyLoss
           << "_mp" << config.maxPositionPercent
           << "_te" << config.maxTotalExposure;
    return stream.str();
}

double calculateMaxDrawdown(const std::vector<double>& periodicReturns)
{
    if (periodicReturns.empty()) {
        return 0.0;
    }

    double cumulativeNetValue = 1.0;
    double peakNetValue = 1.0;
    double maxDrawdown = 0.0;
    for (double periodicReturn : periodicReturns) {
        cumulativeNetValue *= (1.0 + periodicReturn);
        peakNetValue = (std::max)(peakNetValue, cumulativeNetValue);
        if (peakNetValue <= 0.0) {
            continue;
        }

        const double drawdown = (peakNetValue - cumulativeNetValue) / peakNetValue;
        maxDrawdown = (std::max)(maxDrawdown, drawdown);
    }

    return maxDrawdown;
}

struct RiskControlResult {
    std::vector<double> adjustedReturns;
    int triggeredCount = 0;
    std::string summary;
};

RiskControlResult applyRiskControls(const std::vector<double>& periodicReturns,
                                    const BacktestConfig& config)
{
    RiskControlResult result;
    result.adjustedReturns.reserve(periodicReturns.size());

    double cumulativeNetValue = 1.0;
    double peakNetValue = 1.0;
    int stopLossCount = 0;
    int takeProfitCount = 0;
    int dailyLossCount = 0;
    int drawdownBreakCount = 0;
    int positionLimitCount = 0;

    for (double periodicReturn : periodicReturns) {
        double adjustedReturn = periodicReturn;

        // 单只股票仓位限制：将收益按仓位比例缩放
        if (config.maxPositionPercent > 0.0 && config.maxPositionPercent < 1.0) {
            adjustedReturn *= config.maxPositionPercent;
            if (std::abs(adjustedReturn - periodicReturn) > 1e-12) {
                ++positionLimitCount;
            }
        }

        // 总仓位暴露限制
        if (config.maxTotalExposure > 0.0 && config.maxTotalExposure < 1.0) {
            adjustedReturn *= config.maxTotalExposure;
        }

        // 止损
        if (config.stopLossRate > 0.0 && adjustedReturn < -config.stopLossRate) {
            adjustedReturn = -config.stopLossRate;
            ++stopLossCount;
        }

        // 止盈
        if (config.takeProfitRate > 0.0 && adjustedReturn > config.takeProfitRate) {
            adjustedReturn = config.takeProfitRate;
            ++takeProfitCount;
        }

        // 单日最大亏损限制
        if (config.maxDailyLoss > 0.0 && adjustedReturn < -config.maxDailyLoss) {
            adjustedReturn = -config.maxDailyLoss;
            ++dailyLossCount;
        }

        result.adjustedReturns.push_back(adjustedReturn);

        cumulativeNetValue *= (1.0 + adjustedReturn);
        peakNetValue = (std::max)(peakNetValue, cumulativeNetValue);
        if (config.maxDrawdownLimit <= 0.0 || peakNetValue <= 0.0) {
            continue;
        }

        const double currentDrawdown = (peakNetValue - cumulativeNetValue) / peakNetValue;
        if (currentDrawdown >= config.maxDrawdownLimit) {
            ++drawdownBreakCount;
            break;
        }
    }

    result.triggeredCount = stopLossCount + takeProfitCount + dailyLossCount + drawdownBreakCount + positionLimitCount;

    std::ostringstream oss;
    bool hasEntry = false;
    auto appendEntry = [&](const char* label, int count) {
        if (count > 0) {
            if (hasEntry) oss << ", ";
            oss << label << ": " << count << "次";
            hasEntry = true;
        }
    };
    appendEntry("止损触发", stopLossCount);
    appendEntry("止盈触发", takeProfitCount);
    appendEntry("单日亏损限制", dailyLossCount);
    appendEntry("回撤熔断", drawdownBreakCount);
    appendEntry("仓位限制", positionLimitCount);
    if (!hasEntry) {
        oss << "未触发风控";
    }
    result.summary = oss.str();

    return result;
}

// ---- 风控指标计算 ----

double calculateDownsideDeviation(const std::vector<double>& returns, double threshold = 0.0)
{
    if (returns.empty()) return 0.0;
    double sumSqNeg = 0.0;
    for (double r : returns) {
        if (r < threshold) {
            const double diff = r - threshold;
            sumSqNeg += diff * diff;
        }
    }
    return std::sqrt(sumSqNeg / static_cast<double>(returns.size()));
}

double calculateValueAtRisk(const std::vector<double>& returns, double confidenceLevel = 0.95)
{
    if (returns.empty()) return 0.0;
    std::vector<double> sorted = returns;
    std::sort(sorted.begin(), sorted.end());
    const size_t index = static_cast<size_t>(std::floor((1.0 - confidenceLevel) * static_cast<double>(sorted.size())));
    return -sorted[(std::min)(index, sorted.size() - 1)];
}

double calculateConditionalVaR(const std::vector<double>& returns, double confidenceLevel = 0.95)
{
    if (returns.empty()) return 0.0;
    std::vector<double> sorted = returns;
    std::sort(sorted.begin(), sorted.end());
    const size_t cutoff = static_cast<size_t>(std::floor((1.0 - confidenceLevel) * static_cast<double>(sorted.size())));
    const size_t n = (std::max)(cutoff, static_cast<size_t>(1));
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sum += sorted[i];
    }
    return -(sum / static_cast<double>(n));
}

double calculateWinRate(const std::vector<double>& periodicReturns)
{
    if (periodicReturns.empty()) {
        return 0.0;
    }

    const auto wins = std::count_if(periodicReturns.begin(), periodicReturns.end(), [](double value) {
        return value > 0.0;
    });
    return static_cast<double>(wins) / static_cast<double>(periodicReturns.size());
}

double calculateProfitFactor(const std::vector<double>& periodicReturns)
{
    if (periodicReturns.empty()) {
        return 0.0;
    }

    double grossProfit = 0.0;
    double grossLossAbs = 0.0;
    for (double periodicReturn : periodicReturns) {
        if (periodicReturn > 0.0) {
            grossProfit += periodicReturn;
        } else if (periodicReturn < 0.0) {
            grossLossAbs += -periodicReturn;
        }
    }

    if (grossLossAbs <= 1e-12) {
        return grossProfit > 0.0 ? std::numeric_limits<double>::infinity() : 0.0;
    }

    return grossProfit / grossLossAbs;
}

double calculateVariance(const std::vector<double>& values, double mean)
{
    if (values.size() < 2) {
        return 0.0;
    }

    double sum = 0.0;
    for (double value : values) {
        const double diff = value - mean;
        sum += diff * diff;
    }

    return sum / static_cast<double>(values.size() - 1);
}

double calculateCovariance(const std::vector<double>& lhs,
                           double lhsMean,
                           const std::vector<double>& rhs,
                           double rhsMean)
{
    if (lhs.size() < 2 || lhs.size() != rhs.size()) {
        return 0.0;
    }

    double sum = 0.0;
    for (size_t index = 0; index < lhs.size(); ++index) {
        sum += (lhs[index] - lhsMean) * (rhs[index] - rhsMean);
    }

    return sum / static_cast<double>(lhs.size() - 1);
}

bool isBarWithinRange(const CachedMarketBar& bar,
                      const std::string& startDate,
                      const std::string& endDate)
{
    return factor::cached_bars::isBarWithinRange(bar, startDate, endDate);
}

}

FactorBacktestExecutor::FactorBacktestExecutor(
    std::shared_ptr<FactorInstanceManager> instanceManager,
    std::shared_ptr<foundation::thread::ThreadPoolExecutor> threadPool,
    std::shared_ptr<FactorCacheManager> cacheManager)
    : instanceManager_(std::move(instanceManager)),
      threadPool_(std::move(threadPool)),
      cacheManager_(std::move(cacheManager)) {
}

FactorBacktestExecutor::ProgressInfo FactorBacktestExecutor::createProgressInfo(const std::string& instanceId) const
{
    ProgressInfo progress;
    progress.taskId = foundation::utils::Uuid::generate_v4();
    progress.instanceId = instanceId;
    progress.status = "RUNNING";
    progress.currentStep = "初始化回测";
    return progress;
}

void FactorBacktestExecutor::registerTask(const ProgressInfo& progress)
{
    std::lock_guard<std::mutex> lock(taskMutex_);
    activeTasks_[progress.taskId] = progress;
}

void FactorBacktestExecutor::finalizeTask(const foundation::utils::Uuid& taskId)
{
    std::lock_guard<std::mutex> lock(taskMutex_);
    activeTasks_.erase(taskId);
    cancelledTasks_.erase(taskId.to_string());
}

BacktestResult FactorBacktestExecutor::executeTracked(const BacktestConfig& config,
                                                      ProgressInfo progress)
{
    const bool useBacktestCache = config.datasetId <= 0 && config.allowedStockCodes.empty();
    const std::string riskSignature = buildRiskCacheSignature(config);

    if (useBacktestCache && cacheManager_ && cacheManager_->isCacheAvailable()) {
        foundation::json::JsonFacade cachedResult;
        if (cacheManager_->getBacktestResult(
                config.instanceId,
                config.startDate,
                config.endDate,
                config.forwardDays,
                config.numGroups,
                riskSignature,
                cachedResult)) {
            updateProgress(progress, 100, "命中回测缓存");
            return BacktestResult::fromJson(cachedResult);
        }
    }

    BacktestResult result = executeInternal(config, progress);

    if (useBacktestCache && cacheManager_ && cacheManager_->isCacheAvailable() && result.status == "SUCCESS") {
        cacheManager_->setBacktestResult(
            config.instanceId,
            config.startDate,
            config.endDate,
            config.forwardDays,
            config.numGroups,
            riskSignature,
            result.toJson()
        );
    }

    return result;
}

BacktestResult FactorBacktestExecutor::execute(const BacktestConfig& config)
{
    ProgressInfo progress = createProgressInfo(config.instanceId);
    registerTask(progress);
    BacktestResult result = executeTracked(config, progress);
    finalizeTask(progress.taskId);
    return result;
}

std::future<BacktestResult> FactorBacktestExecutor::executeAsync(const BacktestConfig& config)
{
    return executeTrackedAsync(config).future;
}

FactorBacktestExecutor::ExecutionHandle FactorBacktestExecutor::executeTrackedAsync(const BacktestConfig& config)
{
    ProgressInfo progress = createProgressInfo(config.instanceId);
    registerTask(progress);

    if (threadPool_) {
        auto future = threadPool_->submit([this, config, progress]() mutable {
            BacktestResult result = executeTracked(config, progress);
            finalizeTask(progress.taskId);
            return result;
        });
        return ExecutionHandle{progress.taskId, std::move(future)};
    }

    auto future = std::async(std::launch::async, [this, config, progress]() mutable {
        BacktestResult result = executeTracked(config, progress);
        finalizeTask(progress.taskId);
        return result;
    });
    return ExecutionHandle{progress.taskId, std::move(future)};
}

std::vector<BacktestResult> FactorBacktestExecutor::executeBatch(const std::vector<BacktestConfig>& configs)
{
    if (configs.empty()) {
        return {};
    }

    if (configs.size() == 1) {
        return {execute(configs.front())};
    }

    std::vector<ExecutionHandle> handles;
    handles.reserve(configs.size());
    for (const auto& config : configs) {
        handles.push_back(executeTrackedAsync(config));
    }

    std::vector<BacktestResult> results;
    results.reserve(configs.size());
    for (auto& handle : handles) {
        results.push_back(handle.future.get());
    }
    return results;
}

FactorBacktestExecutor::ProgressInfo FactorBacktestExecutor::getProgress(const foundation::utils::Uuid& taskId) const
{
    std::lock_guard<std::mutex> lock(taskMutex_);
    auto it = activeTasks_.find(taskId);
    if (it != activeTasks_.end()) {
        return it->second;
    }

    ProgressInfo progress;
    progress.taskId = taskId;
    progress.status = "NOT_FOUND";
    progress.currentStep = "任务不存在";
    return progress;
}

bool FactorBacktestExecutor::cancel(const foundation::utils::Uuid& taskId)
{
    std::lock_guard<std::mutex> lock(taskMutex_);
    auto it = activeTasks_.find(taskId);
    if (it == activeTasks_.end()) {
        return false;
    }

    it->second.status = "CANCELLED";
    it->second.currentStep = "任务已取消";
    cancelledTasks_.insert(taskId.to_string());
    return true;
}

BacktestResult FactorBacktestExecutor::executeInternal(const BacktestConfig& config,
                                                       ProgressInfo& progress)
{
    QElapsedTimer timer;
    timer.start();

    BacktestResult result;
    result.resultId = foundation::utils::Uuid::generate_v4();
    result.instanceId = config.instanceId;
    result.config = config;
    result.status = "FAILED";

    try {
        if (!instanceManager_) {
            return BacktestResult::createError(config.instanceId, "因子实例管理器未初始化");
        }

        auto info = instanceManager_->getInstanceInfo(config.instanceId);
        if (info.instanceId.empty()) {
            return BacktestResult::createError(config.instanceId, "因子实例不存在");
        }

        result.instanceName = info.instanceName;
        result.dataStatus = info.dataStatus;
        result.dataCoverage = info.dataStatus.coverage;

        std::shared_ptr<BaseFactor> factor;
        std::string prepareFailureReason;
        if (!prepareData(config, progress, factor, &prepareFailureReason)) {
            result.errorMessage = prepareFailureReason.empty() ? "准备回测数据失败" : prepareFailureReason;
            if (isCancelled(progress.taskId)) {
                result.status = "CANCELLED";
                result.errorMessage = "任务已取消";
            }
            result.executionTimeMs = static_cast<int>(timer.elapsed());
            return result;
        }

        ExecutionMarketContext marketContext;
        if (!prepareExecutionMarketContext(config, marketContext, &prepareFailureReason)) {
            result.errorMessage = prepareFailureReason.empty() ? "准备市场上下文失败" : prepareFailureReason;
            result.status = isCancelled(progress.taskId) ? "CANCELLED" : "FAILED";
            result.executionTimeMs = static_cast<int>(timer.elapsed());
            return result;
        }

        std::vector<CalculationResult> factorResults;
        std::string factorFailureReason;
        if (!calculateFactorSeries(config, marketContext, factor, progress, factorResults, &factorFailureReason)) {
            result.errorMessage = isCancelled(progress.taskId)
                ? "任务已取消"
                : (factorFailureReason.empty() ? "因子序列计算失败" : factorFailureReason);
            result.status = isCancelled(progress.taskId) ? "CANCELLED" : "FAILED";
            result.executionTimeMs = static_cast<int>(timer.elapsed());
            return result;
        }

        std::vector<CalculationResult> returnResults;
        if (!calculateReturnSeries(config, marketContext, progress, returnResults)) {
            result.errorMessage = isCancelled(progress.taskId) ? "任务已取消" : "收益序列计算失败";
            result.status = isCancelled(progress.taskId) ? "CANCELLED" : "FAILED";
            result.executionTimeMs = static_cast<int>(timer.elapsed());
            return result;
        }

        calculateICIR(factorResults, returnResults, progress, result.icirResult);
        std::vector<double> longShortSeries;
        std::vector<double> turnoverSeries;
        std::vector<std::string> longShortDates;
        std::string groupFailureReason;
        if (!executeGroupBacktest(factorResults,
                                  returnResults,
                                  config,
                                  progress,
                                  result.groupResult,
                                  &longShortSeries,
                                  &turnoverSeries,
                                  &longShortDates,
                                  &groupFailureReason)) {
            result.errorMessage = isCancelled(progress.taskId)
                ? "任务已取消"
                : (groupFailureReason.empty() ? "未生成有效分组回测结果" : groupFailureReason);
            result.status = isCancelled(progress.taskId) ? "CANCELLED" : "FAILED";
            result.executionTimeMs = static_cast<int>(timer.elapsed());
            return result;
        }

        const auto riskResult = applyRiskControls(longShortSeries, config);
        longShortSeries = riskResult.adjustedReturns;
        const double annualizationFactor = annualizationFactorForPeriods(config.forwardDays);
        const double periodRiskFreeRate = config.riskFreeRate / annualizationFactor;
        const double averageLongShort = factor::icir::calculateMean(longShortSeries);
        const double longShortStd = factor::icir::calculateStdDev(longShortSeries, averageLongShort);
        const double averageTurnover = factor::icir::calculateMean(turnoverSeries);
        const double averageExcessReturn = averageLongShort - periodRiskFreeRate;

        result.annualReturn = averageLongShort * annualizationFactor;
        result.sharpeRatio = longShortStd > 0.0 ? (averageExcessReturn / longShortStd) * std::sqrt(annualizationFactor) : 0.0;
        result.maxDrawdown = calculateMaxDrawdown(longShortSeries);
        result.winRate = calculateWinRate(longShortSeries);
        result.profitFactor = calculateProfitFactor(longShortSeries);
        result.turnoverRate = averageTurnover * annualizationFactor * 100.0;

        std::vector<double> alignedLongShort;
        std::vector<double> alignedBenchmark;
        alignedLongShort.reserve(longShortSeries.size());
        alignedBenchmark.reserve(longShortSeries.size());
        for (size_t index = 0; index < longShortSeries.size() && index < longShortDates.size(); ++index) {
            const double benchmarkReturn = calculateFutureReturn(config.benchmarkSymbol,
                                                                 longShortDates[index],
                                                                 config.forwardDays,
                                                                 config);
            if (!std::isfinite(benchmarkReturn)) {
                continue;
            }

            alignedLongShort.push_back(longShortSeries[index]);
            alignedBenchmark.push_back(benchmarkReturn);
        }

        if (!alignedBenchmark.empty()) {
            const double benchmarkMean = factor::icir::calculateMean(alignedBenchmark);
            const double alignedStrategyMean = factor::icir::calculateMean(alignedLongShort);
            result.benchmarkAnnualReturn = benchmarkMean * annualizationFactor;
            result.excessAnnualReturn = (alignedStrategyMean - benchmarkMean) * annualizationFactor;

            std::vector<double> excessSeries;
            excessSeries.reserve(alignedBenchmark.size());
            for (size_t index = 0; index < alignedBenchmark.size(); ++index) {
                excessSeries.push_back(alignedLongShort[index] - alignedBenchmark[index]);
            }

            const double excessMean = factor::icir::calculateMean(excessSeries);
            const double excessStd = factor::icir::calculateStdDev(excessSeries, excessMean);
            result.trackingError = excessStd * std::sqrt(annualizationFactor);
            result.informationRatio = result.trackingError > 0.0
                ? (result.excessAnnualReturn / result.trackingError)
                : 0.0;

            const double benchmarkVariance = calculateVariance(alignedBenchmark, benchmarkMean);
            if (benchmarkVariance > 0.0) {
                const double covariance = calculateCovariance(alignedLongShort,
                                                              alignedStrategyMean,
                                                              alignedBenchmark,
                                                              benchmarkMean);
                result.beta = covariance / benchmarkVariance;
                result.alpha = result.annualReturn
                    - (config.riskFreeRate + result.beta * (result.benchmarkAnnualReturn - config.riskFreeRate));
            }
        }

        // 风控指标计算
        result.volatility = longShortStd * std::sqrt(annualizationFactor);
        result.downsideDeviation = calculateDownsideDeviation(longShortSeries) * std::sqrt(annualizationFactor);
        result.sortinoRatio = result.downsideDeviation > 0.0
            ? (averageExcessReturn / result.downsideDeviation) * std::sqrt(annualizationFactor)
            : 0.0;
        result.calmarRatio = result.maxDrawdown > 0.0
            ? result.annualReturn / result.maxDrawdown
            : 0.0;
        result.valueAtRisk = calculateValueAtRisk(longShortSeries, 0.95);
        result.conditionalVaR = calculateConditionalVaR(longShortSeries, 0.95);
        result.riskTriggeredCount = riskResult.triggeredCount;
        result.riskControlSummary = riskResult.summary;

        result.groupResult.longShortReturn = averageLongShort;
        result.dataStatus.availability = factorResults.empty() ? DataAvailability::UNAVAILABLE : DataAvailability::AVAILABLE;
        result.dataStatus.coverage = factorResults.empty() ? 0.0 : 1.0;
        result.dataStatus.message = factorResults.empty() ? "未生成有效因子序列" : "回测执行完成";
        result.dataCoverage = result.dataStatus.coverage;
        result.status = isCancelled(progress.taskId) ? "CANCELLED" : "SUCCESS";
        result.executionTimeMs = static_cast<int>(timer.elapsed());

        updateProgress(progress, 100, result.status == "CANCELLED" ? "已取消" : "回测完成");
        return result;
    } catch (const std::exception& e) {
        result.status = isCancelled(progress.taskId) ? "CANCELLED" : "FAILED";
        result.errorMessage = e.what();
        result.executionTimeMs = static_cast<int>(timer.elapsed());
        updateProgress(progress, progress.progress, result.status == "CANCELLED" ? "已取消" : "执行失败");
        return result;
    }
}

bool FactorBacktestExecutor::prepareData(const BacktestConfig& config,
                                        ProgressInfo& progress,
                                        std::shared_ptr<BaseFactor>& factor,
                                        std::string* failureReason)
{
    updateProgress(progress, 10, "加载因子实例");
    if (isCancelled(progress.taskId)) {
        if (failureReason) {
            *failureReason = "任务已取消";
        }
        return false;
    }

    factor = instanceManager_->createInstance(config.instanceId);
    if (!factor && failureReason) {
        *failureReason = "未能创建因子实例，请检查实例是否已激活且定义与实例表保持同步";
    }
    return static_cast<bool>(factor);
}

bool FactorBacktestExecutor::calculateFactorSeries(const BacktestConfig& config,
                                                   const ExecutionMarketContext& marketContext,
                                                   std::shared_ptr<BaseFactor> factor,
                                                   ProgressInfo& progress,
                                                   std::vector<CalculationResult>& factorResults,
                                                   std::string* failureReason)
{
    if (!factor) {
        if (failureReason) {
            *failureReason = "因子实例无效";
        }
        return false;
    }

    updateProgress(progress, 25, "计算因子序列");
    const auto& tradeDates = marketContext.tradeDates;
    if (tradeDates.empty()) {
        if (failureReason) {
            *failureReason = "未找到可用于回测的交易日";
        }
        return false;
    }

    factorResults.clear();
    factorResults.reserve(tradeDates.size());
    size_t emptyCalculationCount = 0;
    std::string lastEmptyReason;
    std::shared_ptr<FactorDataProvider> dataProvider;
    if (!config.cachedBars.empty()) {
        dataProvider = std::make_shared<CachedRowFactorDataProvider>(config.cachedBars);
    }

    qDebug() << "FactorBacktestExecutor: 计算因子序列"
             << "instanceId=" << QString::fromStdString(config.instanceId)
             << "tradeDateCount=" << static_cast<int>(tradeDates.size())
             << "cachedBarCount=" << static_cast<qulonglong>(config.cachedBars.size())
             << "allowedSymbolCount=" << static_cast<int>(config.allowedStockCodes.size())
             << "usingCacheProvider=" << static_cast<bool>(dataProvider);

    const QString runtimeFactorType = factor ? QString::fromStdString(factor->getFactorType()).trimmed() : QString();
    const bool profileLiquidity = runtimeFactorType == QString::fromUtf8("流动性因子")
        || runtimeFactorType.compare(QStringLiteral("liquidity"), Qt::CaseInsensitive) == 0;

    for (size_t i = 0; i < tradeDates.size(); ++i) {
        if (isCancelled(progress.taskId)) {
            return false;
        }

        CalculationContext context;
        context.date = tradeDates[i];
        auto symbolsIt = marketContext.symbolsByDate.find(tradeDates[i]);
        if (symbolsIt != marketContext.symbolsByDate.end()) {
            context.symbols = symbolsIt->second;
        }
        context.dataProvider = dataProvider;
        if (i < 3 || i + 1 == tradeDates.size()) {
            qDebug() << "FactorBacktestExecutor: 单日样本"
                     << "date=" << QString::fromStdString(context.date)
                     << "symbolCount=" << static_cast<int>(context.symbols.size());
        }

        QElapsedTimer calculateTimer;
        calculateTimer.start();
        CalculationResult calculation = factor->calculate(context);
        const qint64 calculationElapsedMs = calculateTimer.elapsed();
        if (profileLiquidity && calculationElapsedMs >= 300) {
            qDebug() << "FactorBacktestExecutor(liquidity): 单日因子计算耗时较长"
                     << "date=" << QString::fromStdString(context.date)
                     << "symbolCount=" << static_cast<int>(context.symbols.size())
                     << "resultCount=" << static_cast<int>(calculation.values.size())
                     << "elapsedMs=" << calculationElapsedMs
                     << "usingCacheProvider=" << static_cast<bool>(dataProvider);
        }
        if (!calculation.dataStatus.isValid()) {
            if (failureReason) {
                *failureReason = calculation.dataStatus.message.empty()
                    ? "因子序列计算失败"
                    : calculation.dataStatus.message;
            }
            return false;
        }
        if (!calculation.isEmpty()) {
            factorResults.push_back(std::move(calculation));
        } else {
            if (calculation.metadata.has("empty_reason")) {
                lastEmptyReason = calculation.metadata.get("empty_reason").asString();
            }
            ++emptyCalculationCount;
        }

        const int progressValue = 25 + static_cast<int>((static_cast<double>(i + 1) / tradeDates.size()) * 25.0);
        updateProgress(progress, progressValue, "计算因子序列");
    }
    if (factorResults.empty() && failureReason) {
        if (emptyCalculationCount == tradeDates.size() && !lastEmptyReason.empty()) {
            *failureReason = lastEmptyReason + "，因此因子在全部交易日都未产出有效值";
        } else {
            *failureReason = emptyCalculationCount == tradeDates.size()
                ? "因子在全部交易日都未产出有效值，常见原因包括样本不足、字段缺失、参数窗口过长或筛选后全部被剔除"
                : "未生成有效因子序列";
        }
    }
    return !factorResults.empty();
}

bool FactorBacktestExecutor::calculateReturnSeries(const BacktestConfig& config,
                                                   const ExecutionMarketContext& marketContext,
                                                   ProgressInfo& progress,
                                                   std::vector<CalculationResult>& returnResults)
{
    updateProgress(progress, 55, "计算未来收益");
    const auto& tradeDates = marketContext.tradeDates;
    if (tradeDates.empty()) {
        return false;
    }

    returnResults.clear();
    returnResults.reserve(tradeDates.size());

    for (size_t i = 0; i < tradeDates.size(); ++i) {
        if (isCancelled(progress.taskId)) {
            return false;
        }

        CalculationResult result;
        result.calculationId = foundation::utils::Uuid::generate_v4();
        result.date = tradeDates[i];
        result.metadata = foundation::json::JsonFacade::createObject();

        const auto symbolsIt = marketContext.symbolsByDate.find(tradeDates[i]);
        if (symbolsIt == marketContext.symbolsByDate.end()) {
            continue;
        }

        const auto& symbols = symbolsIt->second;
        for (const auto& symbol : symbols) {
            const double futureReturn = calculateFutureReturn(symbol, tradeDates[i], config.forwardDays, config);
            if (std::isfinite(futureReturn)) {
                result.values[symbol] = futureReturn;
            }
        }

        if (!result.isEmpty()) {
            returnResults.push_back(std::move(result));
        }

        const int progressValue = 55 + static_cast<int>((static_cast<double>(i + 1) / tradeDates.size()) * 15.0);
        updateProgress(progress, progressValue, "计算未来收益");
    }

    return !returnResults.empty();
}

bool FactorBacktestExecutor::calculateICIR(const std::vector<CalculationResult>& factorResults,
                                           const std::vector<CalculationResult>& returnResults,
                                           ProgressInfo& progress,
                                           ICIRResult& icirResult)
{
    updateProgress(progress, 75, "计算IC/IR");
    const auto summary = factor::icir::aggregate(factorResults, returnResults);
    icirResult = summary.result;
    return summary.hasValidSeries;
}

bool FactorBacktestExecutor::executeGroupBacktest(const std::vector<CalculationResult>& factorResults,
                                                  const std::vector<CalculationResult>& returnResults,
                                                  const BacktestConfig& config,
                                                  ProgressInfo& progress,
                                                  GroupBacktestResult& groupResult,
                                                  std::vector<double>* longShortSeries,
                                                  std::vector<double>* turnoverSeries,
                                                  std::vector<std::string>* longShortDates,
                                                  std::string* failureReason)
{
    updateProgress(progress, 85, "执行分组回测");
    const auto summary = factor::group_backtest::aggregate(
        factorResults,
        returnResults,
        config.numGroups,
        config.transactionCost,
        config.rebalanceDays);
    groupResult = summary.groupResult;
    if (longShortSeries) {
        *longShortSeries = summary.longShortReturnsByDate;
    }
    if (turnoverSeries) {
        *turnoverSeries = summary.longShortTurnoversByDate;
    }
    if (longShortDates) {
        *longShortDates = summary.longShortDatesByDate;
    }

    if (!summary.hasValidGroup && failureReason) {
        if (returnResults.empty()) {
            *failureReason = "未生成未来收益序列，请检查所选数据区间是否至少覆盖到下一个交易日";
        } else if (summary.overlapDateCount == 0) {
            *failureReason = "因子值与未来收益没有重叠样本，无法执行分组回测";
        } else if (summary.maxMatchedStocks <= 1) {
            *failureReason = "有效股票数不足，至少需要 2 只股票才能执行分组回测";
        } else {
            *failureReason = "未生成有效分组回测结果：最大有效股票数为 " + std::to_string(summary.maxMatchedStocks)
                + "，请求分组数为 " + std::to_string(config.numGroups)
                + "，成功分组日期数为 " + std::to_string(summary.groupedDateCount);
        }
    }

    return summary.hasValidGroup && !summary.longShortReturnsByDate.empty();
}

std::vector<std::string> FactorBacktestExecutor::getTradeDates(const std::string& startDate,
                                                               const std::string& endDate,
                                                               const BacktestConfig& config)
{
    if (!config.cachedBars.empty()) {
        return factor::cached_bars::extractTradeDates(config.cachedBars, startDate, endDate);
    }

    auto db = instanceManager_ ? instanceManager_->getDatabase() : nullptr;
    if (!db) {
        return {};
    }

    auto result = db->executeQuery(
        "SELECT DISTINCT trade_date FROM daily_bar WHERE trade_date BETWEEN :start_date AND :end_date ORDER BY trade_date",
        makeNamedParams({
            {":start_date", QString::fromStdString(startDate)},
            {":end_date", QString::fromStdString(endDate)}
        })
    );

    std::vector<std::string> tradeDates;
    tradeDates.reserve(result.rowCount());
    for (size_t i = 0; i < result.rowCount(); ++i) {
        tradeDates.push_back(result.getRow(i).getString("trade_date").toStdString());
    }
    return tradeDates;
}

std::vector<std::string> FactorBacktestExecutor::getSymbols(const std::string& date,
                                                            const std::unordered_set<std::string>& allowedSymbols,
                                                            const BacktestConfig& config)
{
    if (!config.cachedBars.empty()) {
        return factor::cached_bars::extractSymbols(config.cachedBars, date, allowedSymbols);
    }

    auto db = instanceManager_ ? instanceManager_->getDatabase() : nullptr;
    if (!db) {
        return {};
    }

    auto result = db->executeQuery(
        "SELECT DISTINCT symbol FROM daily_bar WHERE trade_date = :date ORDER BY symbol",
        makeNamedParams({
            {":date", QString::fromStdString(date)}
        })
    );

    std::vector<std::string> symbols;
    symbols.reserve(result.rowCount());
    for (size_t i = 0; i < result.rowCount(); ++i) {
        const std::string symbol = result.getRow(i).getString("symbol").toStdString();
        if (!allowedSymbols.empty() && allowedSymbols.find(symbol) == allowedSymbols.end()) {
            continue;
        }
        symbols.push_back(symbol);
    }
    return symbols;
}

double FactorBacktestExecutor::calculateFutureReturn(const std::string& symbol,
                                                     const std::string& startDate,
                                                     int forwardDays,
                                                     const BacktestConfig& config)
{
    if (!config.cachedBars.empty()) {
        const double cachedFutureReturn = factor::cached_bars::calculateFutureReturn(config.cachedBars, symbol, startDate, forwardDays);
        if (std::isfinite(cachedFutureReturn)) {
            return cachedFutureReturn;
        }

        return std::numeric_limits<double>::quiet_NaN();
    }

    auto db = instanceManager_ ? instanceManager_->getDatabase() : nullptr;
    if (!db || forwardDays <= 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    auto result = db->executeQuery(
        QString("SELECT trade_date, close FROM daily_bar WHERE symbol = :symbol AND trade_date >= :date ORDER BY trade_date ASC LIMIT %1")
            .arg(forwardDays + 1),
        makeNamedParams({
            {":symbol", QString::fromStdString(symbol)},
            {":date", QString::fromStdString(startDate)}
        })
    );

    if (result.rowCount() <= static_cast<size_t>(forwardDays)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double startClose = result.getRow(0).getDouble("close");
    const double endClose = result.getRow(static_cast<size_t>(forwardDays)).getDouble("close");
    if (startClose <= 0.0 || endClose <= 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    return (endClose - startClose) / startClose;
}

bool FactorBacktestExecutor::prepareExecutionMarketContext(const BacktestConfig& config,
                                                           ExecutionMarketContext& marketContext,
                                                           std::string* failureReason)
{
    marketContext.tradeDates = getTradeDates(config.startDate, config.endDate, config);
    if (marketContext.tradeDates.empty()) {
        if (failureReason) {
            *failureReason = "未找到可用于回测的交易日";
        }
        return false;
    }

    marketContext.allowedSymbols = std::unordered_set<std::string>(
        config.allowedStockCodes.begin(),
        config.allowedStockCodes.end());
    marketContext.symbolsByDate.clear();
    marketContext.symbolsByDate.reserve(marketContext.tradeDates.size());

    for (const auto& tradeDate : marketContext.tradeDates) {
        marketContext.symbolsByDate.emplace(
            tradeDate,
            getSymbols(tradeDate, marketContext.allowedSymbols, config));
    }

    return true;
}

void FactorBacktestExecutor::updateProgress(ProgressInfo& progress,
                                            int newProgress,
                                            const std::string& step)
{
    progress.progress = newProgress;
    progress.currentStep = step;
    if (progress.status.empty()) {
        progress.status = "RUNNING";
    }

    std::lock_guard<std::mutex> lock(taskMutex_);
    auto it = activeTasks_.find(progress.taskId);
    if (it != activeTasks_.end()) {
        it->second = progress;
    } else {
        activeTasks_[progress.taskId] = progress;
    }
}

bool FactorBacktestExecutor::isCancelled(const foundation::utils::Uuid& taskId) const
{
    std::lock_guard<std::mutex> lock(taskMutex_);
    return cancelledTasks_.find(taskId.to_string()) != cancelledTasks_.end();
}

} // namespace factor