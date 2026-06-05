// FactorBacktestBridge.cpp — 桥接层，纯参数转换、线程调度、进度信号。
// 因子计算、模拟成交、分析统计等业务逻辑全部在域层执行。

#include "FactorBacktestBridge.h"
#include "DataServiceCache.h"
#include "DataFetchFieldContractUtils.h"
#include "CachedMarketDataView.h"
#include "CachedMarketDataViewHistoricalAdapter.h"

#include "BaseFactor.h"
#include "factor_enums.h"
#include "FactorInstanceManager.h"
#include "ValueFactor.h"
#include "QualityFactor.h"
#include "SizeFactor.h"
#include "MomentumFactor.h"
#include "LowVolFactor.h"
#include "ConfigurableFactor.h"
#include "FactorConfigAccess.h"
#include "DataAvailabilityChecker.h"

#include "factor_compute/IAnalysisModule.h"
#include "factor_compute/SimulatedTradingExecutor.h"
#include "factor_compute/SignalSetBuilder.h"

#include "DataService.h"

#include <QDebug>

#include "foundation/thread/ThreadPoolExecutor.h"

namespace {

int32_t dateToInt(const QString& s)
{
    QString digits; for (QChar c : s) { if (c.isDigit()) digits += c; }
    if (digits.size() >= 8) digits = digits.left(8);
    bool ok = false; int32_t v = digits.toInt(&ok); return ok ? v : 0;
}

/// 创建指定类型的因子实例
std::shared_ptr<factor::BaseFactor> createFactor(factor::FactorType type,
                                                  const foundation::json::JsonFacade& config,
                                                  std::shared_ptr<factor::DataAvailabilityChecker> checker)
{
    factor::FactorInstanceInfo info;
    info.instanceId = "backtest";
    info.instanceName = "backtest";
    info.description = "Backtest Factor Instance";
    info.config = config;

    switch (type) {
    case factor::FactorType::VALUE:
        return factor::ValueFactor::create(info, std::move(checker));
    case factor::FactorType::QUALITY:
        return factor::QualityFactor::create(info, std::move(checker));
    case factor::FactorType::SIZE:
        return factor::SizeFactor::create(info, std::move(checker));
    case factor::FactorType::MOMENTUM:
        return factor::MomentumFactor::create(info, std::move(checker));
    case factor::FactorType::LOW_VOLATILITY:
        return factor::LowVolFactor::create(info, std::move(checker));
    default:
        return nullptr;
    }
}

} // anonymous namespace

QVariantMap FactorBacktestBridge::convertAnalysisReport(
    const factor::compute::AnalysisReport& report,
    const QString& factorId,
    const factor::compute::SimulatedTradingResult& tradingResult) const
{
    QVariantMap result;
    result["status"]   = QStringLiteral("SUCCESS");
    result["factorId"] = factorId;

    QVariantMap metrics;

    // === 执行指标（来自域层 SimulatedTradingResult） ===
    QVariantMap exec;
    exec["annualReturn"]          = tradingResult.annualizedReturn;
    exec["sharpeRatio"]           = tradingResult.sharpeRatio;
    exec["maxDrawdown"]           = tradingResult.maxDrawdown;
    exec["volatility"]            = tradingResult.annualStdDev;
    exec["totalReturn"]           = tradingResult.totalReturn;
    exec["winRate"]               = report.informationCoefficientPositiveRate.available
        ? report.informationCoefficientPositiveRate.value : 0.0;
    exec["turnoverRate"]          = report.turnoverRatio.available ? report.turnoverRatio.value : 0.0;
    exec["benchmarkAnnualReturn"] = 0.0;
    exec["excessAnnualReturn"]    = tradingResult.annualizedReturn;
    exec["trackingError"]         = tradingResult.annualStdDev;
    exec["informationRatio"]      = (tradingResult.annualStdDev > 1e-12)
        ? (tradingResult.annualizedReturn / tradingResult.annualStdDev) : 0.0;
    exec["alpha"]                 = report.alpha.available ? report.alpha.value : 0.0;
    exec["beta"]                  = 0.0;
    exec["calmarRatio"]           = (tradingResult.maxDrawdown > 0.001)
        ? (tradingResult.annualizedReturn / tradingResult.maxDrawdown) : 0.0;
    exec["maxConsecutiveLosses"]  = 0;
    exec["recoveryDays"]          = 0;
    exec["profitFactor"]          = exec["winRate"].toDouble() > 0.01
        ? (exec["winRate"].toDouble() / (1.0 - exec["winRate"].toDouble())) : 0.0;
    exec["sortinoRatio"]          = 0.0;
    metrics["execution"] = exec;

    // === IC 指标 ===
    QVariantMap ic;
    ic["value"] = report.informationCoefficient.available
        ? report.informationCoefficient.value : 0.0;
    ic["ir"]    = report.informationRatio.available
        ? report.informationRatio.value : 0.0;
    metrics["ic"] = ic;

    // === 因子质量 ===
    QVariantMap fq;
    fq["coreRating"]      = report.numGroups.available ? (int)report.numGroups.value : 0;
    fq["coreRatingLabel"] = QStringLiteral("待评估");
    fq["rankIcir"]        = report.informationRatio.available
        ? report.informationRatio.value : 0.0;
    metrics["factorQuality"] = fq;

    // === 研究指标 ===
    QVariantMap rs;
    rs["dataCoverage"] = report.coverageRatio;
    rs["spreadReturn"] = report.layeredReturnSpread.available
        ? report.layeredReturnSpread.value : 0.0;
    metrics["research"] = rs;

    // === 分组结果（来自域层 SimulatedTradingResult） ===
    QVariantList groups;
    for (const auto& gm : tradingResult.groups) {
        QVariantMap grp;
        grp["groupIndex"]      = gm.groupIndex;
        grp["stockCount"]      = gm.stockCount > 0 ? gm.stockCount : 0;
        grp["returnRate"]      = gm.returnRate;
        grp["annualizedReturn"] = gm.annualizedReturn;
        grp["minFactorValue"]  = gm.minFactorValue;
        grp["maxFactorValue"]  = gm.maxFactorValue;
        groups.append(grp);
    }
    metrics["groups"] = groups;
    result["metrics"] = metrics;
    return result;
}


FactorBacktestBridge::FactorBacktestBridge(QObject* parent)
    : QObject(parent), m_timeoutTimer(new QTimer(this))
{ qDebug() << "[FactBacktestBridge] 实例已创建"; }

FactorBacktestBridge::~FactorBacktestBridge() = default;

bool FactorBacktestBridge::initialize()
{
    m_statusText = QStringLiteral("因子回测引擎就绪");
    emit statusChanged();
    return true;
}

bool FactorBacktestBridge::ensureEngineInitialized()
{
    if (m_engineReady) return true;

    DataServiceCache& cache = DataServiceCache::getInstance();
    if (m_selectedDatasetId <= 0) {
        qWarning() << "[FactorBacktestBridge] 未选择数据集";
        return false;
    }

    QVariantList data = cache.getDataSetById(m_selectedDatasetId);
    if (data.isEmpty()) {
        qWarning() << "[FactorBacktestBridge] 数据集为空, id=" << m_selectedDatasetId;
        return false;
    }

    // ═══ 解析数据集，构建列式矩阵视图 ═══
    QMap<QString, int> dateToIdx, symToIdx;
    std::vector<QString> datesVec;
    std::vector<QString> symsOrdered;
    QSet<QString> numericFields;

    struct FlatRow { QString date, symbol; QVariantMap row; };
    std::vector<FlatRow> flatRows; flatRows.reserve(data.size());
    for (const QVariant& var : data) {
        QVariantMap row = var.toMap(); if (row.isEmpty()) continue;
        QString d = row.value("trade_date").toString().left(10);
        QString s = row.value("symbol").toString();
        if (d.isEmpty() || s.isEmpty()) continue;
        flatRows.push_back({d, s, row});
        if (!dateToIdx.contains(d)) {
            dateToIdx[d] = static_cast<int>(datesVec.size());
            datesVec.push_back(d);
        }
        if (!symToIdx.contains(s)) {
            symToIdx[s] = static_cast<int>(symsOrdered.size());
            symsOrdered.push_back(s);
        }
    }

    if (datesVec.empty() || symsOrdered.empty()) {
        qWarning() << "[FactorBacktestBridge] 无有效日期/标的";
        return false;
    }

    std::sort(datesVec.begin(), datesVec.end());
    dateToIdx.clear();
    for (int i = 0; i < (int)datesVec.size(); ++i) {
        dateToIdx[datesVec[i]] = i;
    }

    for (const auto& fr : flatRows) {
        for (auto it = fr.row.constBegin(); it != fr.row.constEnd(); ++it) {
            const QString key = it.key();
            if (key == "trade_date" || key == "symbol" || key == "data_source") continue;
            bool canConvert = false;
            it.value().toDouble(&canConvert);
            if (canConvert) numericFields.insert(key);
        }
        if (numericFields.size() > 50) break;
    }

    const int D = static_cast<int>(datesVec.size());
    const int S = static_cast<int>(symsOrdered.size());
    const int totalCells = D * S;

    using ColumnData = factor::compute::CachedMarketDataView::ColumnData;

    std::unordered_map<std::string, ColumnData> fieldColumns;
    for (const QString& field : numericFields) {
        ColumnData col;
        col.values.assign(totalCells, 0.0);
        col.dateCount = D;
        col.instrumentCount = S;
        fieldColumns[field.toStdString()] = std::move(col);
    }

    ColumnData sharedTemplate;
    sharedTemplate.dates.reserve(D);
    for (const auto& d : datesVec) {
        factor::compute::DateKey dk{}; dk.value = dateToInt(d);
        sharedTemplate.dates.push_back(dk);
    }
    sharedTemplate.instruments.reserve(S);
    for (int i = 0; i < S; ++i) {
        factor::compute::InstrumentId id; id.value = static_cast<uint32_t>(i + 1);
        sharedTemplate.instruments.push_back(id);
    }
    sharedTemplate.dateCount = D;
    sharedTemplate.instrumentCount = S;

    for (const auto& fr : flatRows) {
        int di = dateToIdx.value(fr.date, -1);
        int si = symToIdx.value(fr.symbol, -1);
        if (di < 0 || si < 0) continue;
        int fi = di * S + si;
        for (const QString& field : numericFields) {
            double val = fr.row.value(field).toDouble();
            fieldColumns[field.toStdString()].values[fi] = val;
        }
    }

    auto view = std::make_unique<factor::compute::CachedMarketDataView>();

    auto ensureColumn = [&](const char* name) -> ColumnData {
        auto it = fieldColumns.find(name);
        if (it != fieldColumns.end()) {
            ColumnData col = std::move(it->second);
            col.dates = sharedTemplate.dates;
            col.instruments = sharedTemplate.instruments;
            col.dateCount = D;
            col.instrumentCount = S;
            return col;
        }
        ColumnData col;
        col.values.assign(totalCells, 0.0);
        col.dates = sharedTemplate.dates;
        col.instruments = sharedTemplate.instruments;
        col.dateCount = D;
        col.instrumentCount = S;
        return col;
    };

    view->loadFromColumnData(ensureColumn("open"), ensureColumn("high"),
                             ensureColumn("low"), ensureColumn("close"),
                             ensureColumn("volume"));

    for (auto& [fieldName, col] : fieldColumns) {
        if (fieldName == "open" || fieldName == "high" || fieldName == "low"
            || fieldName == "close" || fieldName == "volume") {
            continue;
        }
        col.dates = sharedTemplate.dates;
        col.instruments = sharedTemplate.instruments;
        col.dateCount = D;
        col.instrumentCount = S;
        view->loadAdditionalField(fieldName, std::move(col));
    }

    m_marketDataView = std::move(view);

    m_historicalAdapter = std::make_shared<factor::bridge::CachedMarketDataViewHistoricalAdapter>(
        *m_marketDataView);

    m_analysisModule = std::make_unique<factor::compute::AnalysisModule>();

    DataService* ds = qobject_cast<DataService*>(m_dataService);
    if (ds) {
        m_historicalAdapter->setDbFallback(
            [ds](const std::string& date, const std::string& field,
                 const std::vector<std::string>& symbols)
                -> std::unordered_map<std::string, double> {
                QStringList qSymbols;
                for (const auto& s : symbols) {
                    qSymbols << QString::fromStdString(s);
                }
                QVariantMap qResult = ds->fetchFieldCrossSectionSync(
                    QString::fromStdString(field),
                    QString::fromStdString(date),
                    qSymbols);

                std::unordered_map<std::string, double> result;
                for (auto it = qResult.constBegin(); it != qResult.constEnd(); ++it) {
                    bool ok = false;
                    double val = it.value().toDouble(&ok);
                    if (ok) {
                        result[it.key().toStdString()] = val;
                    }
                }
                return result;
            });
    }

    m_engineReady = true;
    qDebug() << "[FactorBacktestBridge] 引擎就绪, dates=" << D << "instruments=" << S;
    return true;
}

void FactorBacktestBridge::startBacktestWithFactors(
    const QVariantList& factorIds,
    const QString& /*groupText*/,
    const QString& /*startDate*/,
    const QString& /*endDate*/,
    const QVariantMap& /*cacheSnapshot*/)
{
    if (m_isRunning.load()) return;
    m_isRunning.store(true);
    emit isRunningChanged();
    m_progress = 0.0; emit progressChanged();
    m_statusText = QStringLiteral("启动中..."); emit statusChanged();
    emit backtestStarted(factorIds.isEmpty() ? QStringLiteral("default") : factorIds.first().toString());
    emit backtestProgress(0.0, m_statusText);

    if (!m_workerPool) {
        m_workerPool = std::make_unique<foundation::thread::ThreadPoolExecutor>(
            1, 1, std::chrono::milliseconds(60000), "FactorBacktestWorker");
    }

    QVariantList capturedFactors = factorIds;
    int factorTypeInt = m_factorTypeInt;
    int numGroups = m_numGroups;

    // 从 backtestRuntimeParams 提取回测参数
    QVariantMap runtimeParams = m_backtestRuntimeParams;
    int forwardDays = runtimeParams.value("forwardDays", 30).toInt();
    int rebalanceDays = runtimeParams.value("rebalanceDays", 15).toInt();
    double commissionRate = runtimeParams.value("commissionRate", 0.001).toDouble();
    double slippageRate = runtimeParams.value("slippageRate", 0.001).toDouble();
    double riskFreeRate = runtimeParams.value("riskFreeRate", 0.02).toDouble();

    m_workerPool->post([this, capturedFactors, factorTypeInt, numGroups,
                        forwardDays, rebalanceDays, commissionRate, slippageRate, riskFreeRate]() {
        auto emitOnMain = [this](std::function<void()> fn) {
            QMetaObject::invokeMethod(this, std::move(fn), Qt::QueuedConnection);
        };

        try {
            if (!ensureEngineInitialized()) {
                emitOnMain([this] {
                    emit backtestFailed(QStringLiteral("引擎初始化失败"));
                    m_isRunning.store(false); emit isRunningChanged();
                });
                return;
            }

            const int nFactors = capturedFactors.size();
            QVariantList resultsArr;
            QVariantMap lastMetrics;

            for (int fi = 0; fi < nFactors; ++fi) {
                if (!m_isRunning.load()) break;
                QString fId = capturedFactors[fi].toString();
                double pct = 25.0 + 65.0 * fi / nFactors;
                m_progress = pct;
                m_statusText = QStringLiteral("因子 %1/%2").arg(fi + 1).arg(nFactors);

                emitOnMain([this, pct, msg = m_statusText] {
                    m_progress = pct; m_statusText = msg;
                    emit progressChanged(); emit statusChanged();
                    emit backtestProgress(pct, msg);
                });

                // 创建因子实例
                foundation::json::JsonFacade config = foundation::json::JsonFacade::createObject();
                auto checker = std::shared_ptr<factor::DataAvailabilityChecker>{};
                std::shared_ptr<factor::BaseFactor> factor = createFactor(
                    static_cast<factor::FactorType>(factorTypeInt), config, std::move(checker));
                if (!factor) {
                    emitOnMain([this, fId] {
                        emit backtestFailed(QStringLiteral("无法创建因子: ") + fId);
                        m_isRunning.store(false); emit isRunningChanged();
                    });
                    return;
                }

                // 获取日期和标的列表
                const auto& dates = m_marketDataView->dates();
                auto compactToIso = [](const std::string& compact) -> std::string {
                    if (compact.size() == 8) {
                        return compact.substr(0, 4) + "-" + compact.substr(4, 2) + "-" + compact.substr(6, 2);
                    }
                    return compact;
                };
                std::vector<std::string> dateStrs;
                dateStrs.reserve(dates.size());
                for (const auto& d : dates) {
                    dateStrs.push_back(compactToIso(std::to_string(d.value)));
                }

                auto symbolsVec = m_historicalAdapter->getAvailableSymbols("");

                // === 因子值计算（域层：BaseFactor::calculate） ===
                int totalDates = static_cast<int>(dateStrs.size());
                factor::compute::SimulatedTradingExecutor::FactorValuesByDate factorValuesByDate;
                int validDays = 0;

                for (int di = 0; di < totalDates; ++di) {
                    if (!m_isRunning.load()) break;
                    factor::CalculationContext ctx(dateStrs[di], symbolsVec, m_historicalAdapter);
                    auto calcResult = factor->calculate(ctx);
                    if (!calcResult.isEmpty()) {
                        factorValuesByDate[dateStrs[di]] = std::move(calcResult.values);
                        ++validDays;
                    }
                    double subPct = pct + 55.0 * di / totalDates / nFactors;
                    m_progress = subPct;
                }

                if (validDays == 0) {
                    emitOnMain([this, fId] {
                        emit backtestFailed(QStringLiteral("因子计算无有效结果: ") + fId);
                        m_isRunning.store(false); emit isRunningChanged();
                    });
                    return;
                }

                // === SignalSet 构建（域层：SignalSetBuilder） ===
                const auto& instruments = m_marketDataView->instruments();
                factor::compute::SignalSet signalSet = factor::compute::SignalSetBuilder::build(
                    factorValuesByDate, dates, dateStrs, instruments);

                m_progress = pct + 58.0 / nFactors;

                // === 因子分析（域层：AnalysisModule::analyze） ===
                auto closeView = m_marketDataView->close();
                auto analysisResult = m_analysisModule->analyze(
                    signalSet, factor::compute::GenerateSpec{}, closeView);
                if (!analysisResult.hasValue()) {
                    QString err = QStringLiteral("分析计算失败: err=%1")
                        .arg(static_cast<int>(analysisResult.error()));
                    emitOnMain([this, err] {
                        emit backtestFailed(err);
                        m_isRunning.store(false); emit isRunningChanged();
                    });
                    return;
                }

                // === 模拟成交（域层：SimulatedTradingExecutor） ===
                factor::compute::SimulatedTradingParams tradingParams;
                tradingParams.numGroups = numGroups;
                tradingParams.forwardDays = forwardDays;
                tradingParams.rebalanceDays = rebalanceDays;
                tradingParams.commissionRate = commissionRate;
                tradingParams.slippageRate = slippageRate;
                tradingParams.riskFreeRate = riskFreeRate;
                factor::compute::SimulatedTradingExecutor executor(tradingParams);

                // 构建 instrumentIdToSymbol 映射
                std::unordered_map<uint32_t, std::string> instrumentIdToSymbol;
                for (size_t si = 0; si < symbolsVec.size() && si < instruments.size(); ++si) {
                    instrumentIdToSymbol[instruments[si].value] = symbolsVec[si];
                }

                auto tradingResult = executor.execute(
                    factorValuesByDate,
                    dateStrs,
                    closeView,
                    instruments,
                    instrumentIdToSymbol);

                // 设置分组 stockCount
                int instCount = static_cast<int>(instruments.size());
                for (auto& gm : tradingResult.groups) {
                    if (gm.stockCount == 0 && numGroups > 0) {
                        gm.stockCount = instCount / numGroups;
                    }
                }

                // === QVariant 转换（桥接层唯一职责） ===
                QVariantMap fr = convertAnalysisReport(
                    analysisResult.value(), fId, tradingResult);
                lastMetrics = fr["metrics"].toMap();
                resultsArr.append(fr);

                emitOnMain([this, fr, metrics = lastMetrics] {
                    m_backtestResult = fr;
                    m_resultMetrics = metrics;
                    emit resultMetricsChanged();
                    emit backtestResultChanged();
                });

                m_progress = 25.0 + 65.0 * (fi + 1) / nFactors;
            }

            QVariantMap finalResult;
            finalResult["status"] = QStringLiteral("SUCCESS");
            finalResult["results"] = resultsArr;
            finalResult["metrics"] = lastMetrics;

            emitOnMain([this, finalResult] {
                m_backtestResult = finalResult;
                emit backtestResultChanged();
                emit backtestCompleted(finalResult);
                m_progress = 100.0;
                m_statusText = QStringLiteral("回测完成");
                emit progressChanged(); emit statusChanged();
                emit backtestProgress(100.0, m_statusText);
            });
        } catch (const std::exception& e) {
            emitOnMain([this, msg = QString::fromStdString(e.what())] {
                emit backtestFailed(msg);
                m_isRunning.store(false); emit isRunningChanged();
            });
            return;
        }
        emitOnMain([this] {
            m_isRunning.store(false); emit isRunningChanged();
        });
    });
}

// ── 其余辅助方法保持不变 ──
void FactorBacktestBridge::startCompositeBacktest(const QVariantMap&, const QString&, const QString&, const QString&, const QVariantMap&)
{ qDebug() << "[FactBacktestBridge] 组合回测暂未实现"; }

void FactorBacktestBridge::cancelBacktest()
{ m_isRunning.store(false); emit isRunningChanged(); m_statusText = QLatin1String("已取消"); emit statusChanged(); emit backtestCancelled(); }

QVariantMap FactorBacktestBridge::getDefaultConfig() const { QVariantMap c; c["numGroups"] = 10; return c; }
QVariantList FactorBacktestBridge::getAvailableDataSets() const { return {}; }

QVariantList FactorBacktestBridge::buildBacktestDatasetOptions(const QVariantList& list) const
{ QVariantList opts; for (const QVariant& v : list) {
      QVariantMap ds = v.toMap(); QVariantMap o;
      o["value"] = ds.value("id");
      o["text"]  = ds.value("displayName").toString().isEmpty() ? QStringLiteral("数据集 #%1").arg(ds.value("id").toInt())
                                                                : ds.value("displayName").toString();
      o["raw"] = ds; opts.append(o);
  } return opts; }
bool FactorBacktestBridge::datasetSelectableForBacktest(const QVariantMap& ds) const { return ds.value("isBacktestReady").toBool(); }
void FactorBacktestBridge::runFactorBacktestAsync(const QString&, const QVariantMap&) { startBacktestWithFactors({}, {}, {}, {}, {}); }

QVariantMap FactorBacktestBridge::buildFactorSupportMap(const QVariantList& factorIds, const QString&, const QString&, const QVariantMap&)
{ QVariantMap map; for (const QVariant& id : factorIds) {
      QVariantMap i; i["supported"] = true; i["reason"] = QStringLiteral("当前数据源支持");
      i["category"] = QStringLiteral("supported"); map[id.toString()] = i;
  } m_factorSupportMapCache = map; return map; }

int FactorBacktestBridge::beginFactorSupportMapRefresh(const QVariantList& f, const QString& s, const QString& e, const QVariantMap& c)
{ m_supportMapRequestInFlight.store(true); emit supportMapRequestInFlightChanged();
  QVariantMap map = buildFactorSupportMap(f, s, e, c);
  m_supportMapRequestInFlight.store(false); emit supportMapRequestInFlightChanged();
  emit factorSupportMapReady(1, map); return 1; }

bool FactorBacktestBridge::handleFactorSupportMapReady(int id, const QVariantMap& map)
{ if (id != 1) return false; m_factorSupportMapCache = map; emit factorSupportMapCacheChanged(); return true; }

QVariantList FactorBacktestBridge::normalizeFactorIds(const QVariantList& ids) const
{ QVariantList out; QSet<QString> seen;
  for (const QVariant& v : ids) { QString s = v.toString().trimmed();
      if (!s.isEmpty() && !seen.contains(s)) { seen.insert(s); out.append(s); } }
  std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) { return a.toString() < b.toString(); });
  return out; }

QVariantList FactorBacktestBridge::displayedBacktestResults(const QVariantMap& r) const { QVariantList l; l.append(r); return l; }
QString FactorBacktestBridge::displayedBacktestResultName(const QVariantMap& e) const { return e.value("factorId").toString(); }
QVariantMap FactorBacktestBridge::buildSingleFactorRunEntry(const QVariantMap& r, const QString& n) const { QVariantMap m = r; m["factorId"] = n; return m; }
QVariantList FactorBacktestBridge::pushSingleFactorRunHistory(const QVariantList& h, const QVariantMap& e, int limit, const QString&) const
{ QVariantList u = h; u.prepend(e); if (u.size() > limit) u = u.mid(0, limit); return u; }

QVariantMap FactorBacktestBridge::factorValidationState(
    const QString& factorId, const QString&, bool, const QVariantMap& si,
    const QVariantList&, const QVariantMap&, const QString&, const QVariantList&,
    const QString&, bool, int) const
{ QVariantMap s; s["factorId"] = factorId; s["supported"] = si.value("supported").toBool();
  s["reason"] = si.value("reason").toString(); s["statusText"] = QStringLiteral("可回测");
  s["accentColor"] = QStringLiteral("#3B82F6"); return s; }
QVariantMap FactorBacktestBridge::resolveRiskConfigurationSnapshot(const QVariantMap&, const QVariantMap&, const QVariantMap& sn) const
{ return sn.isEmpty() ? QVariantMap() : sn; }
QVariantList FactorBacktestBridge::riskConfigMetricCards(const QVariantMap&) const { return {}; }

QVariantMap FactorBacktestBridge::backtestRuntimeParams() const { return m_backtestRuntimeParams; }
void FactorBacktestBridge::setBacktestRuntimeParams(const QVariantMap& p) { m_backtestRuntimeParams = p; emit backtestRuntimeParamsChanged(); }
QVariantMap FactorBacktestBridge::backtestResult() const { return m_backtestResult; }
bool   FactorBacktestBridge::isRunning()          const { return m_isRunning.load(); }
double FactorBacktestBridge::progress()            const { return m_progress; }
QString FactorBacktestBridge::status()              const { return m_statusText; }
QVariantMap FactorBacktestBridge::factorSupportMapCache() const { return m_factorSupportMapCache; }
int    FactorBacktestBridge::selectedDatasetId()   const { return m_selectedDatasetId; }
void   FactorBacktestBridge::setSelectedDatasetId(int id) { if (m_selectedDatasetId != id) { m_selectedDatasetId = id; m_engineReady = false; emit selectedDatasetIdChanged(); } }
QString FactorBacktestBridge::dataSourceMode()      const { return m_dataSourceMode; }
void   FactorBacktestBridge::setDataSourceMode(const QString& m) { if (m_dataSourceMode != m) { m_dataSourceMode = m; emit dataSourceModeChanged(); } }
QVariantMap FactorBacktestBridge::selectedDatasetBenchmarkMetadata() const { return m_selectedDatasetBenchmarkMetadata; }
void   FactorBacktestBridge::setSelectedDatasetBenchmarkMetadata(const QVariantMap& md) { m_selectedDatasetBenchmarkMetadata = md; emit selectedDatasetBenchmarkMetadataChanged(); }
QVariantList FactorBacktestBridge::lastPreflightFailures() const { return m_lastPreflightFailures; }
bool   FactorBacktestBridge::supportMapRequestInFlight()  const { return m_supportMapRequestInFlight.load(); }
QVariantList FactorBacktestBridge::selectedStockPoolSymbols() const { return m_selectedStockPoolSymbols; }
void   FactorBacktestBridge::setSelectedStockPoolSymbols(const QVariantList& s) { m_selectedStockPoolSymbols = s; emit selectedStockPoolSymbolsChanged(); }
QVariantMap FactorBacktestBridge::resultMetrics() const { return m_resultMetrics; }