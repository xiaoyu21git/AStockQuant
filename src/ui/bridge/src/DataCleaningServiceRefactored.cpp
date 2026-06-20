// DataCleaningServiceRefactored.cpp — 纯 C++ 清洗服务实现
// 使用 foundation::ThreadPoolExecutor + 纯 C++ CleaningEngine
// 纯 C++ 引擎与 Qt 桥接层
#include "DataCleaningServiceRefactored.h"
#include "CleaningEngine.h"
#include "DataCacheAdapter.h"
#include "rules/CoreCleaningRules.h"
#include "AppStoragePaths.h"
#include "foundation/thread/ThreadPoolExecutor.h"
#include "foundation/json/json_facade.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QPointer>
#include <QCoreApplication>

#include <cstdio>
#include <stdexcept>

using J = foundation::json::JsonFacade;


// 根据规则 key 名字符串创建对应的纯 C++ 规则实例
std::unique_ptr<cleaning::ICleaningRule> createCppRule(const std::string& ruleKey, const std::string& configJson) {
    using namespace cleaning;
    if (ruleKey == "completeness")           return std::make_unique<CompletenessRule>();
    if (ruleKey == "duplicateRemoval")        return std::make_unique<DuplicateRemovalRule>(configJson);
    if (ruleKey == "financialDateValidity")   return std::make_unique<FinancialDateValidityRule>();
    if (ruleKey == "financialMetricSanitize") return std::make_unique<FinancialMetricSanitizeRule>();
    if (ruleKey == "suspensionFill")          return std::make_unique<SuspensionFillRule>(configJson);
    if (ruleKey == "missingValueFill")        return std::make_unique<MissingValueFillRule>(configJson);
    if (ruleKey == "adjustedPrice")           return std::make_unique<AdjustedPriceRule>();
    if (ruleKey == "priceValidity")           return std::make_unique<PriceValidityRule>(configJson);
    if (ruleKey == "volumeFilter")            return std::make_unique<VolumeFilterRule>(configJson);
    if (ruleKey == "limitMoveTag")            return std::make_unique<LimitMoveTagRule>(configJson);
    if (ruleKey == "valuationSanitize")       return std::make_unique<ValuationSanitizeRule>();
    if (ruleKey == "fieldStandardization")    return std::make_unique<FieldStandardizationRule>();
    // P0-2: 新增 4 个纯 C++ 规则（与 Qt 桥接层行为对齐）
    if (ruleKey == "reportDateAlignment")    return std::make_unique<ReportDateAlignmentRule>();
    if (ruleKey == "survivorBias")           return std::make_unique<SurvivorBiasRule>();
    if (ruleKey == "newStockFilter")         return std::make_unique<NewStockFilterRule>(configJson);
    if (ruleKey == "stFilter")               return std::make_unique<STFilterRule>();

    // 未识别的规则键 — 打印警告，避免静默跳过
    fprintf(stderr, "[CleaningSvcRefactored] WARNING: unknown cleaning rule key \"%s\", skipped. "
                    "请检查规则名是否与 CoreCleaningRules.h 中的 ruleName() 一致\n", ruleKey.c_str());
    fflush(stderr);
    return nullptr;
}

// ── 内部实现 ──
class DataCleaningServiceRefactored::Impl {
public:
    Impl() {
        // 创建专属线程池 (1 worker thread, max 4)
        executor = std::make_shared<foundation::thread::ThreadPoolExecutor>(
            1, 4, std::chrono::seconds(120), "CleaningServiceRefactored");
    }

    mutable QMutex stateMutex;
    std::shared_ptr<foundation::thread::ThreadPoolExecutor> executor;

    bool cleaningInProgress{false};
    std::map<QString, RefactoredCleaningStats> statsByRequest;
    QSet<QString> cancelledRequests;
    QVariantMap customRules;
    int maxPreviewRecords{1000};
    int asyncThreadCount{2};

    QDateTime startTimer;
};

// ── 构造/析构 ──
DataCleaningServiceRefactored::DataCleaningServiceRefactored(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
    , m_initialized(false)
{
    qRegisterMetaType<RefactoredCleaningStats>("RefactoredCleaningStats");
}

DataCleaningServiceRefactored::~DataCleaningServiceRefactored() {
    if (m_impl->executor) {
        m_impl->executor->shutdown(false);
    }
}

// ── 初始化 ──
bool DataCleaningServiceRefactored::initialize() {
    if (m_initialized) return true;
    DataCacheAdapter::instance().initialize(bridge::storage::persistentDatasetRootDir());
    m_initialized = true;
    fprintf(stderr, "[CleaningSvcRefactored] initialized\n"); fflush(stderr);
    return true;
}

// ── 默认规则 ──
QVariantMap DataCleaningServiceRefactored::getDefaultRules() const {
    QVariantMap rules;
    auto on = [](bool b) { QVariantMap m; m["enabled"] = b; return m; };

    rules[QStringLiteral("completeness")]           = on(true);
    rules[QStringLiteral("duplicateRemoval")]       = on(true);
    rules[QStringLiteral("financialDateValidity")]  = on(true);
    rules[QStringLiteral("financialMetricSanitize")]= on(true);
    rules[QStringLiteral("suspensionFill")]         = on(true);
    rules[QStringLiteral("missingValueFill")]       = on(true);
    rules[QStringLiteral("adjustedPrice")]          = on(true);
    rules[QStringLiteral("priceValidity")]          = on(true);
    rules[QStringLiteral("volumeFilter")]           = on(true);
    rules[QStringLiteral("valuationSanitize")]      = on(true);
    rules[QStringLiteral("limitMoveTag")]           = on(true);
    rules[QStringLiteral("reportDateAlignment")]    = on(false);
    rules[QStringLiteral("survivorBias")]           = on(false);
    rules[QStringLiteral("newStockFilter")]         = on(false);
    rules[QStringLiteral("stFilter")]               = on(false);
    return rules;
}

// ── 取消 ──
void DataCleaningServiceRefactored::cancelCleaning(const QString& requestId) {
    {
        QMutexLocker lock(&m_impl->stateMutex);
        m_impl->cleaningInProgress = false;
        // 标记此请求已取消，防止回调中重复发信号
        if (!requestId.isEmpty()) m_impl->cancelledRequests.insert(requestId);
    }
    fprintf(stderr, "[CleaningSvcRefactored] cancelCleaning: %s\n", requestId.toStdString().c_str());
    fflush(stderr);
}

// ── 从 DataSet 清洗（纯 C++ 类型，零 QVariant） ──

void DataCleaningServiceRefactored::cleanDataFromDataSet(int dataSetId,
                                                          const QVariantMap& rules)
{
    if (!m_impl->executor) {
        emit dataSetCleaned(dataSetId, -1, QStringLiteral("线程池未初始化"), 0, 0);
        return;
    }
    if (dataSetId <= 0) {
        emit dataSetCleaned(dataSetId, -1, QStringLiteral("无效的数据集ID"), 0, 0);
        return;
    }

    {
        QMutexLocker lock(&m_impl->stateMutex);
        m_impl->cleaningInProgress = true;
        m_impl->startTimer = QDateTime::currentDateTime();
    }

    QVariantMap effectiveRules = rules.isEmpty() ? getDefaultRules() : rules;
    auto executor = m_impl->executor;
    QPointer<DataCleaningServiceRefactored> self(this);

    executor->post([self, dataSetId, effectiveRules]() {
        if (!self) return;

        QString message;
        int resultId = -1;
        int inputRows = 0, outputRows = 0;

        try {
            QMetaObject::invokeMethod(self.get(), [self, dataSetId]() {
                emit self->cleaningProgress(QString::number(dataSetId), 5, QStringLiteral("加载数据..."));
            }, Qt::QueuedConnection);

            // 1. 从 Arrow 加载数据
            auto rows = cleaning::DataCache::instance().loadDataSetFile(dataSetId);
            inputRows = static_cast<int>(rows.size());
            if (rows.empty()) {
                QMetaObject::invokeMethod(self.get(), [self, dataSetId]() {
                    emit self->dataSetCleaned(dataSetId, -1, QStringLiteral("数据集为空"), 0, 0);
                }, Qt::QueuedConnection);
                return;
            }

            // 2. 构建清洗引擎 + 进度回调
            cleaning::CleaningEngine engine;
            for (auto it = effectiveRules.begin(); it != effectiveRules.end(); ++it) {
                std::string ruleKey = it.key().toStdString();
                QJsonDocument cfgDoc(QJsonObject::fromVariantMap(it.value().toMap()));
                std::string configJson = cfgDoc.toJson(QJsonDocument::Compact).toStdString();
                auto rule = createCppRule(ruleKey, configJson);
                if (rule) engine.addRule(std::move(rule));
            }

            int total = inputRows;
            engine.setOnProgress([self, dataSetId, total](int current, int totalRows, const std::string& stage) {
                int pct = totalRows > 0 ? (10 + (current * 85) / totalRows) : 10;
                QMetaObject::invokeMethod(self.get(), [self, dataSetId, pct, stage]() {
                    emit self->cleaningProgress(QString::number(dataSetId), pct,
                        QString::fromStdString(stage));
                }, Qt::QueuedConnection);
            });

            // 3. 清洗
            auto cleaned = engine.clean(std::move(rows));
            outputRows = static_cast<int>(cleaned.size());

            // 4. 写入新 DataSet
            auto info = DataCacheAdapter::instance().getDataSetInfo(dataSetId);
            QVariantMap infoMap;
            infoMap["displayName"] = QString("清洗结果_%1").arg(info.value("displayName").toString());
            infoMap["sourceType"] = "cleaning";
            infoMap["rowCount"] = outputRows;
            infoMap["stockCodes"] = info.value("stockCodes");
            infoMap["startDate"] = info.value("startDate");
            infoMap["endDate"] = info.value("endDate");

            resultId = DataCacheAdapter::instance().storeDataSetFromRows(cleaned, infoMap);
            message = QString("清洗完成: %1 → %2 条").arg(inputRows).arg(outputRows);

        } catch (const std::exception& e) {
            message = QString("清洗异常: %1").arg(e.what());
        } catch (...) {
            message = QStringLiteral("清洗未知异常");
        }

        int ri = resultId;
        int in = inputRows, out = outputRows;
        QMetaObject::invokeMethod(self.get(), [self, dataSetId, ri, message, in, out]() {
            self->m_impl->cleaningInProgress = false;
            emit self->dataSetCleaned(dataSetId, ri, message, in, out);
        }, Qt::QueuedConnection);
    });
}

// ── 自定义规则 ──
void DataCleaningServiceRefactored::addCustomRule(const QString& ruleName, const QVariantMap& ruleConfig) {
    QMutexLocker lock(&m_impl->stateMutex);
    m_impl->customRules[ruleName] = ruleConfig;
    emit rulesUpdated();
}

void DataCleaningServiceRefactored::removeCustomRule(const QString& ruleName) {
    QMutexLocker lock(&m_impl->stateMutex);
    m_impl->customRules.remove(ruleName);
    emit rulesUpdated();
}

QVariantMap DataCleaningServiceRefactored::getCustomRules() const {
    QMutexLocker lock(&m_impl->stateMutex);
    return m_impl->customRules;
}

// ── 统计 ──
RefactoredCleaningStats DataCleaningServiceRefactored::getLastCleaningStats(const QString& requestId) const {
    QMutexLocker lock(&m_impl->stateMutex);
    auto it = m_impl->statsByRequest.find(requestId);
    return it != m_impl->statsByRequest.end() ? it->second : RefactoredCleaningStats{};
}

QVariantMap DataCleaningServiceRefactored::getAllCleaningStats() const {
    QMutexLocker lock(&m_impl->stateMutex);
    QVariantMap map;
    for (const auto& [key, stats] : m_impl->statsByRequest) {
        map[key] = stats.toVariantMap();
    }
    return map;
}

void DataCleaningServiceRefactored::resetStats() {
    QMutexLocker lock(&m_impl->stateMutex);
    m_impl->statsByRequest.clear();
}

// ── 状态 ──
bool DataCleaningServiceRefactored::isCleaningInProgress() const {
    QMutexLocker lock(&m_impl->stateMutex);
    return m_impl->cleaningInProgress;
}

QStringList DataCleaningServiceRefactored::getActiveCleaningRequests() const {
    return isCleaningInProgress() ? QStringList{ QStringLiteral("active") } : QStringList{};
}

// ── 配置 ──
void DataCleaningServiceRefactored::setMaxPreviewRecords(int maxRecords) {
    m_impl->maxPreviewRecords = std::max(1, maxRecords);
}

int DataCleaningServiceRefactored::getMaxPreviewRecords() const {
    return m_impl->maxPreviewRecords;
}

void DataCleaningServiceRefactored::setAsyncThreadCount(int count) {
    m_impl->asyncThreadCount = std::max(1, std::min(count, 8));
}

int DataCleaningServiceRefactored::getAsyncThreadCount() const {
    return m_impl->asyncThreadCount;
}

// ── 工厂函数 ──
std::shared_ptr<DataCleaningServiceRefactored> createDataCleaningServiceRefactored(QObject* parent) {
    return std::make_shared<DataCleaningServiceRefactored>(parent);
}
