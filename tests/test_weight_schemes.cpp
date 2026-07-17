#include "RuntimeStrategyFactory.h"
#include "IStrategyService.h"
#include "MultiFactorSelectionStrategy.h"
#include "factor_compute/IMarketDataView.h"
#include "domain/market/include/MarketDataService.h"
#include "engine/include/GmSessionEngine.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/// @brief 权重方案单元测试 (市值加权 / 风险平价)
///
/// 验收标准:
/// - MARKET_CAP: 权重 = 标的市值 / 候选总市值
/// - RISK_PARITY: 权重与日波动率成反比 (低波动 > 高波动)
/// - 视图缺失 / market_cap 字段缺失 → 回退等权, 不崩溃
/// - currentEvaluationRow 生效 (回测时点语义, 无前视偏差)
namespace {

constexpr int kExitSuccess = 0;
constexpr int kExitFailure = 1;

constexpr int kDayCount = 30;
constexpr int kSymbolCount = 7;
constexpr double kWeightEpsilon = 1e-9;

// 候选标的: z-score > 0 的 s5/s6/s7 (因子值 10/20/30, 其余为 0)
constexpr std::uint32_t kLowVolSymbol = 5;   // 低波动, 末行市值 100
constexpr std::uint32_t kMidVolSymbol = 6;   // 中波动, 末行市值 300
constexpr std::uint32_t kHighVolSymbol = 7;  // 高波动, 末行市值 600

/// @brief 行情视图测试替身: 7 标的 × 30 日, close + market_cap 两个矩阵
///
/// market_cap 行 0..10 与行 11..29 取值相反, 用于验证 evaluationRow 时点语义:
///   行 ≤ 10: s5=600, s6=300, s7=100 (反转)
///   行 > 10: s5=100, s6=300, s7=600 (正常)
class FakeMarketDataView final : public factor::compute::IMarketDataView {
public:
    explicit FakeMarketDataView(bool withMarketCapField)
        : m_withMarketCapField(withMarketCapField)
    {
        m_dates.resize(kDayCount);
        for (int r = 0; r < kDayCount; ++r) m_dates[r] = factor::compute::DateKey{20260101 + r};
        m_instruments.reserve(kSymbolCount);
        m_symbolStrings.reserve(kSymbolCount);
        for (std::uint32_t i = 1; i <= kSymbolCount; ++i) {
            m_instruments.push_back(factor::compute::InstrumentId(i));
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%06u", i);
            m_symbolStrings.emplace_back(buf);
        }
        buildCloseMatrix();
        buildMarketCapMatrix();
    }

    [[nodiscard]] factor::compute::NumericConstMatrixView open() const override { return closeView(); }
    [[nodiscard]] factor::compute::NumericConstMatrixView high() const override { return closeView(); }
    [[nodiscard]] factor::compute::NumericConstMatrixView low() const override { return closeView(); }
    [[nodiscard]] factor::compute::NumericConstMatrixView close() const override { return closeView(); }
    [[nodiscard]] factor::compute::NumericConstMatrixView volume() const override { return closeView(); }

    [[nodiscard]] std::optional<factor::compute::NumericConstMatrixView>
    getField(const std::string& fieldName) const override
    {
        if (m_withMarketCapField && fieldName == "market_cap") {
            return factor::compute::NumericConstMatrixView{
                m_marketCap.data(), kDayCount, kSymbolCount, kSymbolCount};
        }
        return std::nullopt;
    }

    [[nodiscard]] const std::vector<factor::compute::DateKey>& dates() const override { return m_dates; }
    [[nodiscard]] const std::vector<factor::compute::InstrumentId>& instruments() const override { return m_instruments; }
    [[nodiscard]] const std::vector<std::string>& symbolStrings() const override { return m_symbolStrings; }

    [[nodiscard]] std::unique_ptr<IMarketDataView> slice(factor::compute::DateRange) const override { return nullptr; }
    [[nodiscard]] std::unique_ptr<IMarketDataView>
    slice(const std::vector<factor::compute::InstrumentId>&) const override { return nullptr; }

private:
    [[nodiscard]] factor::compute::NumericConstMatrixView closeView() const
    {
        return factor::compute::NumericConstMatrixView{
            m_close.data(), kDayCount, kSymbolCount, kSymbolCount};
    }

    /// s5 微幅震荡(±0.05%), s6 中幅(±2%), s7 大幅(±5%), 其余恒定 10.0
    void buildCloseMatrix()
    {
        m_close.assign(static_cast<std::size_t>(kDayCount) * kSymbolCount, 10.0f);
        for (int r = 0; r < kDayCount; ++r) {
            const bool oddRow = (r % 2) != 0;
            m_close[idx(r, kLowVolSymbol)] = oddRow ? 10.01f : 10.0f;
            m_close[idx(r, kMidVolSymbol)] = oddRow ? 10.2f : 9.8f;
            m_close[idx(r, kHighVolSymbol)] = oddRow ? 10.5f : 9.5f;
        }
    }

    void buildMarketCapMatrix()
    {
        m_marketCap.assign(static_cast<std::size_t>(kDayCount) * kSymbolCount, 50.0f);
        for (int r = 0; r < kDayCount; ++r) {
            const bool earlyRows = (r <= 10);
            m_marketCap[idx(r, kLowVolSymbol)] = earlyRows ? 600.0f : 100.0f;
            m_marketCap[idx(r, kMidVolSymbol)] = 300.0f;
            m_marketCap[idx(r, kHighVolSymbol)] = earlyRows ? 100.0f : 600.0f;
        }
    }

    [[nodiscard]] static std::size_t idx(int row, std::uint32_t symbolId)
    {
        // symbolId 1..7 → 列 0..6
        return static_cast<std::size_t>(row) * kSymbolCount + (symbolId - 1);
    }

    bool m_withMarketCapField;
    std::vector<factor::compute::DateKey> m_dates;
    std::vector<factor::compute::InstrumentId> m_instruments;
    std::vector<std::string> m_symbolStrings;
    std::vector<factor::compute::signal_value_t> m_close;
    std::vector<factor::compute::signal_value_t> m_marketCap;
};

/// @brief 构建指定权重方案的多因子策略定义
[[nodiscard]] std::shared_ptr<const domain::strategies::MultiFactorSelectionStrategy>
makeStrategy(domain::strategies::WeightScheme scheme)
{
    domain::strategies::StrategyCommonConfig commonCfg;
    commonCfg.allowShort = false;
    commonCfg.maxPositions = 10;
    commonCfg.maxWeightPerStock = 1.0;   // 不截断, 便于断言原始比例
    commonCfg.minWeightPerStock = 0.0;
    commonCfg.weightScheme = scheme;

    domain::strategies::StrategyMetadata meta;
    meta.name = "weight-scheme-test";
    meta.enabled = true;

    domain::strategies::MultiFactorSelectionStrategySpec spec;
    spec.topN = 3;
    spec.factorWeights.push_back({"f1", 1.0});

    return std::make_shared<domain::strategies::MultiFactorSelectionStrategy>(commonCfg, meta, spec);
}

/// @brief 跑一次 evaluate, 返回 symbolId → targetWeight
[[nodiscard]] std::unordered_map<std::uint32_t, double> runEvaluate(
    domain::strategies::WeightScheme scheme,
    const factor::compute::IMarketDataView* view,
    int evaluationRow)
{
    constexpr domain::strategy::StrategyInstanceId kInstanceId = 1;
    auto runtimeStrategy = domain::strategy::createMultiFactorSelectionRuntimeStrategy(
        makeStrategy(scheme), kInstanceId);

    domain::strategy::RuntimeStrategyContext context(kInstanceId, 1, 1000, 1.0, true);
    context.setHistoricalView(view);
    context.setCurrentEvaluationRow(evaluationRow);

    // 因子值: s1..s4=0, s5=10, s6=20, s7=30 → z-score 仅 s5/s6/s7 为正
    std::vector<domain::strategy::RuntimeFactorSnapshot> snapshots;
    const double factorValues[kSymbolCount] = {0, 0, 0, 0, 10, 20, 30};
    for (std::uint32_t i = 1; i <= kSymbolCount; ++i) {
        snapshots.push_back({i, "f1", factorValues[i - 1], 0});
    }

    std::vector<domain::strategy::StrategySignal> signals;
    runtimeStrategy->evaluate(snapshots, context, signals);

    std::unordered_map<std::uint32_t, double> weights;
    for (const auto& signal : signals) {
        weights[signal.instrumentId().value] = signal.targetWeight();
    }
    return weights;
}

[[nodiscard]] bool approxEqual(double actual, double expected)
{
    return std::fabs(actual - expected) < kWeightEpsilon;
}

[[nodiscard]] bool checkWeight(const std::unordered_map<std::uint32_t, double>& weights,
                               std::uint32_t symbolId, double expected, const char* label)
{
    const auto it = weights.find(symbolId);
    if (it == weights.end()) {
        std::printf("[FAIL] %s: symbol %u 无信号\n", label, symbolId);
        return false;
    }
    if (!approxEqual(it->second, expected)) {
        std::printf("[FAIL] %s: symbol %u weight=%.9f 期望 %.9f\n",
                    label, symbolId, it->second, expected);
        return false;
    }
    return true;
}

/// @brief 基线: 等权方案 3 候选各 1/3
bool testEqualWeightBaseline()
{
    FakeMarketDataView view(true);
    const auto weights = runEvaluate(domain::strategies::WeightScheme::EQUAL, &view, -1);
    if (weights.size() != 3) {
        std::printf("[FAIL] EQUAL: 信号数=%zu 期望 3\n", weights.size());
        return false;
    }
    const double kThird = 1.0 / 3.0;
    return checkWeight(weights, kLowVolSymbol, kThird, "EQUAL")
        && checkWeight(weights, kMidVolSymbol, kThird, "EQUAL")
        && checkWeight(weights, kHighVolSymbol, kThird, "EQUAL");
}

/// @brief 市值加权: 末行市值 100/300/600 → 权重 0.1/0.3/0.6
bool testMarketCapWeights()
{
    FakeMarketDataView view(true);
    const auto weights = runEvaluate(domain::strategies::WeightScheme::MARKET_CAP, &view, -1);
    return checkWeight(weights, kLowVolSymbol, 0.1, "MARKET_CAP")
        && checkWeight(weights, kMidVolSymbol, 0.3, "MARKET_CAP")
        && checkWeight(weights, kHighVolSymbol, 0.6, "MARKET_CAP");
}

/// @brief 风险平价: 权重排序与波动率反向 (s5 > s6 > s7), 且归一化
bool testRiskParityWeights()
{
    FakeMarketDataView view(true);
    const auto weights = runEvaluate(domain::strategies::WeightScheme::RISK_PARITY, &view, -1);
    if (weights.size() != 3) {
        std::printf("[FAIL] RISK_PARITY: 信号数=%zu 期望 3\n", weights.size());
        return false;
    }
    const double wLow = weights.at(kLowVolSymbol);
    const double wMid = weights.at(kMidVolSymbol);
    const double wHigh = weights.at(kHighVolSymbol);
    if (!(wLow > wMid && wMid > wHigh)) {
        std::printf("[FAIL] RISK_PARITY: 权重排序错误 low=%.6f mid=%.6f high=%.6f\n",
                    wLow, wMid, wHigh);
        return false;
    }
    if (!approxEqual(wLow + wMid + wHigh, 1.0)) {
        std::printf("[FAIL] RISK_PARITY: 权重和=%.9f 期望 1.0\n", wLow + wMid + wHigh);
        return false;
    }
    return true;
}

/// @brief 回退: 视图为空 / market_cap 字段缺失 → 等权
bool testFallbackToEqualWeight()
{
    const double kThird = 1.0 / 3.0;

    const auto nullViewWeights =
        runEvaluate(domain::strategies::WeightScheme::MARKET_CAP, nullptr, -1);
    if (!checkWeight(nullViewWeights, kLowVolSymbol, kThird, "MARKET_CAP(null view)")
        || !checkWeight(nullViewWeights, kHighVolSymbol, kThird, "MARKET_CAP(null view)")) {
        return false;
    }

    FakeMarketDataView viewWithoutCap(false);
    const auto noFieldWeights =
        runEvaluate(domain::strategies::WeightScheme::MARKET_CAP, &viewWithoutCap, -1);
    return checkWeight(noFieldWeights, kLowVolSymbol, kThird, "MARKET_CAP(no field)")
        && checkWeight(noFieldWeights, kHighVolSymbol, kThird, "MARKET_CAP(no field)");
}

/// @brief 时点语义: 行 10 的市值与末行相反 → 权重随 evaluationRow 反转
bool testEvaluationRowPointInTime()
{
    FakeMarketDataView view(true);
    const auto weights = runEvaluate(domain::strategies::WeightScheme::MARKET_CAP, &view, 10);
    return checkWeight(weights, kLowVolSymbol, 0.6, "MARKET_CAP(row=10)")
        && checkWeight(weights, kMidVolSymbol, 0.3, "MARKET_CAP(row=10)")
        && checkWeight(weights, kHighVolSymbol, 0.1, "MARKET_CAP(row=10)");
}

/// @brief 实时价缩放: EOD 时今日数据未入库 (activeTradingDay > 视图末日),
/// s5 今收=2×昨收 → T-1 市值 100 缩放为 200; 其余标的无 tick 不缩放。
/// 注意: 本用例向单例注入 tick 状态, 必须最后执行。
bool testLiveScalingWhenTodayBarMissing()
{
    // 注入 s5 ("000005") 的当日 tick: 交易日 20260131 > 视图末日 20260130
    engine::GmTickData tick;
    tick.symbol = "000005";
    tick.price = 20.0;
    tick.tradingDay = 20260131;
    domain::market::MarketDataService::instance().onTick(tick);
    domain::market::MarketDataService::instance().mutableLiveData("000005").setPreClose(10.0);

    FakeMarketDataView view(true);
    const auto weights = runEvaluate(domain::strategies::WeightScheme::MARKET_CAP, &view, -1);
    // 缩放后市值 200/300/600, 总计 1100
    return checkWeight(weights, kLowVolSymbol, 200.0 / 1100.0, "MARKET_CAP(live)")
        && checkWeight(weights, kMidVolSymbol, 300.0 / 1100.0, "MARKET_CAP(live)")
        && checkWeight(weights, kHighVolSymbol, 600.0 / 1100.0, "MARKET_CAP(live)");
}

} // anonymous namespace

int main()
{
    struct TestCase {
        const char* name;
        bool (*run)();
    };
    const TestCase cases[] = {
        {"等权基线", testEqualWeightBaseline},
        {"市值加权", testMarketCapWeights},
        {"风险平价", testRiskParityWeights},
        {"缺数据回退等权", testFallbackToEqualWeight},
        {"evaluationRow 时点语义", testEvaluationRowPointInTime},
        {"实时价缩放 T-1 市值", testLiveScalingWhenTodayBarMissing},  // 注入单例状态, 必须最后
    };

    int failedCount = 0;
    for (const auto& testCase : cases) {
        const bool passed = testCase.run();
        std::printf("[%s] %s\n", passed ? "PASS" : "FAIL", testCase.name);
        if (!passed) ++failedCount;
    }

    if (failedCount > 0) {
        std::printf("test_weight_schemes: %d 项失败\n", failedCount);
        return kExitFailure;
    }
    std::printf("test_weight_schemes: 全部通过\n");
    return kExitSuccess;
}
