// DataCleaningServiceRefactored.cpp
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/ipc/api.h>
#include "DataCleaningServiceRefactored.h"
#include "CleaningEngine.h"
#include "DataCacheAdapter.h"
#include "DataCache.h"
#include "RawMarketDataAssembler.h"
#include "DataSourceRegistry.h"
#include "database/MarketDataRepository.h"
#include "database/NativePgConnectionPool.h"
#include "rules/CoreCleaningRules.h"
#include "AppStoragePaths.h"
#include "LightRow.h"
#include "foundation/thread/ThreadPoolExecutor.h"
#include "foundation/json/json_facade.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QPointer>
#include <QCoreApplication>
#include <QDate>

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <unordered_set>

using J = cleaning::LightRow;


// 注册所有清洗规则到 RuleFactoryRegistry（在 initialize 时调用一次）
static void registerAllCleaningRules() {
    using namespace cleaning;
    auto& reg = RuleFactoryRegistry::instance();
    reg.registerFactory("completeness",           [](auto&){ return std::make_unique<CompletenessRule>(); });
    reg.registerFactory("duplicateRemoval",       [](auto& c){ return std::make_unique<DuplicateRemovalRule>(c); });
    reg.registerFactory("financialDateValidity",  [](auto&){ return std::make_unique<FinancialDateValidityRule>(); });
    reg.registerFactory("financialMetricSanitize",[](auto&){ return std::make_unique<FinancialMetricSanitizeRule>(); });
    reg.registerFactory("suspensionFill",         [](auto& c){ return std::make_unique<SuspensionFillRule>(c); });
    reg.registerFactory("missingValueFill",       [](auto& c){ return std::make_unique<MissingValueFillRule>(c); });
    reg.registerFactory("adjustedPrice",          [](auto&){ return std::make_unique<AdjustedPriceRule>(); });
    reg.registerFactory("priceValidity",          [](auto& c){ return std::make_unique<PriceValidityRule>(c); });
    reg.registerFactory("volumeFilter",           [](auto& c){ return std::make_unique<VolumeFilterRule>(c); });
    reg.registerFactory("limitMoveTag",           [](auto& c){ return std::make_unique<LimitMoveTagRule>(c); });
    reg.registerFactory("valuationSanitize",      [](auto&){ return std::make_unique<ValuationSanitizeRule>(); });
    reg.registerFactory("fieldStandardization",   [](auto&){ return std::make_unique<FieldStandardizationRule>(); });
    reg.registerFactory("reportDateAlignment",    [](auto&){ return std::make_unique<ReportDateAlignmentRule>(); });
    reg.registerFactory("survivorBias",           [](auto&){ return std::make_unique<SurvivorBiasRule>(); });
    reg.registerFactory("newStockFilter",         [](auto& c){ return std::make_unique<NewStockFilterRule>(c); });
    reg.registerFactory("stFilter",               [](auto&){ return std::make_unique<STFilterRule>(); });
}

// 根据规则 key 名字符串创建对应的纯 C++ 规则实例
std::unique_ptr<cleaning::ICleaningRule> createCppRule(const std::string& ruleKey, const std::string& configJson) {
    auto rule = cleaning::RuleFactoryRegistry::instance().create(ruleKey, configJson);
    if (!rule) {
        INTERNAL_WARN_STREAM << "[CleaningSvcRefactored] WARNING: unknown cleaning rule key \""
            << ruleKey << "\", skipped. 请检查规则名是否与 CoreCleaningRules.h 中的 ruleName() 一致";
    }
    return rule;
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
DataCleaningServiceRefactored* DataCleaningServiceRefactored::s_instance = nullptr;

DataCleaningServiceRefactored::DataCleaningServiceRefactored(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
    , m_initialized(false)
{
    s_instance = this;
    qRegisterMetaType<RefactoredCleaningStats>("RefactoredCleaningStats");
}

DataCleaningServiceRefactored::~DataCleaningServiceRefactored() {
    if (s_instance == this) s_instance = nullptr;
    if (m_impl->executor) {
        m_impl->executor->shutdown(false);
    }
}

// ── 初始化 ──
bool DataCleaningServiceRefactored::initialize() {
    if (m_initialized) return true;
    DataCacheAdapter::instance().initialize(bridge::storage::persistentDatasetRootDir());
    registerAllCleaningRules();
    m_initialized = true;
    INTERNAL_INFO_STREAM << "[CleaningSvcRefactored] initialized";
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
    rules[QStringLiteral("adjustedPrice")]          = on(false);  // 关闭管线复权: 因子内部有独立复权, 策略回测需原始价算PnL
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
    INTERNAL_INFO_STREAM << "[CleaningSvcRefactored] cancelCleaning: " << requestId.toStdString();
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

            // 1. 打开 Arrow 文件
            std::string path = cleaning::DataCache::instance().dataFilePath(dataSetId);
            auto inR = arrow::io::ReadableFile::Open(path);
            if (!inR.ok()) { emit self->dataSetCleaned(dataSetId, -1, "无法打开数据文件", 0, 0); return; }
            auto rdR = arrow::ipc::RecordBatchFileReader::Open(inR.ValueOrDie());
            if (!rdR.ok()) { emit self->dataSetCleaned(dataSetId, -1, "无法读取数据文件", 0, 0); return; }
            auto reader = rdR.ValueOrDie();
            int numBatches = reader->num_record_batches();
            if (numBatches == 0) { emit self->dataSetCleaned(dataSetId, -1, "数据集为空", 0, 0); return; }

            // 2. 初始化 Schema + 提取字段信息
            auto schema = reader->schema();
            std::vector<std::string> fieldNames;
            std::unordered_set<std::string> numericFields;
            for (int fi = 0; fi < schema->num_fields(); ++fi) {
                auto f = schema->field(fi);
                fieldNames.push_back(f->name());
                if (f->type()->id() == arrow::Type::DOUBLE || f->type()->id() == arrow::Type::FLOAT)
                    numericFields.insert(f->name());
            }
            cleaning::LightSchema::instance().init(fieldNames);

            // 3. 构建引擎 + 创建输出
            cleaning::CleaningEngine engine;
            int ruleCount = 0;
            for (auto it = effectiveRules.begin(); it != effectiveRules.end(); ++it) {
                // 只添加“启用”的规则。原逻辑忽略 enabled 标志，把禁用规则(尤其 reportDateAlignment)
                // 也加入引擎 → trade_date 被披露日覆盖 → 日期重复/数据错位。这是数据损坏的根因。
                const QVariant rv = it.value();
                const bool enabled = (rv.canConvert<QVariantMap>() && rv.toMap().contains("enabled"))
                    ? rv.toMap().value("enabled").toBool() : rv.toBool();
                if (!enabled) continue;
                auto r = createCppRule(it.key().toStdString(), "");
                if (r) { engine.addRule(std::move(r)); ++ruleCount; }
            }
            auto info = DataCacheAdapter::instance().getDataSetInfo(dataSetId);
            QVariantMap infoMap;
            infoMap["displayName"] = QString("cleaning:%1:%2:%3")
                .arg(info.value("sourceType").toString(), info.value("startDate").toString(), info.value("endDate").toString());
            infoMap["sourceType"] = "cleaning";
            infoMap["sourceDataSetId"] = dataSetId;  // 溯源: 此清洗结果来自哪个原始数据集
            infoMap["stockCodes"] = info.value("stockCodes");
            infoMap["startDate"] = info.value("startDate");
            infoMap["endDate"] = info.value("endDate");
            infoMap["isBacktestReady"] = true;
            QStringList fields;
            for (const auto& f : fieldNames) fields.append(QString::fromStdString(f));
            infoMap["availableFields"] = fields;

            // 删除同一原始数据源的旧清洗结果(避免 dataset_X 目录堆积)
            {
                auto& cppCache = cleaning::DataCache::instance();
                for (const auto& ds : cppCache.listDataSets()) {
                    if (ds.sourceType == "cleaning" && ds.sourceDataSetId == dataSetId) {
                        cppCache.removeDataSet(ds.id);
                    }
                }
            }

            resultId = DataCacheAdapter::instance().storeDataSet(QVariantList(), infoMap);
            auto token = DataCacheAdapter::instance().beginArrowWrite(resultId, fieldNames, numericFields);

            QMetaObject::invokeMethod(self.get(), [self, dataSetId]() {
                emit self->cleaningProgress(QString::number(dataSetId), 10, QStringLiteral("清洗中..."));
            }, Qt::QueuedConnection);

            // 4. 预分配行池 + 逐批清洗直写
            const int poolSize = 5000;
            std::vector<cleaning::LightRow> pool(poolSize);
            bool engineFirst = true;
            INTERNAL_INFO_STREAM << "[CleaningSvc] rules=" << ruleCount << " batches=" << numBatches;

            for (int bi = 0; bi < numBatches; ++bi) {
                auto batch = reader->ReadRecordBatch(bi).ValueOrDie();
                int64_t n = batch->num_rows();
                if (n == 0) continue;
                // 关键修复：pool 精确调成本批 n 行。原逻辑只增不减，导致 pool[n..size] 残留
                // 上一批的行被 cleanSortedBatch 反复处理 → 重复输出 + 污染有状态规则（是数据既多又缺的根因）
                pool.resize(static_cast<size_t>(n));
                inputRows += static_cast<int>(n);

                // 填池：预提取列数组 + 字段名 + 类型，避免行循环内 shared_ptr 分配 + hash 查找
                int nCols = batch->num_columns();
                std::vector<std::shared_ptr<arrow::Array>> colOwners(static_cast<size_t>(nCols));
                std::vector<const arrow::DoubleArray*> doublePtrs(static_cast<size_t>(nCols), nullptr);
                std::vector<const arrow::StringArray*> stringPtrs(static_cast<size_t>(nCols), nullptr);
                std::vector<std::string> colNames(static_cast<size_t>(nCols));
                std::vector<bool> colIsNum(static_cast<size_t>(nCols), false);

                for (int c = 0; c < nCols; ++c) {
                    colOwners[static_cast<size_t>(c)] = batch->column(c);
                    colNames[static_cast<size_t>(c)] = schema->field(c)->name();
                    colIsNum[static_cast<size_t>(c)] = numericFields.count(colNames[static_cast<size_t>(c)]) > 0;
                    if (colIsNum[static_cast<size_t>(c)])
                        doublePtrs[static_cast<size_t>(c)] = static_cast<const arrow::DoubleArray*>(colOwners[static_cast<size_t>(c)].get());
                    else
                        stringPtrs[static_cast<size_t>(c)] = static_cast<const arrow::StringArray*>(colOwners[static_cast<size_t>(c)].get());
                }

                for (int64_t ri = 0; ri < n; ++ri) {
                    auto& row = pool[static_cast<size_t>(ri)];
                    row = J::createObject();  // 每行重置为全新对象，避免复用行残留上一批规则加的内部字段
                    for (size_t c = 0; c < static_cast<size_t>(nCols); ++c) {
                        if (colIsNum[c]) {
                            if (doublePtrs[c]->IsNull(ri)) row.setNull(colNames[c].c_str());
                            else row.setDouble(colNames[c].c_str(), doublePtrs[c]->Value(ri));
                        } else {
                            if (stringPtrs[c]->IsNull(ri)) row.setNull(colNames[c].c_str());
                            else row.setString(colNames[c].c_str(), stringPtrs[c]->GetString(ri));
                        }
                    }
                }

                // Arrow Builders
                std::vector<std::unique_ptr<arrow::ArrayBuilder>> builders;
                std::vector<std::shared_ptr<arrow::Field>> sf;
                for (const auto& fn : fieldNames) {
                    if (numericFields.count(fn)) { builders.push_back(std::make_unique<arrow::DoubleBuilder>()); sf.push_back(arrow::field(fn, arrow::float64())); }
                    else { builders.push_back(std::make_unique<arrow::StringBuilder>()); sf.push_back(arrow::field(fn, arrow::utf8())); }
                }

                engine.cleanSortedBatch(pool, engineFirst, bi == numBatches - 1, [&](J& row) {
                    for (size_t ci = 0; ci < fieldNames.size(); ++ci) {
                        const char* cn = fieldNames[ci].c_str();
                        if (!row.has(cn)) {
                            if (numericFields.count(cn)) static_cast<arrow::DoubleBuilder*>(builders[ci].get())->AppendNull();
                            else static_cast<arrow::StringBuilder*>(builders[ci].get())->AppendNull();
                            continue;
                        }
                        auto v = row.get(cn);
                        if (numericFields.count(cn)) {
                            if (v.isNumber()) static_cast<arrow::DoubleBuilder*>(builders[ci].get())->Append(v.asDouble());
                            else static_cast<arrow::DoubleBuilder*>(builders[ci].get())->AppendNull();
                        } else {
                            if (v.isString()) static_cast<arrow::StringBuilder*>(builders[ci].get())->Append(v.asString());
                            else static_cast<arrow::StringBuilder*>(builders[ci].get())->AppendNull();
                        }
                    }
                });
                engineFirst = false;

                int keptThisBatch = engine.lastStats().keptRecords - outputRows;
                outputRows = engine.lastStats().keptRecords;
                if (keptThisBatch > 0) {
                    std::vector<std::shared_ptr<arrow::ChunkedArray>> ca;
                    for (auto& b : builders) { std::shared_ptr<arrow::Array> a; b->Finish(&a); ca.push_back(std::make_shared<arrow::ChunkedArray>(a)); }
                    DataCacheAdapter::instance().appendArrowTable(token, arrow::Table::Make(arrow::schema(sf), ca, static_cast<int64_t>(keptThisBatch)));
                }
                int pct = 10 + (bi + 1) * 85 / numBatches;
                QMetaObject::invokeMethod(self.get(), [self, dataSetId, pct, bi, numBatches]() {
                    if (!self) return;
                    emit self->cleaningProgress(QString::number(dataSetId), pct, QStringLiteral("清洗中 %1/%2 批").arg(bi + 1).arg(numBatches));
                }, Qt::QueuedConnection);
            }
            DataCacheAdapter::instance().finishArrowWrite(token, outputRows);
            message = QString("清洗完成: %1 → %2 条").arg(inputRows).arg(outputRows);
            INTERNAL_INFO_STREAM << "[CleaningSvc] done: " << inputRows << " -> " << outputRows << " rows removed=" << (inputRows - outputRows);

        } catch (const std::exception& e) {
            message = QString("清洗异常: %1").arg(e.what());
        } catch (...) {
            message = QStringLiteral("清洗未知异常");
        }

        int ri = resultId;
        int in = inputRows, out = outputRows;
        QMetaObject::invokeMethod(self.get(), [self, dataSetId, ri, message, in, out]() {
            self->m_impl->cleaningInProgress = false;
            self->cleaningProgress(QString::number(dataSetId), 100, QStringLiteral("完成"));
            emit self->dataSetCleaned(dataSetId, ri, message, in, out);
        }, Qt::QueuedConnection);
    });
}

// ── 增量更新：Arrow Table → LightRow 池（已 CombineChunks，单 chunk）──
static std::vector<cleaning::LightRow> tableToLightRows(const std::shared_ptr<arrow::Table>& table)
{
    std::vector<cleaning::LightRow> rows;
    if (!table) return rows;
    const int64_t n = table->num_rows();
    rows.reserve(static_cast<size_t>(n));
    const int nCols = table->num_columns();
    const auto& schema = table->schema();

    std::vector<const arrow::DoubleArray*> dbl(static_cast<size_t>(nCols), nullptr);
    std::vector<const arrow::StringArray*> str(static_cast<size_t>(nCols), nullptr);
    std::vector<std::string> names(static_cast<size_t>(nCols));
    std::vector<std::shared_ptr<arrow::Array>> owners(static_cast<size_t>(nCols));
    for (int c = 0; c < nCols; ++c) {
        names[static_cast<size_t>(c)] = schema->field(c)->name();
        auto chunked = table->column(c);
        owners[static_cast<size_t>(c)] = chunked->num_chunks() > 0 ? chunked->chunk(0) : nullptr;
        auto& own = owners[static_cast<size_t>(c)];
        if (!own) continue;
        if (own->type_id() == arrow::Type::DOUBLE)
            dbl[static_cast<size_t>(c)] = static_cast<const arrow::DoubleArray*>(own.get());
        else if (own->type_id() == arrow::Type::STRING)
            str[static_cast<size_t>(c)] = static_cast<const arrow::StringArray*>(own.get());
    }

    for (int64_t i = 0; i < n; ++i) {
        auto row = cleaning::LightRow::createObject();
        for (size_t c = 0; c < static_cast<size_t>(nCols); ++c) {
            const char* nm = names[c].c_str();
            if (dbl[c]) {
                if (dbl[c]->IsNull(i)) row.setNull(nm); else row.setDouble(nm, dbl[c]->Value(i));
            } else if (str[c]) {
                if (str[c]->IsNull(i)) row.setNull(nm); else row.setString(nm, str[c]->GetString(i));
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

// ── 增量更新已清洗的数据集 ──
void DataCleaningServiceRefactored::incrementalUpdateDataSet(int dataSetId,
                                                             const QVariantMap& rules)
{
    if (!m_impl->executor) {
        emit incrementalUpdateFinished(dataSetId, false, 0, QStringLiteral("线程池未初始化"));
        return;
    }
    if (dataSetId <= 0) {
        emit incrementalUpdateFinished(dataSetId, false, 0, QStringLiteral("无效的数据集ID"));
        return;
    }

    QVariantMap effectiveRules = rules.isEmpty() ? getDefaultRules() : rules;
    auto executor = m_impl->executor;
    QPointer<DataCleaningServiceRefactored> self(this);

    emit incrementalUpdateStarted(dataSetId);

    executor->post([self, dataSetId, effectiveRules]() {
        if (!self) return;
        using JF = foundation::json::JsonFacade;

        auto emitFinished = [self, dataSetId](bool ok, int newRows, const QString& msg) {
            QMetaObject::invokeMethod(self.get(), [self, dataSetId, ok, newRows, msg]() {
                if (!self) return;
                emit self->incrementalUpdateFinished(dataSetId, ok, newRows, msg);
            }, Qt::QueuedConnection);
        };
        auto emitProgress = [self, dataSetId](int pct, const QString& stage) {
            QMetaObject::invokeMethod(self.get(), [self, dataSetId, pct, stage]() {
                if (!self) return;
                emit self->incrementalUpdateProgress(dataSetId, pct, stage);
            }, Qt::QueuedConnection);
        };

        try {
            auto& cache = cleaning::DataCache::instance();
            auto info = cache.getDataSetInfo(dataSetId);
            if (info.id != dataSetId) { emitFinished(false, 0, QStringLiteral("数据集不存在")); return; }
            if (info.sourceType != "cleaning") { emitFinished(false, 0, QStringLiteral("仅支持对已清洗数据集增量更新")); return; }
            if (info.stockCodes.empty()) { emitFinished(false, 0, QStringLiteral("数据集无标的列表")); return; }

            emitProgress(5, QStringLiteral("读取缓存文件字段与截止日期..."));

            // 1. 以文件为准：真实字段集 + 真实最大 trade_date（不信任元数据）
            auto existingFields = cache.loadDataSetSchemaFields(dataSetId);
            if (existingFields.empty()) { emitFinished(false, 0, QStringLiteral("无法读取缓存文件字段")); return; }
            const std::string endDate = cache.getMaxTradeDate(dataSetId);
            if (endDate.empty()) { emitFinished(false, 0, QStringLiteral("无法从缓存文件读取 trade_date")); return; }

            // 2. 从文件字段集推断 dataTypes（kline_daily 恒有；含财务列则加 financial；含分钟日聚合列则加 minute_data）
            std::vector<std::string> dataTypes = {"kline_daily"};
            {
                std::unordered_set<std::string> fileFields(existingFields.begin(), existingFields.end());
                for (const auto& fc : cleaning::financial_columns::names()) {
                    if (fileFields.count(fc)) { dataTypes.push_back("financial"); break; }
                }
                for (const auto& mc : cleaning::minute_daily_columns::names()) {
                    if (fileFields.count(mc)) { dataTypes.push_back("minute_data"); break; }
                }
            }

            // 3. 字段集对齐：文件 schema 必须与装配器产出逐列一致，否则拒绝（转全量重清，绝不 null 填充凑对齐）
            auto schema = bridge::RawMarketDataAssembler::schemaFor(dataTypes);
            if (existingFields != schema.names) {
                emitFinished(false, 0, QStringLiteral("字段集与缓存文件不一致，需全量重新清洗（不做增量以避免数据错位）"));
                return;
            }
            const std::vector<std::string>& fieldNames = existingFields;
            const std::unordered_set<std::string>& numericFields = schema.numeric;

            // 3. 精确回溯窗口：endDate 往前 kLookbackTradingDays 个交易日（覆盖有状态填充规则最大回溯 + 余量）
            constexpr int kLookbackTradingDays = 20; // SuspensionFill(10) + MissingValueFill(5) 最大回溯 + 余量
            std::string sinceDate = endDate;
            std::string today;
            {
                auto db = astock::database::NativePgConnectionPool::instance().getConnection();
                if (!db || !db->isOpen()) { emitFinished(false, 0, QStringLiteral("数据库连接失败")); return; }
                astock::infrastructure::database::MarketDataRepository repo(std::move(db));
                QDate endQd = QDate::fromString(QString::fromStdString(endDate), "yyyy-MM-dd");
                QString calStart = endQd.isValid()
                    ? endQd.addDays(-(kLookbackTradingDays * 2 + 30)).toString("yyyy-MM-dd")
                    : QString::fromStdString(endDate);
                auto days = repo.queryTradeCalendar(calStart.toStdString(), endDate);
                if (!days.empty()) {
                    int idx = static_cast<int>(days.size()) - 1 - kLookbackTradingDays;
                    sinceDate = (idx >= 0) ? days[static_cast<size_t>(idx)] : days.front();
                }
                today = QDate::currentDate().toString("yyyy-MM-dd").toStdString();
            }

            emitProgress(10, QStringLiteral("拉取增量原始数据..."));

            // 4. 同源装配 [sinceDate, today] 原始数据（含财务 join，schema 与旧文件一致）
            std::vector<std::string> symbols = info.stockCodes;
            bridge::RawMarketDataAssembler assembler;
            std::vector<std::shared_ptr<arrow::Table>> tables;
            auto asmR = assembler.assemble(dataTypes, symbols, sinceDate, today,
                [&tables](const std::shared_ptr<arrow::Table>& t) { if (t) tables.push_back(t); },
                [&emitProgress](int dm, int tm, int) {
                    int pct = 10 + (tm > 0 ? dm * 40 / tm : 0);
                    emitProgress(pct, QStringLiteral("拉取原始数据 %1/%2").arg(dm).arg(tm));
                });
            if (!asmR.ok) { emitFinished(false, 0, QStringLiteral("拉取原始数据失败: %1").arg(QString::fromStdString(asmR.error))); return; }
            if (tables.empty()) { emitFinished(true, 0, QStringLiteral("已是最新（无新数据）")); return; }

            // 合并为单表 + 合并 chunk
            auto concatR = arrow::ConcatenateTables(tables);
            tables.clear();
            if (!concatR.ok()) { emitFinished(false, 0, QStringLiteral("合并原始数据失败")); return; }
            auto combineR = concatR.ValueOrDie()->CombineChunks();
            if (!combineR.ok()) { emitFinished(false, 0, QStringLiteral("整理原始数据失败")); return; }
            auto rawTable = combineR.ValueOrDie();

            emitProgress(55, QStringLiteral("清洗增量..."));

            // 5. Table → LightRow 池，按 (symbol, trade_date) 排序（有状态规则按标的顺序 seed）
            cleaning::LightSchema::instance().init(fieldNames);
            std::vector<cleaning::LightRow> pool = tableToLightRows(rawTable);
            rawTable.reset();
            const char* symKey = cleaning::CF::SYMBOL.c_str();
            const char* tdKey = cleaning::CF::TRADE_DATE.c_str();
            std::sort(pool.begin(), pool.end(), [symKey, tdKey](const cleaning::LightRow& a, const cleaning::LightRow& b) {
                std::string sa = a.has(symKey) ? a.get(symKey).asString() : std::string();
                std::string sb = b.has(symKey) ? b.get(symKey).asString() : std::string();
                if (sa != sb) return sa < sb;
                std::string ta = a.has(tdKey) ? a.get(tdKey).asString() : std::string();
                std::string tb = b.has(tdKey) ? b.get(tdKey).asString() : std::string();
                return ta < tb;
            });

            // 6. 清洗合并批：回溯段 seed 状态后丢弃，只保留 trade_date > endDate 的新行
            cleaning::CleaningEngine engine;
            for (auto it = effectiveRules.begin(); it != effectiveRules.end(); ++it) {
                // 同全量清洗：只添加启用的规则，避免 reportDateAlignment 等禁用规则改写 trade_date
                const QVariant rv = it.value();
                const bool enabled = (rv.canConvert<QVariantMap>() && rv.toMap().contains("enabled"))
                    ? rv.toMap().value("enabled").toBool() : rv.toBool();
                if (!enabled) continue;
                auto r = createCppRule(it.key().toStdString(), "");
                if (r) engine.addRule(std::move(r));
            }
            std::vector<JF> newRows;
            engine.cleanSortedBatch(pool, true, true, [&](cleaning::LightRow& row) {
                if (!row.has(tdKey)) return;
                auto tv = row.get(tdKey);
                if (!tv.isString() || tv.asString() <= endDate) return; // 回溯段丢弃
                JF j = JF::createObject();
                for (const auto& f : fieldNames) {
                    const char* fn = f.c_str();
                    if (!row.has(fn)) continue;
                    auto v = row.get(fn);
                    if (v.isNumber()) j.set(fn, JF::createDouble(v.asDouble()));
                    else if (v.isString()) j.set(fn, JF::createString(v.asString()));
                }
                newRows.push_back(std::move(j));
            });

            if (newRows.empty()) { emitFinished(true, 0, QStringLiteral("已是最新（无新交易日）")); return; }

            emitProgress(85, QStringLiteral("原子追加到缓存..."));

            // 7. 原子追加（失败旧文件保持不变）
            int total = cache.appendDataSetFile(dataSetId, newRows, fieldNames, numericFields);
            if (total < 0) { emitFinished(false, 0, QStringLiteral("追加写入失败（旧文件保持不变）")); return; }

            int added = static_cast<int>(newRows.size());
            emitFinished(true, added, QStringLiteral("增量更新完成：新增 %1 行，总计 %2 行").arg(added).arg(total));
        } catch (const std::exception& e) {
            emitFinished(false, 0, QStringLiteral("增量更新异常: %1").arg(e.what()));
        } catch (...) {
            emitFinished(false, 0, QStringLiteral("增量更新未知异常"));
        }
    });
}

// ── 自定义规则 ──
void DataCleaningServiceRefactored::addCustomRule(const QString& ruleName, const QVariantMap& ruleConfig) {    QMutexLocker lock(&m_impl->stateMutex);
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

bool DataCleaningServiceRefactored::saveUserRuleConfig(const QVariantMap& enabledMap) {
    try {
        QDir cfgDir(bridge::storage::configDir());
        if (!cfgDir.exists("cleaning")) {
            cfgDir.mkpath("cleaning");
        }
        QString filePath = cfgDir.filePath("cleaning/cleaning_rules_user.json");
        QFile f(filePath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            INTERNAL_WARN_STREAM << "[CleaningSvcRefactored] saveUserRuleConfig: cannot write "
                                 << filePath.toStdString();
            return false;
        }
        QJsonObject root;
        root["version"] = 1;
        QJsonObject rules;
        for (auto it = enabledMap.begin(); it != enabledMap.end(); ++it) {
            rules[it.key()] = it.value().toBool();
        }
        root["rules"] = rules;
        QJsonDocument doc(root);
        f.write(doc.toJson(QJsonDocument::Indented));
        f.close();
        return true;
    } catch (const std::exception& e) {
        INTERNAL_WARN_STREAM << "[CleaningSvcRefactored] saveUserRuleConfig: "
                             << e.what();
        return false;
    }
}

QVariantMap DataCleaningServiceRefactored::loadUserRuleConfig() const {
    QVariantMap result;
    try {
        QDir cfgDir(bridge::storage::configDir());
        QString filePath = cfgDir.filePath("cleaning/cleaning_rules_user.json");
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly)) {
            return result; // 文件不存在时返回空，由调用方使用默认值
        }
        QByteArray data = f.readAll();
        f.close();
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            return result;
        }
        QJsonObject rules = doc.object().value("rules").toObject();
        for (auto it = rules.begin(); it != rules.end(); ++it) {
            result[it.key()] = it.value().toBool();
        }
    } catch (const std::exception& e) {
        INTERNAL_WARN_STREAM << "[CleaningSvcRefactored] loadUserRuleConfig: "
                             << e.what();
    }
    return result;
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

QVariantMap DataCleaningServiceRefactored::getLatestCleaningStats() const {
    QMutexLocker lock(&m_impl->stateMutex);
    const RefactoredCleaningStats* latest = nullptr;
    for (const auto& [key, stats] : m_impl->statsByRequest) {
        if (!latest || stats.endTime > latest->endTime) {
            latest = &stats;
        }
    }
    if (latest) {
        return latest->toVariantMap();
    }
    return {};
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
