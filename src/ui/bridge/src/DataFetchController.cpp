// DataFetchController.cpp
#include "DataFetchController.h"
#include "DataCleaningServiceRefactored.h"
#include "DataCacheAdapter.h"
#include "PreviewDataModel.h"
#include "foundation/json/json_facade.h"
#include "database/MarketDataRepository.h"
#include "database/NativeMySQLConnectionPool.h"

#include <QMetaObject>
#include <QPointer>
#include <QDebug>
#include <QDateTime>
#include <QSet>
#include <QTimer>

#include <functional>
#include <memory>

namespace {

template <typename Func>
void invokeOnMainThread(DataFetchController* controller, Func&& func)
{
    QPointer<DataFetchController> safe(controller);
    QMetaObject::invokeMethod(controller,
        [safe, fn = std::forward<Func>(func)]() mutable {
            if (safe) fn(safe.data());
        }, Qt::QueuedConnection);
}

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
    });
    connect(m_cleaningSvc, &DataCleaningServiceRefactored::cleaningError,
            this, [this](const QString&, const QString& e) {
        m_operationInProgress = false; emit operationInProgressChanged();
        emit dataCleaningError(e);
    });
    QTimer::singleShot(1000, this, SLOT(logInitMessage()));
}

DataFetchController::~DataFetchController() {}

// ---- 数据查询（GROUP BY → 标的列表 + 后台 Arrow 存储） ----

void DataFetchController::fetchDataTypesBySource(const QString& dataSource,
                                                  const QString& symbol,
                                                  const QStringList& dataTypes,
                                                  const QString& startDate,
                                                  const QString& endDate,
                                                  const QVariantMap&)
{
    Q_UNUSED(dataTypes);
    if (startDate.isEmpty() || endDate.isEmpty()) {
        updateStatus("日期未设置", 0); emit dataFetchError("日期未设置"); return;
    }

    m_isFetching = true; m_fetchedData.clear();
    if (m_previewModel) m_previewModel->clearData();
    emit isFetchingChanged();
    updateStatus("查询标的列表...", 10);

    auto& pool = astock::database::NativeMySQLConnectionPool::instance();
    auto db = pool.getConnection();
    if (!db || !db->isOpen()) {
        m_isFetching = false; emit isFetchingChanged();
        updateStatus("数据库连接失败", 100); emit dataFetchError("数据库连接失败"); return;
    }

    std::string sql = "SELECT symbol, MIN(trade_date) AS start_dt, MAX(trade_date) AS end_dt, "
                      "COUNT(*) AS cnt, MIN(close) AS first_close, MAX(close) AS last_close "
                      "FROM daily_bar WHERE trade_date BETWEEN '" + startDate.toStdString()
                      + "' AND '" + endDate.toStdString() + "' "
                      "GROUP BY symbol ORDER BY symbol";
    auto result = db->executeQuery(sql, {});
    QVariantList symbolList;
    QStringList symbols;
    for (const auto& row : result.getRows()) {
        QVariantMap item;
        item["symbol"] = QString::fromStdString(row.getString("symbol"));
        item["startDate"] = QString::fromStdString(row.getString("start_dt"));
        item["endDate"] = QString::fromStdString(row.getString("end_dt"));
        item["recordCount"] = row.getInt("cnt");
        item["firstClose"] = row.getDouble("first_close");
        item["lastClose"] = row.getDouble("last_close");
        symbolList.append(item);
        symbols.append(item["symbol"].toString());
    }

    m_fetchedData = symbolList;
    m_isFetching = false; emit isFetchingChanged(); emit fetchedDataChanged();

    if (symbols.isEmpty()) {
        updateStatus("查询结果为空", 100); emit dataFetchError("查询结果为空"); return;
    }

    if (m_previewModel) {
        QVector<QVariantMap> previewVec;
        for (const auto& item : symbolList) previewVec.append(item.toMap());
        m_previewModel->updateData(previewVec);
    }

    m_pendingDoneTotal = symbolList.size();
    updateStatus(QString("共 %1 只标的").arg(symbols.size()), 100);
    emit dataFetchProgress(100, QString("共 %1 只标的").arg(symbols.size()));

    // 后台查全量 → Arrow
    auto symbolsShared = std::make_shared<QStringList>(symbols);
    QPointer<DataFetchController> self(this);
    QTimer::singleShot(100, [self, dsSource = dataSource, dsStart = startDate, dsEnd = endDate, symbolsShared]() {
        if (!self) return;
        auto db2 = astock::database::NativeMySQLConnectionPool::instance().getConnection();
        if (!db2 || !db2->isOpen()) { fprintf(stderr, "[DFC] Arrow: conn failed\n"); fflush(stderr); return; }
        astock::infrastructure::database::MarketDataRepository repo(std::move(db2));
        std::vector<foundation::json::JsonFacade> allRows;
        QDate sD = QDate::fromString(dsStart, "yyyy-MM-dd");
        QDate eD = QDate::fromString(dsEnd, "yyyy-MM-dd");
        for (QDate m = QDate(sD.year(), sD.month(), 1); m <= eD; m = m.addMonths(1)) {
            QDate ms = qMax(sD, m);
            QDate me = qMin(eD, QDate(m.year(), m.month(), m.daysInMonth()));
            if (ms > me) continue;
            std::vector<std::string> batchSyms;
            for (const auto& s : *symbolsShared) batchSyms.push_back(s.toStdString());
            auto bars = repo.queryDailyBarBatch(batchSyms,
                ms.toString("yyyy-MM-dd").toStdString(),
                me.toString("yyyy-MM-dd").toStdString());
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
                allRows.push_back(std::move(j));
            }
        }
        if (!allRows.empty()) {
            QVariantMap infoMap;
            infoMap["displayName"] = QString("%1:ALL:%2:%3").arg(dsSource, dsStart, dsEnd);
            infoMap["sourceType"] = dsSource;
            infoMap["stockCodes"] = *symbolsShared;
            infoMap["startDate"] = dsStart;
            infoMap["endDate"] = dsEnd;
            infoMap["rowCount"] = static_cast<int>(allRows.size());
            int id = DataCacheAdapter::instance().storeDataSetFromRows(allRows, infoMap);
            if (id > 0) {
                self->m_lastStoredDataSetId = id;
                emit self->dataSetReadyForCleaning(id);
            }
        }
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

    std::string escaped = "'" + symbol.toStdString() + "'";
    std::string sql = "SELECT symbol, trade_date, open, high, low, close, volume, turnover "
                      "FROM daily_bar WHERE symbol=" + escaped
                      + " AND trade_date BETWEEN '" + m_startDate.toStdString()
                      + "' AND '" + m_endDate.toStdString() + "' "
                      "ORDER BY trade_date LIMIT " + std::to_string(kPageSize)
                      + " OFFSET " + std::to_string(page * kPageSize);
    auto result = db->executeQuery(sql, {});
    QVariantList data;
    for (const auto& row : result.getRows()) {
        QVariantMap m;
        m["symbol"] = QString::fromStdString(row.getString("symbol"));
        m["trade_date"] = QString::fromStdString(row.getString("trade_date"));
        m["open"] = row.getDouble("open");
        m["high"] = row.getDouble("high");
        m["low"] = row.getDouble("low");
        m["close"] = row.getDouble("close");
        m["volume"] = row.getDouble("volume");
        m["turnover"] = row.getDouble("turnover");
        data.append(m);
    }
    emit symbolDetailLoaded(symbol, page, data);
}

// ---- 清洗（从 DataSet） ----

void DataFetchController::cleanDataFromDataSet(int dataSetId, const QVariantMap& rules)
{
    m_operationInProgress = true; emit operationInProgressChanged();
    m_operationPhase = "清洗数据"; emit operationPhaseChanged();
    emit dataCleaningStarted();

    connect(m_cleaningSvc, &DataCleaningServiceRefactored::dataSetCleaned,
            this, [this](int inputId, int resultId, const QString& msg, int in, int out) {
        m_operationInProgress = false; emit operationInProgressChanged();
        if (resultId > 0) {
            updateCleanStats(in, out);
            emit dataSetCleaned(inputId, resultId, msg, in, out);
        } else {
            emit dataCleaningError(msg);
        }
    }, Qt::SingleShotConnection);

    m_cleaningSvc->cleanDataFromDataSet(dataSetId, rules);
}

void DataFetchController::cleanDataAsync(const QVariantMap& rules)
{
    if (m_lastStoredDataSetId <= 0) {
        emit dataCleaningError("请先查询数据生成数据集");
        return;
    }
    cleanDataFromDataSet(m_lastStoredDataSetId, rules);
}

// ---- 数据集刷新 ----

void DataFetchController::refreshDataSetInfos()
{
    auto& cache = DataCacheAdapter::instance();
    auto infos = cache.getAllDataSetInfos();
    QVariantList result;
    for (const QVariantMap& info : infos) {
        QVariantMap map;
        map["id"] = info.value("id");
        map["displayName"] = info.value("displayName");
        map["sourceType"] = info.value("sourceType");
        map["rowCount"] = info.value("rowCount");
        map["stockCodes"] = info.value("stockCodes");
        map["startDate"] = info.value("startDate");
        map["endDate"] = info.value("endDate");
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

void DataFetchController::logInitMessage()
{
    fprintf(stderr, "[DataFetchController] ready\n"); fflush(stderr);
}

void DataFetchController::onDataLoadProgress(int p, const QString& m)
{
    if (m_progress != p) { m_progress = p; emit progressChanged(); }
    if (m_statusMessage != m) { m_statusMessage = m; emit statusMessageChanged(); }
}

void DataFetchController::onDataLoadCompleted(bool ok, const QString& m, const QVariantList& data)
{
    if (ok && !data.isEmpty()) { m_fetchedData = data; emit fetchedDataChanged(); }
    m_isFetching = false; emit isFetchingChanged();
    updateStatus(m, ok ? 100 : 0);
    if (ok) {
        if (m_previewModel && !data.isEmpty()) {
            QVector<QVariantMap> pv; for (const auto& i : data) pv.append(i.toMap());
            m_previewModel->updateData(pv);
        }
    } else {
        emit dataFetchError(m);
    }
}

void DataFetchController::onDataLoadError(const QString& e)
{
    m_isFetching = false; emit isFetchingChanged();
    updateStatus(e, 0); emit dataFetchError(e);
}

void DataFetchController::onDataCleaningProgress(int p, const QString& m)
{
    m_progress = p; m_statusMessage = m;
    emit progressChanged(); emit statusMessageChanged();
}

void DataFetchController::onDataCleaningProgressDetail(int p, const QString& m,
                                                        const QString& sym, int kept, int removed)
{
    m_progress = p; m_statusMessage = m; m_currentProgressStock = sym;
    updateCleanStats(kept + removed, kept);
    emit progressChanged(); emit statusMessageChanged(); emit currentProgressStockChanged();
}

void DataFetchController::onDataCleaningCompleted(bool ok, const QString& m, const QVariantList&)
{
    m_operationInProgress = false; emit operationInProgressChanged();
    if (!ok) emit dataCleaningError(m);
}

// ---- 属性 ----

void DataFetchController::setDataSource(const QString& s) {
    if (m_dataSource != s) { m_dataSource = s; emit dataSourceChanged(); }
}
void DataFetchController::setSymbols(const QStringList& s) {
    if (m_symbols != s) { m_symbols = s; emit symbolsChanged(); }
}
void DataFetchController::setStartDate(const QString& d) {
    if (m_startDate != d) { m_startDate = d; emit startDateChanged(); }
}
void DataFetchController::setEndDate(const QString& d) {
    if (m_endDate != d) { m_endDate = d; emit endDateChanged(); }
}
void DataFetchController::setPreviewModel(PreviewDataModel* m) {
    if (m_previewModel != m) { m_previewModel = m; emit previewModelChanged(); }
}

void DataFetchController::delayedCleanData()
{
    if (!m_pendingCleanAfterLoad) return;
    if (m_fetchedData.isEmpty()) {
        updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
        emit dataCleaningCompleted(false, "没有可用的数据集", QVariantList());
        return;
    }
    updateCleanStats(m_fetchedData.size(), 0);
    m_pendingCleanAfterLoad = false;
    emit requestCleanData(m_fetchedData, m_pendingRules);
}

void DataFetchController::updateStatus(const QString& m, int p) {
    if (p >= 0) { m_progress = p; emit progressChanged(); }
    m_statusMessage = m; emit statusMessageChanged();
}

void DataFetchController::resetProgressState() {
    updateBoolProperty(m_operationInProgress, false, [this]() { emit operationInProgressChanged(); });
    updateStringProperty(m_operationPhase, QString(), [this]() { emit operationPhaseChanged(); });
    updateStringProperty(m_currentProgressStock, QString(), [this]() { emit currentProgressStockChanged(); });
    updateStatus("就绪", 0);
}

void DataFetchController::updateCleanStats(int in, int out) {
    m_cleanInputRecordCount = in;
    m_cleanOutputRecordCount = out;
    m_cleanRemovedRecordCount = in - out;
    emit cleanStatsChanged();
}
