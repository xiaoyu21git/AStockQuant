#include "domain/factor/include/FactorBacktestExecutor.h"

#include "infrastructure/include/database/QtMySQLDatabase.h"

#include <QDate>
#include <QElapsedTimer>
#include <QString>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <future>
#include <set>
#include <limits>
#include <map>
#include <numeric>
#include <unordered_map>
#include <stdexcept>

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

double calculateMean(const std::vector<double>& values)
{
    if (values.empty()) {
        return 0.0;
    }
    return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}

double calculateStdDev(const std::vector<double>& values, double mean)
{
    if (values.size() < 2) {
        return 0.0;
    }

    double variance = 0.0;
    for (double value : values) {
        const double delta = value - mean;
        variance += delta * delta;
    }
    variance /= static_cast<double>(values.size());
    return std::sqrt(variance);
}

double calculateCorrelation(const std::vector<double>& lhs, const std::vector<double>& rhs)
{
    if (lhs.size() != rhs.size() || lhs.size() < 2) {
        return 0.0;
    }

    const double lhsMean = calculateMean(lhs);
    const double rhsMean = calculateMean(rhs);
    double covariance = 0.0;
    double lhsVariance = 0.0;
    double rhsVariance = 0.0;

    for (size_t i = 0; i < lhs.size(); ++i) {
        const double lhsDelta = lhs[i] - lhsMean;
        const double rhsDelta = rhs[i] - rhsMean;
        covariance += lhsDelta * rhsDelta;
        lhsVariance += lhsDelta * lhsDelta;
        rhsVariance += rhsDelta * rhsDelta;
    }

    if (lhsVariance <= 0.0 || rhsVariance <= 0.0) {
        return 0.0;
    }

    return covariance / std::sqrt(lhsVariance * rhsVariance);
}

std::map<QString, QVariant> makeNamedParams(std::initializer_list<std::pair<QString, QVariant>> values)
{
    std::map<QString, QVariant> params;
    for (const auto& [key, value] : values) {
        params.emplace(key, value);
    }
    return params;
}

std::vector<double> buildDailyLongShortSeries(const std::vector<double>& groupReturns)
{
    if (groupReturns.size() < 2) {
        return {};
    }

    return {groupReturns.front() - groupReturns.back()};
}

bool isBarWithinRange(const CachedMarketBar& bar,
                      const std::string& startDate,
                      const std::string& endDate)
{
    return (startDate.empty() || bar.tradeDate >= startDate) &&
           (endDate.empty() || bar.tradeDate <= endDate);
}

std::vector<std::string> extractTradeDatesFromCachedBars(const std::vector<CachedMarketBar>& cachedBars,
                                                         const std::string& startDate,
                                                         const std::string& endDate)
{
    std::set<std::string> tradeDateSet;
    for (const auto& bar : cachedBars) {
        if (!bar.tradeDate.empty() && isBarWithinRange(bar, startDate, endDate)) {
            tradeDateSet.insert(bar.tradeDate);
        }
    }

    return {tradeDateSet.begin(), tradeDateSet.end()};
}

std::vector<std::string> extractSymbolsFromCachedBars(const std::vector<CachedMarketBar>& cachedBars,
                                                      const std::string& date,
                                                      const std::unordered_set<std::string>& allowedSymbols)
{
    std::set<std::string> symbolSet;
    for (const auto& bar : cachedBars) {
        if (bar.tradeDate != date || bar.symbol.empty()) {
            continue;
        }
        if (!allowedSymbols.empty() && allowedSymbols.find(bar.symbol) == allowedSymbols.end()) {
            continue;
        }
        symbolSet.insert(bar.symbol);
    }

    return {symbolSet.begin(), symbolSet.end()};
}

double calculateFutureReturnFromCachedBars(const std::vector<CachedMarketBar>& cachedBars,
                                           const std::string& symbol,
                                           const std::string& startDate,
                                           int forwardDays)
{
    if (forwardDays <= 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    std::vector<CachedMarketBar> symbolBars;
    symbolBars.reserve(cachedBars.size());
    for (const auto& bar : cachedBars) {
        if (bar.symbol == symbol && !bar.tradeDate.empty() && bar.tradeDate >= startDate) {
            symbolBars.push_back(bar);
        }
    }

    std::sort(symbolBars.begin(), symbolBars.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.tradeDate < rhs.tradeDate;
    });

    if (symbolBars.size() <= static_cast<size_t>(forwardDays)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double startClose = symbolBars.front().close;
    const double endClose = symbolBars[static_cast<size_t>(forwardDays)].close;
    if (startClose <= 0.0 || endClose <= 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    return (endClose - startClose) / startClose;
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

    if (useBacktestCache && cacheManager_ && cacheManager_->isCacheAvailable()) {
        foundation::json::JsonFacade cachedResult;
        if (cacheManager_->getBacktestResult(
                config.instanceId,
                config.startDate,
                config.endDate,
                config.forwardDays,
                config.numGroups,
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
    std::vector<BacktestResult> results;
    results.reserve(configs.size());
    for (const auto& config : configs) {
        results.push_back(execute(config));
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
        if (!prepareData(config, progress, factor)) {
            result.errorMessage = "准备回测数据失败";
            if (isCancelled(progress.taskId)) {
                result.status = "CANCELLED";
                result.errorMessage = "任务已取消";
            }
            result.executionTimeMs = static_cast<int>(timer.elapsed());
            return result;
        }

        std::vector<CalculationResult> factorResults;
        std::string factorFailureReason;
        if (!calculateFactorSeries(config, factor, progress, factorResults, &factorFailureReason)) {
            result.errorMessage = isCancelled(progress.taskId)
                ? "任务已取消"
                : (factorFailureReason.empty() ? "因子序列计算失败" : factorFailureReason);
            result.status = isCancelled(progress.taskId) ? "CANCELLED" : "FAILED";
            result.executionTimeMs = static_cast<int>(timer.elapsed());
            return result;
        }

        std::vector<CalculationResult> returnResults;
        if (!calculateReturnSeries(config, progress, returnResults)) {
            result.errorMessage = isCancelled(progress.taskId) ? "任务已取消" : "收益序列计算失败";
            result.status = isCancelled(progress.taskId) ? "CANCELLED" : "FAILED";
            result.executionTimeMs = static_cast<int>(timer.elapsed());
            return result;
        }

        calculateICIR(factorResults, returnResults, progress, result.icirResult);
        std::string groupFailureReason;
        if (!executeGroupBacktest(factorResults, returnResults, config, progress, result.groupResult, &groupFailureReason)) {
            result.errorMessage = isCancelled(progress.taskId)
                ? "任务已取消"
                : (groupFailureReason.empty() ? "未生成有效分组回测结果" : groupFailureReason);
            result.status = isCancelled(progress.taskId) ? "CANCELLED" : "FAILED";
            result.executionTimeMs = static_cast<int>(timer.elapsed());
            return result;
        }

        const std::vector<double> longShortSeries = buildDailyLongShortSeries(result.groupResult.groupReturns);
        const double averageLongShort = calculateMean(longShortSeries);
        const double longShortStd = calculateStdDev(longShortSeries, averageLongShort);

        result.annualReturn = averageLongShort * 252.0;
        result.sharpeRatio = longShortStd > 0.0 ? (averageLongShort / longShortStd) * std::sqrt(252.0) : 0.0;
        result.maxDrawdown = 0.0;
        result.winRate = result.icirResult.icPositiveRatio;
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
                                        std::shared_ptr<BaseFactor>& factor)
{
    updateProgress(progress, 10, "加载因子实例");
    if (isCancelled(progress.taskId)) {
        return false;
    }

    factor = instanceManager_->createInstance(config.instanceId);
    return static_cast<bool>(factor);
}

bool FactorBacktestExecutor::calculateFactorSeries(const BacktestConfig& config,
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
    const auto tradeDates = getTradeDates(config.startDate, config.endDate, config);
    if (tradeDates.empty()) {
        if (failureReason) {
            *failureReason = "未找到可用于回测的交易日";
        }
        return false;
    }

    factorResults.clear();
    factorResults.reserve(tradeDates.size());
    size_t emptyCalculationCount = 0;
    const std::unordered_set<std::string> allowedSymbols(config.allowedStockCodes.begin(),
                                                         config.allowedStockCodes.end());
    std::shared_ptr<FactorDataProvider> dataProvider;
    if (!config.cachedBars.empty()) {
        dataProvider = std::make_shared<CachedRowFactorDataProvider>(config.cachedBars);
    }

    for (size_t i = 0; i < tradeDates.size(); ++i) {
        if (isCancelled(progress.taskId)) {
            return false;
        }

        CalculationContext context;
        context.date = tradeDates[i];
        context.symbols = getSymbols(tradeDates[i], allowedSymbols, config);
        context.dataProvider = dataProvider;
        CalculationResult calculation = factor->calculate(context);
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
            ++emptyCalculationCount;
        }

        const int progressValue = 25 + static_cast<int>((static_cast<double>(i + 1) / tradeDates.size()) * 25.0);
        updateProgress(progress, progressValue, "计算因子序列");
    }
    if (factorResults.empty() && failureReason) {
        *failureReason = emptyCalculationCount == tradeDates.size()
            ? "因子在全部交易日都未产出有效值，常见原因包括样本不足、字段缺失、参数窗口过长或筛选后全部被剔除"
            : "未生成有效因子序列";
    }
    return !factorResults.empty();
}

bool FactorBacktestExecutor::calculateReturnSeries(const BacktestConfig& config,
                                                   ProgressInfo& progress,
                                                   std::vector<CalculationResult>& returnResults)
{
    updateProgress(progress, 55, "计算未来收益");
    const auto tradeDates = getTradeDates(config.startDate, config.endDate, config);
    if (tradeDates.empty()) {
        return false;
    }

    returnResults.clear();
    returnResults.reserve(tradeDates.size());
    const std::unordered_set<std::string> allowedSymbols(config.allowedStockCodes.begin(),
                                                         config.allowedStockCodes.end());

    for (size_t i = 0; i < tradeDates.size(); ++i) {
        if (isCancelled(progress.taskId)) {
            return false;
        }

        CalculationResult result;
        result.calculationId = foundation::utils::Uuid::generate_v4();
        result.date = tradeDates[i];
        result.metadata = foundation::json::JsonFacade::createObject();

        const auto symbols = getSymbols(tradeDates[i], allowedSymbols, config);
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
    std::map<std::string, const CalculationResult*> returnsByDate;
    for (const auto& result : returnResults) {
        returnsByDate[result.date] = &result;
    }

    std::vector<double> icSeries;
    for (const auto& factorResult : factorResults) {
        auto returnIt = returnsByDate.find(factorResult.date);
        if (returnIt == returnsByDate.end()) {
            continue;
        }

        std::vector<double> factorValues;
        std::vector<double> returnValues;
        for (const auto& [symbol, factorValue] : factorResult.values) {
            auto valueIt = returnIt->second->values.find(symbol);
            if (valueIt == returnIt->second->values.end()) {
                continue;
            }
            factorValues.push_back(factorValue);
            returnValues.push_back(valueIt->second);
        }

        if (factorValues.size() < 2) {
            continue;
        }

        icSeries.push_back(calculateCorrelation(factorValues, returnValues));
    }

    icirResult.icSeries = icSeries;
    icirResult.icMean = calculateMean(icSeries);
    icirResult.icStd = calculateStdDev(icSeries, icirResult.icMean);
    icirResult.ir = icirResult.icStd > 0.0 ? icirResult.icMean / icirResult.icStd : 0.0;

    if (!icSeries.empty()) {
        const auto positiveCount = std::count_if(icSeries.begin(), icSeries.end(), [](double value) {
            return value > 0.0;
        });
        icirResult.icPositiveRatio = static_cast<double>(positiveCount) / static_cast<double>(icSeries.size());
    }

    return !icSeries.empty();
}

bool FactorBacktestExecutor::executeGroupBacktest(const std::vector<CalculationResult>& factorResults,
                                                  const std::vector<CalculationResult>& returnResults,
                                                  const BacktestConfig& config,
                                                  ProgressInfo& progress,
                                                  GroupBacktestResult& groupResult,
                                                  std::string* failureReason)
{
    updateProgress(progress, 85, "执行分组回测");
    std::map<std::string, const CalculationResult*> returnsByDate;
    for (const auto& result : returnResults) {
        returnsByDate[result.date] = &result;
    }

    std::vector<double> aggregatedReturns(config.numGroups, 0.0);
    std::vector<int> aggregatedCounts(config.numGroups, 0);
    std::vector<int> aggregatedStockCounts(config.numGroups, 0);
    std::vector<double> aggregatedMinFactorValues(config.numGroups, std::numeric_limits<double>::max());
    std::vector<double> aggregatedMaxFactorValues(config.numGroups, std::numeric_limits<double>::lowest());
    size_t maxMatchedStocks = 0;
    int overlapDateCount = 0;
    int groupedDateCount = 0;
    int maxEffectiveGroupCount = 0;

    for (const auto& factorResult : factorResults) {
        auto returnIt = returnsByDate.find(factorResult.date);
        if (returnIt == returnsByDate.end()) {
            continue;
        }

        std::vector<std::pair<std::string, double>> rankedValues(factorResult.values.begin(), factorResult.values.end());
        rankedValues.erase(
            std::remove_if(rankedValues.begin(), rankedValues.end(), [&](const auto& item) {
                return returnIt->second->values.find(item.first) == returnIt->second->values.end();
            }),
            rankedValues.end()
        );

        if (rankedValues.empty()) {
            continue;
        }

        ++overlapDateCount;
        maxMatchedStocks = std::max(maxMatchedStocks, rankedValues.size());

        std::sort(rankedValues.begin(), rankedValues.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.second > rhs.second;
        });

        const int effectiveGroupCount = std::max(1, std::min(config.numGroups, static_cast<int>(rankedValues.size())));
        maxEffectiveGroupCount = std::max(maxEffectiveGroupCount, effectiveGroupCount);
        const size_t groupSize = std::max<size_t>(1, rankedValues.size() / static_cast<size_t>(effectiveGroupCount));
        bool dateGrouped = false;
        for (int groupIndex = 0; groupIndex < effectiveGroupCount; ++groupIndex) {
            const size_t begin = static_cast<size_t>(groupIndex) * groupSize;
            const size_t end = groupIndex == effectiveGroupCount - 1
                ? rankedValues.size()
                : std::min(rankedValues.size(), begin + groupSize);

            if (begin >= end) {
                continue;
            }

            double groupReturn = 0.0;
            int sampleCount = 0;
            double minFactorValue = std::numeric_limits<double>::max();
            double maxFactorValue = std::numeric_limits<double>::lowest();
            for (size_t i = begin; i < end; ++i) {
                const auto returnValue = returnIt->second->values.at(rankedValues[i].first);
                groupReturn += returnValue;
                ++sampleCount;
                minFactorValue = std::min(minFactorValue, rankedValues[i].second);
                maxFactorValue = std::max(maxFactorValue, rankedValues[i].second);
            }

            if (sampleCount == 0) {
                continue;
            }

            aggregatedReturns[groupIndex] += groupReturn / static_cast<double>(sampleCount);
            aggregatedCounts[groupIndex] += 1;
            aggregatedStockCounts[groupIndex] += sampleCount;
            aggregatedMinFactorValues[groupIndex] = std::min(aggregatedMinFactorValues[groupIndex], minFactorValue);
            aggregatedMaxFactorValues[groupIndex] = std::max(aggregatedMaxFactorValues[groupIndex], maxFactorValue);
            dateGrouped = true;
        }

        if (dateGrouped) {
            ++groupedDateCount;
        }
    }

    const int outputGroupCount = std::max(0, maxEffectiveGroupCount);
    groupResult.groupReturns.resize(outputGroupCount, 0.0);
    groupResult.groupStockCounts.resize(outputGroupCount, 0);
    groupResult.minFactorValues.resize(outputGroupCount, 0.0);
    groupResult.maxFactorValues.resize(outputGroupCount, 0.0);
    bool hasValidGroup = false;
    for (int i = 0; i < outputGroupCount; ++i) {
        if (aggregatedCounts[i] > 0) {
            hasValidGroup = true;
            groupResult.groupReturns[i] = aggregatedReturns[i] / static_cast<double>(aggregatedCounts[i]);
            groupResult.groupStockCounts[i] = aggregatedStockCounts[i] / aggregatedCounts[i];
            groupResult.minFactorValues[i] = aggregatedMinFactorValues[i];
            groupResult.maxFactorValues[i] = aggregatedMaxFactorValues[i];
        }
    }

    if (hasValidGroup && groupResult.groupReturns.size() >= 2) {
        groupResult.topGroupReturn = groupResult.groupReturns.front();
        groupResult.bottomGroupReturn = groupResult.groupReturns.back();
        groupResult.longShortReturn = groupResult.topGroupReturn - groupResult.bottomGroupReturn - (2.0 * config.transactionCost);
    } else if (!hasValidGroup && failureReason) {
        if (returnResults.empty()) {
            *failureReason = "未生成未来收益序列，请检查所选数据区间是否至少覆盖到下一个交易日";
        } else if (overlapDateCount == 0) {
            *failureReason = "因子值与未来收益没有重叠样本，无法执行分组回测";
        } else if (maxMatchedStocks <= 1) {
            *failureReason = "有效股票数不足，至少需要 2 只股票才能执行分组回测";
        } else {
            *failureReason = "未生成有效分组回测结果：最大有效股票数为 " + std::to_string(maxMatchedStocks)
                + "，请求分组数为 " + std::to_string(config.numGroups)
                + "，成功分组日期数为 " + std::to_string(groupedDateCount);
        }
    }

    return hasValidGroup && groupResult.groupReturns.size() >= 2;
}

std::vector<std::string> FactorBacktestExecutor::getTradeDates(const std::string& startDate,
                                                               const std::string& endDate,
                                                               const BacktestConfig& config)
{
    if (!config.cachedBars.empty()) {
        const auto cachedTradeDates = extractTradeDatesFromCachedBars(config.cachedBars, startDate, endDate);
        if (!cachedTradeDates.empty()) {
            return cachedTradeDates;
        }
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
        const auto cachedSymbols = extractSymbolsFromCachedBars(config.cachedBars, date, allowedSymbols);
        if (!cachedSymbols.empty()) {
            return cachedSymbols;
        }
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
        const double cachedFutureReturn = calculateFutureReturnFromCachedBars(config.cachedBars, symbol, startDate, forwardDays);
        if (std::isfinite(cachedFutureReturn)) {
            return cachedFutureReturn;
        }
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