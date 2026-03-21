#include "StrategyBacktestController.h"

#include <QDebug>
#include <QDate>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QtConcurrent>
#include <QFile>
#include <QMetaObject>
#include <algorithm>
#include <chrono>
#include <string>
#include <memory>
#include <stdexcept>

// 简化实现 - 先编译通过，后续再集成真实的服务

StrategyBacktestController::StrategyBacktestController(QObject *parent)
    : QObject(parent)
    , m_isRunning(false)
    , m_progress(0)
    , m_initialCapital(1000000.0)
    , m_startDate(QDate::currentDate().addYears(-1).toString("yyyy-MM-dd"))
    , m_endDate(QDate::currentDate().toString("yyyy-MM-dd"))
{
    qDebug() << "StrategyBacktestController constructed";
    
    // 由于编译依赖问题，暂时使用模拟服务
    // 实际集成时需取消注释以下代码
    /*
    try {
        // m_service = std::make_unique<domain::backtest::StrategyBacktestService>();
        
        // 设置数据提供器
        // auto stockProvider = std::make_shared<DatabaseStockDataProvider>();
        // auto factorProvider = std::make_shared<DatabaseFactorDataProvider>();
        
        // m_service->setDataProvider(stockProvider);
        // m_service->setFactorProvider(factorProvider);
        
        qDebug() << "StrategyBacktestService initialized successfully";
    } catch (const std::exception& e) {
        qWarning() << "Failed to initialize StrategyBacktestService:" << e.what();
    }
    */
}

StrategyBacktestController::~StrategyBacktestController()
{
    qDebug() << "StrategyBacktestController destroyed";
}

void StrategyBacktestController::setSelectedStrategyId(const QString& strategyId)
{
    if (m_selectedStrategyId == strategyId)
        return;
    
    m_selectedStrategyId = strategyId;
    emit selectedStrategyIdChanged(strategyId);
    
    // 加载默认参数
    auto defaultParams = getDefaultStrategyParams(strategyId);
    setStrategyParams(defaultParams);
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
    
    qDebug() << "Starting strategy backtest for strategy:" << strategyId;
    qDebug() << "Params:" << strategyParams;
    qDebug() << "Symbols:" << symbols;
    qDebug() << "Date range:" << startDate << "to" << endDate;
    
    m_isRunning = true;
    m_progress = 0;
    m_status = "准备中...";
    emit isRunningChanged(true);
    emit progressChanged(0);
    emit statusChanged(m_status);
    emit backtestStarted(strategyId);
    
    // 生成任务ID
    m_currentTaskId = QString("strategy_%1_%2").arg(strategyId).arg(QDateTime::currentMSecsSinceEpoch());
    
    // 异步执行回测（模拟）
    QtConcurrent::run([this, strategyId]() {
        // 模拟回测过程
        for (int i = 0; i <= 100; i += 10) {
            QThread::msleep(100);
            QMetaObject::invokeMethod(this, [this, i]() {
                m_progress = i;
                m_status = QString("回测中... %1%").arg(i);
                emit progressChanged(m_progress);
                emit statusChanged(m_status);
                emit backtestProgress(m_progress, m_status);
            }, Qt::QueuedConnection);
        }
        
        // 模拟结果
        QVariantMap qmlResult;
        qmlResult["taskId"] = m_currentTaskId;
        qmlResult["executionTime"] = 5.2;
        qmlResult["strategy"] = strategyId;
        
        // 绩效指标
        QVariantMap performance;
        performance["totalReturn"] = 0.258;
        performance["annualizedReturn"] = 0.152;
        performance["volatility"] = 0.183;
        performance["sharpeRatio"] = 1.42;
        performance["sortinoRatio"] = 2.15;
        performance["calmarRatio"] = 1.85;
        performance["maxDrawdown"] = -0.125;
        performance["winRate"] = 0.583;
        performance["profitFactor"] = 1.68;
        performance["alpha"] = 0.082;
        performance["beta"] = 0.92;
        performance["informationRatio"] = 0.68;
        qmlResult["performance"] = performance;
        
        // 交易统计
        QVariantMap trades;
        trades["totalTrades"] = 156;
        trades["winningTrades"] = 91;
        trades["losingTrades"] = 65;
        trades["totalProfit"] = 0.258;
        trades["totalLoss"] = -0.153;
        trades["largestWin"] = 0.032;
        trades["largestLoss"] = -0.019;
        qmlResult["trades"] = trades;
        
        // 配置信息
        QVariantMap config;
        config["strategyId"] = strategyId;
        config["startDate"] = m_startDate;
        config["endDate"] = m_endDate;
        config["initialCapital"] = m_initialCapital;
        qmlResult["config"] = config;
        
        // 更新状态
        QMetaObject::invokeMethod(this, [this, qmlResult]() {
            m_backtestResult = qmlResult;
            m_progress = 100;
            m_status = "回测完成";
            m_isRunning = false;
            
            emit backtestResultChanged(qmlResult);
            emit progressChanged(m_progress);
            emit statusChanged(m_status);
            emit isRunningChanged(false);
            emit backtestCompleted(qmlResult);
        }, Qt::QueuedConnection);
    });
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
    
    // TODO: 实现批量回测逻辑
    QTimer::singleShot(100, this, [this]() {
        m_isRunning = false;
        m_progress = 100;
        m_status = "批量回测完成";
        
        emit isRunningChanged(false);
        emit progressChanged(m_progress);
        emit statusChanged(m_status);
        
        // 创建示例结果
        QVariantMap result;
        result["total_strategies"] = 3;
        result["completed"] = 3;
        result["success_rate"] = 100.0;
        
        emit backtestCompleted(result);
    });
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
    
    // 异步执行优化（模拟）
    QtConcurrent::run([this, baseStrategyId]() {
        // 模拟优化过程
        for (int i = 0; i <= 100; i += 10) {
            QThread::msleep(50);
            QMetaObject::invokeMethod(this, [this, i]() {
                m_progress = i;
                m_status = QString("参数优化中... %1%").arg(i);
                emit progressChanged(m_progress);
                emit statusChanged(m_status);
                emit optimizationProgress(m_progress, m_status);
            }, Qt::QueuedConnection);
        }
        
        // 模拟结果
        QVariantMap qmlResult;
        qmlResult["optimal_score"] = 1.85;
        qmlResult["optimal_strategy_id"] = baseStrategyId;
        qmlResult["best_params"] = QVariantMap{
            {"short_period", 15},
            {"long_period", 45},
            {"stop_loss", 3.5}
        };
        
        QVariantList qmlHistory;
        for (int i = 0; i < 5; i++) {
            qmlHistory.append(QVariantMap{
                {"iteration", i},
                {"score", 1.5 + i * 0.1},
                {"params", QVariantMap{
                    {"short_period", 10 + i * 5},
                    {"long_period", 40 + i * 5}
                }}
            });
        }
        
        // 更新状态
        QMetaObject::invokeMethod(this, [this, qmlResult, qmlHistory]() {
            m_optimizationHistory = qmlHistory;
            m_backtestResult = qmlResult;
            m_progress = 100;
            m_status = "参数优化完成";
            m_isRunning = false;
            
            emit optimizationHistoryChanged(qmlHistory);
            emit backtestResultChanged(qmlResult);
            emit progressChanged(m_progress);
            emit statusChanged(m_status);
            emit isRunningChanged(false);
            emit optimizationCompleted(qmlResult);
        }, Qt::QueuedConnection);
    });
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
    
    // TODO: 实现策略对比逻辑
    QTimer::singleShot(100, this, [this]() {
        m_isRunning = false;
        m_progress = 100;
        m_status = "策略对比完成";
        
        emit isRunningChanged(false);
        emit progressChanged(m_progress);
        emit statusChanged(m_status);
        
        // 创建示例对比结果
        QVariantMap result;
        result["best_strategy"] = "双均线策略";
        result["best_sharpe"] = 1.8;
        result["comparison_completed"] = true;
        
        emit comparisonResultChanged(result);
        emit comparisonCompleted(result);
    });
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
    QVariantList strategies;
    
    // 返回预定义的策略列表
    strategies.append(QVariantMap{
        {"id", "双均线策略"},
        {"name", "双均线策略"},
        {"description", "基于短期和长期均线交叉的趋势跟踪策略"},
        {"category", "趋势跟踪"},
        {"risk_level", "中等"}
    });
    
    strategies.append(QVariantMap{
        {"id", "RSI超卖反弹"},
        {"name", "RSI超卖反弹策略"},
        {"description", "基于RSI指标超买超卖的均值回归策略"},
        {"category", "均值回归"},
        {"risk_level", "低"}
    });
    
    strategies.append(QVariantMap{
        {"id", "布林带突破"},
        {"name", "布林带突破策略"},
        {"description", "基于布林带通道突破的趋势跟踪策略"},
        {"category", "趋势跟踪"},
        {"risk_level", "中等"}
    });
    
    strategies.append(QVariantMap{
        {"id", "动量策略"},
        {"name", "动量策略"},
        {"description", "基于价格动量的趋势跟随策略"},
        {"category", "动量"},
        {"risk_level", "高"}
    });
    
    return strategies;
}

QVariantMap StrategyBacktestController::getDefaultStrategyParams(const QString& strategyId) const
{
    QVariantMap params;
    
    if (strategyId == "双均线策略") {
        params["short_period"] = 20;
        params["long_period"] = 60;
        params["stop_loss"] = 5.0;
        params["take_profit"] = 10.0;
    } else if (strategyId == "RSI超卖反弹") {
        params["rsi_period"] = 14;
        params["oversold"] = 30.0;
        params["overbought"] = 70.0;
        params["stop_loss"] = 3.0;
    } else if (strategyId == "布林带突破") {
        params["bb_period"] = 20;
        params["bb_std"] = 2.0;
        params["stop_loss"] = 5.0;
        params["take_profit"] = 15.0;
    } else if (strategyId == "动量策略") {
        params["momentum_period"] = 20;
        params["threshold"] = 0.05;
        params["stop_loss"] = 8.0;
        params["take_profit"] = 20.0;
    }
    
    return params;
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
    QVariantList symbols;
    
    // 返回预定义的标的列表
    symbols.append("000001.SZ"); // 平安银行
    symbols.append("000002.SZ"); // 万科A
    symbols.append("000300.SH"); // 沪深300
    symbols.append("000905.SH"); // 中证500
    symbols.append("600000.SH"); // 浦发银行
    symbols.append("600036.SH"); // 招商银行
    symbols.append("AAPL");      // 苹果
    symbols.append("MSFT");      // 微软
    
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

// 由于编译依赖问题，这些函数暂时简化
QVariantMap StrategyBacktestController::createConfig(
    const QString& strategyId,
    const QVariantMap& strategyParams,
    const QVariantList& symbols,
    const QString& startDate,
    const QString& endDate) const
{
    QVariantMap config;
    config["strategyId"] = strategyId;
    config["strategyName"] = strategyId;
    config["startDate"] = startDate;
    config["endDate"] = endDate;
    config["initialCapital"] = m_initialCapital;
    
    // 转换策略参数
    QVariantMap params;
    for (auto it = strategyParams.begin(); it != strategyParams.end(); ++it) {
        params[it.key()] = it.value();
    }
    config["strategyParams"] = params;
    
    // 转换标的
    QVariantList symbolList;
    for (const QVariant& symbol : symbols) {
        symbolList.append(symbol);
    }
    config["symbols"] = symbolList;
    
    return config;
}

QVariantMap StrategyBacktestController::convertResultToQml(const QVariantMap& result) const
{
    // 返回传入的结果，因为我们已经生成了模拟结果
    return result;
}

QVariantList StrategyBacktestController::convertHistoryToQml(
    const QVariantList& history) const
{
    // 返回传入的历史记录
    return history;
}
