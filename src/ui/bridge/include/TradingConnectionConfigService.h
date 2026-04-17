#pragma once

#include <QObject>
#include <QMutex>
#include <QStringList>
#include <QVariantMap>

class TradingConnectionConfigService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)
    Q_PROPERTY(QVariantMap currentConfiguration READ currentConfiguration NOTIFY currentConfigurationChanged)
    Q_PROPERTY(QString configFilePath READ configFilePath CONSTANT)
    Q_PROPERTY(bool hasToken READ hasToken NOTIFY currentConfigurationChanged)
    Q_PROPERTY(bool marketConnectorCompiled READ marketConnectorCompiled CONSTANT)
    Q_PROPERTY(QString marketConnectorBuildStatus READ marketConnectorBuildStatus CONSTANT)
    Q_PROPERTY(bool clientProcessRunning READ clientProcessRunning NOTIFY clientProcessStatusChanged)
    Q_PROPERTY(QString clientProcessStatus READ clientProcessStatus NOTIFY clientProcessStatusChanged)
    Q_PROPERTY(QStringList clientProcessNames READ clientProcessNames NOTIFY currentConfigurationChanged)

public:
    static TradingConnectionConfigService* instance();

    TradingConnectionConfigService(const TradingConnectionConfigService&) = delete;
    TradingConnectionConfigService& operator=(const TradingConnectionConfigService&) = delete;

    Q_INVOKABLE void initialize();
    Q_INVOKABLE void initializeAsync();
    Q_INVOKABLE QVariantMap loadConfiguration();
    Q_INVOKABLE QVariantMap defaultConfiguration() const;
    Q_INVOKABLE QStringList defaultClientProcessNames() const;
    Q_INVOKABLE bool saveConfiguration(const QVariantMap& configuration);
    Q_INVOKABLE QVariantMap evaluateStartupGate(bool requireClientProcess = false) const;
    Q_INVOKABLE QVariantMap bindStrategyConfiguration(const QString& strategyId,
                                                     const QString& strategyName,
                                                     bool enableTrading = true,
                                                     bool readOnly = false);
    Q_INVOKABLE QVariantMap addBoundStrategyConfiguration(const QString& strategyId,
                                                         const QString& strategyName,
                                                         bool enableTrading = true,
                                                         bool readOnly = false);
    Q_INVOKABLE QVariantMap removeBoundStrategyConfiguration(const QString& strategyId);
    Q_INVOKABLE void refreshClientProcessStatus();
    Q_INVOKABLE void refreshClientProcessStatusAsync();

    bool isInitialized() const;
    QVariantMap currentConfiguration() const;
    QString configFilePath() const;
    bool hasToken() const;
    bool marketConnectorCompiled() const;
    QString marketConnectorBuildStatus() const;
    bool clientProcessRunning() const;
    QString clientProcessStatus() const;
    QStringList clientProcessNames() const;

signals:
    void initializedChanged();
    void currentConfigurationChanged();
    void clientProcessStatusChanged();
    void configurationSaved(const QVariantMap& configuration);
    void errorOccurred(const QString& message);

private:
    explicit TradingConnectionConfigService(QObject* parent = nullptr);

    void loadPersistedState();
    bool persistState();
    QVariantMap evaluateStartupGateLocked(bool requireClientProcess) const;
    QVariantMap normalizedConfiguration(const QVariantMap& rawConfiguration) const;
    bool detectClientProcessLocked(QString* matchedProcessName = nullptr) const;

    static TradingConnectionConfigService* m_instance;
    static QMutex m_instanceMutex;

    mutable QMutex m_mutex;
    bool m_initialized;
    bool m_clientProcessRunning;
    QString m_clientProcessStatus;
    QVariantMap m_currentConfiguration;
};