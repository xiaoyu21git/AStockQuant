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

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#ifdef signals
#undef signals
#endif

namespace {

using Clock = std::chrono::steady_clock;
using Microseconds = std::chrono::microseconds;

constexpr domain::strategy::StrategyInstanceId kStrategyInstanceId = 1001;
constexpr std::uint32_t kStartInstrumentId = 600000;
constexpr domain::strategy::StrategyCount kSingleSignalCount = 1;

constexpr int kWarmupRoundsBatch = 10;
constexpr int kMeasureRoundsBatch = 100;
constexpr int kWarmupRoundsSingle = 10;
constexpr int kMeasureRoundsSingle = 200;

constexpr double kScorePass = 1.0;
constexpr double kScoreReject = -1.0;
constexpr double kTargetWeightPass = 0.25;
constexpr double kTargetWeightReject = 1.25;

constexpr double kBatchPerSignalP99TargetUs = 500.0;
constexpr double kSingleP99TargetUs = 3000.0;

constexpr domain::strategy::rules::RuleSetId kPyBenchmarkRuleSetId = 1;

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

Percentiles computePercentiles(std::vector<double> values)
{
    if (values.empty()) {
        return Percentiles{};
    }

    std::sort(values.begin(), values.end());
    const auto pick = [&values](double ratio) {
        const std::size_t idx = static_cast<std::size_t>(
            std::min<double>(values.size() - 1, ratio * static_cast<double>(values.size() - 1)));
        return values[idx];
    };

    Percentiles p;
    p.p50 = pick(0.50);
    p.p95 = pick(0.95);
    p.p99 = pick(0.99);
    return p;
}

std::vector<domain::strategy::StrategySignal> buildSignals(std::size_t signalCount)
{
    std::vector<domain::strategy::StrategySignal> output;
    output.reserve(signalCount);

    for (std::size_t i = 0; i < signalCount; ++i) {
        const bool shouldReject = (i % 7 == 0);
        const auto instrument =
            domain::strategy::InstrumentId(static_cast<std::uint32_t>(kStartInstrumentId + i));
        output.emplace_back(
            kStrategyInstanceId,
            instrument,
            domain::strategy::RuntimeOrderSide::Buy,
            shouldReject ? kScoreReject : kScorePass,
            shouldReject ? kTargetWeightReject : kTargetWeightPass);
    }

    return output;
}

class PersistentPythonRuleAdapter final : public domain::strategy::IPythonRuleAdapter {
public:
    explicit PersistentPythonRuleAdapter(const QString& scriptPath)
    {
        QString program = qEnvironmentVariable("PYTHON_EXECUTABLE");
        if (program.trimmed().isEmpty()) {
            program = "python";
        }

        process_.setProgram(program);
        process_.setArguments({"-u", scriptPath});
        process_.setProcessChannelMode(QProcess::SeparateChannels);
        process_.start();

        if (!process_.waitForStarted(5000)) {
            std::cerr << "[ERROR] unable to start python worker" << std::endl;
            std::exit(10);
        }
    }

    ~PersistentPythonRuleAdapter() override
    {
        process_.closeWriteChannel();
        process_.terminate();
        if (!process_.waitForFinished(1000)) {
            process_.kill();
            (void)process_.waitForFinished(1000);
        }
    }

    [[nodiscard]] domain::strategy::StrategyServiceFlowResult checkBatch(
        const domain::strategy::PythonRuleBatchRequest& request,
        std::vector<domain::strategy::PythonRuleResult>& outputResults) override
    {
        outputResults.clear();

        if (process_.state() != QProcess::Running) {
            return domain::strategy::StrategyServiceFlowResult(
                domain::strategy::StrategyServiceFlowCode::RuleCheckFailed);
        }

        QJsonObject payload;
        QJsonArray signalsJson;
        for (const auto& signal : request.signals) {
            QJsonObject item;
            item.insert("strategy_instance_id", static_cast<qint64>(signal.strategyInstanceId()));
            item.insert("instrument_id", static_cast<int>(signal.instrumentId().value()));
            item.insert("score", signal.score());
            item.insert("target_weight", signal.targetWeight());
            signalsJson.push_back(item);
        }

        QJsonArray ruleIdsJson;
        for (const auto& descriptor : request.descriptors) {
            if (!descriptor.enabled()) {
                continue;
            }
            if (descriptor.kind() == domain::strategy::PythonRuleKind::ScoreNonNegative) {
                ruleIdsJson.push_back(1);
                continue;
            }
            if (descriptor.kind() == domain::strategy::PythonRuleKind::TargetWeightAbsoluteLimit) {
                ruleIdsJson.push_back(2);
                continue;
            }
            if (descriptor.kind() == domain::strategy::PythonRuleKind::Custom) {
                ruleIdsJson.push_back(static_cast<int>(descriptor.thresholdA()));
            }
        }

        payload.insert("signals", signalsJson);
        payload.insert("rule_ids", ruleIdsJson);

        const QByteArray line = QJsonDocument(payload).toJson(QJsonDocument::Compact) + "\n";
        if (process_.write(line) != line.size()) {
            return domain::strategy::StrategyServiceFlowResult(
                domain::strategy::StrategyServiceFlowCode::RuleCheckFailed);
        }
        if (!process_.waitForBytesWritten(3000)) {
            return domain::strategy::StrategyServiceFlowResult(
                domain::strategy::StrategyServiceFlowCode::RuleCheckFailed);
        }

        if (!process_.canReadLine() && !process_.waitForReadyRead(10000)) {
            return domain::strategy::StrategyServiceFlowResult(
                domain::strategy::StrategyServiceFlowCode::RuleCheckFailed);
        }

        const QByteArray responseLine = process_.readLine().trimmed();
        const QJsonDocument responseDoc = QJsonDocument::fromJson(responseLine);
        if (!responseDoc.isArray()) {
            return domain::strategy::StrategyServiceFlowResult(
                domain::strategy::StrategyServiceFlowCode::RuleCheckFailed);
        }

        const QJsonArray arr = responseDoc.array();
        outputResults.reserve(static_cast<std::size_t>(arr.size()));
        for (const QJsonValue& value : arr) {
            if (!value.isObject()) {
                return domain::strategy::StrategyServiceFlowResult(
                    domain::strategy::StrategyServiceFlowCode::RuleCheckFailed);
            }
            const QJsonObject obj = value.toObject();
            const bool passed = obj.value("passed").toBool(false);
            const int rejectCode = obj.value("reject_reason").toInt(3);
            outputResults.emplace_back(
                passed,
                static_cast<domain::strategy::RuleRejectReason>(rejectCode));
        }

        return domain::strategy::StrategyServiceFlowResult(domain::strategy::StrategyServiceFlowCode::Ok);
    }

private:
    QProcess process_;
};

BenchmarkStats runBatchBenchmark(
    domain::strategy::PythonRuleEvaluationService& service,
    const std::vector<domain::strategy::StrategySignal>& signals)
{
    BenchmarkStats stats;
    stats.totalSignals = signals.size() * static_cast<std::size_t>(kMeasureRoundsBatch);

    std::vector<domain::strategy::RuleEvaluationResult> results;
    const auto context = domain::strategy::rules::RuleEvaluationContext(
        domain::strategy::rules::RuleEvaluationPhase::Batch,
        kStrategyInstanceId,
        signals.size());

    for (int i = 0; i < kWarmupRoundsBatch; ++i) {
        const auto flow = service.evaluateBatch(signals, kPyBenchmarkRuleSetId, context, results);
        if (!flow.isOk()) {
            std::cerr << "[ERROR] worker warmup evaluateBatch failed, flow="
                      << static_cast<int>(flow.code()) << std::endl;
            std::exit(21);
        }
    }

    std::vector<double> wallSamplesUs;
    wallSamplesUs.reserve(kMeasureRoundsBatch);
    std::vector<double> resultLatencySamplesUs;
    resultLatencySamplesUs.reserve(kMeasureRoundsBatch * signals.size());

    for (int i = 0; i < kMeasureRoundsBatch; ++i) {
        const auto beginAt = Clock::now();
        const auto flow = service.evaluateBatch(signals, kPyBenchmarkRuleSetId, context, results);
        const auto endAt = Clock::now();

        if (!flow.isOk()) {
            std::cerr << "[ERROR] worker evaluateBatch failed, flow="
                      << static_cast<int>(flow.code()) << std::endl;
            std::exit(22);
        }

        const auto wallUs = std::chrono::duration_cast<Microseconds>(endAt - beginAt).count();
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

BenchmarkStats runSingleBenchmark(
    domain::strategy::PythonRuleEvaluationService& service,
    const domain::strategy::StrategySignal& signal)
{
    BenchmarkStats stats;
    stats.totalSignals = static_cast<std::size_t>(kMeasureRoundsSingle);

    const auto context = domain::strategy::rules::RuleEvaluationContext(
        domain::strategy::rules::RuleEvaluationPhase::LowLatency,
        kStrategyInstanceId,
        kSingleSignalCount);

    for (int i = 0; i < kWarmupRoundsSingle; ++i) {
        (void)service.evaluate(signal, kPyBenchmarkRuleSetId, context);
    }

    std::vector<double> wallSamplesUs;
    wallSamplesUs.reserve(kMeasureRoundsSingle);
    std::vector<double> resultLatencySamplesUs;
    resultLatencySamplesUs.reserve(kMeasureRoundsSingle);

    for (int i = 0; i < kMeasureRoundsSingle; ++i) {
        const auto beginAt = Clock::now();
        const auto result = service.evaluate(signal, kPyBenchmarkRuleSetId, context);
        const auto endAt = Clock::now();

        const auto wallUs = std::chrono::duration_cast<Microseconds>(endAt - beginAt).count();
        wallSamplesUs.push_back(static_cast<double>(wallUs));
        resultLatencySamplesUs.push_back(static_cast<double>(result.latency().count()));

        if (result.passed()) {
            ++stats.passedSignals;
        } else {
            ++stats.rejectedSignals;
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

QString resolveWorkerScriptPath()
{
    const QDir dir(QCoreApplication::applicationDirPath() + "/../../..");
    const QString candidate = dir.absoluteFilePath("tests/rule_service_py_worker.py");
    if (QFile::exists(candidate)) {
        return candidate;
    }
    return QStringLiteral("tests/rule_service_py_worker.py");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    const QString scriptPath = resolveWorkerScriptPath();
    if (!QFile::exists(scriptPath)) {
        std::cerr << "[ERROR] python worker script not found: "
                  << scriptPath.toStdString() << std::endl;
        return 2;
    }

    PersistentPythonRuleAdapter adapter(scriptPath);
    domain::strategy::PythonRuleEvaluationService service(adapter);
    service.saveRuleSet(domain::strategy::rules::RuleSet(kPyBenchmarkRuleSetId, {1, 2}));

    if (!service.isReady()) {
        std::cerr << "[ERROR] PythonRuleEvaluationService is not ready" << std::endl;
        return 3;
    }

    bool allPass = true;
    const std::vector<std::size_t> sizes = {64, 256};

    for (std::size_t size : sizes) {
        const auto batchSignals = buildSignals(size);
        const auto batchStats = runBatchBenchmark(service, batchSignals);
        printStats("py-worker-batch-size=" + std::to_string(size), batchStats);

        const double p99PerSignal = batchStats.wallPercentiles.p99 / static_cast<double>(size);
        const bool pass = p99PerSignal <= kBatchPerSignalP99TargetUs;
        std::cout << "  target-check(py-worker batch p99/signal <= "
                  << kBatchPerSignalP99TargetUs << "us): " << (pass ? "PASS" : "FAIL")
                  << std::endl;
        allPass = allPass && pass;
    }

    const auto singleSignal = buildSignals(1)[0];
    const auto singleStats = runSingleBenchmark(service, singleSignal);
    printStats("py-worker-single-signal", singleStats);
    const bool singlePass = singleStats.wallPercentiles.p99 <= kSingleP99TargetUs;
    std::cout << "  target-check(py-worker single p99 <= "
              << kSingleP99TargetUs << "us): " << (singlePass ? "PASS" : "FAIL")
              << std::endl;
    allPass = allPass && singlePass;

    std::cout << "\n[SUMMARY] python worker rule service HF benchmark "
              << (allPass ? "PASS" : "FAIL") << std::endl;

    // TEST-ONLY NOTE:
    // This benchmark is test-only and disabled by default in CMake.
    return allPass ? 0 : 4;
}
