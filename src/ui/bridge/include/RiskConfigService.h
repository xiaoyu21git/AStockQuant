#pragma once

#include <QObject>
#include <QMutex>
#include <QVariantMap>

class RiskConfigService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)
    Q_PROPERTY(QVariantMap currentConfiguration READ currentConfiguration NOTIFY currentConfigurationChanged)
    Q_PROPERTY(QVariantMap appliedConfiguration READ appliedConfiguration NOTIFY appliedConfigurationChanged)

public:
    static RiskConfigService* instance();

    RiskConfigService(const RiskConfigService&) = delete;
    RiskConfigService& operator=(const RiskConfigService&) = delete;

    Q_INVOKABLE void initialize();
    Q_INVOKABLE QVariantMap loadCurrentConfiguration();
    Q_INVOKABLE QVariantMap loadAppliedConfiguration();
    Q_INVOKABLE bool saveConfiguration(const QVariantMap& configuration);
    Q_INVOKABLE bool applyConfiguration(const QVariantMap& configuration);

    bool isInitialized() const;
    QVariantMap currentConfiguration() const;
    QVariantMap appliedConfiguration() const;

signals:
    void initializedChanged();
    void currentConfigurationChanged();
    void appliedConfigurationChanged();
    void configurationSaved(const QVariantMap& configuration);
    void configurationApplied(const QVariantMap& configuration);
    void errorOccurred(const QString& message);

private:
    explicit RiskConfigService(QObject* parent = nullptr);

    QString configFilePath() const;
    void loadPersistedState();
    bool persistState();

    static RiskConfigService* m_instance;
    static QMutex m_instanceMutex;

    mutable QMutex m_mutex;
    bool m_initialized;
    QVariantMap m_currentConfiguration;
    QVariantMap m_appliedConfiguration;
};