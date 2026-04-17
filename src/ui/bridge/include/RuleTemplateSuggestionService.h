#pragma once

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QVariantMap>

#include <memory>

class QEventLoop;
class QProcess;
class QTimer;

class RuleTemplateSuggestionService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    static RuleTemplateSuggestionService* instance();
    ~RuleTemplateSuggestionService() override;

    RuleTemplateSuggestionService(const RuleTemplateSuggestionService&) = delete;
    RuleTemplateSuggestionService& operator=(const RuleTemplateSuggestionService&) = delete;

    Q_INVOKABLE void initialize();
    Q_INVOKABLE bool isInitialized() const;
    Q_INVOKABLE bool isBusy() const;
    Q_INVOKABLE QString lastError() const;
    Q_INVOKABLE QVariantMap suggestTemplates(const QString& text,
                                            const QVariantMap& options = QVariantMap());
    Q_INVOKABLE QVariantMap suggestTemplatesRequest(const QVariantMap& request);
    Q_INVOKABLE QString suggestTemplatesAsync(const QString& text,
                                             const QVariantMap& options = QVariantMap());
    Q_INVOKABLE QString suggestTemplatesRequestAsync(const QVariantMap& request);

signals:
    void initializedChanged();
    void busyChanged();
    void lastErrorChanged();
    void suggestionReady(const QVariantMap& result);
    void suggestionFailed(const QVariantMap& error);

private:
    struct PendingRequest;

    explicit RuleTemplateSuggestionService(QObject* parent = nullptr);
    QVariantMap executeRequest(const QVariantMap& request);
    QVariantMap prepareRequest(const QVariantMap& request) const;
    bool ensureWorkerProcess(QVariantMap* errorResult);
    bool sendRequestToWorker(const QVariantMap& request, QVariantMap* errorResult);
    std::shared_ptr<PendingRequest> createPendingRequest(const QVariantMap& request, bool emitSignals);
    void registerPendingRequest(const std::shared_ptr<PendingRequest>& pendingRequest);
    void handleWorkerStdout();
    void handleWorkerStderr();
    void handleWorkerTransportFailure(const QString& errorCode, const QString& errorMessage);
    void resetWorkerProcess();
    void completeRequest(const QVariantMap& payload,
                         const std::shared_ptr<PendingRequest>& pendingRequest,
                         bool success);
    void releasePendingRequest(const QString& requestId);
    void setLastError(const QString& errorMessage);

    static RuleTemplateSuggestionService* m_instance;
    static QMutex m_instanceMutex;

    mutable QMutex m_mutex;
    bool m_initialized;
    bool m_workerReady;
    int m_activeRequestCount;
    QString m_lastError;
    QProcess* m_workerProcess;
    QByteArray m_workerStdoutBuffer;
    QByteArray m_workerStderrBuffer;
    QHash<QString, std::shared_ptr<PendingRequest>> m_pendingRequests;
};