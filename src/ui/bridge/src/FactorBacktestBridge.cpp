// FactorBacktestBridge.cpp — 桥接层，直接调用 BaseFactor 因子体系计算
// 通过 HistoricalView 适配器将 CachedMarketDataView 适配为因子所需的 HistoricalView 接口

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

/// 将 AnalysisReport 转换为 QML 期望的 QVariantMap 格式
QVariantMap convertAnalysisReport(const factor::compute::AnalysisReport& report,
                                   const QString& factorId,
                                   int instrumentCount = 0,
                                   const std::vector<double>& lastCrossSectionValues = {},
                                   const std::vector<double>& groupReturns = {},
                                   const std::vector<int>& /*groupCounts*/ = {},
                                   int nGroupsOverride = 5,
                                   double computedAnnRet = 0.0,
                                   double computedMaxDD = 0.0,
                                   double computedAnnStd = 0.0,
                                   double computedSharpe = 0.0,
                                   double computedEquity = 1.0)
{
    QVariantMap result;
    result["status"]   = QStringLiteral("SUCCESS");
    result["factorId"] = factorId;

    QVariantMap metrics;

    QVariantMap exec;
    // 使用独立计算的执行指标，回退到 AnalysisReport 的值
    exec["annualReturn"]          = std::isfinite(computedAnnRet) ? computedAnnRet : 0.0;
    exec["sharpeRatio"]           = std::isfinite(computedSharpe) ? computedSharpe : 0.0;
    exec["maxDrawdown"]           = std::isfinite(computedMaxDD) ? computedMaxDD : 0.0;
    exec["volatility"]            = std::isfinite(computedAnnStd) ? computedAnnStd : 0.0;
    exec["totalReturn"]           = std::isfinite(computedEquity) ? (computedEquity - 1.0) : 0.0;
    auto scalarVal = [](const factor::compute::AnalysisScalarMetric& m) -> double {
        return m.available ? m.value : 0.0;
    };
    exec["winRate"]               = scalarVal(report.informationCoefficientPositiveRate);
    exec["turnoverRate"]          = scalarVal(report.turnoverRatio);
    exec["benchmarkAnnualReturn"] = 0.0;
    exec["excessAnnualReturn"]    = exec["annualReturn"];
    exec["trackingError"]         = std::isfinite(computedAnnStd) ? computedAnnStd : 0.0;
    exec["informationRatio"]      = (computedAnnStd > 1e-12 && std::isfinite(computedAnnRet))
        ? (computedAnnRet / computedAnnStd) : 0.0;
    exec["alpha"]                 = scalarVal(report.alpha);
    exec["beta"]                  = 0.0;
    metrics["execution"] = exec;

    QVariantMap ic;
    ic["value"] = scalarVal(report.informationCoefficient);
    ic["ir"]    = scalarVal(report.informationRatio);
    metrics["ic"] = ic;

    QVariantMap fq;
    fq["coreRating"]      = report.numGroups.available ? (int)report.numGroups.value : 0;
    fq["coreRatingLabel"] = report.numGroups.available
                                 ? QStringLiteral("待评估")
                                 : QStringLiteral("待评估");
    fq["rankIcir"]        = scalarVal(report.informationRatio);
    metrics["factorQuality"] = fq;

    QVariantMap rs;
    rs["dataCoverage"] = report.coverageRatio;
    rs["spreadReturn"] = scalarVal(report.layeredReturnSpread);
    metrics["research"] = rs;

    // 构建分组结果
    QVariantList groups;
    int nGroups = nGroupsOverride > 0 ? nGroupsOverride : 5;
    if (report.numGroups.available && report.numGroups.value > 0) {
        nGroups = report.numGroups.value;
    }

    int realInstrumentCount = (instrumentCount > 0) ? instrumentCount : 0;

    // 因子值范围
    std::vector<double> sortedVals = lastCrossSectionValues;
    if (!sortedVals.empty()) {
        sortedVals.erase(std::remove_if(sortedVals.begin(), sortedVals.end(),
            [](double v) { return !std::isfinite(v); }), sortedVals.end());
        std::sort(sortedVals.begin(), sortedVals.end());
    }

    bool hasRealReturns = !groupReturns.empty() && (int)groupReturns.size() >= nGroups;

    if (realInstrumentCount > 0) {
        for (int g = 0; g < nGroups; ++g) {
            QVariantMap grp;
            grp["groupIndex"] = g + 1;
            grp["stockCount"] = static_cast<int>(realInstrumentCount / nGroups);

            // 使用模拟成交计算的真实收益，否则回退到 spread
            double ret = 0.0;
            if (hasRealReturns) {
                ret = groupReturns[g];
            } else if (report.layeredReturnSpread.available && nGroups > 1) {
                double spreadPerGroup = report.layeredReturnSpread.value / (nGroups - 1);
                ret = g * spreadPerGroup;
            }

            grp["returnRate"] = ret;
            grp["annualizedReturn"] = ret * 252; // 日化→年化

            double minFV = 0.0, maxFV = 0.0;
            if (!sortedVals.empty()) {
                size_t n = sortedVals.size();
                size_t groupSize = n / nGroups;
                size_t startIdx = g * groupSize;
                size_t endIdx = (g + 1 == nGroups) ? n - 1 : startIdx + groupSize - 1;
                if (startIdx < n) {
                    minFV = sortedVals[startIdx];
                    maxFV = sortedVals[std::min(endIdx, n - 1)];
                }
            }
            grp["minFactorValue"] = minFV;
            grp["maxFactorValue"] = maxFV;
            groups.append(grp);
        }
    } else {
        for (int g = 0; g < nGroups; ++g) {
            QVariantMap grp;
            grp["groupIndex"] = g + 1;
            grp["stockCount"] = 0;
            grp["returnRate"] = 0.0;
            grp["annualizedReturn"] = 0.0;
            grp["minFactorValue"] = 0.0;
            grp["maxFactorValue"] = 0.0;
            groups.append(grp);
        }
    }

    metrics["groups"] = groups;
    result["metrics"] = metrics;
    return result;
}

} // anonymous namespace


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
    std::vector<QString> symsOrdered;  // 保证顺序一致性
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

    // ⚠️ 关键修复：对日期排序，避免序列回绕导致净值归零
    std::sort(datesVec.begin(), datesVec.end());
    // 重建日期索引
    dateToIdx.clear();
    for (int i = 0; i < (int)datesVec.size(); ++i) {
        dateToIdx[datesVec[i]] = i;
    }

    // 发现所有数值字段
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

    // 构建 HistoricalView 适配器
    m_historicalAdapter = std::make_shared<factor::bridge::CachedMarketDataViewHistoricalAdapter>(
        *m_marketDataView);

    // DB fallback 已预留接口（HistoricalAdapter::setDbFallback）
    // 待 DataService 引用传递后连接

    // AnalysisModule 仅用于最终指标计算
    m_analysisModule = std::make_unique<factor::compute::AnalysisModule>();

    // 设置数据库跨截面查询回调（标准业务流程）
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

    m_workerPool->post([this, capturedFactors, factorTypeInt]() {
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
                qDebug() << "[FactorBacktestBridge] 标的数=" << symbolsVec.size()
                         << " fields: pb_ratio=" << m_historicalAdapter->hasField("pb_ratio")
                         << " pe_ratio=" << m_historicalAdapter->hasField("pe_ratio")
                         << " market_cap=" << m_historicalAdapter->hasField("market_cap")
                         << " close=" << m_historicalAdapter->hasField("close")
                         << " industry_code=" << m_historicalAdapter->hasField("industry_code");

                // 检查 close 数据是否真实存在（非全 0）
                {
                    auto closeField = m_marketDataView->getField("close");
                    if (closeField.has_value()) {
                        const auto& cv = closeField.value();
                        double sum = 0.0; int nonZero = 0;
                        for (int i = 0; i < cv.rowCount && i < cv.columnCount && i < 200; ++i) {
                            double v = cv.data[i * cv.rowStride + i];
                            sum += v; if (v != 0.0) ++nonZero;
                        }
                        qDebug() << "[FactBridge] close数据 前200对角 sum=" << sum << " nonZero=" << nonZero
                                 << " rows=" << cv.rowCount << " cols=" << cv.columnCount;
                    }
                    auto pbField = m_marketDataView->getField("pb_ratio");
                    if (pbField.has_value()) {
                        const auto& pv = pbField.value();
                        double sum = 0.0; int nonZero = 0;
                        for (int i = 0; i < pv.rowCount && i < pv.columnCount && i < 200; ++i) {
                            double v = pv.data[i * pv.rowStride + i];
                            sum += v; if (v != 0.0) ++nonZero;
                        }
                        qDebug() << "[FactBridge] pb_ratio 前200对角 sum=" << sum << " nonZero=" << nonZero
                                 << " rows=" << pv.rowCount << " cols=" << pv.columnCount;
                    }
                }

                // 先做一次诊断：直接查询 HistoricalView
                {
                    auto test = m_historicalAdapter->getCrossSection(dateStrs[0], "pb_ratio", {symbolsVec[0]});
                    qDebug() << "[FactBridge] getCrossSection(" << QString::fromStdString(dateStrs[0])
                             << ", pb_ratio, " << QString::fromStdString(symbolsVec[0]) << ") size=" << test.size();
                    if (!test.empty()) {
                        qDebug() << "[FactBridge]   值=" << test.begin()->second;
                    }
                    test = m_historicalAdapter->getCrossSection(dateStrs[0], "pb_ratio", {});
                    qDebug() << "[FactBridge] getCrossSection(all) size=" << test.size();
                    if (!test.empty()) {
                        auto it = test.begin();
                        qDebug() << "[FactBridge]   首项: symbol=" << QString::fromStdString(it->first) << " val=" << it->second;
                    }
                    // 检查 pe_ratio 也有横截面数据
                    auto testPE = m_historicalAdapter->getCrossSection(dateStrs[0], "pe_ratio", {});
                    qDebug() << "[FactBridge] getCrossSection(all, pe_ratio) size=" << testPE.size();
                    if (!testPE.empty()) {
                        auto itPE = testPE.begin();
                        qDebug() << "[FactBridge]   pe首项: symbol=" << QString::fromStdString(itPE->first) << " val=" << itPE->second;
                    }
                    // 检查前一天/后一天是否有数据
                    qDebug() << "[FactBridge] dateStrs 前3个日期:" << QString::fromStdString(dateStrs[0])
                             << QString::fromStdString(dateStrs[1]) << QString::fromStdString(dateStrs[2]);
                }

                // 对每个日期调用 factor->calculate()
                int totalDates = static_cast<int>(dateStrs.size());
                std::unordered_map<std::string, std::unordered_map<std::string, double>> factorValuesByDate;

                int validDays = 0;
                for (int di = 0; di < totalDates; ++di) {
                    if (!m_isRunning.load()) break;
                    factor::CalculationContext ctx(dateStrs[di], symbolsVec, m_historicalAdapter);
                    auto calcResult = factor->calculate(ctx);
                    if (!calcResult.isEmpty()) {
                        factorValuesByDate[dateStrs[di]] = std::move(calcResult.values);
                        ++validDays;
                    }
                    if (di == 0) {
                        qDebug() << "[FactBridge] 首个日期(" << QString::fromStdString(dateStrs[di])
                                 << ")计算" << (calcResult.isEmpty() ? "失败(空)" : "成功")
                                 << ", values数=" << calcResult.values.size()
                                 << " availability=" << static_cast<int>(calcResult.dataStatus.availability);
                        if (!calcResult.dataStatus.message.empty()) {
                            qDebug() << "[FactBridge] 状态:" << QString::fromStdString(calcResult.dataStatus.message);
                        }
                        // 打印首个因子值用于诊断
                        if (!calcResult.values.empty()) {
                            auto firstEntry = calcResult.values.begin();
                            qDebug() << "[FactBridge]   首个因子值: symbol=" << QString::fromStdString(firstEntry->first)
                                     << " val=" << firstEntry->second;
                        }
                        // 检查因子getDataRequirements需要哪些字段
                        auto req = factor->getDataRequirements();
                        QStringList fields;
                        for (const auto& f : req.requiredFields) fields << QString::fromStdString(f);
                        qDebug() << "[FactBridge] 因子需求字段:" << fields;
                        for (const auto& f : req.requiredFields) {
                            qDebug() << "[FactBridge]   hasField(" << QString::fromStdString(f) << ")="
                                     << m_historicalAdapter->hasField(f);
                        }
                        // 检查有效日期数组
                        qDebug() << "[FactBridge] dateStrs前3:" << QString::fromStdString(dateStrs[0])
                                 << QString::fromStdString(dateStrs[1]) << QString::fromStdString(dateStrs[2]);
                        qDebug() << "[FactBridge] symbolsVec前3:" << QString::fromStdString(symbolsVec[0])
                                 << QString::fromStdString(symbolsVec[1]) << QString::fromStdString(symbolsVec[2]);
                        qDebug() << "[FactBridge] totalDates=" << totalDates << " validDays=" << validDays;
                    }
                    double subPct = pct + 55.0 * di / totalDates / nFactors;
                    m_progress = subPct;
                }

                // 构建 SignalSet 供 AnalysisModule 使用
                // SignalSet 需要紧凑日期（DateKey::value 为 YYYYMMDD）
                factor::compute::SignalSet signalSet;
                signalSet.dates.reserve(dates.size());
                for (const auto& d : dates) {
                    factor::compute::DateKey dk;
                    dk.value = d.value;  // int32_t YYYYMMDD 直接赋值
                    signalSet.dates.push_back(dk);
                }

                signalSet.instruments = m_marketDataView->instruments();
                signalSet.signalIds.push_back({1});

                int timeCount = static_cast<int>(dateStrs.size());
                int instCount = static_cast<int>(signalSet.instruments.size());
                int flatSize = timeCount * instCount;
                signalSet.values.assign(flatSize, 0.0);
                signalSet.mask.assign(flatSize, 1U);  // 1=缺失, 0=存在 (kPresentMaskValue=0U)
                signalSet.index = {instCount, 1, 1};
                signalSet.progress = {1, 1};
                signalSet.isPartial = false;

                int totalMasks = 0;
                for (int di = 0; di < timeCount; ++di) {
                    const auto& dateStr = dateStrs[di];
                    auto dateIt = factorValuesByDate.find(dateStr);
                    if (dateIt == factorValuesByDate.end()) continue;
                    for (int ii = 0; ii < instCount; ++ii) {
                        std::string symStr = std::to_string(signalSet.instruments[ii].value);
                        auto valIt = dateIt->second.find(symStr);
                        if (valIt != dateIt->second.end()) {
                            signalSet.values[di * instCount + ii] = valIt->second;
                            signalSet.mask[di * instCount + ii] = 0U;  // 0=存在
                            ++totalMasks;
                        }
                    }
                }
                qDebug() << "[FactBridge] SignalSet 构建完成, totalMasks(存在)=" << totalMasks
                         << " flatSize=" << flatSize
                         << " mask[0]=" << signalSet.mask[0]
                         << " values[0]=" << signalSet.values[0];

                m_progress = pct + 58.0 / nFactors;

                // 调用 AnalysisModule::analyze()
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

                // 收集最后一个日期所有标的的因子值，用于分组 min/max 计算
                std::vector<double> lastCrossSectionValues;
                if (validDays > 0 && !dateStrs.empty()) {
                    const auto& lastDate = dateStrs.back();
                    auto itLast = factorValuesByDate.find(lastDate);
                    if (itLast != factorValuesByDate.end()) {
                        lastCrossSectionValues.reserve(itLast->second.size());
                        for (const auto& [sym, val] : itLast->second) {
                            lastCrossSectionValues.push_back(val);
                        }
                    }
                }

                // 模拟成交计算分组收益和执行指标
                int nGroups = m_numGroups > 0 ? m_numGroups : 5;
                std::vector<double> groupReturns(nGroups, 0.0);
                std::vector<int> groupCounts(nGroups, 0);
                std::vector<double> strategyDailyReturns;  // 策略日收益=各组收益均值（已过滤极端值）
                double strategyEquity = 1.0;               // 净值
                double maxEquity = 1.0;
                double maxDrawdown = 0.0;
                int sampleCount = 0;
                // 注意：以下变量在外层声明，不要在 if 块内重复声明（避免影子变量遮蔽）
                double annualizedReturn = 0.0, annualStd = 0.0, sharpe = 0.0;

                if (validDays > 1 && !dateStrs.empty()) {
                    qDebug() << "[FactBridge] 开始模拟成交, validDays=" << validDays
                             << " dateStrs.size=" << dateStrs.size()
                             << " dateStrs[0]=" << QString::fromStdString(dateStrs[0])
                             << " dateStrs[last]=" << QString::fromStdString(dateStrs.back());
                    for (size_t di = 0; di + 1 < dateStrs.size(); ++di) {
                        const auto& curDate = dateStrs[di];
                        const auto& nxtDate = dateStrs[di + 1];
                        auto fvIt = factorValuesByDate.find(curDate);
                        if (fvIt == factorValuesByDate.end()) continue;
                        auto curClose = m_historicalAdapter->getCrossSection(curDate, "close", {});
                        auto nxtClose = m_historicalAdapter->getCrossSection(nxtDate, "close", {});
                        if (curClose.empty() || nxtClose.empty()) continue;

                        // 按因子值排序标的
                        std::vector<std::pair<std::string, double>> ranked;
                        for (const auto& [sym, fv] : fvIt->second) {
                            ranked.emplace_back(sym, fv);
                        }
                        std::sort(ranked.begin(), ranked.end(),
                            [](const auto& a, const auto& b) { return a.second < b.second; });

                        size_t N = ranked.size();
                        if (N < static_cast<size_t>(nGroups)) continue;
                        size_t groupSize = N / nGroups;

                        // 每日各组收益
                        std::vector<double> dayGroupReturns(nGroups, 0.0);
                        int validGroups = 0;

                        for (int g = 0; g < nGroups; ++g) {
                            size_t start = g * groupSize;
                            size_t end = (g + 1 == nGroups) ? N : start + groupSize;
                            double sumRet = 0.0;
                            int cnt = 0;
                            for (size_t i = start; i < end; ++i) {
                                const auto& sym = ranked[i].first;
                                auto curIt = curClose.find(sym);
                                auto nxtIt = nxtClose.find(sym);
                                if (curIt != curClose.end() && nxtIt != nxtClose.end()
                                    && std::abs(curIt->second) > 1e-9
                                    && std::isfinite(nxtIt->second) && nxtIt->second > 1e-9) {
                                    double fwdRet = (nxtIt->second / curIt->second) - 1.0;
                                    if (std::isfinite(fwdRet) && std::abs(fwdRet) < 0.5) {
                                        sumRet += fwdRet;
                                        ++cnt;
                                    }
                                }
                            }
                            if (cnt > 0) {
                                double avgRet = sumRet / cnt;
                                groupReturns[g] += avgRet;
                                groupCounts[g]++;
                                dayGroupReturns[g] = avgRet;
                                ++validGroups;
                            }
                        }

                        // 策略日收益 = 各组收益均值（天然过滤极端值，且与分组结果一致）
                        if (validGroups > 0) {
                            double dailyStrategyRet = 0.0;
                            for (int g = 0; g < nGroups; ++g) {
                                dailyStrategyRet += dayGroupReturns[g];
                            }
                            dailyStrategyRet /= validGroups;

                            strategyDailyReturns.push_back(dailyStrategyRet);
                            strategyEquity *= (1.0 + dailyStrategyRet);
                            if (strategyEquity > maxEquity) maxEquity = strategyEquity;
                            double dd = (maxEquity > 1e-9) ? (maxEquity - strategyEquity) / maxEquity : 0.0;
                            if (dd > maxDrawdown) maxDrawdown = dd;
                            if (sampleCount < 5 || sampleCount % 200 == 0) {
                                qDebug() << "[FactBridge] dailyRet[" << sampleCount << "] =" << dailyStrategyRet
                                         << "  equity=" << strategyEquity
                                         << "  maxDD=" << maxDrawdown
                                         << " validGroups=" << validGroups;
                            }
                            ++sampleCount;
                        }
                    }

                    // 平均每日每组收益
                    for (int g = 0; g < nGroups; ++g) {
                        if (groupCounts[g] > 0) {
                            groupReturns[g] /= groupCounts[g];
                        }
                    }
                    qDebug() << "[FactBridge] 模拟分组收益(日化):" << groupReturns;

                    // 计算执行指标（注意：不要声明新变量，直接用外层变量赋值，避免影子遮蔽）
                    {
                        double totalReturn = strategyEquity - 1.0;
                        int nDays = (int)strategyDailyReturns.size();
                        annualizedReturn = (nDays > 0 && nDays < 10000)
                            ? std::pow(1.0 + totalReturn, 252.0 / nDays) - 1.0 : totalReturn;

                        double avgDaily = 0.0, variance = 0.0;
                        for (auto r : strategyDailyReturns) avgDaily += r;
                        avgDaily /= nDays;
                        for (auto r : strategyDailyReturns) {
                            double d = r - avgDaily;
                            variance += d * d;
                        }
                        variance /= nDays;
                        double dailyStd = std::sqrt(std::max(0.0, variance));
                        annualStd = dailyStd * std::sqrt(252.0);
                        sharpe = (annualStd > 1e-12) ? (annualizedReturn / annualStd) : 0.0;

                        qDebug() << "[FactBridge] 执行指标: annRet=" << annualizedReturn
                                 << " annVol=" << annualStd
                                 << " maxDD=" << maxDrawdown
                                 << " sharpe=" << sharpe
                                 << " totalReturn=" << totalReturn
                                 << " dailyReturns count=" << nDays;
                    }
                }
                #define SET_IF_FINITE(Dst, Src) if (std::isfinite(Src)) Dst = Src;
                double computedAnnRet = annualizedReturn;
                double computedMaxDD = maxDrawdown;
                double computedSharpe = sharpe;

                QVariantMap fr = convertAnalysisReport(
                    analysisResult.value(), fId, instCount, lastCrossSectionValues,
                    groupReturns, groupCounts, nGroups,
                    annualizedReturn, maxDrawdown, annualStd, sharpe,
                    strategyEquity);
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