// DataFetchController.cpp
#include "DataFetchController.h"
#include "DataCleaningServiceRefactored.h"
#include "DataCacheAdapter.h"
#include "PreviewDataModel.h"
#include "foundation/json/json_facade.h"
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

QString tableForType(const QString& dt) {
    if (dt == "kline_daily" || dt == "kline_weekly" || dt == "kline_monthly" || dt == "minute_data") return "daily_bar";
    if (dt == "financial") return "financial_indicator";
    if (dt == "news") return "news_data";
    if (dt == "realtime") return "realtime_quote";
    if (dt == "historical") return "historical_data";
    if (dt == "index_constituents") return "index_constituents";
    if (dt == "index_list") return "index_info";
    if (dt == "policy") return "policy_data";
    if (dt == "alternative") return "alternative_data";
    if (dt == "derivatives") return "derivatives_data";
    return QString();
}
QString dateColForType(const QString& dt) {
    if (dt == "kline_daily" || dt == "kline_weekly" || dt == "kline_monthly" || dt == "minute_data") return "trade_date";
    if (dt == "financial" || dt == "news") return "report_date";
    if (dt == "realtime") return "update_time";
    if (dt == "historical") return "trade_date";
    if (dt == "index_constituents") return "snapshot_date";
    if (dt == "index_list") return "listing_date";
    if (dt == "policy") return "announce_date";
    if (dt == "alternative") return "data_date";
    if (dt == "derivatives") return "trade_date";
    return "trade_date";
}
QString symColForType(const QString& dt) {
    if (dt == "financial" || dt == "news") return "fi.symbol_id";
    return "symbol";
}
QString symTableJoinClause(const QString& dt) {
    if (dt == "financial" || dt == "news")
        return " JOIN symbol_info si ON fi.symbol_id = si.symbol_id ";
    return "";
}

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
            QString table = tableForType(dt);
            QString dateCol = dateColForType(dt);
            QString joinClause = symTableJoinClause(dt);
            if (table.isEmpty()) continue;

            std::string fromClause;
            if (joinClause.isEmpty()) {
                fromClause = "FROM " + table.toStdString();
            } else {
                fromClause = "FROM " + table.toStdString() + " fi" + joinClause.toStdString();
            }
            std::string sql = "SELECT si.symbol, MIN(fi." + dateCol.toStdString() + ") AS start_dt, "
                + "MAX(fi." + dateCol.toStdString() + ") AS end_dt, COUNT(*) AS cnt "
                + fromClause + " WHERE fi." + dateCol.toStdString()
                + " BETWEEN '" + startDate.toStdString() + "' AND '"
                + endDate.toStdString() + "' GROUP BY si.symbol";
            if (joinClause.isEmpty()) {
                sql = "SELECT symbol, MIN(" + dateCol.toStdString() + ") AS start_dt, "
                    + "MAX(" + dateCol.toStdString() + ") AS end_dt, COUNT(*) AS cnt "
                    + fromClause + " WHERE " + dateCol.toStdString()
                    + " BETWEEN '" + startDate.toStdString() + "' AND '"
                    + endDate.toStdString() + "' GROUP BY symbol";
            }
                              "MAX(" + dateCol.toStdString() + ") AS end_dt, COUNT(*) AS cnt "
                              "FROM " + table.toStdString() + " WHERE " + dateCol.toStdString()
                              + " BETWEEN '" + startDate.toStdString() + "' AND '"
                              + endDate.toStdString() + "' GROUP BY symbol";
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

        // 先 COUNT 拿到每种类型的真实行数
        std::vector<int> expectedRows;
        int totalExpected = 0;
        for (const QString& dt : dataTypes) {
            QString table = tableForType(dt);
            if (table.isEmpty()) continue;
            QString joinClause = symTableJoinClause(dt);
            std::string fromClause = joinClause.isEmpty()
                ? ("FROM " + table.toStdString())
                : ("FROM " + table.toStdString() + " fi" + joinClause.toStdString());
            std::string prefix = joinClause.isEmpty() ? "" : "fi.";
            std::string countSql = "SELECT COUNT(*) AS cnt " + fromClause
                + " WHERE " + prefix + dateColForType(dt).toStdString()
                + " BETWEEN '" + startDate.toStdString() + "' AND '" + endDate.toStdString() + "'";
            auto cntDb = astock::database::NativeMySQLConnectionPool::instance().getConnection();
            if (!cntDb || !cntDb->isOpen()) { expectedRows.push_back(-1); continue; }
            auto cntResult = cntDb->executeQuery(countSql, {});
            int n = cntResult.rowCount() > 0 ? cntResult.getRows()[0].getInt("cnt") : 0;
            expectedRows.push_back(n);
            totalExpected += n;
            fprintf(stderr, "[DFC] COUNT %s: %d rows\n", dt.toStdString().c_str(), n);
            fflush(stderr);
        }
        QMetaObject::invokeMethod(self.get(), [self, totalExpected]() {
            if (self) self->updateStatus(QString("预计 %1 行，开始下载...").arg(totalExpected), 30);
        }, Qt::QueuedConnection);

        int totalDownloaded = 0;

        // 按月分片下载，每月更新进度
        auto y1 = startDate.mid(0,4).toInt(), m1 = startDate.mid(5,2).toInt(), d1 = startDate.mid(8,2).toInt();
        auto y2 = endDate.mid(0,4).toInt(),   m2 = endDate.mid(5,2).toInt(),   d2 = endDate.mid(8,2).toInt();
        int totalMonths = (y2 - y1) * 12 + (m2 - m1) + 1;
        int monthCount = totalMonths * static_cast<int>(dataTypes.size());
        int doneMonths = 0;
        QMetaObject::invokeMethod(self.get(), [self, monthCount]() { if (self) self->updateStatus(QString("开始下载 %1 个批次...").arg(monthCount), 30); }, Qt::BlockingQueuedConnection);

        auto rowToJson = [](const astock::database::SqlQueryResultRow& r) { auto j = foundation::json::JsonFacade::createObject(); for (const auto& [col, val] : r.getValues()) { if (val.empty()) continue; char* end = nullptr; double d = strtod(val.c_str(), &end); if (end && (size_t)(end - val.c_str()) == val.size()) j.set(col, foundation::json::JsonFacade::createDouble(d)); else j.set(col, foundation::json::JsonFacade::createString(val)); } return j; };
        // 每种数据类型独立 Arrow 文件（schema 不同不能合并）
        std::map<std::string, int> typeIds;
        std::map<std::string, cleaning::DataCache::ArrowWriteToken> typeTokens;
        std::map<std::string, int> typeRowCounts;
        for (const QString& dt : dataTypes) {
            QVariantMap im; im["displayName"] = QString("%1:%2:%3:%4").arg(dataSource, dt, startDate, endDate); im["sourceType"] = dt; im["stockCodes"] = allSymbols; im["startDate"] = startDate; im["endDate"] = endDate;
            int id = DataCacheAdapter::instance().storeDataSet(QVariantList(), im);
            if (id > 0) { typeIds[dt.toStdString()] = id; typeTokens[dt.toStdString()] = DataCacheAdapter::instance().beginArrowWrite(id); typeRowCounts[dt.toStdString()] = 0; }
        }
        static const int dtab[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
        for (int mi = 0; mi < totalMonths; mi++) {
            int cm = m1 + mi, cy = y1 + (cm - 1) / 12; cm = (cm - 1) % 12 + 1;
            int cs = (cy == y1 && cm == m1) ? d1 : 1;
            int ce = (cy == y2 && cm == m2) ? d2 : dtab[cm] + (cm==2 && cy%4==0 && (cy%100!=0||cy%400==0) ? 1 : 0);
            char buf[32]; snprintf(buf, 32, "%04d-%02d-%02d", cy, cm, cs); std::string ms = buf;
            snprintf(buf, 32, "%04d-%02d-%02d", cy, cm, ce); std::string me = buf;
            for (const QString& dt : dataTypes) {
                QString table = tableForType(dt); if (table.isEmpty()) continue;
                QString jc = symTableJoinClause(dt);
                std::string fc = jc.isEmpty() ? ("FROM "+table.toStdString()) : ("FROM "+table.toStdString()+" fi"+jc.toStdString());
                std::string sc = jc.isEmpty() ? "*" : "si.symbol, fi.*";
                std::string pf = jc.isEmpty() ? "" : "fi.";
                std::string sql = "SELECT "+sc+" "+fc+" WHERE "+pf+dateColForType(dt).toStdString()+" BETWEEN '"+ms+"' AND '"+me+"'";
                if (allSymbols.size()>0 && allSymbols.size()<=1000 && symColForType(dt)=="symbol") {
                    sql += " AND symbol IN ("; for (size_t si=0; si<allSymbols.size(); ++si) { if (si>0) sql+=","; sql+="'"+allSymbols[si].toStdString()+"'"; } sql+=")";
                }
                auto db2 = astock::database::NativeMySQLConnectionPool::instance().getConnection(); if (!db2||!db2->isOpen()) goto dl_end;
                auto result = db2->executeQuery(sql, {});
                std::vector<foundation::json::JsonFacade> batch;
                for (const auto& row : result.getRows()) batch.push_back(rowToJson(row));
                if (!batch.empty()) { auto t = typeTokens[dt.toStdString()]; DataCacheAdapter::instance().appendArrowBatch(t, batch); typeRowCounts[dt.toStdString()] += (int)batch.size(); }
            }
            int totalNow = 0; for (auto& [k,v] : typeRowCounts) totalNow += v;
            doneMonths++; int dm=doneMonths, tm=monthCount, tr=totalNow;
            QMetaObject::invokeMethod(self.get(), [self,dm,tm,tr](){ if(!self)return; int pct=30+(dm*68)/qMax(1,tm); self->updateStatus(QString("下载 %1/%2 月 (%3 行)").arg(dm).arg(tm).arg(tr),pct); emit self->dataFetchProgress(pct,QString("下载 %1/%2 月").arg(dm).arg(tm)); }, Qt::BlockingQueuedConnection);
            fprintf(stderr, "[DFC] month %d/%d: total=%d", dm, tm, totalNow); for (auto& [k,v] : typeRowCounts) fprintf(stderr, " %s=%d", k.c_str(), v); fprintf(stderr, "\n"); fflush(stderr);
        }
        dl_end:
        for (const QString& dt : dataTypes) {
            auto t = typeTokens[dt.toStdString()]; int rows = typeRowCounts[dt.toStdString()];
            DataCacheAdapter::instance().finishArrowWrite(t, rows);
        }
        int grandTotal = 0; for (auto& [k,v] : typeRowCounts) grandTotal += v;
        int did = typeIds.begin()->second, tr = grandTotal;
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
