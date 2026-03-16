#include "FactorBacktestController.h"
#include "../../../domain/backtest/include/FactorBacktestService.h"
#include "../../../domain/backtest/include/FactorBacktestTypes.h"
#include "../../../foundation/include/foundation.h"
#include <QThread>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

using namespace domain::backtest;

FactorBacktestController::FactorBacktestController(QObject *parent)
    : QObject(parent)
    , m_isRunning(false)
    , m_progress(0)
    , m_status("未初始化")
{
    qDebug() << "FactorBacktestController 创建";
}

FactorBacktestController::~FactorBacktestController()
{
    shutdown();
    qDebug() << "FactorBacktestController 销毁";
}

bool FactorBacktestController::initialize()
{
    qDebug() << "初始化因子回测控制器";
    
    try {
        // 初始化foundation（如果尚未初始化）
        if (!foundation::Foundation::instance().is_initialized()) {
            foundation::Config config;
            config.thread_pool_size = 4;
            foundation::Foundation::instance().initialize(config);
        }
        
        // 初始化回测服务
        if (!initializeService()) {
            m_lastError = "无法初始化回测服务";
            emit lastErrorChanged(m_lastError);
            return false;
        }
        
        m_status = "已就绪";
        emit statusChanged(m_status);
        
        qDebug() << "因子回测控制器初始化成功";
        return true;
        
    } catch (const std::exception& e) {
        m_lastError = QString("初始化失败: %1").arg(e.what());
        emit lastErrorChanged(m_lastError);
        qCritical() << "因子回测控制器初始化失败:" << e.what();
        return false;
    }
}

void FactorBacktestController::shutdown()
{
    qDebug() << "关闭因子回测控制器";
    
    if (m_isRunning) {
        cancelBacktest();
    }
    
    m_service.reset();
    m_status = "已关闭";
    emit statusChanged(m_status);
}

bool FactorBacktestController::initializeService()
{
    try {
        m_service = std::make_unique<FactorBacktestService>();
        
        // 设置数据提供器（这里需要从全局数据服务获取）
        // 暂时使用空实现，实际使用时需要从全局数据服务获取
        
        qDebug() << "因子回测服务初始化成功";
        return true;
        
    } catch (const std::exception& e) {
        qCritical() << "因子回测服务初始化失败:" << e.what();
        return false;
    }
}

QVariantMap FactorBacktestController::runFactorBacktestSync(
    const QString& factorId,
    const QVariantMap& config)
{
    qDebug() << "开始同步因子回测:" << factorId;
    
    if (!m_service) {
        m_lastError = "回测服务未初始化";
        emit lastErrorChanged(m_lastError);
        return QVariantMap();
    }
    
    try {
        // 转换配置
        FactorBacktestConfig backtestConfig = convertConfigFromVariantMap(config);
        backtestConfig.factorId = factorId.toStdString();
        
        // 运行回测
        FactorBacktestResult result = m_service->runFactorBacktestSync(backtestConfig);
        
        // 转换结果
        m_lastResult = convertResultToVariantMap(result);
        m_groupResults = convertGroupsToVariantList(result);
        m_icirResult = convertICIRToVariantMap(result);
        m_summaryStats = convertSummaryToVariantMap(result);
        
        // 发出信号
        emit lastResultChanged(m_lastResult);
        emit groupResultsChanged(m_groupResults);
        emit icirResultChanged(m_icirResult);
        emit summaryStatsChanged(m_summaryStats);
        
        qDebug() << "同步因子回测完成:" << factorId;
        return m_lastResult;
        
    } catch (const std::exception& e) {
        m_lastError = QString("回测失败: %1").arg(e.what());
        emit lastErrorChanged(m_lastError);
        qCritical() << "因子回测失败:" << e.what();
        return QVariantMap();
    }
}

void FactorBacktestController::runFactorBacktestAsync(
    const QString& factorId,
    const QVariantMap& config)
{
    qDebug() << "开始异步因子回测:" << factorId;
    
    if (!m_service) {
        m_lastError = "回测服务未初始化";
        emit lastErrorChanged(m_lastError);
        emit backtestFailed(m_lastError);
        return;
    }
    
    if (m_isRunning) {
        m_lastError = "已有回测任务正在运行";
        emit lastErrorChanged(m_lastError);
        emit backtestFailed(m_lastError);
        return;
    }
    
    // 设置运行状态
    m_isRunning = true;
    m_progress = 0;
    m_status = "正在回测";
    
    emit isRunningChanged(m_isRunning);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    emit backtestStarted(factorId);
    
    // 在后台线程中运行回测
    QThread* thread = new QThread();
    
    // 创建任务对象
    QObject* task = new QObject();
    
    // 连接信号
    connect(thread, &QThread::started, this, [=]() {
        try {
            // 转换配置
            FactorBacktestConfig backtestConfig = convertConfigFromVariantMap(config);
            backtestConfig.factorId = factorId.toStdString();
            
            // 运行回测
            FactorBacktestResult result = m_service->runFactorBacktestSync(backtestConfig);
            
            // 转换结果
            QVariantMap resultMap = convertResultToVariantMap(result);
            
            // 发送完成信号
            QMetaObject::invokeMethod(this, "onBacktestCompleted", 
                Qt::QueuedConnection, Q_ARG(QVariantMap, resultMap));
            
        } catch (const std::exception& e) {
            QString error = QString("回测失败: %1").arg(e.what());
            QMetaObject::invokeMethod(this, "onBacktestFailed", 
                Qt::QueuedConnection, Q_ARG(QString, error));
        }
        
        thread->quit();
    });
    
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    connect(thread, &QThread::finished, task, &QObject::deleteLater);
    
    // 启动线程
    task->moveToThread(thread);
    thread->start();
    
    // 保存取消回调
    m_cancelCallback = [thread]() {
        if (thread->isRunning()) {
            thread->quit();
            thread->wait();
        }
    };
}

void FactorBacktestController::cancelBacktest()
{
    qDebug() << "取消因子回测";
    
    if (!m_isRunning) {
        return;
    }
    
    if (m_cancelCallback) {
        m_cancelCallback();
        m_cancelCallback = nullptr;
    }
    
    m_isRunning = false;
    m_status = "已取消";
    
    emit isRunningChanged(m_isRunning);
    emit statusChanged(m_status);
    emit backtestCancelled();
}

bool FactorBacktestController::saveResultToFile(const QString& filePath)
{
    if (m_lastResult.isEmpty()) {
        m_lastError = "没有可保存的回测结果";
        emit lastErrorChanged(m_lastError);
        return false;
    }
    
    try {
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            m_lastError = QString("无法打开文件: %1").arg(filePath);
            emit lastErrorChanged(m_lastError);
            return false;
        }
        
        QJsonDocument doc(QJsonObject::fromVariantMap(m_lastResult));
        file.write(doc.toJson());
        file.close();
        
        qDebug() << "回测结果已保存到:" << filePath;
        return true;
        
    } catch (const std::exception& e) {
        m_lastError = QString("保存失败: %1").arg(e.what());
        emit lastErrorChanged(m_lastError);
        return false;
    }
}

QVariantMap FactorBacktestController::loadResultFromFile(const QString& filePath)
{
    try {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_lastError = QString("无法打开文件: %1").arg(filePath);
            emit lastErrorChanged(m_lastError);
            return QVariantMap();
        }
        
        QByteArray data = file.readAll();
        file.close();
        
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull()) {
            m_lastError = "文件格式错误";
            emit lastErrorChanged(m_lastError);
            return QVariantMap();
        }
        
        m_lastResult = doc.object().toVariantMap();
        emit lastResultChanged(m_lastResult);
        
        qDebug() << "回测结果已从文件加载:" << filePath;
        return m_lastResult;
        
    } catch (const std::exception& e) {
        m_lastError = QString("加载失败: %1").arg(e.what());
        emit lastErrorChanged(m_lastError);
        return QVariantMap();
    }
}

QVariantMap FactorBacktestController::getDefaultConfig() const
{
    FactorBacktestConfig defaultConfig;
    return convertConfigToVariantMap(defaultConfig);
}

QVariantMap FactorBacktestController::validateConfig(const QVariantMap& config) const
{
    QVariantMap result;
    
    try {
        FactorBacktestConfig backtestConfig = convertConfigFromVariantMap(config);
        bool isValid = backtestConfig.validate();
        
        result["isValid"] = isValid;
        result["errors"] = QString::fromStdString(backtestConfig.getValidationErrors());
        
    } catch (const std::exception& e) {
        result["isValid"] = false;
        result["errors"] = QString("配置验证失败: %1").arg(e.what());
    }
    
    return result;
}

QVariantList FactorBacktestController::getGroupingMethods() const
{
    QVariantList methods;
    
    methods.append(QVariantMap{{"value", "quantile"}, {"label", "分位数分组"}});
    methods.append(QVariantMap{{"value", "equal_value"}, {"label", "等值分组"}});
    methods.append(QVariantMap{{"value", "custom"}, {"label", "自定义分组"}});
    
    return methods;
}

QVariantList FactorBacktestController::getBacktestStrategies() const
{
    QVariantList strategies;
    
    strategies.append(QVariantMap{{"value", "equal_weight"}, {"label", "等权重策略"}});
    strategies.append(QVariantMap{{"value", "factor_weight"}, {"label", "因子权重策略"}});
    strategies.append(QVariantMap{{"value", "risk_parity"}, {"label", "风险平价策略"}});
    strategies.append(QVariantMap{{"value", "custom"}, {"label", "自定义策略"}});
    
    return strategies;
}

void FactorBacktestController::clearCache()
{
    // FactorBacktestService没有clearCache方法
    // 如果需要清除缓存，需要通过CacheManager实现
    // 暂时只记录日志
    qDebug() << "回测缓存清除功能需要实现CacheManager";
}

void FactorBacktestController::onBacktestProgress(int progress, const QString& status)
{
    m_progress = progress;
    m_status = status;
    
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    emit backtestProgress(progress, status);
}

void FactorBacktestController::onBacktestCompleted(const QVariantMap& result)
{
    m_isRunning = false;
    m_progress = 100;
    m_status = "回测完成";
    
    m_lastResult = result;
    
    // 从结果中提取分组、ICIR和汇总信息
    if (result.contains("groups")) {
        m_groupResults = result["groups"].toList();
    }
    
    if (result.contains("icirResult")) {
        m_icirResult = result["icirResult"].toMap();
    }
    
    if (result.contains("summary")) {
        m_summaryStats = result["summary"].toMap();
    }
    
    emit isRunningChanged(m_isRunning);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    emit lastResultChanged(m_lastResult);
    emit groupResultsChanged(m_groupResults);
    emit icirResultChanged(m_icirResult);
    emit summaryStatsChanged(m_summaryStats);
    emit backtestCompleted(result);
    
    qDebug() << "异步因子回测完成";
}

void FactorBacktestController::onBacktestFailed(const QString& error)
{
    m_isRunning = false;
    m_status = "回测失败";
    m_lastError = error;
    
    emit isRunningChanged(m_isRunning);
    emit statusChanged(m_status);
    emit lastErrorChanged(m_lastError);
    emit backtestFailed(error);
    
    qCritical() << "异步因子回测失败:" << error;
}

// 转换函数实现
QVariantMap FactorBacktestController::convertResultToVariantMap(const FactorBacktestResult& result)
{
    QVariantMap map;
    
    map["taskId"] = QString::fromStdString(result.taskId);
    map["executionTime"] = result.executionTime;
    map["success"] = true; // 假设成功，因为没有失败字段
    map["errorMessage"] = ""; // 没有错误信息字段
    
    // 配置信息
    QVariantMap configMap;
    configMap["factorId"] = QString::fromStdString(result.config.factorId);
    configMap["factorName"] = QString::fromStdString(result.config.factorName);
    configMap["startDate"] = QString::fromStdString(result.config.startDate);
    configMap["endDate"] = QString::fromStdString(result.config.endDate);
    configMap["numGroups"] = result.config.numGroups;
    configMap["initialCapital"] = result.config.initialCapital;
    map["config"] = configMap;
    
    // 分组信息
    map["groups"] = convertGroupsToVariantList(result);
    
    // ICIR信息
    map["icirResult"] = convertICIRToVariantMap(result);
    
    // 汇总统计
    map["summary"] = convertSummaryToVariantMap(result);
    
    return map;
}

QVariantList FactorBacktestController::convertGroupsToVariantList(const FactorBacktestResult& result)
{
    QVariantList groups;
    
    for (const auto& group : result.groups) {
        QVariantMap groupMap;
        groupMap["groupId"] = group.groupId;
        groupMap["groupName"] = QString::fromStdString(group.groupName);
        groupMap["minFactorValue"] = group.minFactorValue;
        groupMap["maxFactorValue"] = group.maxFactorValue;
        groupMap["stockCount"] = group.stockCount;
        // 注意：FactorGroup结构体中没有return_、volatility、sharpeRatio、maxDrawdown字段
        // 这些字段可能在其他地方计算
        groupMap["return"] = 0.0; // 占位符
        groupMap["volatility"] = 0.0; // 占位符
        groupMap["sharpeRatio"] = 0.0; // 占位符
        groupMap["maxDrawdown"] = 0.0; // 占位符
        
        groups.append(groupMap);
    }
    
    return groups;
}

QVariantMap FactorBacktestController::convertICIRToVariantMap(const FactorBacktestResult& result)
{
    QVariantMap map;
    
    map["icValue"] = result.icirResult.icValue;
    map["irValue"] = result.icirResult.irValue;
    map["icTStat"] = result.icirResult.icTStat;
    map["icPValue"] = result.icirResult.icPValue;
    map["icPositiveRate"] = result.icirResult.icPositiveRate;
    map["isSignificant"] = result.icirResult.isSignificant;
    map["isValid"] = result.icirResult.isValid();
    
    return map;
}

QVariantMap FactorBacktestController::convertSummaryToVariantMap(const FactorBacktestResult& result)
{
    QVariantMap map;
    
    map["topGroupReturn"] = result.summary.topGroupReturn;
    map["bottomGroupReturn"] = result.summary.bottomGroupReturn;
    map["spreadReturn"] = result.summary.spreadReturn;
    map["monotonicity"] = result.summary.monotonicity;
    map["discrimination"] = result.summary.discrimination;
    map["winRate"] = result.summary.winRate;
    map["sharpeRatio"] = result.summary.sharpeRatio;
    map["maxDrawdown"] = result.summary.maxDrawdown;
    
    return map;
}

FactorBacktestConfig FactorBacktestController::convertConfigFromVariantMap(const QVariantMap& config) const
{
    FactorBacktestConfig backtestConfig;
    
    if (config.contains("factorId")) {
        backtestConfig.factorId = config["factorId"].toString().toStdString();
    }
    
    if (config.contains("factorName")) {
        backtestConfig.factorName = config["factorName"].toString().toStdString();
    }
    
    if (config.contains("startDate")) {
        backtestConfig.startDate = config["startDate"].toString().toStdString();
    }
    
    if (config.contains("endDate")) {
        backtestConfig.endDate = config["endDate"].toString().toStdString();
    }
    
    if (config.contains("groupingMethod")) {
        QString method = config["groupingMethod"].toString();
        if (method == "quantile") {
            backtestConfig.groupingMethod = GroupingMethod::QUANTILE;
        } else if (method == "equal_value") {
            backtestConfig.groupingMethod = GroupingMethod::EQUAL_VALUE;
        } else if (method == "custom") {
            backtestConfig.groupingMethod = GroupingMethod::CUSTOM;
        }
    }
    
    if (config.contains("numGroups")) {
        backtestConfig.numGroups = config["numGroups"].toInt();
    }
    
    if (config.contains("strategy")) {
        QString strategy = config["strategy"].toString();
        if (strategy == "equal_weight") {
            backtestConfig.strategy = BacktestStrategy::EQUAL_WEIGHT;
        } else if (strategy == "factor_weight") {
            backtestConfig.strategy = BacktestStrategy::FACTOR_WEIGHT;
        } else if (strategy == "risk_parity") {
            backtestConfig.strategy = BacktestStrategy::RISK_PARITY;
        } else if (strategy == "custom") {
            backtestConfig.strategy = BacktestStrategy::CUSTOM;
        }
    }
    
    if (config.contains("initialCapital")) {
        backtestConfig.initialCapital = config["initialCapital"].toDouble();
    }
    
    if (config.contains("transactionCost")) {
        backtestConfig.transactionCost = config["transactionCost"].toDouble();
    }
    
    if (config.contains("slippage")) {
        backtestConfig.slippage = config["slippage"].toDouble();
    }
    
    if (config.contains("maxThreads")) {
        backtestConfig.maxThreads = config["maxThreads"].toInt();
    }
    
    if (config.contains("enableCache")) {
        backtestConfig.enableCache = config["enableCache"].toBool();
    }
    
    if (config.contains("cacheTTL")) {
        backtestConfig.cacheTTL = config["cacheTTL"].toInt();
    }
    
    return backtestConfig;
}

QVariantMap FactorBacktestController::convertConfigToVariantMap(const FactorBacktestConfig& config) const
{
    QVariantMap map;
    
    map["factorId"] = QString::fromStdString(config.factorId);
    map["factorName"] = QString::fromStdString(config.factorName);
    map["startDate"] = QString::fromStdString(config.startDate);
    map["endDate"] = QString::fromStdString(config.endDate);
    
    // 分组方法
    QString groupingMethod;
    switch (config.groupingMethod) {
        case GroupingMethod::QUANTILE: groupingMethod = "quantile"; break;
        case GroupingMethod::EQUAL_VALUE: groupingMethod = "equal_value"; break;
        case GroupingMethod::CUSTOM: groupingMethod = "custom"; break;
        default: groupingMethod = "quantile";
    }
    map["groupingMethod"] = groupingMethod;
    
    map["numGroups"] = config.numGroups;
    
    // 回测策略
    QString strategy;
    switch (config.strategy) {
        case BacktestStrategy::EQUAL_WEIGHT: strategy = "equal_weight"; break;
        case BacktestStrategy::FACTOR_WEIGHT: strategy = "factor_weight"; break;
        case BacktestStrategy::RISK_PARITY: strategy = "risk_parity"; break;
        case BacktestStrategy::CUSTOM: strategy = "custom"; break;
        default: strategy = "equal_weight";
    }
    map["strategy"] = strategy;
    
    map["initialCapital"] = config.initialCapital;
    map["transactionCost"] = config.transactionCost;
    map["slippage"] = config.slippage;
    map["maxThreads"] = config.maxThreads;
    map["enableCache"] = config.enableCache;
    map["cacheTTL"] = config.cacheTTL;
    
    return map;
}
