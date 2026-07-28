#include "domain/factor/include/HighFreqFactor.h"
#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace factor {

namespace {

constexpr double kEpsilon = 1e-12;  // 统一防除零常量

/// @brief 从 HistoricalDataPoint 序列提取 double 值，并过滤非有限值
std::vector<double> extractValues(const std::vector<HistoricalDataPoint>& series)
{
    std::vector<double> values;
    values.reserve(series.size());
    for (const auto& dp : series) {
        if (std::isfinite(dp.value)) {
            values.push_back(dp.value);
        }
    }
    return values;
}

} // namespace

// ============================================================================
// 辅助函数
// ============================================================================
bool HighFreqFactor::isCumulative(const std::vector<double>& series)
{
    if (series.size() < 2) return false;
    for (size_t i = 1; i < series.size(); ++i) {
        if (!std::isfinite(series[i]) || !std::isfinite(series[i - 1]))
            return false;
        if (series[i] <= series[i - 1])
            return false;
    }
    return true;
}

std::vector<double> HighFreqFactor::diff(const std::vector<double>& series)
{
    if (series.size() < 2) return {};
    std::vector<double> result;
    result.reserve(series.size() - 1);
    for (size_t i = 1; i < series.size(); ++i) {
        result.push_back(series[i] - series[i - 1]);
    }
    return result;
}

double HighFreqFactor::movingAverage(
    const std::vector<double>& series, int window, int endIdx)
{
    if (window <= 0 || endIdx < 0) return 0.0;
    const int start = (std::max)(0, endIdx - window + 1);
    const int count = endIdx - start + 1;
    if (count <= 0) return 0.0;
    double sum = 0.0;
    int valid = 0;
    for (int i = start; i <= endIdx; ++i) {
        if (std::isfinite(series[static_cast<size_t>(i)])) {
            sum += series[static_cast<size_t>(i)];
            ++valid;
        }
    }
    return valid > 0 ? sum / static_cast<double>(valid) : 0.0;
}

// ============================================================================
// 构造函数
// ============================================================================
HighFreqFactor::HighFreqFactor()
{
    factorType_ = FactorType::HIGH_FREQ;
}

// ============================================================================
// Smart Money — 聪明钱因子
// ============================================================================
double HighFreqFactor::smartMoneySignal(
    const std::vector<double>& returns,
    const std::vector<double>& volumes) const
{
    const int N = static_cast<int>(returns.size());
    if (N < 2) return 0.0;

    // Step 1: 计算每日/窗口聪明钱分数 S_i = |R_i| / V_i^0.25
    struct ScoredDay {
        double ret;
        double volume;
        double score;
    };
    std::vector<ScoredDay> days;
    days.reserve(N);
    for (int i = 0; i < N; ++i) {
        const double R = returns[static_cast<size_t>(i)];
        const double V = volumes[static_cast<size_t>(i)];
        if (!std::isfinite(R) || !std::isfinite(V) || V < 0.0) continue;
        const double absR = std::abs(R);
        const double denom = std::pow((std::max)(V, kEpsilon), 0.25) + kEpsilon;
        days.push_back({R, V, absR / denom});
    }
    if (days.empty()) return 0.0;

    // Step 2: 按 S_i 降序排列，取前 k = max(1, floor(N * percentile))
    std::sort(days.begin(), days.end(),
        [](const ScoredDay& a, const ScoredDay& b) {
            return a.score > b.score;
        });

    const double percentile = (std::max)(0.01, (std::min)(params_.percentile, 1.0));
    const int k = (std::max)(1, static_cast<int>(std::floor(
        static_cast<double>(days.size()) * percentile)));

    // Step 3: 按 aggregation 聚合
    double signal = 0.0;
    switch (params_.aggregation) {
    case HFAggregation::MAX: {
        // 取最大 S 值对应的收益（已在排好序的 days[0]）
        signal = -days[0].ret;
        break;
    }
    case HFAggregation::CUMULATIVE: {
        // Top-k 收益简单累加
        double sumR = 0.0;
        for (int i = 0; i < k; ++i) sumR += days[static_cast<size_t>(i)].ret;
        signal = -sumR;
        break;
    }
    case HFAggregation::MEAN:
    default: {
        // VWAP 加权平均
        double weightedR = 0.0;
        double totalV = 0.0;
        for (int i = 0; i < k; ++i) {
            const double w = (std::max)(days[static_cast<size_t>(i)].volume, kEpsilon);
            weightedR += days[static_cast<size_t>(i)].ret * w;
            totalV += w;
        }
        signal = -(weightedR / (totalV + kEpsilon));
        break;
    }
    }

    return std::isfinite(signal) ? signal : 0.0;
}

// ============================================================================
// Realized Moments — 已实现高阶矩
// ============================================================================
double HighFreqFactor::realizedMoment(const std::vector<double>& returns) const
{
    const double N = static_cast<double>(returns.size());
    if (N < 2) return 0.0;

    // Step 1: 计算核心统计量
    double s2 = 0.0, s3 = 0.0, s4 = 0.0;
    int valid = 0;
    for (const auto& r : returns) {
        if (!std::isfinite(r)) continue;
        const double r2 = r * r;
        s2 += r2;
        s3 += r * r2;
        s4 += r2 * r2;
        ++valid;
    }
    if (valid < 2 || s2 < kEpsilon) return 0.0;

    // Step 2: 按 momentType 计算
    switch (params_.momentType) {
    case HFMomentType::VARIANCE: {
        // 已实现方差 → 取负（低波动=好）
        return -s2;
    }
    case HFMomentType::SKEWNESS: {
        // 已实现偏度 RSk = √N × ΣR³ / (ΣR²)^(3/2)
        const double rvPow = std::pow(s2, 1.5);  // (ΣR²)^(3/2)
        if (rvPow < kEpsilon) return 0.0;
        const double rsk = std::sqrt(N) * s3 / rvPow;
        return std::isfinite(rsk) ? rsk : 0.0;  // 正偏=好
    }
    case HFMomentType::KURTOSIS: {
        // 已实现峰度 RK = N × ΣR⁴ / (ΣR²)²
        const double rvSq = s2 * s2;  // (ΣR²)²
        if (rvSq < kEpsilon) return 0.0;
        const double rk = N * s4 / rvSq;
        // 超额峰度取负（低峰度=稳定=好）
        const double excess = rk - 3.0;
        return std::isfinite(excess) ? -excess : 0.0;
    }
    default:
        return 0.0;
    }
}

// ============================================================================
// Volume-Price — 量价关系
// ============================================================================
double HighFreqFactor::volumePriceSignal(
    const std::vector<double>& returns,
    const std::vector<double>& volumes,
    const std::vector<double>& opens,
    const std::vector<double>& highs,
    const std::vector<double>& lows) const
{
    const int N = static_cast<int>(returns.size());
    const int usedLookback = (std::min)(params_.lookbackDays, N);
    if (N < 2 || usedLookback < 2) return 0.0;

    // Step 1: 计算衍生序列
    std::vector<double> absR(N);
    std::vector<double> amplitudes(N);
    for (int i = 0; i < N; ++i) {
        absR[static_cast<size_t>(i)] = std::abs(returns[static_cast<size_t>(i)]);
        const double O = opens[static_cast<size_t>(i)];
        const double H = highs[static_cast<size_t>(i)];
        const double L = lows[static_cast<size_t>(i)];
        amplitudes[static_cast<size_t>(i)] = (H - L) / (O + kEpsilon);
    }

    // Step 2: 滚动窗口计算信号序列
    std::vector<double> signals;
    signals.reserve(N - usedLookback + 1);

    for (int i = usedLookback - 1; i < N; ++i) {
        const int subStart = i - usedLookback + 1;
        const int subLen = usedLookback;

        // 2a. 子窗口均值
        double meanAbsR = 0.0, meanV = 0.0;
        int validCount = 0;
        for (int j = subStart; j <= i; ++j) {
            const double ar = absR[static_cast<size_t>(j)];
            const double v = volumes[static_cast<size_t>(j)];
            if (std::isfinite(ar) && std::isfinite(v)) {
                meanAbsR += ar;
                meanV += v;
                ++validCount;
            }
        }
        if (validCount < 2) continue;
        meanAbsR /= static_cast<double>(validCount);
        meanV /= static_cast<double>(validCount);

        // 2b. Pearson 相关系数
        double cov = 0.0, varR = 0.0, varV = 0.0;
        for (int j = subStart; j <= i; ++j) {
            const double ar = absR[static_cast<size_t>(j)];
            const double v = volumes[static_cast<size_t>(j)];
            if (!std::isfinite(ar) || !std::isfinite(v)) continue;
            const double dR = ar - meanAbsR;
            const double dV = v - meanV;
            cov += dR * dV;
            varR += dR * dR;
            varV += dV * dV;
        }
        double corr = 0.0;
        const double denom = std::sqrt(varR * varV) + kEpsilon;
        if (denom > kEpsilon) {
            corr = cov / denom;
        }

        // 2c. 相对成交量和振幅比
        const double maV = movingAverage(volumes, usedLookback, i);
        const double maA = movingAverage(amplitudes, usedLookback, i);
        const double relV = volumes[static_cast<size_t>(i)] / (maV + kEpsilon);
        const double ampR = amplitudes[static_cast<size_t>(i)] / (maA + kEpsilon);

        // 2d. 复合信号
        const double sig = -0.5 * corr - 0.3 * (relV - 1.0) + 0.2 * (ampR - 1.0);
        if (std::isfinite(sig)) {
            signals.push_back(sig);
        }
    }

    if (signals.empty()) return 0.0;

    // Step 3: 聚合
    switch (params_.aggregation) {
    case HFAggregation::CUMULATIVE: {
        double sum = 0.0;
        for (const auto s : signals) sum += s;
        return sum;
    }
    case HFAggregation::MAX: {
        return *std::max_element(signals.begin(), signals.end());
    }
    case HFAggregation::MEAN:
    default: {
        double sum = 0.0;
        for (const auto s : signals) sum += s;
        return sum / static_cast<double>(signals.size());
    }
    }
}

// ============================================================================
// calculate() — 主入口
// ============================================================================
CalculationResult HighFreqFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(context,
            "高频因子需要 HistoricalView");
    }

    // 字段可用性检查
    if (!context.historicalView->hasField("close_minute")) {
        CalculationResult result;
        result.date = context.date;
        result.dataStatus.availability = DataAvailability::UNAVAILABLE;
        result.dataStatus.message = "高频因子需要 minute 字段（close_minute 等）";
        result.metadata.set("error",
            json_helper::toJsonValue("minute fields unavailable"));
        result.metadata.set("emptyReason",
            json_helper::toJsonValue("minute fields unavailable"));
        return result;
    }

    const CommonParams& common = params_;
    const auto symbols = effectiveSymbols(context);

    // barFrequency 换算: 5分钟线一天约 240/5=48 根 bar, 频率越高有效数据点越多
    // usedLookback 按频率缩放: 基准 5min, 10min 需 2x 回看天数获得相同数据量
    const int barsPerDay = 240 / (std::max)(params_.barFrequency, 1);
    const int freqScale = (std::max)(1, params_.barFrequency / 5);
    const int usedLookback = (std::min)(params_.lookbackDays, params_.window) * freqScale;
    const int dataWindow = params_.window + usedLookback + 1;

    return executeWithCommonParams(
        context,
        common,
        [&]() { return context.date; },
        [&](const CommonRuntimeState& runtime, CalculationResult& result) {
            int skippedSymbols = 0;

            for (const auto& symbol : symbols) {
                // 1. 获取 close_minute 序列
                auto closeSeries = context.historicalView->getSeries(
                    symbol, runtime.effectiveDate, dataWindow, "close_minute");
                auto closeVals = extractValues(closeSeries);
                if (closeVals.size() < 2) {
                    ++skippedSymbols;
                    continue;
                }

                // 2. 获取 volume_minute 序列
                auto rawVolSeries = context.historicalView->getSeries(
                    symbol, runtime.effectiveDate, dataWindow, "volume_minute");
                auto rawVolVals = extractValues(rawVolSeries);

                // 自动检测累积值并差分
                std::vector<double> volVals;
                if (isCumulative(rawVolVals)) {
                    volVals = diff(rawVolVals);
                } else {
                    volVals = std::move(rawVolVals);
                }
                if (volVals.empty()) {
                    ++skippedSymbols;
                    continue;
                }

                // 3. 截取 window 长度的收益率和成交量
                const int maxLen = (std::min)(
                    static_cast<int>(closeVals.size()) - 1,
                    static_cast<int>(volVals.size()));
                const int usedLen = (std::min)(maxLen, params_.window);
                if (usedLen < 2) {
                    ++skippedSymbols;
                    continue;
                }

                // 取尾部 usedLen + 1 个 close（收益率需要多 1 个前值用于差分）
                const int closeOffset = static_cast<int>(closeVals.size()) - usedLen - 1;
                const int volOffset = static_cast<int>(volVals.size()) - usedLen;

                std::vector<double> R(usedLen);
                for (int i = 0; i < usedLen; ++i) {
                    const double prev = closeVals[static_cast<size_t>(closeOffset + i)];
                    const double curr = closeVals[static_cast<size_t>(closeOffset + i + 1)];
                    if (std::isfinite(prev) && std::isfinite(curr)
                        && std::abs(prev) > kEpsilon) {
                        R[static_cast<size_t>(i)] = (curr - prev) / prev;
                    }
                }

                std::vector<double> V(usedLen);
                for (int i = 0; i < usedLen; ++i) {
                    V[static_cast<size_t>(i)] = volVals[static_cast<size_t>(volOffset + i)];
                }

                // 4. 按 method 分发
                double signal = 0.0;
                switch (params_.method) {
                case HFMethod::SMART_MONEY:
                    signal = smartMoneySignal(R, V);
                    break;
                case HFMethod::REALIZED_MOMENTS:
                    signal = realizedMoment(R);
                    break;
                case HFMethod::VOLUME_PRICE: {
                    // 额外获取 open/high/low_minute
                    auto openSeries = context.historicalView->getSeries(
                        symbol, runtime.effectiveDate, dataWindow, "open_minute");
                    auto highSeries = context.historicalView->getSeries(
                        symbol, runtime.effectiveDate, dataWindow, "high_minute");
                    auto lowSeries = context.historicalView->getSeries(
                        symbol, runtime.effectiveDate, dataWindow, "low_minute");
                    auto O = extractValues(openSeries);
                    auto H = extractValues(highSeries);
                    auto L = extractValues(lowSeries);

                    if (O.size() < static_cast<size_t>(usedLen)
                        || H.size() < static_cast<size_t>(usedLen)
                        || L.size() < static_cast<size_t>(usedLen)) {
                        ++skippedSymbols;
                        continue;
                    }
                    // 取尾部
                    const int ohlcOffset = static_cast<int>(O.size()) - usedLen;
                    std::vector<double> O2(usedLen), H2(usedLen), L2(usedLen);
                    for (int i = 0; i < usedLen; ++i) {
                        O2[static_cast<size_t>(i)] = O[static_cast<size_t>(ohlcOffset + i)];
                        H2[static_cast<size_t>(i)] = H[static_cast<size_t>(ohlcOffset + i)];
                        L2[static_cast<size_t>(i)] = L[static_cast<size_t>(ohlcOffset + i)];
                    }
                    signal = volumePriceSignal(R, V, O2, H2, L2);
                    break;
                }
                default:
                    break;
                }

                // threshold 过滤弱信号
                if (std::isfinite(signal) && std::abs(signal) >= params_.threshold) {
                    result.values[symbol] = signal;
                } else if (std::isfinite(signal)) {
                    ++skippedSymbols;  // 被 threshold 过滤
                }
            }

            if (result.values.empty()) {
                result.metadata.set("emptyReason",
                    json_helper::toJsonValue(
                        std::to_string(skippedSymbols) + " 只股票数据不足"));
            } else if (skippedSymbols > 0) {
                result.metadata.set("skippedSymbols",
                    json_helper::toJsonValue(skippedSymbols));
            }
        },
        [](const CommonRuntimeState&, CalculationResult&) {},
        [&](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("method",
                json_helper::toJsonValue(static_cast<int>(params_.method)));
            result.metadata.set("window",
                json_helper::toJsonValue(params_.window));
            result.metadata.set("lookbackDays",
                json_helper::toJsonValue(params_.lookbackDays));
        });
}

// ============================================================================
// create() — 工厂方法
// ============================================================================
std::shared_ptr<HighFreqFactor> HighFreqFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<HighFreqFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    return factor;
}

// ============================================================================
// getDataRequirements() — 数据需求
// ============================================================================
DataRequirements HighFreqFactor::getDataRequirements() const
{
    DataRequirements req;
    appendRequiredField(req, "close_minute");
    appendRequiredField(req, "volume_minute");
    appendRequiredField(req, "open_minute");
    appendRequiredField(req, "high_minute");
    appendRequiredField(req, "low_minute");
    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

// ============================================================================
// getBoundaryRules() — 边界规则
// ============================================================================
BoundaryRules HighFreqFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints, params_.window);
    return rules;
}

// ============================================================================
// loadConfig() — 配置加载
// ============================================================================
void HighFreqFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (config::hasCalculationConfig(config))
        params_.fromJson(config::calculationConfig(config));
    dataRequirements_ = getDataRequirements();
}

} // namespace factor
