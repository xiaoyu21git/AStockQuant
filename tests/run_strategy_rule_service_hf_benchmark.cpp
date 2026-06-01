#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "domain/strategy/include/IStrategyService.h"

namespace {

using Clock = std::chrono::steady_clock;
using Microseconds = std::chrono::microseconds;

constexpr domain::strategy::StrategyInstanceId kStrategyInstanceId = 1001;
constexpr std::uint32_t kStartInstrumentId = 600000;
constexpr domain::strategy::StrategyCount kLowLatencySignalCount = 1;
constexpr int kWarmupRounds = 20;
constexpr int kMeasureRounds = 200;

constexpr double kScorePass = 1.0;
constexpr double kScoreReject = -1.0;
constexpr double kTargetWeightPass = 0.25;
constexpr double kTargetWeightReject = 1.25;

constexpr double kSingleP99TargetUs = 200.0;
constexpr double kBatchPerSignalP99TargetUs = 50.0;

struct Percentiles final {
    double p50{0.0};
    double p95{0.0};
    double p99{0.0};
};

struct BenchmarkStats final {
    std::size_t totalSignals{0};
    double wallTotalUs{0.0};
    double wallPerSignalUs{0.0};
    double throughputSignalsPerSecond{0.0};
    Percentiles wallPercentiles{};
    Percentiles resultLatencyPercentiles{};
    domain::strategy::StrategyCount passedSignals{0};
    domain::strategy::StrategyCount rejectedSignals{0};
};

std::vector<domain::strategy::StrategySignal> buildSignals(std::size_t signalCount)
{
    std::vector<domain::strategy::StrategySignal> signals;
    signals.reserve(signalCount);

    for (std::size_t i = 0; i < signalCount; ++i) {
        const bool shouldReject = (i % 7 == 0);
        const auto instrument =
            domain::strategy::InstrumentId(static_cast<std::uint32_t>(kStartInstrumentId + i));
        signals.emplace_back(
            kStrategyInstanceId,
            instrument,
            domain::strategy::RuntimeOrderSide::Buy,
            shouldReject ? kScoreReject : kScorePass,
            shouldReject ? kTargetWeightReject : kTargetWeightPass);
    }

    return signals;
}

domain::strategy::rules::RuleEvaluationContext makeBatchContext(std::size_t signalCount)
{
    return domain::strategy::rules::RuleEvaluationContext(
        domain::strategy::rules::RuleEvaluationPhase::Batch,
        kStrategyInstanceId,
        signalCount);
}

domain::strategy::rules::RuleEvaluationContext makeLowLatencyContext()
{
    return domain::strategy::rules::RuleEvaluationContext(
        domain::strategy::rules::RuleEvaluationPhase::LowLatency,
        kStrategyInstanceId,
        kLowLatencySignalCount);
}

Percentiles computePercentiles(std::vector<double> values)
{
    if (values.empty()) {
        return Percentiles{};
    }

    std::sort(values.begin(), values.end());

    const auto pick = [&values](double ratio) {
        const std::size_t idx = static_cast<std::size_t>(
            std::min<double>((values.size() - 1), ratio * static_cast<double>(values.size() - 1)));
        return values[idx];
    };

    Percentiles p;
    p.p50 = pick(0.50);
    p.p95 = pick(0.95);
    p.p99 = pick(0.99);
    return p;
}

BenchmarkStats runBatchBenchmark(
    domain::strategy::IRuleEvaluationService& service,
    domain::strategy::rules::RuleSetId ruleSetId,
    const std::vector<domain::strategy::StrategySignal>& signals)
{
    BenchmarkStats stats;
    stats.totalSignals = signals.size() * static_cast<std::size_t>(kMeasureRounds);

    std::vector<domain::strategy::RuleEvaluationResult> results;
    const auto context = makeBatchContext(signals.size());

    for (int i = 0; i < kWarmupRounds; ++i) {
        const auto flow = service.evaluateBatch(signals, ruleSetId, context, results);
        if (!flow.isOk()) {
            std::cerr << "[ERROR] warmup evaluateBatch failed, flow="
                      << static_cast<int>(flow.code()) << std::endl;
            std::exit(2);
        }
    }

    std::vector<double> wallSamplesUs;
    wallSamplesUs.reserve(kMeasureRounds);
    std::vector<double> resultLatencySamplesUs;
    resultLatencySamplesUs.reserve(kMeasureRounds * signals.size());

    for (int i = 0; i < kMeasureRounds; ++i) {
        const auto beginAt = Clock::now();
        const auto flow = service.evaluateBatch(signals, ruleSetId, context, results);
        const auto endAt = Clock::now();

        if (!flow.isOk()) {
            std::cerr << "[ERROR] evaluateBatch failed, flow="
                      << static_cast<int>(flow.code()) << std::endl;
            std::exit(3);
        }

        const auto wallUs =
            std::chrono::duration_cast<Microseconds>(endAt - beginAt).count();
        wallSamplesUs.push_back(static_cast<double>(wallUs));

        for (const auto& result : results) {
            resultLatencySamplesUs.push_back(static_cast<double>(result.latency().count()));
            if (result.passed()) {
                ++stats.passedSignals;
            } else {
                ++stats.rejectedSignals;
            }
        }
    }

    stats.wallTotalUs = std::accumulate(wallSamplesUs.begin(), wallSamplesUs.end(), 0.0);
    stats.wallPerSignalUs = stats.wallTotalUs / static_cast<double>(stats.totalSignals);
    stats.throughputSignalsPerSecond =
        stats.totalSignals * 1000000.0 / std::max<double>(1.0, stats.wallTotalUs);
    stats.wallPercentiles = computePercentiles(wallSamplesUs);
    stats.resultLatencyPercentiles = computePercentiles(resultLatencySamplesUs);
    return stats;
}

BenchmarkStats runLowLatencyBenchmark(
    domain::strategy::IRuleEvaluationService& service,
    domain::strategy::rules::RuleSetId ruleSetId,
    const std::vector<domain::strategy::StrategySignal>& signals)
{
    BenchmarkStats stats;
    stats.totalSignals = signals.size() * static_cast<std::size_t>(kMeasureRounds);

    const auto context = makeLowLatencyContext();

    for (int i = 0; i < kWarmupRounds; ++i) {
        for (const auto& signal : signals) {
            (void)service.evaluate(signal, ruleSetId, context);
        }
    }

    std::vector<double> wallSamplesUs;
    wallSamplesUs.reserve(kMeasureRounds * signals.size());
    std::vector<double> resultLatencySamplesUs;
    resultLatencySamplesUs.reserve(kMeasureRounds * signals.size());

    for (int i = 0; i < kMeasureRounds; ++i) {
        for (const auto& signal : signals) {
            const auto beginAt = Clock::now();
            const auto result = service.evaluate(signal, ruleSetId, context);
            const auto endAt = Clock::now();

            const auto wallUs =
                std::chrono::duration_cast<Microseconds>(endAt - beginAt).count();
            wallSamplesUs.push_back(static_cast<double>(wallUs));
            resultLatencySamplesUs.push_back(static_cast<double>(result.latency().count()));

            if (result.passed()) {
                ++stats.passedSignals;
            } else {
                ++stats.rejectedSignals;
            }
        }
    }

    stats.wallTotalUs = std::accumulate(wallSamplesUs.begin(), wallSamplesUs.end(), 0.0);
    stats.wallPerSignalUs = stats.wallTotalUs / static_cast<double>(stats.totalSignals);
    stats.throughputSignalsPerSecond =
        stats.totalSignals * 1000000.0 / std::max<double>(1.0, stats.wallTotalUs);
    stats.wallPercentiles = computePercentiles(wallSamplesUs);
    stats.resultLatencyPercentiles = computePercentiles(resultLatencySamplesUs);
    return stats;
}

void printStats(const std::string& title, const BenchmarkStats& stats)
{
    std::cout << "\n[" << title << "]" << std::endl;
    std::cout << "  totalSignals          : " << stats.totalSignals << std::endl;
    std::cout << "  passed/rejected       : " << stats.passedSignals
              << " / " << stats.rejectedSignals << std::endl;
    std::cout << std::fixed << std::setprecision(2)
              << "  wallTotal(us)         : " << stats.wallTotalUs << std::endl
              << "  wallPerSignal(us)     : " << stats.wallPerSignalUs << std::endl
              << "  throughput(sig/s)     : " << stats.throughputSignalsPerSecond << std::endl
              << "  wall p50/p95/p99(us)  : "
              << stats.wallPercentiles.p50 << " / "
              << stats.wallPercentiles.p95 << " / "
              << stats.wallPercentiles.p99 << std::endl
              << "  result p50/p95/p99(us): "
              << stats.resultLatencyPercentiles.p50 << " / "
              << stats.resultLatencyPercentiles.p95 << " / "
              << stats.resultLatencyPercentiles.p99 << std::endl;
}

} // namespace

int main()
{
    domain::strategy::LocalRuleEvaluationService localService;
    if (!localService.isReady()) {
        std::cerr << "[ERROR] LocalRuleEvaluationService is not ready" << std::endl;
        return 1;
    }

    const std::vector<std::size_t> signalSizes = {64, 256, 1024};
    const auto ruleSetId = domain::strategy::rules::kRuleSetAllPass;

    bool allPass = true;

    for (const std::size_t signalSize : signalSizes) {
        const auto signals = buildSignals(signalSize);

        const BenchmarkStats batchStats = runBatchBenchmark(localService, ruleSetId, signals);
        const BenchmarkStats lowLatencyStats =
            runLowLatencyBenchmark(localService, ruleSetId, signals);

        printStats("batch-size=" + std::to_string(signalSize), batchStats);
        printStats("low-latency-size=" + std::to_string(signalSize), lowLatencyStats);

        const double batchP99PerSignal =
            batchStats.wallPercentiles.p99 / static_cast<double>(signalSize);
        const bool batchPass = batchP99PerSignal <= kBatchPerSignalP99TargetUs;
        const bool singlePass = lowLatencyStats.wallPercentiles.p99 <= kSingleP99TargetUs;

        std::cout << "  target-check(batch p99/signal <= " << kBatchPerSignalP99TargetUs
                  << "us): " << (batchPass ? "PASS" : "FAIL") << std::endl;
        std::cout << "  target-check(single p99 <= " << kSingleP99TargetUs
                  << "us): " << (singlePass ? "PASS" : "FAIL") << std::endl;

        allPass = allPass && batchPass && singlePass;
    }

    std::cout << "\n[SUMMARY] rule service HF benchmark "
              << (allPass ? "PASS" : "FAIL") << std::endl;

    // TEST-ONLY NOTE:
    // 1) This executable is isolated under tests/ and does not affect production runtime path.
    // 2) If you need to keep repository quiet after one-off profiling, comment out the CMake
    //    target block added for run_strategy_rule_service_hf_benchmark.

    return allPass ? 0 : 4;
}
