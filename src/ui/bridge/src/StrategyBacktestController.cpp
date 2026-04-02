#include "StrategyBacktestController.h"
#include "../../domain/backtest/include/StrategyBacktestService.h"
#include "../../domain/backtest/include/DatabaseStockDataProvider.h"
#include "../../domain/backtest/include/DatabaseFactorDataProvider.h"
#include "../include/FactorService.h"
#include "RiskConfigService.h"

#include <QDebug>
#include <QDate>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QFile>
#include <QMetaObject>
#include <QPointer>
#include "foundation.h"
#include <algorithm>
#include <chrono>
#include <string>
#include <memory>
#include <stdexcept>

namespace {

QString normalizeDataSourceMode(const QString& rawMode) {
    const QString mode = rawMode.trimmed().toLower();
    if (mode == "cleaned" || mode == "cleaned_table" || mode == "cleaned_daily_bar") {
        return "cleaned";
    }
    if (mode == "cache" || mode == "dataset") {
        return "cache";
    }
    return "raw";
}

bool isPortfolioStrategyContext(const QVariantMap& strategyParams)
{
    const QString strategyType = strategyParams.value("selectedStrategyType").toString().trimmed().toUpper();
    const QString strategySubtype = strategyParams.value("selectedStrategySubtype").toString().trimmed().toLower();
    const QString portfolioSource = strategyParams.value("portfolio_source").toString().trimmed().toLower();
    return strategyType == "PORTFOLIO"
        || strategySubtype == "portfolio_builder"
        || portfolioSource == "portfolio_builder";
}

QVariantList parsePortfolioAllocations(const QVariant& rawAllocations)
{
    if (rawAllocations.typeId() == QMetaType::QVariantList) {
        return rawAllocations.toList();
    }

    const QString jsonText = rawAllocations.toString().trimmed();
    if (jsonText.isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        qWarning() << "StrategyBacktestController: failed to parse portfolio allocations json:" << parseError.errorString();
        return {};
    }

    return document.array().toVariantList();
}

double normalizedPercentRate(const QVariant& value, double fallback = 0.0) {
    bool ok = false;
    double numeric = value.toDouble(&ok);
    if (!ok) {
        return fallback;
    }
    return numeric > 1.0 ? numeric / 100.0 : numeric;
}

QVariantMap loadAppliedRiskConfiguration()
{
    if (auto* service = RiskConfigService::instance()) {
        return service->loadAppliedConfiguration();
    }
    return {};
}

template <typename Func>
bool submitToFoundationThreadPool(StrategyBacktestController* controller, Func&& func, QString* errorMessage = nullptr)
{
    QPointer<StrategyBacktestController> safeController(controller);
    try {
        foundation::Foundation::instance().thread_pool().post(
            [safeController, fn = std::forward<Func>(func)]() mutable {
                if (safeController) {
                    fn(safeController.data());
                }
            }
        );
        return true;
    } catch (const std::exception& e) {
        if (errorMessage) {
            *errorMessage = QString::fromUtf8(e.what());
        }
    } catch (...) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未知线程池错误");
        }
    }
    return false;
}

} // namespace

StrategyBacktestController::StrategyBacktestController(QObject *parent)
    : QObject(parent)
    , m_isRunning(false)
    , m_progress(0)
    , m_initialCapital(1000000.0)
    , m_startDate(QDate::currentDate().addYears(-1).toString("yyyy-MM-dd"))
    , m_endDate(QDate::currentDate().toString("yyyy-MM-dd"))
{
    qDebug() << "StrategyBacktestController constructed";

    try {
        m_service = std::make_unique<domain::backtest::StrategyBacktestService>();
        m_stockDataProvider = std::make_shared<domain::backtest::DatabaseStockDataProvider>(nullptr);
        m_stockDataProvider->setDataSourceContext(m_dataSourceMode.toStdString(), m_selectedDatasetId);
        m_service->setDataProvider(m_stockDataProvider);

        if (FactorService* factorService = FactorService::instance()) {
            auto sharedFactorService = std::shared_ptr<FactorService>(factorService, [](FactorService*) {});
            m_factorDataProvider = std::make_shared<domain::backtest::DatabaseFactorDataProvider>(sharedFactorService);
            m_service->setFactorProvider(m_factorDataProvider);
        }
        qDebug() << "StrategyBacktestService initialized successfully";
    } catch (const std::exception& e) {
        qWarning() << "Failed to initialize StrategyBacktestService:" << e.what();
    }
}

StrategyBacktestController::~StrategyBacktestController()
{
    qDebug() << "StrategyBacktestController destroyed";
}

domain::backtest::StrategyBacktestConfig StrategyBacktestController::resolveConfigForTesting(
    const QString& strategyId,
    const QVariantMap& strategyParams,
    const QVariantList& symbols,
    const QString& startDate,
    const QString& endDate)
{
    StrategyBacktestController controller;
    return controller.createConfig(strategyId, strategyParams, symbols, startDate, endDate);
}

double StrategyBacktestController::normalizeInitialCapitalValue(const QVariant& value, double fallback)
{
    bool ok = false;
    const double numeric = value.toDouble(&ok);
    if (!ok) {
        return fallback;
    }

    // 兼容旧版 QML 将初始资金按“万元”保存的历史数据。
    if (numeric > 0.0 && numeric < 10000.0) {
        return numeric * 10000.0;
    }

    return numeric;
}

void StrategyBacktestController::setSelectedStrategyId(const QString& strategyId)
{
    if (m_selectedStrategyId == strategyId)
        return;
    
    m_selectedStrategyId = strategyId;
    emit selectedStrategyIdChanged(strategyId);
}

void StrategyBacktestController::setStrategyParams(const QVariantMap& params)
{
    if (m_strategyParams == params)
        return;
    
    m_strategyParams = params;
    emit strategyParamsChanged(params);
}

void StrategyBacktestController::setInitialCapital(double capital)
{
    if (qFuzzyCompare(m_initialCapital, capital))
        return;
    
    m_initialCapital = capital;
    emit initialCapitalChanged(capital);
}

void StrategyBacktestController::setStartDate(const QString& date)
{
    if (m_startDate == date)
        return;
    
    m_startDate = date;
    emit startDateChanged(date);
}

void StrategyBacktestController::setEndDate(const QString& date)
{
    if (m_endDate == date)
        return;
    
    m_endDate = date;
    emit endDateChanged(date);
}

void StrategyBacktestController::setSelectedSymbols(const QVariantList& symbols)
{
    if (m_selectedSymbols == symbols)
        return;
    
    m_selectedSymbols = symbols;
    emit selectedSymbolsChanged(symbols);
}

void StrategyBacktestController::setDataSourceMode(const QString& dataSourceMode)
{
    const QString normalizedMode = normalizeDataSourceMode(dataSourceMode);
    if (m_dataSourceMode == normalizedMode)
        return;

    m_dataSourceMode = normalizedMode;
    if (m_stockDataProvider) {
        m_stockDataProvider->setDataSourceContext(m_dataSourceMode.toStdString(), m_selectedDatasetId);
    }
    emit dataSourceModeChanged(m_dataSourceMode);
}

void StrategyBacktestController::setSelectedDatasetId(int datasetId)
{
    if (m_selectedDatasetId == datasetId)
        return;

    m_selectedDatasetId = datasetId;
    if (m_stockDataProvider) {
        m_stockDataProvider->setDataSourceContext(m_dataSourceMode.toStdString(), m_selectedDatasetId);
    }
    emit selectedDatasetIdChanged(datasetId);
}

void StrategyBacktestController::startStrategyBacktest(
    const QString& strategyId,
    const QVariantMap& strategyParams,
    const QVariantList& symbols,
    const QString& startDate,
    const QString& endDate)
{
    if (m_isRunning) {
        qWarning() << "Strategy backtest already running";
        return;
    }

    if (!m_service || !m_stockDataProvider) {
        emit backtestFailed("策略回测服务未初始化");
        return;
    }
    
    qDebug() << "Starting strategy backtest for strategy:" << strategyId;
    qDebug() << "Params:" << strategyParams;
    qDebug() << "Symbols:" << symbols;
    qDebug() << "Date range:" << startDate << "to" << endDate;
    qDebug() << "Data source mode:" << m_dataSourceMode << "datasetId:" << m_selectedDatasetId;
    
    m_isRunning = true;
    m_progress = 0;
    m_status = "准备中...";
    emit isRunningChanged(true);
    emit progressChanged(0);
    emit statusChanged(m_status);
    emit backtestStarted(strategyId);
    
    // 生成任务ID
    m_currentTaskId = QString("strategy_%1_%2").arg(strategyId).arg(QDateTime::currentMSecsSinceEpoch());

    const auto config = createConfig(strategyId, strategyParams, symbols, startDate, endDate);
    QString submitError;
    if (!submitToFoundationThreadPool(this, [config](StrategyBacktestController* controller) {
        try {
            qDebug() << "StrategyBacktestController: background task started"
                     << "strategyId=" << QString::fromStdString(config.strategyId)
                     << "universeId=" << QString::fromStdString(config.universeId)
                     << "dataSourceMode=" << QString::fromStdString(config.dataSourceMode)
                     << "datasetId=" << config.datasetId;

            QMetaObject::invokeMethod(controller, [controller]() {
                controller->m_progress = 25;
                controller->m_status = "加载回测数据源...";
                emit controller->progressChanged(controller->m_progress);
                emit controller->statusChanged(controller->m_status);
                emit controller->backtestProgress(controller->m_progress, controller->m_status);
            }, Qt::QueuedConnection);

            qDebug() << "StrategyBacktestController: invoking StrategyBacktestService::runStrategyBacktestSync";
            auto result = controller->m_service->runStrategyBacktestSync(config);
            QVariantMap qmlResult = controller->convertResultToQml(result);

            QMetaObject::invokeMethod(controller, [controller, qmlResult]() {
                controller->m_backtestResult = qmlResult;
                controller->m_progress = 100;
                controller->m_status = "回测完成";
                controller->m_isRunning = false;

                emit controller->backtestResultChanged(qmlResult);
                emit controller->progressChanged(controller->m_progress);
                emit controller->statusChanged(controller->m_status);
                emit controller->isRunningChanged(false);
                emit controller->backtestCompleted(qmlResult);
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            const QString error = QString::fromUtf8(e.what());
            QMetaObject::invokeMethod(controller, [controller, error]() {
                controller->m_progress = 0;
                controller->m_status = error;
                controller->m_isRunning = false;
                emit controller->progressChanged(controller->m_progress);
                emit controller->statusChanged(controller->m_status);
                emit controller->isRunningChanged(false);
                emit controller->backtestFailed(error);
            }, Qt::QueuedConnection);
        }
    }, &submitError)) {
        m_isRunning = false;
        m_progress = 0;
        m_status = QString("线程池不可用，无法启动回测: %1").arg(submitError);
        emit progressChanged(m_progress);
        emit statusChanged(m_status);
        emit isRunningChanged(false);
        emit backtestFailed(m_status);
    }
}

void StrategyBacktestController::startBatchStrategyBacktest(const QVariantList& strategies)
{
    if (m_isRunning) {
        qWarning() << "Strategy backtest already running";
        return;
    }
    
    qDebug() << "Starting batch strategy backtest for" << strategies.size() << "strategies";
    
    m_isRunning = true;
    m_progress = 0;
    m_status = "准备批量回测...";
    emit isRunningChanged(true);
    emit progressChanged(0);
    emit statusChanged(m_status);

    Q_UNUSED(strategies)
    m_isRunning = false;
    m_progress = 0;
    m_status = "批量回测尚未接入真实实现";

    emit isRunningChanged(false);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    emit backtestFailed(m_status);
}

void StrategyBacktestController::optimizeStrategyParameters(
    const QString& baseStrategyId,
    const QVariantMap& paramRanges,
    const QString& objectiveFunction,
    int maxIterations)
{
    if (m_isRunning) {
        qWarning() << "Strategy optimization already running";
        return;
    }
    
    qDebug() << "Starting strategy parameter optimization for:" << baseStrategyId;
    qDebug() << "Parameter ranges:" << paramRanges;
    
    m_isRunning = true;
    m_progress = 0;
    m_status = "准备参数优化...";
    emit isRunningChanged(true);
    emit progressChanged(0);
    emit statusChanged(m_status);
    emit optimizationStarted();
    
    Q_UNUSED(baseStrategyId)
    Q_UNUSED(paramRanges)
    Q_UNUSED(objectiveFunction)
    Q_UNUSED(maxIterations)

    m_isRunning = false;
    m_progress = 0;
    m_status = "参数优化尚未接入真实实现";

    emit isRunningChanged(false);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    emit optimizationFailed(m_status);
}

void StrategyBacktestController::compareStrategies(const QVariantList& strategies)
{
    if (m_isRunning) {
        qWarning() << "Strategy comparison already running";
        return;
    }
    
    qDebug() << "Starting strategy comparison for" << strategies.size() << "strategies";
    
    m_isRunning = true;
    m_progress = 0;
    m_status = "准备策略对比...";
    emit isRunningChanged(true);
    emit progressChanged(0);
    emit statusChanged(m_status);
    emit comparisonStarted();
    
    Q_UNUSED(strategies)
    m_isRunning = false;
    m_progress = 0;
    m_status = "策略对比尚未接入真实实现";

    emit isRunningChanged(false);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    emit comparisonFailed(m_status);
}

void StrategyBacktestController::cancelBacktest()
{
    if (!m_isRunning) {
        qWarning() << "No backtest running to cancel";
        return;
    }
    
    qDebug() << "Cancelling current backtest";
    
    // 由于编译依赖问题，暂时简化实现
    // 实际集成时需使用m_service->cancelBacktest(m_currentTaskId.toStdString());
    
    m_isRunning = false;
    m_progress = 0;
    m_status = "已取消";
    
    emit isRunningChanged(false);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    emit backtestCancelled();
}

QVariantList StrategyBacktestController::getAvailableStrategies() const
{
    return QVariantList();
}

QVariantMap StrategyBacktestController::getDefaultStrategyParams(const QString& strategyId) const
{
    Q_UNUSED(strategyId)
    return QVariantMap();
}

QVariantMap StrategyBacktestController::getDefaultDateRange() const
{
    QVariantMap range;
    range["startDate"] = QDate::currentDate().addYears(-1).toString("yyyy-MM-dd");
    range["endDate"] = QDate::currentDate().toString("yyyy-MM-dd");
    return range;
}

QVariantList StrategyBacktestController::getAvailableSymbols(const QString& universeId) const
{
    Q_UNUSED(universeId)
    QVariantList symbols;
    if (!m_stockDataProvider) {
        return symbols;
    }

    const_cast<StrategyBacktestController*>(this)->m_stockDataProvider->setDataSourceContext(
        m_dataSourceMode.toStdString(), m_selectedDatasetId);
    for (const auto& symbol : m_stockDataProvider->getAvailableSymbols()) {
        symbols.append(QString::fromStdString(symbol));
    }

    return symbols;
}

QVariantList StrategyBacktestController::getIndexConstituentSymbols(const QString& indexSymbol,
                                                                    const QString& snapshotDate) const
{
    QVariantList symbols;
    if (!m_stockDataProvider) {
        return symbols;
    }

    const auto indexSymbols = m_stockDataProvider->getIndexConstituentSymbols(indexSymbol, snapshotDate);
    for (const auto& symbol : indexSymbols) {
        symbols.append(QString::fromStdString(symbol));
    }
    return symbols;
}

bool StrategyBacktestController::saveResultToFile(const QString& filePath) const
{
    if (m_backtestResult.isEmpty()) {
        qWarning() << "No backtest result to save";
        return false;
    }
    
    QJsonDocument doc(QJsonObject::fromVariantMap(m_backtestResult));
    QFile file(filePath);
    
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open file for writing:" << filePath;
        return false;
    }
    
    file.write(doc.toJson());
    file.close();
    
    qDebug() << "Backtest result saved to:" << filePath;
    return true;
}

bool StrategyBacktestController::loadResultFromFile(const QString& filePath)
{
    QFile file(filePath);
    
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open file for reading:" << filePath;
        return false;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        qWarning() << "Invalid JSON data in file:" << filePath;
        return false;
    }
    
    m_backtestResult = doc.object().toVariantMap();
    emit backtestResultChanged(m_backtestResult);
    
    qDebug() << "Backtest result loaded from:" << filePath;
    return true;
}

std::map<std::string, std::pair<double, double>> StrategyBacktestController::parseParamRanges(const QVariantMap& qmlRanges) const
{
    std::map<std::string, std::pair<double, double>> ranges;
    
    for (auto it = qmlRanges.begin(); it != qmlRanges.end(); ++it) {
        QString paramName = it.key();
        QVariantList rangeList = it.value().toList();
        
        if (rangeList.size() >= 2) {
            double minVal = rangeList[0].toDouble();
            double maxVal = rangeList[1].toDouble();
            ranges[paramName.toStdString()] = {minVal, maxVal};
        }
    }
    
    return ranges;
}

domain::backtest::StrategyBacktestConfig StrategyBacktestController::createConfig(
    const QString& strategyId,
    const QVariantMap& strategyParams,
    const QVariantList& symbols,
    const QString& startDate,
    const QString& endDate) const
{
    domain::backtest::StrategyBacktestConfig config;
    config.strategyId = strategyId.toStdString();
    config.strategyName = strategyParams.value("selectedStrategyName", strategyId).toString().toStdString();
    config.startDate = startDate.toStdString();
    config.endDate = endDate.toStdString();
    config.initialCapital = m_initialCapital;
    config.dataSourceMode = m_dataSourceMode.toStdString();
    config.datasetId = m_selectedDatasetId;

    const QVariantMap runtimeParams = strategyParams.value("backtest_runtime").toMap();
    const QVariantMap appliedRiskConfig = loadAppliedRiskConfiguration();
    const bool portfolioContext = isPortfolioStrategyContext(strategyParams);
    const QVariantList portfolioAllocations = parsePortfolioAllocations(
        strategyParams.value("portfolio_allocations_json", strategyParams.value("factor_allocations"))
    );
    const QString universeType = runtimeParams.value("universeType").toString().trimmed().toLower();
    auto resolveParamValue = [&runtimeParams, &strategyParams, &appliedRiskConfig](const QStringList& keys,
                                                                                    const QVariant& defaultValue) -> QVariant {
        for (const QString& key : keys) {
            if (runtimeParams.contains(key)) {
                return runtimeParams.value(key);
            }
        }
        for (const QString& key : keys) {
            if (strategyParams.contains(key)) {
                return strategyParams.value(key);
            }
        }
        for (const QString& key : keys) {
            if (appliedRiskConfig.contains(key)) {
                return appliedRiskConfig.value(key);
            }
        }
        return defaultValue;
    };

    for (auto it = strategyParams.begin(); it != strategyParams.end(); ++it) {
        if (it.key() == "backtest_runtime" || it.key() == "selectedStrategyId" || it.key() == "selectedStrategyName") {
            continue;
        }
        if (it.value().canConvert<double>()) {
            config.strategyParams[it.key().toStdString()] = it.value().toDouble();
            continue;
        }
        if (it.value().typeId() == QMetaType::Bool) {
            config.strategyOptions[it.key().toStdString()] = it.value().toBool() ? "true" : "false";
            continue;
        }
        config.strategyOptions[it.key().toStdString()] = it.value().toString().toStdString();
    }

    const QString selectedStrategySubtype = strategyParams.value("selectedStrategySubtype").toString().trimmed();
    if (!selectedStrategySubtype.isEmpty()) {
        config.strategyOptions["strategy_subtype"] = selectedStrategySubtype.toStdString();
        config.strategyOptions["sub_type"] = selectedStrategySubtype.toStdString();
    }

    const QString selectedStrategyType = strategyParams.value("selectedStrategyType").toString().trimmed();
    if (!selectedStrategyType.isEmpty()) {
        config.strategyOptions["strategy_type"] = selectedStrategyType.toStdString();
    }

    if (portfolioContext) {
        config.strategyOptions["portfolio_source"] = strategyParams.value("portfolio_source", QStringLiteral("portfolio_builder")).toString().toStdString();
        config.strategyOptions["portfolio_name"] = strategyParams.value("portfolio_name").toString().toStdString();
        config.strategyOptions["portfolio_strategy_subtype"] = config.strategyOptions["strategy_subtype"];

        const double allocationCount = static_cast<double>(portfolioAllocations.size());
        if (allocationCount > 0.0) {
            config.strategyParams["portfolio_factor_count"] = allocationCount;
            config.strategyParams["top_n"] = allocationCount;
        }

        double maxWeight = 0.0;
        double totalWeight = 0.0;
        QStringList factorIds;
        for (const QVariant& allocationVariant : portfolioAllocations) {
            const QVariantMap allocation = allocationVariant.toMap();
            const double weightPercent = allocation.value("weight").toDouble();
            const double weightRatio = weightPercent > 1.0 ? weightPercent / 100.0 : weightPercent;
            maxWeight = (std::max)(maxWeight, weightRatio);
            totalWeight += weightRatio;

            const QString factorId = allocation.value("factor_id", allocation.value("factorId")).toString().trimmed();
            if (!factorId.isEmpty()) {
                factorIds.push_back(factorId);
            }
        }

        if (maxWeight > 0.0) {
            config.strategyParams["position_size"] = maxWeight;
        }
        if (totalWeight > 0.0) {
            config.strategyParams["portfolio_total_weight"] = totalWeight;
        }
        if (!factorIds.isEmpty()) {
            config.strategyOptions["portfolio_factor_ids"] = factorIds.join(',').toStdString();
        }
    }

    for (const QVariant& symbol : symbols) {
        const QString symbolText = symbol.toString().trimmed();
        if (!symbolText.isEmpty()) {
            config.symbols.push_back(symbolText.toStdString());
        }
    }

    if (universeType == "index") {
        const QString universeId = runtimeParams.value("universeId",
                                 runtimeParams.value("indexSymbol")).toString().trimmed();
        config.universeId = universeId.toStdString();
        config.strategyOptions["universeType"] = "index";
    } else if (!universeType.isEmpty()) {
        config.strategyOptions["universeType"] = universeType.toStdString();
    }

    const QVariant defaultTotalExposure = appliedRiskConfig.value("maxTotalExposure", 100);
    const QVariant defaultSinglePosition = appliedRiskConfig.value("maxPositionPercent", defaultTotalExposure);

    config.commissionRate = normalizedPercentRate(resolveParamValue({"commissionRate", "commission", "transactionCost"}, 0.0003), 0.0003);
    config.slippageRate = normalizedPercentRate(resolveParamValue({"slippageRate", "slippage", "slippageCost", "slippageLimit"}, 0.0002), 0.0002);
    config.maxPositionRatio = normalizedPercentRate(
        resolveParamValue({"maxTotalExposure", "maxPositionRatio", "maxPositionPercent", "positionPercent", "position_size", "positionSize"}, defaultTotalExposure),
        1.0
    );
    config.maxSinglePositionRatio = normalizedPercentRate(
        resolveParamValue({"maxPositionPercent", "maxSinglePositionRatio", "positionPercent", "position_size", "positionSize"}, defaultSinglePosition),
        config.maxPositionRatio
    );
    config.maxDrawdownLimit = normalizedPercentRate(resolveParamValue({"maxDrawdownLimit"}, 20), 0.2);
    const bool autoStopEnabled = resolveParamValue({"autoStopEnabled"}, true).toBool();
    config.stopLossRate = autoStopEnabled
        ? normalizedPercentRate(resolveParamValue({"stopLossPercent", "stop_loss", "stopLoss"}, 5), 0.05)
        : 0.0;
    const double resolvedTakeProfitRate = normalizedPercentRate(
        resolveParamValue({"takeProfitPercent", "take_profit", "takeProfit"}, 15),
        0.15
    );
    config.strategyParams["stop_loss"] = config.stopLossRate;
    config.strategyParams["stopLoss"] = config.stopLossRate;
    config.strategyParams["stopLossPercent"] = config.stopLossRate;
    config.strategyOptions["autoStopEnabled"] = autoStopEnabled ? "true" : "false";
    config.strategyParams["take_profit"] = resolvedTakeProfitRate;
    config.strategyParams["takeProfit"] = resolvedTakeProfitRate;
    config.strategyParams["takeProfitPercent"] = resolvedTakeProfitRate;
    config.strategyParams["maxTotalExposure"] = config.maxPositionRatio;
    config.strategyParams["maxPositionRatio"] = config.maxPositionRatio;
    config.strategyParams["maxPositionPercent"] = config.maxSinglePositionRatio;
    config.strategyParams["maxSinglePositionRatio"] = config.maxSinglePositionRatio;
    config.strategyParams["maxDrawdownLimit"] = config.maxDrawdownLimit;
    const QVariant varWarningPercent = resolveParamValue({"varWarningPercent"}, QVariant());
    if (varWarningPercent.isValid()) {
        config.strategyParams["varWarningPercent"] = varWarningPercent.toDouble();
    }
    const QVariant orderSizeLimit = resolveParamValue({"orderSizeLimit"}, QVariant());
    if (orderSizeLimit.isValid()) {
        config.strategyParams["orderSizeLimit"] = orderSizeLimit.toDouble();
    }
    const QVariant turnoverLimit = resolveParamValue({"turnoverLimit"}, QVariant());
    if (turnoverLimit.isValid()) {
        config.strategyParams["turnoverLimit"] = turnoverLimit.toDouble();
    }
    const QVariant slippageLimit = resolveParamValue({"slippageLimit"}, QVariant());
    if (slippageLimit.isValid()) {
        config.strategyParams["slippageLimit"] = slippageLimit.toDouble();
    }
    const QVariant level1Breaker = resolveParamValue({"level1Breaker"}, QVariant());
    if (level1Breaker.isValid()) {
        config.strategyParams["level1Breaker"] = level1Breaker.toDouble();
    }
    const QVariant level2Breaker = resolveParamValue({"level2Breaker"}, QVariant());
    if (level2Breaker.isValid()) {
        config.strategyParams["level2Breaker"] = level2Breaker.toDouble();
    }
    const QVariant level3Breaker = resolveParamValue({"level3Breaker"}, QVariant());
    if (level3Breaker.isValid()) {
        config.strategyParams["level3Breaker"] = level3Breaker.toDouble();
    }
    config.strategyParams["autoStopEnabled"] = autoStopEnabled ? 1.0 : 0.0;
    if (runtimeParams.contains("initialCapital")) {
        config.initialCapital = normalizeInitialCapitalValue(runtimeParams.value("initialCapital"), config.initialCapital);
    }
    if (runtimeParams.contains("dataSourceMode")) {
        config.dataSourceMode = runtimeParams.value("dataSourceMode").toString().toStdString();
    }
    if (resolveParamValue({"rebalanceDays", "rebalancingPeriod", "rebalance_days"}, QVariant()).isValid()) {
        config.rebalanceFrequency = (std::max)(
            1,
            resolveParamValue({"rebalanceDays", "rebalancingPeriod", "rebalance_days"}, 5).toInt()
        );
    }
    config.strategyParams["rebalanceDays"] = static_cast<double>(config.rebalanceFrequency);
    config.strategyParams["rebalance_days"] = static_cast<double>(config.rebalanceFrequency);
    config.strategyParams["rebalancingPeriod"] = static_cast<double>(config.rebalanceFrequency);

    qDebug() << "StrategyBacktestController: resolved config"
             << "strategyId=" << strategyId
             << "strategyName=" << QString::fromStdString(config.strategyName)
             << "portfolioContext=" << portfolioContext
             << "portfolioAllocations=" << portfolioAllocations.size()
             << "initialCapital=" << config.initialCapital
             << "commissionRate=" << config.commissionRate
             << "slippageRate=" << config.slippageRate
             << "maxPositionRatio=" << config.maxPositionRatio
             << "dataSourceMode=" << QString::fromStdString(config.dataSourceMode)
             << "datasetId=" << config.datasetId;

    return config;
}

QVariantMap StrategyBacktestController::convertResultToQml(const domain::backtest::StrategyBacktestResult& result) const
{
    const auto jsonText = QString::fromStdString(result.toJson());
    const auto document = QJsonDocument::fromJson(jsonText.toUtf8());
    if (!document.isObject()) {
        return QVariantMap();
    }
    QVariantMap resultMap = document.object().toVariantMap();
    QVariantMap configMap = resultMap.value("config").toMap();
    if (configMap.isEmpty()) {
        const QString configJsonText = resultMap.value("config").toString().trimmed();
        if (!configJsonText.isEmpty()) {
            QJsonParseError configParseError;
            const QJsonDocument configDocument = QJsonDocument::fromJson(configJsonText.toUtf8(), &configParseError);
            if (configParseError.error == QJsonParseError::NoError && configDocument.isObject()) {
                configMap = configDocument.object().toVariantMap();
            } else {
                qWarning() << "StrategyBacktestController: failed to parse result config json:" << configParseError.errorString();
            }
        }
    }
    QVariantMap strategyParamsMap = configMap.value("strategyParams").toMap();
    strategyParamsMap.insert("maxTotalExposure", configMap.value("maxPositionRatio"));
    strategyParamsMap.insert("maxPositionRatio", configMap.value("maxPositionRatio"));
    strategyParamsMap.insert("maxPositionPercent", configMap.value("maxSinglePositionRatio"));
    strategyParamsMap.insert("maxSinglePositionRatio", configMap.value("maxSinglePositionRatio"));
    strategyParamsMap.insert("maxDrawdownLimit", configMap.value("maxDrawdownLimit"));
    if (strategyParamsMap.contains("varWarningPercent")) {
        configMap.insert("varWarningPercent", strategyParamsMap.value("varWarningPercent"));
    }
    if (strategyParamsMap.contains("orderSizeLimit")) {
        configMap.insert("orderSizeLimit", strategyParamsMap.value("orderSizeLimit"));
    }
    if (strategyParamsMap.contains("turnoverLimit")) {
        configMap.insert("turnoverLimit", strategyParamsMap.value("turnoverLimit"));
    }
    if (strategyParamsMap.contains("slippageLimit")) {
        configMap.insert("slippageLimit", strategyParamsMap.value("slippageLimit"));
    }
    if (strategyParamsMap.contains("level1Breaker")) {
        configMap.insert("level1Breaker", strategyParamsMap.value("level1Breaker"));
    }
    if (strategyParamsMap.contains("level2Breaker")) {
        configMap.insert("level2Breaker", strategyParamsMap.value("level2Breaker"));
    }
    if (strategyParamsMap.contains("level3Breaker")) {
        configMap.insert("level3Breaker", strategyParamsMap.value("level3Breaker"));
    }
    if (strategyParamsMap.contains("autoStopEnabled")) {
        configMap.insert("autoStopEnabled", strategyParamsMap.value("autoStopEnabled"));
    }
    strategyParamsMap.insert("stop_loss", configMap.value("stopLossRate"));
    strategyParamsMap.insert("stopLoss", configMap.value("stopLossRate"));
    strategyParamsMap.insert("stopLossPercent", configMap.value("stopLossRate"));
    if (!strategyParamsMap.contains("take_profit") && configMap.contains("takeProfitRate")) {
        strategyParamsMap.insert("take_profit", configMap.value("takeProfitRate"));
    }
    if (!strategyParamsMap.contains("takeProfit") && configMap.contains("takeProfitRate")) {
        strategyParamsMap.insert("takeProfit", configMap.value("takeProfitRate"));
    }
    if (!strategyParamsMap.contains("takeProfitPercent") && configMap.contains("takeProfitRate")) {
        strategyParamsMap.insert("takeProfitPercent", configMap.value("takeProfitRate"));
    }
    strategyParamsMap.insert("rebalanceDays", configMap.value("rebalanceFrequency"));
    strategyParamsMap.insert("rebalance_days", configMap.value("rebalanceFrequency"));
    strategyParamsMap.insert("rebalancingPeriod", configMap.value("rebalanceFrequency"));
    configMap.insert("strategyParams", strategyParamsMap);
    configMap.insert("maxTotalExposure", configMap.value("maxPositionRatio"));
    configMap.insert("maxPositionPercent", configMap.value("maxSinglePositionRatio"));
    configMap.insert("rebalanceDays", configMap.value("rebalanceFrequency"));
    configMap.insert("stopLossPercent", configMap.value("stopLossRate"));
    if (!configMap.contains("takeProfitRate")) {
        configMap.insert("takeProfitRate", strategyParamsMap.value("take_profit"));
    }
    configMap.insert("takeProfitPercent", strategyParamsMap.value("takeProfitPercent", configMap.value("takeProfitRate")));
    configMap["dataSourceMode"] = m_dataSourceMode;
    configMap["selectedDatasetId"] = m_selectedDatasetId;
    resultMap["config"] = configMap;
    return resultMap;
}

QVariantList StrategyBacktestController::convertHistoryToQml(
    const QVariantList& history) const
{
    // 返回传入的历史记录
    return history;
}
