// DataFetchController.cpp
#include "DataFetchController.h"
#include "DataCleaningServiceRefactored.h"
#include "DataCacheAdapter.h"
#include "PreviewDataModel.h"
#include "foundation/json/json_facade.h"
#include "DataSourceRegistry.h"
#include "database/MarketDataRepository.h"
#include "DataTableAssembler.h"
#include "RawMarketDataAssembler.h"
#include <arrow/api.h>
#include "database/NativePgConnectionPool.h"

#include "foundation.h"
#include <QMetaObject>
#include <QPointer>
#include <QDebug>
#include <QDateTime>
#include <QSet>
#include <QTimer>

#include <functional>
#include <memory>

namespace {

void updateStringProperty(QString& target, const QString& value, const std::function<void()>& notifier)
{
    if (target != value) { target = value; notifier(); }
}

void updateBoolProperty(bool& target, bool value, const std::function<void()>& notifier)
{
    if (target != value) { target = value; notifier(); }
}

static constexpr int kPageSize = 100;


} // anonymous namespace

// ---- 构造/析构 ----

DataFetchController::DataFetchController(QObject* parent)
    : QObject(parent)
    , m_cleaningSvc(new DataCleaningServiceRefactored(this))
    , m_previewModel(new PreviewDataModel(this))
{
    INTERNAL_INFO_STREAM << "[DataFetchController] constructed";
    QDateTime now = QDateTime::currentDateTime();
    m_startDate = now.addDays(-30).toString("yyyy-MM-dd");
    m_endDate = now.toString("yyyy-MM-dd");

    m_cleaningSvc->initialize();

    connect(m_cleaningSvc, &DataCleaningServiceRefactored::cleaningProgress,
            this, [this](const QString&, int p, const QString& m) {
        m_progress = p; m_statusMessage = m;
        emit progressChanged(); emit statusMessageChanged();
        emit dataCleaningProgress(p, m);
    });
    connect(m_cleaningSvc, &DataCleaningServiceRefactored::cleaningError,
            this, [this](const QString&, const QString& e) {
        m_operationInProgress = false; emit operationInProgressChanged();
        emit dataCleaningError(e);
    });
    // 增量更新信号转发（一次性连接，避免重复触发）；同步驱动进度条属性 progress/operationInProgress
    connect(m_cleaningSvc, &DataCleaningServiceRefactored::incrementalUpdateStarted,
            this, [this](int id) {
        m_operationInProgress = true; emit operationInProgressChanged();
        m_operationPhase = QStringLiteral("增量更新"); emit operationPhaseChanged();
        m_progress = 0; emit progressChanged();
        emit datasetUpdateStarted(id);
    });
    connect(m_cleaningSvc, &DataCleaningServiceRefactored::incrementalUpdateProgress,
            this, [this](int id, int pct, const QString& stage) {
        m_progress = pct; m_statusMessage = stage;
        emit progressChanged(); emit statusMessageChanged();
        emit datasetUpdateProgress(id, pct, stage);
    });
    connect(m_cleaningSvc, &DataCleaningServiceRefactored::incrementalUpdateFinished,
            this, [this](int id, bool ok, int newRows, const QString& msg) {
        m_operationInProgress = false; emit operationInProgressChanged();
        m_progress = ok ? 100 : m_progress; emit progressChanged();
        if (ok && newRows > 0) { refreshDataSetInfos(); refreshCleanedDataSetInfos(); } // 新数据落盘后刷新列表
        emit datasetUpdateFinished(id, ok, newRows, msg);
    });
    QTimer::singleShot(0, this, [this]() { refreshDataSetInfos(); refreshCleanedDataSetInfos(); });
    QTimer::singleShot(1000, this, SLOT(logInitMessage()));
}

DataFetchController::~DataFetchController() {}

// ---- 数据查询 ----

void DataFetchController::fetchDataTypesBySource(const QString& dataSource,
                                                  const QString& symbol,
                                                  const QStringList& dataTypes,
                                                  const QString& startDate,
                                                  const QString& endDate,
                                                  const QVariantMap&)
{
    Q_UNUSED(symbol);
    if (startDate.isEmpty() || endDate.isEmpty()) {
        updateStatus("日期未设置", 0); emit dataFetchError("日期未设置"); return;
    }
    if (dataTypes.isEmpty()) {
        updateStatus("未选择数据类型", 0); emit dataFetchError("未选择数据类型"); return;
    }

    m_isFetching = true; m_fetchedData.clear();
    if (m_previewModel) m_previewModel->clearData();
    emit isFetchingChanged();
    updateStatus("分析数据范围...", 0);

    QPointer<DataFetchController> self(this);
    FOUNDATION_THREADS.post([self, dataSource, startDate, endDate, dataTypes]() {
        if (!self) return;

        auto db = astock::database::NativePgConnectionPool::instance().getConnection();
        if (!db || !db->isOpen()) {
            QMetaObject::invokeMethod(self.get(), [self]() {
                if (!self) return;
                self->m_isFetching = false; emit self->isFetchingChanged();
                self->updateStatus("数据库连接失败", 100);
                emit self->dataFetchError("数据库连接失败");
            }, Qt::QueuedConnection);
            return;
        }

        // 使用 MarketDataRepository 获取标的覆盖信息（替代 DataSourceRegistry::buildGroupQuery）
        astock::infrastructure::database::MarketDataRepository repo(db);
        QMap<QString, QVariantMap> merged;
        QStringList allSymbols;

        std::string sd = startDate.toStdString(), ed = endDate.toStdString();

        // 按需查询各数据类型的覆盖信息
        for (const QString& dt : dataTypes) {
            std::vector<astock::database::SqlQueryResultRow> coverage;
            if (dt == "kline_daily") {
                coverage = repo.querySymbolCoverage("mkt.daily_bar", "trade_date", sd, ed, "id");
            } else if (dt == "financial") {
                coverage = repo.querySymbolCoverage("fund.financial_indicator_daily", "report_date", sd, ed, "id");
            } else {
                continue;
            }
            for (const auto& row : coverage) {
                QString sym = QString::fromStdString(row.getString("symbol"));
                if (merged.contains(sym)) {
                    QVariantMap& m = merged[sym];
                    m["recordCount"] = m["recordCount"].toInt() + row.getInt("cnt");
                    std::string s = row.getString("start_dt"), e = row.getString("end_dt");
                    if (s < m["startDate"].toString().toStdString()) m["startDate"] = QString::fromStdString(s);
                    if (e > m["endDate"].toString().toStdString()) m["endDate"] = QString::fromStdString(e);
                } else {
                    QVariantMap item;
                    item["symbol"] = sym;
                    item["startDate"] = QString::fromStdString(row.getString("start_dt"));
                    item["endDate"] = QString::fromStdString(row.getString("end_dt"));
                    item["recordCount"] = row.getInt("cnt");
                    item["dataType"] = dt;
                    merged[sym] = item;
                    allSymbols.append(sym);
                }
            }
        }

        QVariantList symbolList;
        for (const QString& s : allSymbols) symbolList.append(merged[s]);
        int total = symbolList.size();

        QMetaObject::invokeMethod(self.get(), [self, symbolList, total]() {
            if (!self) return;
            self->m_fetchedData = symbolList;
            self->m_isFetching = false; emit self->isFetchingChanged(); emit self->fetchedDataChanged();
            if (total > 0 && self->m_previewModel) {
                QVector<QVariantMap> pv; for (const auto& item : symbolList) pv.append(item.toMap());
                self->m_previewModel->updateData(pv);
            }
            self->m_pendingDoneTotal = total;
            self->updateStatus(QString("共 %1 只标的").arg(total), 20);
        }, Qt::QueuedConnection);

        // ── 构建统一 Schema（用于建数据集写入 token；装配细节委托 RawMarketDataAssembler）──
        std::vector<std::string> typeNames;
        for (const QString& dt : dataTypes) typeNames.push_back(dt.toStdString());
        auto mergedSchema = bridge::RawMarketDataAssembler::schemaFor(typeNames);
        const auto& allFields = mergedSchema.names;
        const auto& numericFields = mergedSchema.numeric;
        if (allFields.empty()) { return; }

        // 预构建全量 symbol 列表
        std::vector<std::string> allSymbolsVec;
        for (const auto& s : allSymbols) allSymbolsVec.push_back(s.toStdString());

        QMetaObject::invokeMethod(self.get(), [self]() {
            if (self) self->updateStatus("加载财务数据并下载K线...", 30);
        }, Qt::QueuedConnection);

        // 创建数据集 + 预设 Schema
        QVariantMap infoMap;
        infoMap["displayName"] = QString("%1:%2:%3:%4").arg(dataSource, dataTypes.join(","), startDate, endDate);
        infoMap["sourceType"] = dataSource;
        infoMap["stockCodes"] = allSymbols;
        infoMap["startDate"] = startDate;
        infoMap["endDate"] = endDate;
        int dataId = DataCacheAdapter::instance().storeDataSet(QVariantList(), infoMap);
        auto token = dataId > 0 ? DataCacheAdapter::instance().beginArrowWrite(dataId, allFields, numericFields) : nullptr;
        if (!token) { /* error handling */ return; }

        // ── 委托共享组装器：按月分片装配原始行情，逐块写入 token（与增量路径同源）──
        bridge::RawMarketDataAssembler assembler;
        auto asmResult = assembler.assemble(
            typeNames, allSymbolsVec, startDate.toStdString(), endDate.toStdString(),
            [token](const std::shared_ptr<arrow::Table>& table) {
                DataCacheAdapter::instance().appendArrowTable(token, table);
            },
            [self](int dm, int tm, int tr) {
                QMetaObject::invokeMethod(self.get(), [self, dm, tm, tr]() {
                    if (!self) return;
                    int pct = 30 + (dm * 68) / qMax(1, tm);
                    self->updateStatus(QString("下载 %1/%2 月 (%3 行)").arg(dm).arg(tm).arg(tr), pct);
                    emit self->dataFetchProgress(pct, QString("下载 %1/%2 月").arg(dm).arg(tm));
                }, Qt::QueuedConnection);
            });
        int totalRows = asmResult.totalRows;
        DataCacheAdapter::instance().finishArrowWrite(token, totalRows);
        int did = dataId, tr = totalRows;
        QMetaObject::invokeMethod(self.get(), [self, did, tr]() {
            if (!self || did <= 0) return;
            self->updateStatus(QString("查询完成，共 %1 行").arg(tr), 100);
            emit self->dataFetchProgress(100, QString("查询完成，共 %1 行").arg(tr));
            self->m_lastStoredDataSetId = did;
            self->refreshDataSetInfos();
            emit self->dataSetReadyForCleaning(did);
        }, Qt::QueuedConnection);
    });
}

void DataFetchController::fetchDataByType(const QString& dataSource,
                                           const QString& symbol,
                                           const QString& dataType,
                                           const QString& startDate,
                                           const QString& endDate,
                                           const QVariantMap& options)
{
    fetchDataTypesBySource(dataSource, symbol, {dataType}, startDate, endDate, options);
}

// ---- 按需预览 ----


void DataFetchController::loadSymbolDetail(const QString& symbol, int page)
{
    auto& pool = astock::database::NativePgConnectionPool::instance();
    auto db = pool.getConnection();
    if (!db || !db->isOpen()) return;

    QVariantList data;

    // K线（通过 MarketDataRepository，SQL 不对外暴露）
    astock::infrastructure::database::MarketDataRepository detailRepo(db);
    auto kRows = detailRepo.queryKlineDetail(
        symbol.toStdString(), m_startDate.toStdString(), m_endDate.toStdString(),
        kPageSize, page * kPageSize);
    for (const auto& row : kRows) {
        QVariantMap m;
        for (const auto& f : cleaning::kline_columns::names())
            m[QString::fromStdString(f)] = QString::fromStdString(row.getString(f));
        for (const auto& f : cleaning::symbol_info_columns::names())
            m[QString::fromStdString(f)] = QString::fromStdString(row.getString(f));
        m["dataType"] = "kline_daily";
        data.append(m);
    }

    // 财务（通过 MarketDataRepository，SQL 不对外暴露）
    auto fRows = detailRepo.queryFinancialDetail(
        symbol.toStdString(), m_startDate.toStdString(), m_endDate.toStdString(),
        kPageSize, page * kPageSize);
    for (const auto& row : fRows) {
        QVariantMap m;
        for (const auto& col : row.getValues()) {
            const auto& val = col.second;
            if (val.empty()) continue;
            char* end = nullptr;
            double d = strtod(val.c_str(), &end);
            if (end && static_cast<size_t>(end - val.c_str()) == val.size())
                m[QString::fromStdString(col.first)] = d;
            else
                m[QString::fromStdString(col.first)] = QString::fromStdString(val);
        }
        m["dataType"] = "financial";
        data.append(m);
    }

    emit symbolDetailLoaded(symbol, page, data);
}

void DataFetchController::refreshDataSetInfos()
{
    auto& cache = DataCacheAdapter::instance();
    auto infos = cache.getAllDataSetInfos();
    QVariantList result;
    for (const QVariantMap& info : infos) {
        // 清洗下拉框只显示未清洗的缓存（sourceType 非 "cleaning"）
        const QString st = info.value("sourceType").toString();
        if (st == QStringLiteral("cleaning")) {
            continue;
        }
        QVariantMap map;
        map["id"] = info.value("id"); map["displayName"] = info.value("displayName");
        map["sourceType"] = st; map["rowCount"] = info.value("rowCount");
        map["stockCodes"] = info.value("stockCodes");
        map["startDate"] = info.value("startDate"); map["endDate"] = info.value("endDate");
        map["tags"] = info.value("tags");
        qint64 created = info.value("createdAt", 0).toLongLong();
        map["createdTime"] = created > 0 ? QDateTime::fromSecsSinceEpoch(created).toString("yyyy-MM-dd HH:mm:ss") : "";
        result.append(map);
    }
    emit dataSetInfosRefreshed(result);
}

void DataFetchController::refreshCleanedDataSetInfos()
{
    auto& cache = DataCacheAdapter::instance();
    auto infos = cache.getAllDataSetInfos();
    QVariantList result;
    for (const QVariantMap& info : infos) {
        // 只列已清洗缓存（sourceType == "cleaning"），与原始集下拉框互补
        if (info.value("sourceType").toString() != QStringLiteral("cleaning")) {
            continue;
        }
        QVariantMap map;
        map["id"] = info.value("id"); map["displayName"] = info.value("displayName");
        map["sourceType"] = info.value("sourceType"); map["rowCount"] = info.value("rowCount");
        map["stockCodes"] = info.value("stockCodes");
        map["startDate"] = info.value("startDate"); map["endDate"] = info.value("endDate");
        qint64 created = info.value("createdAt", 0).toLongLong();
        map["createdTime"] = created > 0 ? QDateTime::fromSecsSinceEpoch(created).toString("yyyy-MM-dd HH:mm:ss") : "";
        result.append(map);
    }
    emit cleanedDataSetInfosRefreshed(result);
}

void DataFetchController::onDropdownRefreshed(int count)
{
    Q_UNUSED(count);
    if (m_pendingDoneTotal <= 0) return;
    m_isFetching = false; emit isFetchingChanged();
    emit dataFetchProgress(100, m_pendingDoneMsg.isEmpty()
        ? QString("查询完成，共 %1 只标的").arg(m_pendingDoneTotal) : m_pendingDoneMsg);
    m_pendingDoneTotal = 0;
}

bool DataFetchController::removeDataSet(int dataSetId) {
    if (dataSetId <= 0) return false;
    bool ok = DataCacheAdapter::instance().removeDataSet(dataSetId);
    if (ok) refreshDataSetInfos();
    return ok;
}

void DataFetchController::logInitMessage() { INTERNAL_INFO_STREAM << "[DataFetchController] ready"; }

void DataFetchController::onDataLoadProgress(int p, const QString& m) {
    if (m_progress != p) { m_progress = p; emit progressChanged(); }
    if (m_statusMessage != m) { m_statusMessage = m; emit statusMessageChanged(); }
}
void DataFetchController::onDataLoadCompleted(bool ok, const QString& m, const QVariantList& data) {
    if (ok && !data.isEmpty()) { m_fetchedData = data; emit fetchedDataChanged(); }
    m_isFetching = false; emit isFetchingChanged(); updateStatus(m, ok ? 100 : 0);
    if (ok) { if (m_previewModel && !data.isEmpty()) { QVector<QVariantMap> pv; for (const auto& i : data) pv.append(i.toMap()); m_previewModel->updateData(pv); } }
    else emit dataFetchError(m);
}
void DataFetchController::onDataLoadError(const QString& e) { m_isFetching = false; emit isFetchingChanged(); updateStatus(e, 0); emit dataFetchError(e); }
void DataFetchController::onDataCleaningProgress(int p, const QString& m) { m_progress = p; m_statusMessage = m; emit progressChanged(); emit statusMessageChanged(); }
void DataFetchController::onDataCleaningProgressDetail(int p, const QString& m, const QString& sym, int kept, int removed) {
    m_progress = p; m_statusMessage = m; m_currentProgressStock = sym; updateCleanStats(kept + removed, kept);
    emit progressChanged(); emit statusMessageChanged(); emit currentProgressStockChanged();
}
void DataFetchController::onDataCleaningCompleted(bool ok, const QString& m, const QVariantList&) {
    m_operationInProgress = false; emit operationInProgressChanged(); if (!ok) emit dataCleaningError(m);
}
void DataFetchController::setDataSource(const QString& s) { if (m_dataSource != s) { m_dataSource = s; emit dataSourceChanged(); } }
void DataFetchController::setSymbols(const QStringList& s) { if (m_symbols != s) { m_symbols = s; emit symbolsChanged(); } }
void DataFetchController::setStartDate(const QString& d) { if (m_startDate != d) { m_startDate = d; emit startDateChanged(); } }
void DataFetchController::setEndDate(const QString& d) { if (m_endDate != d) { m_endDate = d; emit endDateChanged(); } }
void DataFetchController::setPreviewModel(PreviewDataModel* m) { if (m_previewModel != m) { m_previewModel = m; emit previewModelChanged(); } }
void DataFetchController::delayedCleanData() {
    if (!m_pendingCleanAfterLoad) return;
    if (m_fetchedData.isEmpty()) { updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); }); emit dataCleaningCompleted(false, "没有可用的数据集", QVariantList()); return; }
    updateCleanStats(m_fetchedData.size(), 0); m_pendingCleanAfterLoad = false; emit requestCleanData(m_fetchedData, m_pendingRules);
}
void DataFetchController::updateStatus(const QString& m, int p) { if (p >= 0) { m_progress = p; emit progressChanged(); } m_statusMessage = m; emit statusMessageChanged(); }
void DataFetchController::resetProgressState() { updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); }); updateStringProperty(m_operationPhase, QString(), [this]() { emit operationPhaseChanged(); }); updateStringProperty(m_currentProgressStock, QString(), [this]() { emit currentProgressStockChanged(); }); updateStatus("就绪", 0); }
void DataFetchController::updateCleanStats(int in, int out) { m_cleanInputRecordCount = in; m_cleanOutputRecordCount = out; m_cleanRemovedRecordCount = in - out; emit cleanStatsChanged(); }

// ---- 清洗 ----

void DataFetchController::cleanDataFromDataSet(int dataSetId, const QVariantMap& rules)
{
    m_operationInProgress = true; emit operationInProgressChanged();
    m_operationPhase = "清洗数据"; emit operationPhaseChanged();
    emit dataCleaningStarted();

    connect(m_cleaningSvc, &DataCleaningServiceRefactored::dataSetCleaned,
            this, [this](int inputId, int resultId, const QString& msg, int in, int out) {
        m_operationInProgress = false; emit operationInProgressChanged();
        if (resultId > 0) { updateCleanStats(in, out); emit dataSetCleaned(inputId, resultId, msg, in, out); }
        else emit dataCleaningError(msg);
    });

    m_cleaningSvc->cleanDataFromDataSet(dataSetId, rules);
}

void DataFetchController::incrementalUpdateDataSet(int dataSetId)
{
    // 增量更新转发到清洗服务；进度/结果经构造函数中的连接转发为 datasetUpdate* 信号
    m_cleaningSvc->incrementalUpdateDataSet(dataSetId, QVariantMap());
}

// ── 缓存数据查看 ──
QVariantList DataFetchController::allDataSetInfos()
{
    QVariantList out;
    for (const auto& m : DataCacheAdapter::instance().getAllDataSetInfos()) out.append(m);
    return out;
}

QVariantList DataFetchController::loadCacheRowsBySymbol(int dataId, const QString& symbol)
{
    return DataCacheAdapter::instance().loadRowsBySymbol(dataId, symbol);
}

void DataFetchController::cleanDataAsync(const QVariantMap& rules)
{
    if (m_lastStoredDataSetId <= 0) { emit dataCleaningError("请先查询数据生成数据集"); return; }
    cleanDataFromDataSet(m_lastStoredDataSetId, rules);
}
