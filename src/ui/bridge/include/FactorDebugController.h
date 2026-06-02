// FactorDebugController.h
// 因子调试控制器 - 提供因子调试功能，实时参数调整和预览
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QTimer>
#include <QMutex>
#include <QReadWriteLock>
#include <memory>
#include <atomic>

class FactorViewModel;

/**
 * 因子调试控制器
 * 
 * 功能：
 * 1. 加载因子并准备调试环境
 * 2. 实时参数调整和预览
 * 3. 计算因子性能指标（IC、IR、波动率等）
 * 4. 生成因子值曲线数据
 * 5. 保存调试配置
 */
class FactorDebugController : public QObject {
    Q_OBJECT
    
    // 属性定义
    Q_PROPERTY(QString currentFactorId READ currentFactorId WRITE setCurrentFactorId NOTIFY currentFactorIdChanged)
    Q_PROPERTY(QString currentFactorName READ currentFactorName NOTIFY currentFactorNameChanged)
    Q_PROPERTY(QString currentFactorType READ currentFactorType NOTIFY currentFactorTypeChanged)
    Q_PROPERTY(QVariantMap currentFactor READ currentFactor NOTIFY currentFactorChanged)
    Q_PROPERTY(QVariantMap debugParameters READ debugParameters WRITE setDebugParameters NOTIFY debugParametersChanged)
    Q_PROPERTY(QVariantMap performanceMetrics READ performanceMetrics NOTIFY performanceMetricsChanged)
    Q_PROPERTY(QVariantList factorValueSeries READ factorValueSeries NOTIFY factorValueSeriesChanged)
    Q_PROPERTY(bool isDebugging READ isDebugging NOTIFY isDebuggingChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
    Q_PROPERTY(bool realtimeUpdateEnabled READ realtimeUpdateEnabled WRITE setRealtimeUpdateEnabled NOTIFY realtimeUpdateEnabledChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QVariantList availableFactors READ availableFactors NOTIFY availableFactorsChanged)
    
public:
    // 调试状态枚举
    enum class DebugState {
        Idle,           // 空闲
        Loading,        // 加载中
        Ready,          // 准备就绪
        Debugging,      // 调试中
        Calculating,    // 计算中
        Error           // 错误
    };
    Q_ENUM(DebugState)
    
    // 计算方法枚举
    enum class CalculationMethod {
        SimpleReturn,       // 简单收益率
        LogReturn,          // 对数收益率
        PriceMomentum,      // 价格动量
        RelativeStrength    // 相对强弱
    };
    Q_ENUM(CalculationMethod)
    
    explicit FactorDebugController(QObject* parent = nullptr);
    ~FactorDebugController();
    
    // ============ QML可调用的方法 ============
    
    // 初始化
    Q_INVOKABLE void initialize();
    
    // 因子加载
    Q_INVOKABLE bool loadFactor(const QString& factorId);
    Q_INVOKABLE void unloadFactor();
    Q_INVOKABLE void refreshFactorList();
    
    // 参数调试
    Q_INVOKABLE void setDebugParameter(const QString& paramName, const QVariant& value);
    Q_INVOKABLE QVariant getDebugParameter(const QString& paramName) const;
    Q_INVOKABLE void resetDebugParameters();
    
    // 调试控制
    Q_INVOKABLE void startDebug();
    Q_INVOKABLE void stopDebug();
    Q_INVOKABLE void pauseDebug();
    Q_INVOKABLE void resumeDebug();
    
    // 计算和预览
    Q_INVOKABLE void calculateFactorValues();
    Q_INVOKABLE void updatePreview();
    Q_INVOKABLE QVariantList generateFactorValueSeries(int count = 50);
    
    // 性能指标计算
    Q_INVOKABLE double calculateIC();
    Q_INVOKABLE double calculateIR();
    Q_INVOKABLE double calculateVolatility();
    Q_INVOKABLE double calculateSkewness();
    Q_INVOKABLE double calculateKurtosis();
    Q_INVOKABLE QVariantMap calculateAllMetrics();
    
    // 配置管理
    Q_INVOKABLE bool saveDebugConfiguration();
    Q_INVOKABLE bool loadDebugConfiguration(const QString& configId);
    Q_INVOKABLE QVariantList getSavedConfigurations();
    
    // 工具方法
    Q_INVOKABLE QString getCalculationMethodName(int method) const;
    Q_INVOKABLE QStringList getAvailableCalculationMethods() const;
    
    // ============ 属性访问器 ============
    
    QString currentFactorId() const;
    void setCurrentFactorId(const QString& factorId);
    
    QString currentFactorName() const;
    QString currentFactorType() const;
    QVariantMap currentFactor() const;
    
    QVariantMap debugParameters() const;
    void setDebugParameters(const QVariantMap& params);
    
    QVariantMap performanceMetrics() const;
    QVariantList factorValueSeries() const;
    
    bool isDebugging() const;
    bool isLoading() const;
    
    bool realtimeUpdateEnabled() const;
    void setRealtimeUpdateEnabled(bool enabled);
    
    QString statusMessage() const;
    QVariantList availableFactors() const;
    
signals:
    // 属性变化信号
    void currentFactorIdChanged();
    void currentFactorNameChanged();
    void currentFactorTypeChanged();
    void currentFactorChanged();
    void debugParametersChanged();
    void performanceMetricsChanged();
    void factorValueSeriesChanged();
    void isDebuggingChanged();
    void isLoadingChanged();
    void realtimeUpdateEnabledChanged();
    void statusMessageChanged();
    void availableFactorsChanged();
    
    // 操作信号
    void factorLoaded(const QString& factorId, const QVariantMap& factorData);
    void factorUnloaded();
    void debugStarted();
    void debugStopped();
    void debugPaused();
    void debugResumed();
    void previewUpdated(const QVariantList& values);
    void metricsCalculated(const QVariantMap& metrics);
    void configurationSaved(bool success, const QString& message);
    void errorOccurred(const QString& error);
    
private slots:
    void onRealtimeUpdateTimer();
    
private:
    // 内部方法
    void updateStatusMessage(const QString& message);
    void setDebugState(DebugState state);
    void generateMockFactorValues();
    void calculatePerformanceMetrics();
    QVariantMap getDefaultDebugParameters() const;
    
    // 数学计算辅助方法
    double mean(const QVector<double>& values) const;
    double standardDeviation(const QVector<double>& values) const;
    double correlation(const QVector<double>& x, const QVector<double>& y) const;
    
private:
    // 当前因子信息
    QString m_currentFactorId;
    QVariantMap m_currentFactor;
    
    // 调试参数
    QVariantMap m_debugParameters;
    
    // 性能指标
    QVariantMap m_performanceMetrics;
    
    // 因子值序列
    QVariantList m_factorValueSeries;
    
    // 可用因子列表
    QVariantList m_availableFactors;
    
    // 状态
    std::atomic<DebugState> m_debugState;
    std::atomic<bool> m_realtimeUpdateEnabled;
    QString m_statusMessage;
    
    // 定时器
    QTimer* m_realtimeUpdateTimer;
    
    // 读写锁
    mutable QReadWriteLock m_rwLock;
    
};
