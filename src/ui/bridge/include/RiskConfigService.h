#pragma once
// ═════════════════════════════════════════════════════════════════════════
// RiskConfigService — 风控配置服务桥接
// 管理风控参数的保存、加载和应用
// ═════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace bridge {

class RiskConfigService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap appliedConfiguration READ appliedConfiguration
               NOTIFY appliedConfigurationChanged)
public:
    explicit RiskConfigService(QObject* parent = nullptr);

    QVariantMap appliedConfiguration() const;

    /// @brief 初始化服务，加载已保存的配置
    Q_INVOKABLE void initialize();

    /// @brief 加载当前编辑中的配置
    Q_INVOKABLE QVariantMap loadCurrentConfiguration();

    /// @brief 加载已应用的配置
    Q_INVOKABLE QVariantMap loadAppliedConfiguration();

    /// @brief 保存配置到文件
    Q_INVOKABLE bool saveConfiguration(const QVariantMap& config);

    /// @brief 应用配置（立即生效）
    Q_INVOKABLE bool applyConfiguration(const QVariantMap& config);

signals:
    void appliedConfigurationChanged();
    void configurationSaved();
    void configurationApplied();
    void errorOccurred(const QString& message);

private:
    QString configFilePath() const;
    QVariantMap defaultConfiguration() const;
    QVariantMap normalizeConfiguration(const QVariantMap& raw) const;
    bool writeConfigFile(const QVariantMap& config) const;
    QVariantMap readConfigFile() const;

    QVariantMap m_appliedConfig;
    bool m_initialized{false};
};

} // namespace bridge
