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
#include <QTemporaryDir>

#ifdef signals
#undef signals
#endif

namespace {

using Clock = std::chrono::steady_clock;
using Microseconds = std::chrono::microseconds;

constexpr domain::strategy::StrategyInstanceId kStrategyInstanceId = 1001;
constexpr std::uint32_t kStartInstrumentId = 600000;
constexpr domain::strategy::StrategyCount kSingleSignalCount = 1;

constexpr int kWarmupRoundsBatch = 3;
constexpr int kMeasureRoundsBatch = 20;
constexpr int kWarmupRoundsSingle = 5;
constexpr int kMeasureRoundsSingle = 80;

constexpr double kScorePass = 1.0;
constexpr double kScoreReject = -1.0;
constexpr double kTargetWeightPass = 0.25;
constexpr double kTargetWeightReject = 1.25;

constexpr double kBatchPerSignalP99TargetUs = 2000.0;
constexpr double kSingleP99TargetUs = 5000.0;

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

class SubprocessPythonRuleAdapter final : public domain::strategy::IPythonRuleAdapter {
public:
    explicit SubprocessPythonRuleAdapter(const QString& scriptPath)
        : scriptPath_(scriptPath)
    {
    }

    [[nodiscard]] domain::strategy::StrategyServiceFlowResult checkBatch(
        const domain::strategy::PythonRuleBatchRequest& request,
        std::vector<domain::strategy::PythonRuleResult>& outputResults) override
    {
        outputResults.clear();

        if (scriptPath_.isEmpty() || !QFile::exists(scriptPath_)) {
            return domain::strategy::StrategyServiceFlowResult(
                domain::strategy::StrategyServiceFlowCode::RuleCheckFailed);
        }

        QTemporaryDir tempDir;
        if (!tempDir.isValid()) {
            return domain::strategy::StrategyServiceFlowResult(
                domain::strategy::StrategyServiceFlowCode::RuleCheckFailed);
        }

        const QString requestPath = tempDir.filePath("rule_request.json");
        QJsonObject requestObject;

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

        requestObject.insert("signals", signalsJson);
        requestObject.insert("rule_ids", ruleIdsJson);

        QFile requestFile(requestPath);
        if (!requestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return domain::strategy::StrategyServiceFlowResult(
                domain::strategy::StrategyServiceFlowCode::RuleCheckFailed);
        }
        requestFile.write(QJsonDocument(requestObject).toJson(QJsonDocument::Compact));
        requestFile.close();

        QString program = qEnvironmentVariable("PYTHON_EXECUTABLE");
        if (program.trimmed().isEmpty()) {
            program = "python";
        }

        QProcess process;
        process.setProgram(program);
        process.setArguments({scriptPath_, requestPath});
        process.start();
        if (!process.waitForStarted(3000)) {
            return domain::strategy::StrategyServiceFlowResult(
                domain::strategy::StrategyServiceFlowCode::RuleCheckFailed);
        }
        if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit
            || process.exitCode() != 0) {
            return domain::strategy::StrategyServiceFlowResult(
                domain::strategy::StrategyServiceFlowCode::RuleCheckFailed);
        }

        const QByteArray stdOut = process.readAllStandardOutput();
        const QJsonDocument responseDoc = QJsonDocument::fromJson(stdOut);
        if (!responseDoc.isArray()) {
            return domain::strategy::StrategyServiceFlowResult(
                domain::strategy::StrategyServiceFlowCode::RuleCheckFailed);
        }

        const QJsonArray arr = responseDoc.array();
        outputResults.reserve(static_cast<std::size_t>(arr.size()));
        for (const QJsonValue& item : arr) {
            if (!item.isObject()) {
                return domain::strategy::StrategyServiceFlowResult(
                    domain::strategy::StrategyServiceFlowCode::RuleCheckFailed);
            }
            const QJsonObject obj = item.toObject();
            const bool passed = obj.value("passed").toBool(false);
            const int rejectCode = obj.value("reject_reason").toInt(3);
            const auto rejectReason = static_cast<domain::strategy::RuleRejectReason>(rejectCode);
            outputResults.emplace_back(passed, rejectReason);
        }

        return domain::strategy::StrategyServiceFlowResult(domain::strategy::StrategyServiceFlowCode::Ok);
    }

private:
    QString scriptPath_;
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
            std::cerr << "[ERROR] py warmup evaluateBatch failed, flow="
                      << static_cast<int>(flow.code()) << std::endl;
            std::exit(11);
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
            std::cerr << "[ERROR] py evaluateBatch failed, flow="
                      << static_cast<int>(flow.code()) << std::endl;
            std::exit(12);
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

BenchmarkStats runSingleSignalBenchmark(
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

QString resolveScriptPath()
{
    const QDir testsDir(QCoreApplication::applicationDirPath() + "/../../..");
    const QString candidate = testsDir.absoluteFilePath("tests/rule_service_py_adapter.py");
    if (QFile::exists(candidate)) {
        return candidate;
    }
    return QStringLiteral("tests/rule_service_py_adapter.py");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    const QString scriptPath = resolveScriptPath();
    if (!QFile::exists(scriptPath)) {
        std::cerr << "[ERROR] python adapter script not found: "
                  << scriptPath.toStdString() << std::endl;
        return 2;
    }

    SubprocessPythonRuleAdapter adapter(scriptPath);
    domain::strategy::PythonRuleEvaluationService service(adapter);

    service.saveRuleSet(domain::strategy::rules::RuleSet(kPyBenchmarkRuleSetId, {1, 2}));

    if (!service.isReady()) {
        std::cerr << "[ERROR] PythonRuleEvaluationService is not ready" << std::endl;
        return 3;
    }

    const std::vector<std::size_t> signalSizes = {64, 256};
    bool allPass = true;

    for (std::size_t signalSize : signalSizes) {
        const auto signals = buildSignals(signalSize);
        const auto batchStats = runBatchBenchmark(service, signals);
        printStats("py-batch-size=" + std::to_string(signalSize), batchStats);

        const double batchP99PerSignal =
            batchStats.wallPercentiles.p99 / static_cast<double>(signalSize);
        const bool batchPass = batchP99PerSignal <= kBatchPerSignalP99TargetUs;

        std::cout << "  target-check(py batch p99/signal <= " << kBatchPerSignalP99TargetUs
                  << "us): " << (batchPass ? "PASS" : "FAIL") << std::endl;

        allPass = allPass && batchPass;
    }

    const auto singleSignal = buildSignals(1)[0];
    const auto singleStats = runSingleSignalBenchmark(service, singleSignal);
    printStats("py-single-signal", singleStats);

    const bool singlePass = singleStats.wallPercentiles.p99 <= kSingleP99TargetUs;
    std::cout << "  target-check(py single p99 <= " << kSingleP99TargetUs
              << "us): " << (singlePass ? "PASS" : "FAIL") << std::endl;
    allPass = allPass && singlePass;

    std::cout << "\n[SUMMARY] python rule service HF benchmark "
              << (allPass ? "PASS" : "FAIL") << std::endl;

    // TEST-ONLY NOTE:
    // Keep this benchmark isolated in tests/. For one-off profiling only.
    // Build is OFF by default through ENABLE_RULE_SERVICE_PY_HF_BENCHMARK.

    return allPass ? 0 : 4;
}
