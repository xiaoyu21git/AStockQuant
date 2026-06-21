// DataFetchController.cpp
#include "DataFetchController.h"
#include "DataCleaningServiceRefactored.h"
#include "DataCacheAdapter.h"
#include "PreviewDataModel.h"
#include "foundation/json/json_facade.h"
#include "DataSourceRegistry.h"
#include "database/MarketDataRepository.h"
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
    fprintf(stderr, "[DataFetchController] constructed\n"); fflush(stderr);
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

        QMap<QString, QVariantMap> merged;
        QStringList allSymbols;

        for (const QString& dt : dataTypes) {
            auto* src = cleaning::sourceByName(dt.toStdString());
            if (!src) continue;
            std::string sql = src->buildGroupQuery(startDate.toStdString(), endDate.toStdString());
            auto result = db->executeQuery(sql, {});
            for (const auto& row : result.getRows()) {
                QString sym = QString::fromStdString(row.getString("symbol"));
                if (merged.contains(sym)) {
                    QVariantMap& m = merged[sym];
                    m["recordCount"] = m["recordCount"].toInt() + row.getInt("cnt");
                    std::string sd = row.getString("start_dt"), ed = row.getString("end_dt");
                    if (sd < m["startDate"].toString().toStdString()) m["startDate"] = QString::fromStdString(sd);
                    if (ed > m["endDate"].toString().toStdString()) m["endDate"] = QString::fromStdString(ed);
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
        int monthCount = totalMonths * static_cast<int>(dataTypes.size());
        int doneMonths = 0;
        QMetaObject::invokeMethod(self.get(), [self, monthCount]() { if (self) self->updateStatus(QString("开始下载 %1 个批次...").arg(monthCount), 30); }, Qt::BlockingQueuedConnection);

        // 第一步：扫描所有类型的列名，合并成统一集合
        std::vector<std::string> allFields;
        std::unordered_set<std::string> fieldSet;
        for (const QString& dt : dataTypes) {
            auto* src = cleaning::sourceByName(dt.toStdString()); if (!src) continue;
            std::vector<std::string> sv; for(const auto& s : allSymbols) sv.push_back(s.toStdString());
            std::string sql = src->buildDataQuery(startDate.toStdString(), endDate.toStdString(), sv) + " LIMIT 1";
            auto dbr = astock::database::NativeMySQLConnectionPool::instance().getConnection();
            if (!dbr||!dbr->isOpen()) continue;
            auto rr = dbr->executeQuery(sql,{});
            for (const auto& row : rr.getRows()) for (const auto& [col,_] : row.getValues()) if (fieldSet.insert(col).second) allFields.push_back(col);
        }
        auto rowToJson = [&allFields](const astock::database::SqlQueryResultRow& r) { auto j = foundation::json::JsonFacade::createObject(); for (const auto& col : allFields) { auto it = r.getValues().find(col); if (it == r.getValues().end() || it->second.empty()) { j.set(col, foundation::json::JsonFacade::createDouble(NAN)); continue; } char* end = nullptr; double d = strtod(it->second.c_str(), &end); if (end && (size_t)(end - it->second.c_str()) == it->second.size()) j.set(col, foundation::json::JsonFacade::createDouble(d)); else j.set(col, foundation::json::JsonFacade::createString(it->second)); } return j; };
        // 写单个 Arrow 文件
        QVariantMap infoMap; infoMap["displayName"] = QString("%1:%2:%3:%4").arg(dataSource, dataTypes.join(","), startDate, endDate); infoMap["sourceType"] = dataSource; infoMap["stockCodes"] = allSymbols; infoMap["startDate"] = startDate; infoMap["endDate"] = endDate;
        // 从首行数据自动判定数值/字符串类型，避免硬编码字段列表
        std::unordered_set<std::string> numericFields;
        auto testNumeric = [&](const std::string& v) { if(v.empty())return false; char* e=nullptr; strtod(v.c_str(),&e); return e && (size_t)(e-v.c_str())==v.size(); };
        if (!allFields.empty()) {
            auto dbr = astock::database::NativeMySQLConnectionPool::instance().getConnection();
            if (dbr && dbr->isOpen()) {
                for (const QString& dt : dataTypes) {
                    auto* src = cleaning::sourceByName(dt.toStdString()); if (!src) continue;
                    std::vector<std::string> sv; for(const auto& s:allSymbols) sv.push_back(s.toStdString());
                    std::string sql = src->buildDataQuery(startDate.toStdString(),endDate.toStdString(),sv) + " LIMIT 1";
                    auto rr = dbr->executeQuery(sql,{});
                    for (const auto& row : rr.getRows())
                        for (const auto& [col,val] : row.getValues()) {}
                }
            }
        }
        int dataId = DataCacheAdapter::instance().storeDataSet(QVariantList(), infoMap);
        auto token = dataId > 0 ? DataCacheAdapter::instance().beginArrowWrite(dataId, allFields, numericFields) : nullptr;
        int totalRows = 0;
        static const int dtab[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
        for (int mi = 0; mi < totalMonths; mi++) {
            int cm = m1 + mi, cy = y1 + (cm - 1) / 12; cm = (cm - 1) % 12 + 1;
            int cs = (cy == y1 && cm == m1) ? d1 : 1;
            int ce = (cy == y2 && cm == m2) ? d2 : dtab[cm] + (cm==2 && cy%4==0 && (cy%100!=0||cy%400==0) ? 1 : 0);
            char buf[32]; snprintf(buf, 32, "%04d-%02d-%02d", cy, cm, cs); std::string ms = buf;
            snprintf(buf, 32, "%04d-%02d-%02d", cy, cm, ce); std::string me = buf;
            for (const QString& dt : dataTypes) {
                auto* src = cleaning::sourceByName(dt.toStdString()); if (!src) continue;
                std::vector<foundation::json::JsonFacade> batch;
                auto db2 = astock::database::NativeMySQLConnectionPool::instance().getConnection(); if (!db2||!db2->isOpen()) goto dl_end;
                astock::infrastructure::database::MarketDataRepository repo(std::move(db2));
                if (src->typeName() == "financial") {
                    auto rows = repo.queryAllMarketFinancialData(ms, me);
                    for (const auto& row : rows) batch.push_back(rowToJson(row));
                } else {
                    std::vector<std::string> sv; for(const auto& s:allSymbols) sv.push_back(s.toStdString());
                    auto bars = repo.queryDailyBarBatch(sv, ms, me);
                    for (const auto& bar : bars) {
                        auto j = foundation::json::JsonFacade::createObject();
                        j.set("symbol", foundation::json::JsonFacade::createString(bar.symbol));
                        j.set("trade_date", foundation::json::JsonFacade::createString(bar.tradeDate));
                        j.set("open", foundation::json::JsonFacade::createDouble(bar.open));
                        j.set("high", foundation::json::JsonFacade::createDouble(bar.high));
                        j.set("low", foundation::json::JsonFacade::createDouble(bar.low));
                        j.set("close", foundation::json::JsonFacade::createDouble(bar.close));
                        j.set("volume", foundation::json::JsonFacade::createDouble(bar.volume));
                        j.set("turnover", foundation::json::JsonFacade::createDouble(bar.turnover));
                        batch.push_back(std::move(j));
                    }
                }
                if (!batch.empty()) { DataCacheAdapter::instance().appendArrowBatch(token, batch); totalRows += (int)batch.size(); }
            }
            doneMonths++; int dm=doneMonths, tm=monthCount, tr=totalRows;
            QMetaObject::invokeMethod(self.get(), [self,dm,tm,tr](){ if(!self)return; int pct=30+(dm*68)/qMax(1,tm); self->updateStatus(QString("下载 %1/%2 月 (%3 行)").arg(dm).arg(tm).arg(tr),pct); emit self->dataFetchProgress(pct,QString("下载 %1/%2 月").arg(dm).arg(tm)); }, Qt::BlockingQueuedConnection);
        }
        dl_end:
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
    auto& pool = astock::database::NativeMySQLConnectionPool::instance();
    auto db = pool.getConnection();
    if (!db || !db->isOpen()) return;

    std::string sym = "'" + symbol.toStdString() + "'";
    std::string sd = "'" + m_startDate.toStdString() + "'";
    std::string ed = "'" + m_endDate.toStdString() + "'";
    std::string limit = std::to_string(kPageSize);
    std::string offset = std::to_string(page * kPageSize);

    QVariantList data;

    // K线
    std::string kSql = "SELECT symbol, trade_date, open, high, low, close, volume, turnover "
                       "FROM daily_bar WHERE symbol=" + sym + " AND trade_date BETWEEN " + sd
                       + " AND " + ed + " ORDER BY trade_date LIMIT " + limit + " OFFSET " + offset;
    auto kResult = db->executeQuery(kSql, {});
    for (const auto& row : kResult.getRows()) {
        QVariantMap m;
        m["symbol"] = QString::fromStdString(row.getString("symbol"));
        m["trade_date"] = QString::fromStdString(row.getString("trade_date"));
        m["open"] = row.getDouble("open"); m["high"] = row.getDouble("high");
        m["low"] = row.getDouble("low"); m["close"] = row.getDouble("close");
        m["volume"] = row.getDouble("volume"); m["turnover"] = row.getDouble("turnover");
        m["dataType"] = "kline_daily";
        data.append(m);
    }

    // 财务
    std::string fSql = "SELECT si.symbol, fi.* FROM financial_indicator fi JOIN symbol_info si ON fi.symbol_id=si.symbol_id"
                       " WHERE si.symbol=" + sym + " AND fi.report_date BETWEEN " + sd + " AND " + ed
                       + " ORDER BY fi.report_date LIMIT " + limit + " OFFSET " + offset;
    auto fResult = db->executeQuery(fSql, {});
    for (const auto& row : fResult.getRows()) {
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
        QVariantMap map;
        map["id"] = info.value("id"); map["displayName"] = info.value("displayName");
        map["sourceType"] = info.value("sourceType"); map["rowCount"] = info.value("rowCount");
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

void DataFetchController::logInitMessage() { fprintf(stderr, "[DataFetchController] ready\n"); fflush(stderr); }

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
