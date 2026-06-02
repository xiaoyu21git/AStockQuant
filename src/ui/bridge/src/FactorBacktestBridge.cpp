#include "FactorBacktestBridge.h"

#include "factor_compute/FactorSignalAdapter.h"
#include "factor_compute/FactorSignalTypes.h"
#include "factor_compute/FactorRegistry.h"
#include "factor_compute/FactorOperatorLibrary.h"
#include "factor_compute/FactorComputeDispatcher.h"
#include "factor_compute/FactorSignalSetAssembler.h"
#include "factor_compute/PostProcessingPipeline.h"
#include "factor_compute/ParquetMarketDataView.h"
#include "factor_compute/SignalCache.h"
#include "factor_compute/FactorErrorCatalog.h"

#include "BacktestRunEntry.h"
#include "BacktestContracts.hpp"

#include <QCoreApplication>
#include <QThread>
#include <QtConcurrent>

namespace {
    using namespace factor::compute;
    using namespace application::backtest;
}

FactorBacktestBridge::FactorBacktestBridge(QObject* parent)
    : QObject(parent)
    , m_timeoutTimer(new QTimer(this))
{
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, &FactorBacktestBridge::onBacktestTimeout);
    m_dataSourceMode = QStringLiteral("cache");
}

FactorBacktestBridge::~FactorBacktestBridge() = default;

// ─── 初始化 ───

bool FactorBacktestBridge::initialize(const QString& marketDataPath)
{
    try {
        // 1. 创建 Parquet mmap 行情视图（Arrow 零拷贝）
        ownedMarketDataView_ = std::make_unique<ParquetMarketDataView>(
            marketDataPath.toStdString());
    } catch (const std::exception& e) {
        m_backtestStatusText = QStringLiteral("行情数据加载失败: %1").arg(QString::fromStdString(e.what()));
        return false;
    }

    return ensureEngineInitialized();
}

bool FactorBacktestBridge::initializeWithExistingEngine(
    factor::compute::IFactorComputeEngine* existingEngine)
{
    existingComputeEngine_ = existingEngine;
    return true;
}

bool FactorBacktestBridge::ensureEngineInitialized()
{
    if (externalSignalEngine_ != nullptr || existingComputeEngine_ != nullptr) {
        return true; // 外部已注入
    }

    if (ownedSignalEngine_ != nullptr) {
        return true; // 已初始化
    }

    if (!ownedMarketDataView_) {
        m_backtestStatusText = QStringLiteral("行情数据视图未初始化");
        return false;
    }

    try {
        // 创建 SignalCache
        ownedSignalCache_ = std::make_unique<SignalCache>();

        // 组装 FactorSignalAdapter（ISignalEngine 实现）
        auto registry = std::make_unique<FactorRegistry>();
        auto opLibrary = std::make_unique<FactorOperatorLibrary>();
        auto dispatcher = std::make_unique<FactorComputeDispatcher>(*opLibrary);
        auto assembler = std::make_unique<FactorSignalSetAssembler>();
        auto pipeline = std::make_unique<PostProcessingPipeline>();

        // 持有子组件的所有权
        auto adapter = std::make_unique<FactorSignalAdapter>(
            *registry, *dispatcher, *pipeline,
            *assembler, *ownedMarketDataView_, *ownedSignalCache_);

        ownedSignalEngine_ = std::move(adapter);

        m_backtestStatusText = QStringLiteral("因子回测引擎就绪");
        return true;
    } catch (const std::exception& e) {
        m_backtestStatusText = QStringLiteral("引擎初始化失败: %1").arg(QString::fromStdString(e.what()));
        return false;
    }
}

// ─── 回测执行 ───

void FactorBacktestBridge::startBacktest()
{
    if (!ensureEngineInitialized()) {
        emit backtestFailed(m_backtestStatusText);
        return;
    }

    m_isBacktesting.store(true);
    m_backtestProgress = 0.0;
    m_backtestStatusText = QStringLiteral("回测启动中...");
    emit isBacktestingChanged();
    emit backtestProgressChanged();
    emit backtestStatusTextChanged();

    // 在后台线程执行回测（UI线程不阻塞）
    QtConcurrent::run([this]() {
        try {
            // 构建 RunSpec
            RunSpec spec;
            spec.mode = RunMode::FactorBacktest;

            // 从 QML 参数转换
            const QString startDate = m_backtestRuntimeParams.value("startDate").toString();
            const QString endDate = m_backtestRuntimeParams.value("endDate").toString();
            const QVariantList factorIds = m_backtestRuntimeParams.value("selectedFactorIds").toList();

            m_backtestStatusText = QStringLiteral("参数校验中...");

            // 构建 ExistingModuleSlots
            ExistingModuleSlots slots;
            if (externalSignalEngine_ != nullptr) {
                slots.signalEngine = externalSignalEngine_;
            } else if (ownedSignalEngine_ != nullptr) {
                // FactorSignalAdapter 实现了 ISignalEngine
                slots.signalEngine = static_cast<ISignalEngine*>(ownedSignalEngine_.get());
            } else if (existingComputeEngine_ != nullptr) {
                slots.factorComputeEngine = existingComputeEngine_;
            } else {
                emit backtestFailed(QStringLiteral("因子引擎未初始化"));
                m_isBacktesting.store(false);
                return;
            }

            m_backtestProgress = 10.0;
            emit backtestProgressChanged();
            m_backtestStatusText = QStringLiteral("执行回测...");

            // 执行回测
            auto result = BacktestRunEntry::runBacktest(slots, spec);

            m_backtestProgress = 100.0;
            emit backtestProgressChanged();

            if (result.ok()) {
                m_backtestResult = convertRunResult(result.result);
                m_backtestStatusText = QStringLiteral("回测完成");
                emit backtestResultChanged();
                emit backtestCompleted(m_backtestResult);
            } else {
                const char* errorText = FactorErrorCatalog::displayText(
                    FactorError::InternalError);
                m_backtestStatusText = QStringLiteral("回测失败: %1").arg(
                    QString::fromUtf8(errorText));
                emit backtestFailed(m_backtestStatusText);
            }
        } catch (const std::exception& e) {
            emit backtestFailed(QString::fromStdString(e.what()));
        }

        m_isBacktesting.store(false);
        emit isBacktestingChanged();
        emit backtestStatusTextChanged();
    });
}

void FactorBacktestBridge::cancelBacktest()
{
    m_timeoutTimer->stop();
    m_isBacktesting.store(false);
    m_backtestStatusText = QStringLiteral("回测已取消");
    emit isBacktestingChanged();
    emit backtestStatusTextChanged();
}

// ─── 因子支持校验 ───

QVariantMap FactorBacktestBridge::buildFactorSupportMap(
    const QVariantList& factorIds,
    const QString& startDate,
    const QString& endDate,
    const QVariantMap& cacheSnapshot)
{
    (void)factorIds;
    (void)startDate;
    (void)endDate;
    (void)cacheSnapshot;

    // 桥接层：将 QVariant → 核心类型 → 执行校验 → QVariantMap
    QVariantMap supportMap;
    for (const QVariant& factorIdVar : factorIds) {
        const QString factorId = factorIdVar.toString().trimmed();
        if (factorId.isEmpty()) {
            continue;
        }

        QVariantMap info;
        info["factorId"] = factorId;
        info["supported"] = ensureEngineInitialized();
        info["reason"] = ensureEngineInitialized()
            ? QString::fromUtf8(FactorErrorCatalog::displayText(FactorError::None))
            : QStringLiteral("引擎未初始化");
        info["category"] = ensureEngineInitialized()
            ? QStringLiteral("supported")
            : QStringLiteral("runtime-init-failed");

        supportMap[factorId] = info;
    }

    m_factorSupportMapCache = supportMap;
    emit factorSupportMapCacheChanged();
    return supportMap;
}

int FactorBacktestBridge::beginFactorSupportMapRefresh(
    const QVariantList& factorIds,
    const QString& startDate,
    const QString& endDate,
    const QVariantMap& cacheSnapshot)
{
    static int requestIdCounter = 0;
    const int requestId = ++requestIdCounter;

    // 异步执行支持校验
    QtConcurrent::run([this, factorIds, startDate, endDate, cacheSnapshot, requestId]() {
        const QVariantMap result = buildFactorSupportMap(
            factorIds, startDate, endDate, cacheSnapshot);
        emit supportMapRefreshed(requestId, result);
    });

    return requestId;
}

// ─── 核心类型 → QVariant 转换 ───

QVariantMap FactorBacktestBridge::convertRunResult(const RunResult& result)
{
    QVariantMap map;
    map["errorCode"] = static_cast<int>(result.code);
    map["completedStage"] = static_cast<int>(result.completedStage);
    map["partial"] = result.partial;
    map["persistedArtifactCount"] = static_cast<quint32>(result.persistedArtifactCount);
    map["diagnosticsCount"] = static_cast<quint32>(result.diagnosticsCount);

    const char* errorText = FactorErrorCatalog::displayText(
        result.code == RunErrorCode::None ? FactorError::None : FactorError::InternalError);
    map["displayText"] = QString::fromUtf8(errorText);
    return map;
}

QVariantMap FactorBacktestBridge::convertSignalSetSummary(const SignalSet& signalSet)
{
    QVariantMap map;
    map["dateCount"] = static_cast<quint32>(signalSet.dates.size());
    map["instrumentCount"] = static_cast<quint32>(signalSet.instruments.size());
    map["signalCount"] = static_cast<quint32>(signalSet.signals.size());
    map["valueCount"] = static_cast<quint32>(signalSet.values.size());
    map["isPartial"] = signalSet.isPartial;
    return map;
}

GenerateSpec FactorBacktestBridge::buildGenerateSpecFromParams(
    const QVariantMap& params,
    const QVariantList& factorIds) const
{
    GenerateSpec spec;
    spec.mode = SignalEngineMode::FullPipeline;

    // 日期范围：QString → YYYYMMDD int
    const int32_t startDateInt = params.value("startDate").toString().toInt();
    const int32_t endDateInt = params.value("endDate").toString().toInt();
    spec.dateRange.from.value = startDateInt > 0 ? startDateInt : 20200101;
    spec.dateRange.to.value = endDateInt > 0 ? endDateInt : 20201231;

    spec.runtimeBudget.timeoutMilliseconds = 600'000;  // 10 分钟
    spec.runtimeBudget.memoryLimitBytes = 1024ULL * 1024ULL * 1024ULL; // 1 GB

    spec.chunkPolicy.dateChunkSize = 64U;
    spec.chunkPolicy.instrumentChunkSize = 1024U;

    for (const QVariant& idVar : factorIds) {
        FactorId fid;
        fid.value = static_cast<uint32_t>(idVar.toString().toUInt());
        spec.requestedFactors.push_back(fid);
    }

    return spec;
}

// ─── 属性访问器（仅桥接层）───

QVariantMap FactorBacktestBridge::backtestRuntimeParams() const {
    return m_backtestRuntimeParams;
}
void FactorBacktestBridge::setBacktestRuntimeParams(const QVariantMap& params) {
    m_backtestRuntimeParams = params;
    emit backtestRuntimeParamsChanged();
}

QVariantMap FactorBacktestBridge::backtestResult() const {
    return m_backtestResult;
}
bool FactorBacktestBridge::isBacktesting() const {
    return m_isBacktesting.load();
}
double FactorBacktestBridge::backtestProgress() const {
    return m_backtestProgress;
}
QString FactorBacktestBridge::backtestStatusText() const {
    return m_backtestStatusText;
}
QVariantMap FactorBacktestBridge::factorSupportMapCache() const {
    return m_factorSupportMapCache;
}
int FactorBacktestBridge::selectedDatasetId() const {
    return m_selectedDatasetId;
}
void FactorBacktestBridge::setSelectedDatasetId(int id) {
    if (m_selectedDatasetId != id) {
        m_selectedDatasetId = id;
        emit selectedDatasetIdChanged();
    }
}
QString FactorBacktestBridge::dataSourceMode() const {
    return m_dataSourceMode;
}
void FactorBacktestBridge::setDataSourceMode(const QString& mode) {
    if (m_dataSourceMode != mode) {
        m_dataSourceMode = mode;
        emit dataSourceModeChanged();
    }
}
QVariantMap FactorBacktestBridge::selectedDatasetBenchmarkMetadata() const {
    return m_selectedDatasetBenchmarkMetadata;
}
void FactorBacktestBridge::setSelectedDatasetBenchmarkMetadata(const QVariantMap& metadata) {
    m_selectedDatasetBenchmarkMetadata = metadata;
    emit selectedDatasetBenchmarkMetadataChanged();
}

void FactorBacktestBridge::onBacktestTimeout() {
    if (m_isBacktesting.load()) {
        cancelBacktest();
    }
}