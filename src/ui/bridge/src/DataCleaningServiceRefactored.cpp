// DataCleaningServiceRefactored.cpp — 纯 C++ 清洗服务实现
// 使用 foundation::ThreadPoolExecutor + 纯 C++ CleaningEngine
// 纯 C++ 引擎与 Qt 桥接层
#include "DataCleaningServiceRefactored.h"
#include "cleaning/CleaningRuleContract.h"
#include "CleaningEngine.h"
#include "DataCache.h"
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

// ── Qt → 纯 C++ 转换工具 ──
namespace {

J qvariantToJson(const QVariant& v) {
    switch (static_cast<int>(v.type())) {
    case QMetaType::Bool:        return J::createBool(v.toBool());
    case QMetaType::Int:
    case QMetaType::LongLong:    return J::createDouble(v.toDouble());
    case QMetaType::Double:      return J::createDouble(v.toDouble());
    case QMetaType::QString:     return J::createString(v.toString().toStdString());
    default: {
        if (v.canConvert<QVariantMap>()) {
            auto obj = J::createObject();
            QVariantMap map = v.toMap();
            for (auto it = map.begin(); it != map.end(); ++it)
                obj.set(it.key().toStdString(), qvariantToJson(it.value()));
            return obj;
        }
        if (v.canConvert<QVariantList>()) {
            auto arr = J::createArray();
            QVariantList list = v.toList();
            for (const auto& item : list)
                arr.push_back(qvariantToJson(item));
            return arr;
        }
        return J::createString(v.toString().toStdString());
    }
    }
}

std::vector<J> qvariantListToJsonVec(const QVariantList& data) {
    std::vector<J> rows;
    rows.reserve(data.size());
    for (const QVariant& item : data) {
        if (item.canConvert<QVariantMap>()) {
            rows.push_back(qvariantToJson(item));
        }
    }
    return rows;
}

QVariantList jsonVecToQvariantList(const std::vector<J>& rows) {
    QVariantList result;
    result.reserve(static_cast<int>(rows.size()));
    for (const auto& row : rows) {
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(row.toString()));
        if (doc.isObject()) {
            result.append(doc.object().toVariantMap());
        }
    }
    return result;
}

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
    return nullptr;
}

} // anonymous namespace

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
    QVariantMap customRules;
    bool cacheEnabled{true};
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
    // 初始化纯 C++ 缓存（与 .bin 同目录）
    auto& cache = cleaning::DataCache::instance();
    if (!cache.isInitialized()) {
        std::string dir = bridge::storage::persistentDatasetRootDir().toStdString();
        cache.initialize(dir);
    }
    m_initialized = true;
    fprintf(stderr, "[CleaningSvcRefactored] initialized\n"); fflush(stderr);
    return true;
}

// ── 默认规则 ──
QVariantMap DataCleaningServiceRefactored::getDefaultRules() const {
    using K = factor::bridge::CleaningRuleKey;
    QVariantMap rules;
    auto on = [](bool b) { QVariantMap m; m["enabled"] = b; return m; };

    rules[factor::bridge::cleaningRuleKeyName(K::Completeness)]           = on(true);
    rules[factor::bridge::cleaningRuleKeyName(K::DuplicateRemoval)]       = on(true);
    rules[factor::bridge::cleaningRuleKeyName(K::FinancialDateValidity)]  = on(true);
    rules[factor::bridge::cleaningRuleKeyName(K::FinancialMetricSanitize)]= on(true);
    rules[factor::bridge::cleaningRuleKeyName(K::SuspensionFill)]         = on(true);
    rules[factor::bridge::cleaningRuleKeyName(K::MissingValueFill)]       = on(true);
    rules[factor::bridge::cleaningRuleKeyName(K::AdjustedPrice)]          = on(true);
    rules[factor::bridge::cleaningRuleKeyName(K::PriceValidity)]          = on(true);
    rules[factor::bridge::cleaningRuleKeyName(K::VolumeFilter)]           = on(true);
    rules[factor::bridge::cleaningRuleKeyName(K::ValuationSanitize)]      = on(true);
    rules[factor::bridge::cleaningRuleKeyName(K::LimitMoveTag)]           = on(true);
    rules[factor::bridge::cleaningRuleKeyName(K::ReportDateAlignment)]    = on(false);
    rules[factor::bridge::cleaningRuleKeyName(K::SurvivorBias)]           = on(false);
    rules[factor::bridge::cleaningRuleKeyName(K::NewStockFilter)]         = on(false);
    rules[factor::bridge::cleaningRuleKeyName(K::STFilter)]               = on(false);
    return rules;
}

// ── 同步预览 ──
QVariantList DataCleaningServiceRefactored::previewCleaning(
    const QVariantList& data, const QVariantMap& rules)
{
    if (data.isEmpty()) return {};

    QVariantList previewData = data;
    if (static_cast<int>(previewData.size()) > m_impl->maxPreviewRecords) {
        previewData = previewData.mid(0, m_impl->maxPreviewRecords);
    }

    try {
        // 转换为纯 C++ 数据
        auto rows = qvariantListToJsonVec(previewData);

        // 构建纯 C++ 引擎
        cleaning::CleaningEngine engine;
        for (auto it = rules.begin(); it != rules.end(); ++it) {
            std::string ruleKey = it.key().toStdString();
            QJsonDocument cfgDoc(QJsonObject::fromVariantMap(it.value().toMap()));
            std::string configJson = cfgDoc.toJson(QJsonDocument::Compact).toStdString();
            auto rule = createCppRule(ruleKey, configJson);
            if (rule) engine.addRule(std::move(rule));
        }

        auto cleaned = engine.clean(std::move(rows));
        fprintf(stderr, "[CleaningSvcRefactored] preview (C++ engine): %d -> %zu rows\n",
                previewData.size(), cleaned.size());
        fflush(stderr);
        return jsonVecToQvariantList(cleaned);
    } catch (const std::exception& e) {
        fprintf(stderr, "[CleaningSvcRefactored] preview error: %s\n", e.what());
        fflush(stderr);
        return data;
    }
}

// ── 异步清洗 (foundation 线程池) ──
void DataCleaningServiceRefactored::executeCleaningAsync(
    const QString& requestId, const QVariantList& data, const QVariantMap& rules)
{
    if (!m_impl->executor) {
        emit cleaningError(requestId, QStringLiteral("线程池未初始化"));
        return;
    }
    if (data.isEmpty()) {
        emit cleaningCompleted(requestId, true, QStringLiteral("空数据"), {});
        return;
    }

    {
        QMutexLocker lock(&m_impl->stateMutex);
        m_impl->cleaningInProgress = true;
        m_impl->startTimer = QDateTime::currentDateTime();
    }

    QVariantMap effectiveRules = rules.isEmpty() ? getDefaultRules() : rules;
    emit cleaningStarted(requestId, QStringLiteral("开始清洗 %1 条记录").arg(data.size()));

    // 拷贝数据，在 worker 线程执行
    auto executor = m_impl->executor;
    QPointer<DataCleaningServiceRefactored> self(this);

    executor->post([self, requestId, data, effectiveRules]() {
        if (!self) return;

        QVariantList result;
        QString message;
        bool success = false;

        try {
            // 构建纯 C++ 引擎
            cleaning::CleaningEngine engine;
            for (auto it = effectiveRules.begin(); it != effectiveRules.end(); ++it) {
                std::string ruleKey = it.key().toStdString();
                QJsonDocument cfgDoc(QJsonObject::fromVariantMap(it.value().toMap()));
                std::string configJson = cfgDoc.toJson(QJsonDocument::Compact).toStdString();
                auto rule = createCppRule(ruleKey, configJson);
                if (rule) engine.addRule(std::move(rule));
            }

            // 纯 C++ 进度回调
            engine.setOnProgress([self, requestId](int current, int total, const std::string& stage) {
                if (self && total > 0) {
                    int pct = current * 100 / total;
                    QMetaObject::invokeMethod(self.get(), [self, requestId, pct, stage]() {
                        if (self) emit self->cleaningProgress(requestId, pct, QString::fromStdString(stage));
                    }, Qt::QueuedConnection);
                }
            });

            auto rows = qvariantListToJsonVec(data);
            auto cleanedRows = engine.clean(std::move(rows));
            result = jsonVecToQvariantList(cleanedRows);
            success = true;
            message = QStringLiteral("清洗完成: %1 -> %2 条").arg(data.size()).arg(result.size());

        } catch (const std::exception& e) {
            message = QStringLiteral("清洗异常: %1").arg(e.what());
        } catch (...) {
            message = QStringLiteral("清洗未知异常");
        }

        QMetaObject::invokeMethod(self.get(), [self, requestId, success, message, result]() {
            if (!self) return;
            {
                QMutexLocker lock(&self->m_impl->stateMutex);
                self->m_impl->cleaningInProgress = false;
                RefactoredCleaningStats stats;
                stats.totalRecords = 0;  // 由 CleaningEngine 填充
                stats.cleanedRecords = result.size();
                stats.durationMs = self->m_impl->startTimer.msecsTo(QDateTime::currentDateTime());
                stats.startTime = self->m_impl->startTimer;
                stats.endTime = QDateTime::currentDateTime();
                self->m_impl->statsByRequest[requestId] = stats;
            }
            if (success) {
                // 缓存清洗结果（纯 C++ DataCache）
                if (self->m_impl->cacheEnabled && !result.isEmpty()) {
                    auto& cache = cleaning::DataCache::instance();
                    auto cppRows = qvariantListToJsonVec(result);
                    cleaning::DataSetInfo info;
                    info.displayName = QStringLiteral("清洗结果 %1").arg(requestId).toStdString();
                    info.sourceType = "cleaning";
                    info.rowCount = result.size();
                    info.schemaVersion = 2;
                    int id = cache.storeDataSet(cppRows, info);
                    fprintf(stderr, "[CleaningSvcRefactored] cached as dataset %d\n", id);
                    fflush(stderr);
                }
                emit self->cleaningCompleted(requestId, true, message, result);
            } else {
                emit self->cleaningError(requestId, message);
            }
        }, Qt::QueuedConnection);
    });
}

// ── 取消 ──
void DataCleaningServiceRefactored::cancelCleaning(const QString& requestId) {
    Q_UNUSED(requestId)
    QMutexLocker lock(&m_impl->stateMutex);
    m_impl->cleaningInProgress = false;
}

// ── 带缓存清洗 ──
QVariantList DataCleaningServiceRefactored::cleanWithCache(
    const QString& requestId, const QVariantList& data, const QVariantMap& rules)
{
    if (!m_impl->cacheEnabled) return previewCleaning(data, rules);

    auto& cache = cleaning::DataCache::instance();
    std::string cacheKey = "cleaned_" + requestId.toStdString();
    auto cached = cache.getData(cacheKey);
    if (!cached.empty()) {
        fprintf(stderr, "[CleaningSvcRefactored] cache hit for %s\n", cacheKey.c_str());
        fflush(stderr);
        return jsonVecToQvariantList(cached);
    }
    QVariantList result = previewCleaning(data, rules);
    if (!result.isEmpty()) {
        auto cppRows = qvariantListToJsonVec(result);
        cache.storeData(cacheKey, cppRows);
    }
    return result;
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
void DataCleaningServiceRefactored::setCacheEnabled(bool enabled) {
    m_impl->cacheEnabled = enabled;
}

bool DataCleaningServiceRefactored::isCacheEnabled() const {
    return m_impl->cacheEnabled;
}

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

// ── 纯 C++ JSON 管道 (零 QVariant 转换) ──

static void buildEngineFromQtRules(cleaning::CleaningEngine& engine, const QVariantMap& rules) {
    for (auto it = rules.begin(); it != rules.end(); ++it) {
        std::string ruleKey = it.key().toStdString();
        QJsonDocument cfgDoc(QJsonObject::fromVariantMap(it.value().toMap()));
        std::string configJson = cfgDoc.toJson(QJsonDocument::Compact).toStdString();
        auto rule = createCppRule(ruleKey, configJson);
        if (rule) engine.addRule(std::move(rule));
    }
}

std::string DataCleaningServiceRefactored::cleanJsonSync(
    const std::string& jsonData, const QVariantMap& rules)
{
    if (jsonData.empty()) return "[]";
    auto root = J::parse(jsonData);
    if (!root.isArray()) return "[]";

    std::vector<J> rows;
    rows.reserve(root.size());
    for (size_t i = 0; i < root.size(); ++i) rows.push_back(root.at(i));

    cleaning::CleaningEngine engine; buildEngineFromQtRules(engine, rules);
    auto cleaned = engine.clean(std::move(rows));

    auto result = J::createArray();
    for (auto& row : cleaned) result.push_back(std::move(row));
    return result.toString();
}

void DataCleaningServiceRefactored::cleanJsonAsync(
    const QString& requestId, const QString& jsonData, const QVariantMap& rules)
{
    if (!m_impl->executor) {
        emit cleaningError(requestId, QStringLiteral("线程池未初始化"));
        return;
    }
    {
        QMutexLocker lock(&m_impl->stateMutex);
        m_impl->cleaningInProgress = true;
        m_impl->startTimer = QDateTime::currentDateTime();
    }
    emit cleaningStarted(requestId, QStringLiteral("开始清洗(纯C++管道)"));

    auto executor = m_impl->executor;
    std::string jsonStr = jsonData.toStdString();
    QVariantMap effectiveRules = rules.isEmpty() ? getDefaultRules() : rules;

    executor->post([this, requestId, jsonStr, effectiveRules]() {
        std::string result;
        bool success = false;
        QString message;
        try {
            cleaning::CleaningEngine engine; buildEngineFromQtRules(engine, effectiveRules);
            result = this->cleanJsonSync(jsonStr, effectiveRules);
            success = true;
            auto cleaned = J::parse(result);
            int count = cleaned.isArray() ? static_cast<int>(cleaned.size()) : 0;
            message = QStringLiteral("清洗完成: %1 条").arg(count);
        } catch (const std::exception& e) {
            message = QStringLiteral("清洗异常: %1").arg(QString::fromStdString(e.what()));
        }
        QMetaObject::invokeMethod(this, [this, requestId, success, message, result]() {
            this->m_impl->cleaningInProgress = false;
            if (success) {
                auto cleaned = J::parse(result);
                QVariantList qvl;
                if (cleaned.isArray()) {
                    for (size_t i = 0; i < cleaned.size(); ++i)
                        qvl.append(QJsonDocument::fromJson(QByteArray::fromStdString(cleaned.at(i).toString())).object().toVariantMap());
                }
                if (this->m_impl->cacheEnabled && !qvl.isEmpty()) {
                    auto& cache = cleaning::DataCache::instance();
                    auto cppRows = qvariantListToJsonVec(qvl);
                    cleaning::DataSetInfo info;
                    info.displayName = QStringLiteral("清洗结果 %1").arg(requestId).toStdString();
                    info.sourceType = "cleaning"; info.rowCount = qvl.size(); info.schemaVersion = 2;
                    cache.storeDataSet(cppRows, info);
                }
                emit this->cleaningCompleted(requestId, true, message, qvl);
            } else {
                emit this->cleaningError(requestId, message);
            }
        }, Qt::QueuedConnection);
    });
}

// ── 工厂函数 ──
std::shared_ptr<DataCleaningServiceRefactored> createDataCleaningServiceRefactored(QObject* parent) {
    return std::make_shared<DataCleaningServiceRefactored>(parent);
}
