#pragma once
// ═════════════════════════════════════════════════════════════════════════
// TradingConnectionConfigService — 交易连接配置服务桥接
// 管理策略绑定、启动门控、配置持久化和客户端进程状态
// 配置以 JSON 文件持久化存储
// ═════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QRecursiveMutex>

namespace bridge {

class TradingConnectionConfigService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap currentConfiguration READ currentConfiguration
               NOTIFY currentConfigurationChanged)
    Q_PROPERTY(QString configFilePath READ configFilePath
               NOTIFY configFilePathChanged)
public:
    explicit TradingConnectionConfigService(QObject* parent = nullptr);

    QVariantMap currentConfiguration() const;
    QString configFilePath() const;

    // ── 配置持久化 ──

    /// @brief 从文件加载配置
    Q_INVOKABLE QVariantMap loadConfiguration();

    /// @brief 保存配置到文件
    Q_INVOKABLE bool saveConfiguration(const QVariantMap& payload);

    /// @brief 获取默认配置
    Q_INVOKABLE QVariantMap defaultConfiguration() const;

    // ── 策略绑定管理 ──

    /// @brief 绑定策略配置（主绑定）
    Q_INVOKABLE QVariantMap bindStrategyConfiguration(const QString& strategyId,
                                                        const QString& strategyName,
                                                        bool isPrimary,
                                                        bool autoStart);

    /// @brief 添加绑定策略配置（附加绑定）
    Q_INVOKABLE QVariantMap addBoundStrategyConfiguration(const QString& strategyId,
                                                            const QString& strategyName,
                                                            bool isPrimary,
                                                            bool autoStart);

    /// @brief 移除绑定策略配置
    Q_INVOKABLE QVariantMap removeBoundStrategyConfiguration(const QString& strategyId);

    // ── 启动门控 ──

    /// @brief 评估启动门控条件
    /// @return {ready: bool, reasonCode: string, reasons: [...]}
    Q_INVOKABLE QVariantMap evaluateStartupGate(bool forceRecheck);

    // ── 客户端进程状态 ──

    /// @brief 同步刷新客户端进程状态
    Q_INVOKABLE QVariantMap refreshClientProcessStatus();

    /// @brief 异步刷新客户端进程状态
    Q_INVOKABLE void refreshClientProcessStatusAsync();

signals:
    void currentConfigurationChanged();
    void configFilePathChanged();
    void clientProcessStatusChanged(const QVariantMap& status);
    void errorOccurred(const QString& message);

private:
    // ── 内部辅助 ──

    QVariantMap normalizeConfiguration(const QVariantMap& raw) const;
    QString resolveConfigFilePath() const;
    bool writeConfigFile(const QVariantMap& config) const;
    QVariantMap readConfigFile() const;
    QVariantMap buildStartupGateResult(bool ready, const QString& reasonCode,
                                        const QStringList& reasons) const;
    QVariantMap detectClientProcesses() const;
    QString sanitizeStrategyId(const QString& raw) const;

    // ── 绑定的策略列表操作 ──
    QVariantList boundStrategies() const;
    void setBoundStrategies(const QVariantList& strategies);
    void mergeBoundStrategy(const QVariantMap& entry);
    bool removeBoundStrategy(const QString& strategyId);

    mutable QRecursiveMutex m_mutex;
    QVariantMap m_currentConfig;
    QString m_configFilePath;
    bool m_initialized{false};
};

} // namespace bridge
