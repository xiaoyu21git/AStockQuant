// FactorDebugController.cpp
// 因子调试控制器实现 - 提供因子调试功能，实时参数调整和预览

#include "../../ui/bridge/include/FactorDebugController.h"
#include "../../ui/bridge/include/FactorViewModel.h"
#include "foundation.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <cmath>
#include <random>
#include <sstream>

namespace {

std::string toStdString(const QString& value)
{
    return value.toStdString();
}

std::string toStdString(const QVariant& value)
{
    return value.toString().toStdString();
}

} // namespace

// 构造函数
FactorDebugController::FactorDebugController(QObject* parent)
    : QObject(parent)
    , m_currentFactorId("")
    , m_debugState(DebugState::Idle)
    , m_realtimeUpdateEnabled(true)
    , m_statusMessage("就绪")
    , m_realtimeUpdateTimer(new QTimer(this))
{
    LOG_DEBUG("FactorDebugController constructor");
    
    // 初始化调试参数
    m_debugParameters = getDefaultDebugParameters();
    
    // 初始化性能指标
    m_performanceMetrics["ic"] = 0.0;
    m_performanceMetrics["ir"] = 0.0;
    m_performanceMetrics["volatility"] = 0.0;
    m_performanceMetrics["skewness"] = 0.0;
    m_performanceMetrics["kurtosis"] = 0.0;
    
    // 设置实时更新定时器
    m_realtimeUpdateTimer->setInterval(1000); // 1秒更新一次
    connect(m_realtimeUpdateTimer, &QTimer::timeout, this, &FactorDebugController::onRealtimeUpdateTimer);
    
}

// 析构函数
FactorDebugController::~FactorDebugController()
{
    LOG_DEBUG("FactorDebugController destructor");
    
    if (m_realtimeUpdateTimer) {
        m_realtimeUpdateTimer->stop();
    }
}

// 初始化
void FactorDebugController::initialize()
{
    LOG_DEBUG("FactorDebugController::initialize");
    
    if (m_debugState != DebugState::Idle) {
        LOG_WARN("FactorDebugController::initialize: 已经在初始化状态");
        return;
    }
    
    setDebugState(DebugState::Loading);
    updateStatusMessage("初始化中...");
    
    // 刷新因子列表
    refreshFactorList();
    
    setDebugState(DebugState::Ready);
    updateStatusMessage("初始化完成");
    
    LOG_DEBUG("FactorDebugController::initialize: 完成");
}

// 加载因子
bool FactorDebugController::loadFactor(const QString& factorId)
{
    LOG_DEBUG(std::string("FactorDebugController::loadFactor: 加载因子 ") + toStdString(factorId));
    
    if (factorId.isEmpty()) {
        updateStatusMessage("因子ID不能为空");
        return false;
    }
    
    setDebugState(DebugState::Loading);
    updateStatusMessage("加载因子中...");

    QVariantMap factorData;
    {
        QReadLocker locker(&m_rwLock);
        for (const QVariant& factorEntry : m_availableFactors) {
            const QVariantMap candidate = factorEntry.toMap();
            if (candidate.value(QStringLiteral("factorId")).toString().trimmed() == factorId.trimmed()) {
                factorData = candidate;
                break;
            }
        }
    }
    if (factorData.isEmpty()) {
        setDebugState(DebugState::Error);
        updateStatusMessage("加载因子失败: 因子服务已删除");
        return false;
    }
    
    // 保存当前因子信息
    {
        QWriteLocker locker(&m_rwLock);
        m_currentFactorId = factorId;
        m_currentFactor = factorData;
    }
    
    // 重置调试参数为默认值
    resetDebugParameters();
    
    // 生成模拟因子值
    generateMockFactorValues();
    
    // 计算性能指标
    calculatePerformanceMetrics();
    
    setDebugState(DebugState::Ready);
    updateStatusMessage("因子加载完成");
    
    emit currentFactorIdChanged();
    emit currentFactorChanged();
    emit factorLoaded(factorId, factorData);
    
    LOG_DEBUG(std::string("FactorDebugController::loadFactor: 成功加载因子 ") + toStdString(factorId));
    return true;
}

// 卸载因子
void FactorDebugController::unloadFactor()
{
    LOG_DEBUG("FactorDebugController::unloadFactor");
    
    {
        QWriteLocker locker(&m_rwLock);
        m_currentFactorId.clear();
        m_currentFactor.clear();
        m_factorValueSeries.clear();
    }
    
    setDebugState(DebugState::Idle);
    updateStatusMessage("因子已卸载");
    
    emit currentFactorIdChanged();
    emit currentFactorChanged();
    emit factorValueSeriesChanged();
    emit factorUnloaded();
}

// 刷新因子列表
void FactorDebugController::refreshFactorList()
{
    LOG_DEBUG("FactorDebugController::refreshFactorList");
    
    {
        QWriteLocker locker(&m_rwLock);
        m_availableFactors.clear();
    }

    emit availableFactorsChanged();

    LOG_DEBUG("FactorDebugController::refreshFactorList: 因子服务已删除，列表清空");
}

// 设置调试参数
void FactorDebugController::setDebugParameter(const QString& paramName, const QVariant& value)
{
    LOG_DEBUG(std::string("FactorDebugController::setDebugParameter: ") + toStdString(paramName) + "=" + toStdString(value));
    
    if (paramName.isEmpty()) {
        LOG_WARN("参数名不能为空");
        return;
    }
    
    {
        QWriteLocker locker(&m_rwLock);
        m_debugParameters[paramName] = value;
    }
    
    emit debugParametersChanged();
    
    // 如果启用了实时更新，则立即更新预览
    if (m_realtimeUpdateEnabled && m_debugState == DebugState::Debugging) {
        updatePreview();
    }
}

// 获取调试参数
QVariant FactorDebugController::getDebugParameter(const QString& paramName) const
{
    QReadLocker locker(&m_rwLock);
    
    if (m_debugParameters.contains(paramName)) {
        return m_debugParameters[paramName];
    }
    
    return QVariant();
}

// 重置调试参数
void FactorDebugController::resetDebugParameters()
{
    LOG_DEBUG("FactorDebugController::resetDebugParameters");
    
    {
        QWriteLocker locker(&m_rwLock);
        m_debugParameters = getDefaultDebugParameters();
    }
    
    emit debugParametersChanged();
    
    // 如果启用了实时更新，则立即更新预览
    if (m_realtimeUpdateEnabled && m_debugState == DebugState::Debugging) {
        updatePreview();
    }
}

// 开始调试
void FactorDebugController::startDebug()
{
    LOG_DEBUG("FactorDebugController::startDebug");
    
    if (m_currentFactorId.isEmpty()) {
        updateStatusMessage("请先加载一个因子");
        return;
    }
    
    if (m_debugState == DebugState::Debugging) {
        LOG_WARN("已经在调试状态");
        return;
    }
    
    setDebugState(DebugState::Debugging);
    updateStatusMessage("调试已开始");
    
    // 启动实时更新定时器
    if (m_realtimeUpdateEnabled) {
        m_realtimeUpdateTimer->start();
    }
    
    // 生成初始因子值
    generateMockFactorValues();
    
    emit debugStarted();
}

// 停止调试
void FactorDebugController::stopDebug()
{
    LOG_DEBUG("FactorDebugController::stopDebug");
    
    if (m_debugState != DebugState::Debugging) {
        LOG_WARN("不在调试状态");
        return;
    }
    
    // 停止定时器
    m_realtimeUpdateTimer->stop();
    
    setDebugState(DebugState::Ready);
    updateStatusMessage("调试已停止");
    
    emit debugStopped();
}

// 暂停调试
void FactorDebugController::pauseDebug()
{
    LOG_DEBUG("FactorDebugController::pauseDebug");
    
    if (m_debugState != DebugState::Debugging) {
        LOG_WARN("不在调试状态");
        return;
    }
    
    // 停止定时器
    m_realtimeUpdateTimer->stop();
    
    setDebugState(DebugState::Calculating);
    updateStatusMessage("调试已暂停");
    
    emit debugPaused();
}

// 恢复调试
void FactorDebugController::resumeDebug()
{
    LOG_DEBUG("FactorDebugController::resumeDebug");
    
    if (m_debugState != DebugState::Calculating) {
        LOG_WARN("不在暂停状态");
        return;
    }
    
    setDebugState(DebugState::Debugging);
    updateStatusMessage("调试已恢复");
    
    // 启动定时器
    if (m_realtimeUpdateEnabled) {
        m_realtimeUpdateTimer->start();
    }
    
    emit debugResumed();
}

// 计算因子值
void FactorDebugController::calculateFactorValues()
{
    LOG_DEBUG("FactorDebugController::calculateFactorValues");
    
    if (m_currentFactorId.isEmpty()) {
        LOG_WARN("没有加载因子");
        return;
    }
    
    setDebugState(DebugState::Calculating);
    updateStatusMessage("计算因子值中...");
    
    // 生成模拟因子值
    generateMockFactorValues();
    
    // 计算性能指标
    calculatePerformanceMetrics();
    
    setDebugState(DebugState::Ready);
    updateStatusMessage("计算完成");
}

// 更新预览
void FactorDebugController::updatePreview()
{
    LOG_DEBUG("FactorDebugController::updatePreview");
    
    if (m_currentFactorId.isEmpty()) {
        return;
    }
    
    // 生成新的因子值序列
    generateMockFactorValues();
    
    // 计算性能指标
    calculatePerformanceMetrics();
    
    emit previewUpdated(m_factorValueSeries);
}

// 生成因子值序列
QVariantList FactorDebugController::generateFactorValueSeries(int count)
{
    LOG_DEBUG("FactorDebugController::generateFactorValueSeries: 生成 " + std::to_string(count) + " 个值");
    
    QVariantList values;
    
    // 获取当前调试参数
    QReadLocker locker(&m_rwLock);
    int windowPeriod = m_debugParameters.value("windowPeriod", 20).toInt();
    QString calculationMethod = m_debugParameters.value("calculationMethod", "SimpleReturn").toString();
    int lookbackWindow = m_debugParameters.value("lookbackWindow", 60).toInt();
    locker.unlock();
    
    // 基于参数生成模拟数据
    double baseValue = 0.0;
    double volatility = 0.02 * (windowPeriod / 20.0); // 窗口期越大，波动越小
    volatility /= std::max(1, lookbackWindow / 20);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<> dist(0.0, volatility);
    
    for (int i = 0; i < count; i++) {
        // 添加随机波动
        double randomChange = dist(gen);
        baseValue += randomChange;
        
        // 添加趋势成分（基于计算方法）
        if (calculationMethod == "PriceMomentum") {
            baseValue += 0.001 * (windowPeriod / 20.0);
        } else if (calculationMethod == "RelativeStrength") {
            baseValue += 0.0005 * (windowPeriod / 20.0);
        }
        
        // 限制值范围
        baseValue = std::max(-1.0, std::min(1.0, baseValue));
        values.append(baseValue);
    }
    
    return values;
}

// 计算IC
double FactorDebugController::calculateIC()
{
    // 模拟IC计算
    QReadLocker locker(&m_rwLock);
    int windowPeriod = m_debugParameters.value("windowPeriod", 20).toInt();
    locker.unlock();
    
    // 窗口期越大，IC通常越高
    double baseIC = 0.03;
    double windowEffect = (windowPeriod - 10) * 0.0005;
    
    return baseIC + windowEffect + (std::rand() % 100) * 0.0001;
}

// 计算IR
double FactorDebugController::calculateIR()
{
    // 模拟IR计算
    double ic = calculateIC();
    return ic * 1.5 + (std::rand() % 100) * 0.0001;
}

// 计算波动率
double FactorDebugController::calculateVolatility()
{
    QReadLocker locker(&m_rwLock);
    int windowPeriod = m_debugParameters.value("windowPeriod", 20).toInt();
    locker.unlock();
    
    // 窗口期越大，波动率通常越小
    return 0.015 - (windowPeriod - 10) * 0.0001 + (std::rand() % 100) * 0.00001;
}

// 计算偏度
double FactorDebugController::calculateSkewness()
{
    // 模拟偏度计算
    return 0.45 + (std::rand() % 100) * 0.001;
}

// 计算峰度
double FactorDebugController::calculateKurtosis()
{
    // 模拟峰度计算
    return 3.2 + (std::rand() % 100) * 0.001;
}

// 计算所有指标
QVariantMap FactorDebugController::calculateAllMetrics()
{
    LOG_DEBUG("FactorDebugController::calculateAllMetrics");
    
    QVariantMap metrics;
    
    metrics["ic"] = calculateIC();
    metrics["ir"] = calculateIR();
    metrics["volatility"] = calculateVolatility();
    metrics["skewness"] = calculateSkewness();
    metrics["kurtosis"] = calculateKurtosis();
    
    // 更新性能指标
    {
        QWriteLocker locker(&m_rwLock);
        m_performanceMetrics = metrics;
    }
    
    emit performanceMetricsChanged();
    emit metricsCalculated(metrics);
    
    return metrics;
}

// 保存调试配置
bool FactorDebugController::saveDebugConfiguration()
{
    LOG_DEBUG("FactorDebugController::saveDebugConfiguration");
    
    if (m_currentFactorId.isEmpty()) {
        updateStatusMessage("请先加载一个因子");
        return false;
    }
    
    // 创建配置对象
    QVariantMap config;
    config["factorId"] = m_currentFactorId;
    config["factorName"] = currentFactorName();
    config["timestamp"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    config["debugParameters"] = m_debugParameters;
    config["performanceMetrics"] = m_performanceMetrics;
    
    // 这里应该保存到数据库或文件
    // 暂时只记录日志
    LOG_DEBUG(std::string("保存调试配置: ") + toStdString(config));
    
    updateStatusMessage("调试配置已保存");
    emit configurationSaved(true, "配置保存成功");
    
    return true;
}

// 加载调试配置
bool FactorDebugController::loadDebugConfiguration(const QString& configId)
{
    LOG_DEBUG(std::string("FactorDebugController::loadDebugConfiguration: ") + toStdString(configId));
    
    // 这里应该从数据库或文件加载配置
    // 暂时只记录日志
    LOG_DEBUG(std::string("加载调试配置: ") + toStdString(configId));
    
    updateStatusMessage("调试配置已加载");
    return true;
}

// 获取已保存的配置
QVariantList FactorDebugController::getSavedConfigurations()
{
    LOG_DEBUG("FactorDebugController::getSavedConfigurations");
    
    // 这里应该从数据库或文件获取配置列表
    // 暂时返回空列表
    return QVariantList();
}

// 获取计算方法名称
QString FactorDebugController::getCalculationMethodName(int method) const
{
    switch (static_cast<CalculationMethod>(method)) {
        case CalculationMethod::SimpleReturn: return "简单收益率";
        case CalculationMethod::LogReturn: return "对数收益率";
        case CalculationMethod::PriceMomentum: return "价格动量";
        case CalculationMethod::RelativeStrength: return "相对强弱";
        default: return "未知方法";
    }
}

// 获取可用的计算方法
QStringList FactorDebugController::getAvailableCalculationMethods() const
{
    return {
        "简单收益率",
        "对数收益率", 
        "价格动量",
        "相对强弱"
    };
}

// ============ 私有方法 ============

void FactorDebugController::onRealtimeUpdateTimer()
{
    if (m_debugState == DebugState::Debugging && m_realtimeUpdateEnabled) {
        updatePreview();
    }
}

void FactorDebugController::updateStatusMessage(const QString& message)
{
    if (m_statusMessage != message) {
        m_statusMessage = message;
        emit statusMessageChanged();
    }
}

void FactorDebugController::setDebugState(DebugState state)
{
    if (m_debugState != state) {
        m_debugState = state;
        
        // 发出相关状态变化信号
        emit isDebuggingChanged();
        emit isLoadingChanged();
    }
}

void FactorDebugController::generateMockFactorValues()
{
    QVariantList values = generateFactorValueSeries(50);
    
    {
        QWriteLocker locker(&m_rwLock);
        m_factorValueSeries = values;
    }
    
    emit factorValueSeriesChanged();
}

void FactorDebugController::calculatePerformanceMetrics()
{
    calculateAllMetrics();
}

QVariantMap FactorDebugController::getDefaultDebugParameters() const
{
    QVariantMap params;
    
    params["windowPeriod"] = 20;
    params["calculationMethod"] = "SimpleReturn";
    params["lookbackWindow"] = 60;
    params["smoothingEnabled"] = true;
    params["normalizationEnabled"] = true;
    params["outlierRemovalEnabled"] = false;
    params["minValue"] = -1.0;
    params["maxValue"] = 1.0;
    params["trendWeight"] = 0.5;
    params["volatilityWeight"] = 0.3;
    
    return params;
}

// ============ 属性访问器 ============

QString FactorDebugController::currentFactorId() const
{
    QReadLocker locker(&m_rwLock);
    return m_currentFactorId;
}

void FactorDebugController::setCurrentFactorId(const QString& factorId)
{
    if (m_currentFactorId != factorId) {
        {
            QWriteLocker locker(&m_rwLock);
            m_currentFactorId = factorId;
        }
        
        // 加载因子
        if (!factorId.isEmpty()) {
            loadFactor(factorId);
        }
        
        emit currentFactorIdChanged();
    }
}

QString FactorDebugController::currentFactorName() const
{
    QReadLocker locker(&m_rwLock);
    return m_currentFactor.value("displayName").toString();
}

QString FactorDebugController::currentFactorType() const
{
    QReadLocker locker(&m_rwLock);
    return m_currentFactor.value("majorCategory").toString();
}

QVariantMap FactorDebugController::currentFactor() const
{
    QReadLocker locker(&m_rwLock);
    return m_currentFactor;
}

QVariantMap FactorDebugController::debugParameters() const
{
    QReadLocker locker(&m_rwLock);
    return m_debugParameters;
}

void FactorDebugController::setDebugParameters(const QVariantMap& params)
{
    {
        QWriteLocker locker(&m_rwLock);
        m_debugParameters = params;
    }
    
    emit debugParametersChanged();
    
    // 如果启用了实时更新，则立即更新预览
    if (m_realtimeUpdateEnabled && m_debugState == DebugState::Debugging) {
        updatePreview();
    }
}

QVariantMap FactorDebugController::performanceMetrics() const
{
    QReadLocker locker(&m_rwLock);
    return m_performanceMetrics;
}

QVariantList FactorDebugController::factorValueSeries() const
{
    QReadLocker locker(&m_rwLock);
    return m_factorValueSeries;
}

bool FactorDebugController::isDebugging() const
{
    return m_debugState == DebugState::Debugging;
}

bool FactorDebugController::isLoading() const
{
    return m_debugState == DebugState::Loading;
}

bool FactorDebugController::realtimeUpdateEnabled() const
{
    return m_realtimeUpdateEnabled;
}

void FactorDebugController::setRealtimeUpdateEnabled(bool enabled)
{
    if (m_realtimeUpdateEnabled != enabled) {
        m_realtimeUpdateEnabled = enabled;
        
        // 如果启用了实时更新且正在调试，则启动定时器
        if (enabled && m_debugState == DebugState::Debugging) {
            m_realtimeUpdateTimer->start();
        } else if (!enabled) {
            m_realtimeUpdateTimer->stop();
        }
        
        emit realtimeUpdateEnabledChanged();
    }
}

QString FactorDebugController::statusMessage() const
{
    return m_statusMessage;
}

QVariantList FactorDebugController::availableFactors() const
{
    QReadLocker locker(&m_rwLock);
    return m_availableFactors;
}
