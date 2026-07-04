// DataFetchController.cpp
#include "DataFetchController.h"
#include "DataCleaningServiceRefactored.h"
#include "DataCacheAdapter.h"
#include "PreviewDataModel.h"
#include "foundation/json/json_facade.h"
#include "DataSourceRegistry.h"
#include "database/MarketDataRepository.h"
#include "DataTableAssembler.h"
#include <arrow/api.h>
#include "database/NativeMySQLConnectionPool.h"

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
    QTimer::singleShot(0, this, [this]() { refreshDataSetInfos(); });
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

        auto db = astock::database::NativeMySQLConnectionPool::instance().getConnection();
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

        // 按月分片下载，每月更新进度
        auto y1 = startDate.mid(0,4).toInt(), m1 = startDate.mid(5,2).toInt(), d1 = startDate.mid(8,2).toInt();
        auto y2 = endDate.mid(0,4).toInt(),   m2 = endDate.mid(5,2).toInt(),   d2 = endDate.mid(8,2).toInt();
        int totalMonths = (y2 - y1) * 12 + (m2 - m1) + 1;
        int monthCount = totalMonths;
        int doneMonths = 0;
        QMetaObject::invokeMethod(self.get(), [self, monthCount]() { if (self) self->updateStatus(QString("开始下载 %1 个批次...").arg(monthCount), 30); }, Qt::QueuedConnection);

        // ── 构建统一 Schema（DataSourceRegistry 唯一定义点）──
        std::vector<std::string> typeNames;
        for (const QString& dt : dataTypes) typeNames.push_back(dt.toStdString());
        auto mergedSchema = cleaning::fullSchemaForTypes(typeNames);
        const auto& allFields = mergedSchema.names;
        const auto& numericFields = mergedSchema.numeric;
        if (allFields.empty()) { /* error handling */ return; }

        // 财务字段名集合（用于后续合并判断）
        const auto& finCols = cleaning::financial_columns::names();
        std::unordered_set<std::string> finColSet(finCols.begin(), finCols.end());

        // 预构建全量 symbol 列表 + 分片大小
        std::vector<std::string> allSymbolsVec;
        for (const auto& s : allSymbols) allSymbolsVec.push_back(s.toStdString());
        static const int symbolChunkSize = 200;

        // ── 第一步：全量加载财务数据到内存（~11万行，<200MB）──
        //   symbol → [(report_date, {col: val})], report_date 升序
        std::map<std::string, std::vector<std::pair<std::string, std::unordered_map<std::string, std::string>>>> finCache;
        {
            auto dbFin = astock::database::NativeMySQLConnectionPool::instance().getConnection();
            if (dbFin && dbFin->isOpen()) {
                astock::infrastructure::database::MarketDataRepository finRepo(std::move(dbFin));
                std::string ed = endDate.toStdString();
                // 财务数据需覆盖缓存起始之前的报告期，确保首个交易日能对齐最近财报
                std::string finSd = "2014-01-01"; // 数据库最早 report_date=2014-12-31
                // 分批查询避免单次结果集过大
                for (size_t fs = 0; fs < allSymbolsVec.size(); fs += symbolChunkSize) {
                    size_t fe = (std::min)(fs + symbolChunkSize, allSymbolsVec.size());
                    std::vector<std::string> fchunk(allSymbolsVec.begin() + fs, allSymbolsVec.begin() + fe);
                    auto frows = finRepo.queryFinancialData(fchunk, finSd, ed);
                    for (const auto& row : frows) {
                        const auto& vals = row.getValues();
                        auto symIt = vals.find("symbol");
                        auto rptIt = vals.find("report_date");
                        if (symIt == vals.end() || rptIt == vals.end()
                            || rptIt->second.empty()) continue;
                        std::unordered_map<std::string, std::string> fv;
                        for (const auto& [col, val] : vals) {
                            if (!val.empty() && finColSet.count(col)) fv[col] = val;
                        }
                        // 按报告日排序，对齐时 report_date ≤ trade_date
                        finCache[symIt->second].emplace_back(rptIt->second, std::move(fv));
                    }
                }
            }
            // 每标的 report_date 升序
            for (auto& [sym, vec] : finCache)
                std::sort(vec.begin(), vec.end(),
                    [](auto& a, auto& b) { return a.first < b.first; });
        }
        QMetaObject::invokeMethod(self.get(), [self]() {
            if (self) self->updateStatus("财务数据已加载，开始下载K线...", 30);
        }, Qt::QueuedConnection);

        // index_code 映射（直接 Arrow 构建时用）
        std::map<std::string, std::string> indexMap;
        {
            auto idxDb = astock::database::NativeMySQLConnectionPool::instance().getConnection();
            if (idxDb && idxDb->isOpen()) {
                astock::infrastructure::database::MarketDataRepository idxRepo(std::move(idxDb));
                indexMap = idxRepo.queryIndexCodeMap(endDate.toStdString());
            }
        }

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
        int totalRows = 0;

        // ── 第二步：按月分片下载 K线，每行即时合并财务数据 ──
        static const int dtab[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
        for (int mi = 0; mi < totalMonths; mi++) {
            int cm = m1 + mi, cy = y1 + (cm - 1) / 12; cm = (cm - 1) % 12 + 1;
            int cs = (cy == y1 && cm == m1) ? d1 : 1;
            int ce = (cy == y2 && cm == m2) ? d2 : dtab[cm] + (cm==2 && cy%4==0 && (cy%100!=0||cy%400==0) ? 1 : 0);
            char buf[32]; snprintf(buf, 32, "%04d-%02d-%02d", cy, cm, cs); std::string ms = buf;
            snprintf(buf, 32, "%04d-%02d-%02d", cy, cm, ce); std::string me = buf;

            for (size_t start = 0; start < allSymbolsVec.size(); start += symbolChunkSize) {
                size_t end = (std::min)(start + symbolChunkSize, allSymbolsVec.size());
                std::vector<std::string> chunk(allSymbolsVec.begin() + start, allSymbolsVec.begin() + end);

                auto db2 = astock::database::NativeMySQLConnectionPool::instance().getConnection();
                if (!db2 || !db2->isOpen()) goto dl_end;
                astock::infrastructure::database::MarketDataRepository repo(std::move(db2));
                auto rows = repo.queryDailyBarJoined(chunk, ms, me);
                if (rows.empty()) continue;
                int64_t nRows = static_cast<int64_t>(rows.size());

                // ── 委托领域层构建 Arrow Table ──
                auto table = domain::data::DataTableAssembler::buildFromSqlRows(
                    rows, allFields, numericFields, finCache, indexMap);
                rows.clear(); rows.shrink_to_fit();

                if (table) DataCacheAdapter::instance().appendArrowTable(token, table);
                totalRows += static_cast<int>(nRows);
            }
            // 本月 K线已写完，清除不会再被后续月份引用的旧财报
            for (auto it = finCache.begin(); it != finCache.end(); ) {
                auto& vec = it->second;
                auto cut = std::lower_bound(vec.begin(), vec.end(), ms,
                    [](const auto& rp, const std::string& c) { return rp.first < c; });
                if (cut != vec.begin()) --cut;
                if (cut != vec.begin()) { vec.erase(vec.begin(), cut); vec.shrink_to_fit(); }
                if (vec.empty()) it = finCache.erase(it);
                else ++it;
            }
            doneMonths++; int dm=doneMonths, tm=monthCount, tr=totalRows;
            QMetaObject::invokeMethod(self.get(), [self,dm,tm,tr](){
                if(!self)return;
                int pct=30+(dm*68)/qMax(1,tm);
                self->updateStatus(QString("下载 %1/%2 月 (%3 行)").arg(dm).arg(tm).arg(tr),pct);
                emit self->dataFetchProgress(pct,QString("下载 %1/%2 月").arg(dm).arg(tm));
            }, Qt::QueuedConnection);
        }
        dl_end:
        DataCacheAdapter::instance().finishArrowWrite(token, totalRows);
        // 释放财务缓存和标的列表
        finCache.clear();
        allSymbolsVec.clear(); allSymbolsVec.shrink_to_fit();
        indexMap.clear();
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
    auto& pool = astock::database::NativeMySQLConnectionPool::instance();
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

void DataFetchController::cleanDataAsync(const QVariantMap& rules)
{
    if (m_lastStoredDataSetId <= 0) { emit dataCleaningError("请先查询数据生成数据集"); return; }
    cleanDataFromDataSet(m_lastStoredDataSetId, rules);
}
