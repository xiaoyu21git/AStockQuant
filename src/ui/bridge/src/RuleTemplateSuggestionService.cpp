#include "RuleTemplateSuggestionService.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutexLocker>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>
#include <QTimer>
#include <QUuid>

namespace {

constexpr int kWorkerStartupTimeoutMs = 8000;
constexpr int kRequestTimeoutMs = 12000;

QString resolveRepositoryRoot()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 8; ++depth) {
        if (dir.exists(QStringLiteral("astock_engine/rule_template_bridge_worker.py"))
            && dir.exists(QStringLiteral("astock_engine"))
            && dir.exists(QStringLiteral("src"))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return {};
}

QString resolvePythonExecutable()
{
    const QString configured = qEnvironmentVariable("ASTOCK_PYTHON_EXECUTABLE").trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }
    return QStringLiteral("python");
}

QString resolveWorkerScriptPath(const QString& repoRoot)
{
    if (repoRoot.isEmpty()) {
        return {};
    }

    const QFileInfo workerScript(QDir(repoRoot).filePath(
        QStringLiteral("astock_engine/rule_template_bridge_worker.py")));
    return workerScript.exists() ? workerScript.canonicalFilePath() : QString();
}

QVariantMap startFailurePayload(const QString& errorCode, const QString& error)
{
    return QVariantMap{
        {QStringLiteral("success"), false},
        {QStringLiteral("errorCode"), errorCode},
        {QStringLiteral("error"), error},
    };
}

QString withStderrSuffix(const QString& message, const QByteArray& stderrBuffer)
{
    const QString stderrText = QString::fromUtf8(stderrBuffer).trimmed();
    if (stderrText.isEmpty()) {
        return message;
    }
    return message + QStringLiteral(": ") + stderrText;
}

} // namespace

struct RuleTemplateSuggestionService::PendingRequest {
    QString requestId;
    QString correlationId;
    bool emitSignals{true};
    bool finished{false};
    QVariantMap result;
    QEventLoop* waitLoop{nullptr};
    QTimer* timeoutTimer{nullptr};
};

RuleTemplateSuggestionService* RuleTemplateSuggestionService::m_instance = nullptr;
QMutex RuleTemplateSuggestionService::m_instanceMutex;

RuleTemplateSuggestionService* RuleTemplateSuggestionService::instance()
{
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        QCoreApplication* app = QCoreApplication::instance();
        if (app && QThread::currentThread() != app->thread()) {
            QMetaObject::invokeMethod(app, [app]() {
                if (!m_instance) {
                    m_instance = new RuleTemplateSuggestionService(app);
                }
            }, Qt::BlockingQueuedConnection);
        } else {
            m_instance = new RuleTemplateSuggestionService(app);
        }
    }
    return m_instance;
}

RuleTemplateSuggestionService::RuleTemplateSuggestionService(QObject* parent)
    : QObject(parent)
    , m_initialized(false)
    , m_workerReady(false)
    , m_activeRequestCount(0)
    , m_workerProcess(nullptr)
{
}

RuleTemplateSuggestionService::~RuleTemplateSuggestionService()
{
    resetWorkerProcess();
}

void RuleTemplateSuggestionService::initialize()
{
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_initialized) {
            m_initialized = true;
            changed = true;
        }
    }

    if (changed) {
        emit initializedChanged();
    }
}

bool RuleTemplateSuggestionService::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized;
}

bool RuleTemplateSuggestionService::isBusy() const
{
    QMutexLocker locker(&m_mutex);
    return m_activeRequestCount > 0;
}

QString RuleTemplateSuggestionService::lastError() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastError;
}

QVariantMap RuleTemplateSuggestionService::suggestTemplates(const QString& text, const QVariantMap& options)
{
    QVariantMap request = options;
    request.insert(QStringLiteral("text"), text.trimmed());
    return suggestTemplatesRequest(request);
}

QVariantMap RuleTemplateSuggestionService::suggestTemplatesRequest(const QVariantMap& request)
{
    initialize();
    return executeRequest(request);
}

QString RuleTemplateSuggestionService::suggestTemplatesAsync(const QString& text, const QVariantMap& options)
{
    QVariantMap request = options;
    request.insert(QStringLiteral("text"), text.trimmed());
    return suggestTemplatesRequestAsync(request);
}

QString RuleTemplateSuggestionService::suggestTemplatesRequestAsync(const QVariantMap& request)
{
    initialize();

    const QVariantMap preparedRequest = prepareRequest(request);
    const QString requestId = preparedRequest.value(QStringLiteral("requestId")).toString();
    const QString correlationId = preparedRequest.value(QStringLiteral("correlationId")).toString();

    QVariantMap errorResult;
    if (!ensureWorkerProcess(&errorResult)) {
        errorResult.insert(QStringLiteral("requestId"), requestId);
        errorResult.insert(QStringLiteral("correlationId"), correlationId);
        setLastError(errorResult.value(QStringLiteral("error")).toString());
        emit suggestionFailed(errorResult);
        return requestId;
    }

    const auto pendingRequest = createPendingRequest(preparedRequest, true);
    registerPendingRequest(pendingRequest);
    if (!sendRequestToWorker(preparedRequest, &errorResult)) {
        errorResult.insert(QStringLiteral("requestId"), requestId);
        errorResult.insert(QStringLiteral("correlationId"), correlationId);
        completeRequest(errorResult, pendingRequest, false);
    }
    return requestId;
}

QVariantMap RuleTemplateSuggestionService::executeRequest(const QVariantMap& request)
{
    const QVariantMap preparedRequest = prepareRequest(request);
    const QString requestId = preparedRequest.value(QStringLiteral("requestId")).toString();
    const QString correlationId = preparedRequest.value(QStringLiteral("correlationId")).toString();

    QVariantMap errorResult;
    if (!ensureWorkerProcess(&errorResult)) {
        errorResult.insert(QStringLiteral("requestId"), requestId);
        errorResult.insert(QStringLiteral("correlationId"), correlationId);
        setLastError(errorResult.value(QStringLiteral("error")).toString());
        return errorResult;
    }

    const auto pendingRequest = createPendingRequest(preparedRequest, false);
    QEventLoop loop;
    pendingRequest->waitLoop = &loop;

    registerPendingRequest(pendingRequest);
    if (!sendRequestToWorker(preparedRequest, &errorResult)) {
        errorResult.insert(QStringLiteral("requestId"), requestId);
        errorResult.insert(QStringLiteral("correlationId"), correlationId);
        completeRequest(errorResult, pendingRequest, false);
    }

    if (!pendingRequest->finished) {
        loop.exec();
    }

    return pendingRequest->result;
}

QVariantMap RuleTemplateSuggestionService::prepareRequest(const QVariantMap& request) const
{
    QVariantMap preparedRequest = request;
    const QString text = preparedRequest.value(QStringLiteral("text")).toString().trimmed();
    preparedRequest.insert(QStringLiteral("text"), text);

    QString requestId = preparedRequest.value(QStringLiteral("requestId")).toString().trimmed();
    if (requestId.isEmpty()) {
        requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        preparedRequest.insert(QStringLiteral("requestId"), requestId);
    }

    QString correlationId = preparedRequest.value(QStringLiteral("correlationId")).toString().trimmed();
    if (correlationId.isEmpty()) {
        correlationId = requestId;
        preparedRequest.insert(QStringLiteral("correlationId"), correlationId);
    }

    return preparedRequest;
}

bool RuleTemplateSuggestionService::ensureWorkerProcess(QVariantMap* errorResult)
{
    if (m_workerProcess && m_workerProcess->state() == QProcess::Running && m_workerReady) {
        return true;
    }

    resetWorkerProcess();

    const QString repoRoot = resolveRepositoryRoot();
    if (repoRoot.isEmpty()) {
        if (errorResult) {
            *errorResult = startFailurePayload(
                QStringLiteral("rule_template_bridge_repo_root_missing"),
                QStringLiteral("unable to resolve repository root"));
        }
        return false;
    }

    const QString workerScriptPath = resolveWorkerScriptPath(repoRoot);
    if (workerScriptPath.isEmpty()) {
        if (errorResult) {
            *errorResult = startFailurePayload(
                QStringLiteral("rule_template_bridge_worker_missing"),
                QStringLiteral("unable to resolve python worker script"));
        }
        return false;
    }

    m_workerProcess = new QProcess(this);
    m_workerReady = false;
    m_workerStdoutBuffer.clear();
    m_workerStderrBuffer.clear();

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString existingPythonPath = environment.value(QStringLiteral("PYTHONPATH"));
    environment.insert(
        QStringLiteral("PYTHONPATH"),
        existingPythonPath.isEmpty() ? repoRoot : repoRoot + QStringLiteral(";") + existingPythonPath);
    environment.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));

    m_workerProcess->setProcessEnvironment(environment);
    m_workerProcess->setWorkingDirectory(repoRoot);
    m_workerProcess->setProgram(resolvePythonExecutable());
    m_workerProcess->setArguments(
        {QStringLiteral("-X"),
         QStringLiteral("utf8"),
         workerScriptPath,
         repoRoot});

    connect(m_workerProcess, &QProcess::readyReadStandardOutput, this, [this]() { handleWorkerStdout(); });
    connect(m_workerProcess, &QProcess::readyReadStandardError, this, [this]() { handleWorkerStderr(); });
    connect(m_workerProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                QString message = exitStatus == QProcess::NormalExit
                    ? QStringLiteral("python worker exited")
                    : QStringLiteral("python worker crashed");
                if (exitCode != 0) {
                    message += QStringLiteral(" (exit code %1)").arg(exitCode);
                }
                handleWorkerTransportFailure(QStringLiteral("rule_template_bridge_worker_exited"), message);
            });
    connect(m_workerProcess,
            &QProcess::errorOccurred,
            this,
            [this](QProcess::ProcessError) {
                const QString message = m_workerProcess
                    ? m_workerProcess->errorString()
                    : QStringLiteral("python worker process error");
                handleWorkerTransportFailure(QStringLiteral("rule_template_bridge_process_error"), message);
            });

    m_workerProcess->start();
    if (!m_workerProcess->waitForStarted(kWorkerStartupTimeoutMs)) {
        const QByteArray startupStderr = m_workerProcess ? m_workerProcess->readAllStandardError() : QByteArray{};
        const QString message = withStderrSuffix(
            QStringLiteral("python worker failed to start"),
            startupStderr);
        if (errorResult) {
            *errorResult = startFailurePayload(QStringLiteral("rule_template_bridge_start_failed"), message);
        }
        resetWorkerProcess();
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (!m_workerReady && timer.elapsed() < kWorkerStartupTimeoutMs) {
        const int remainingMs = kWorkerStartupTimeoutMs - static_cast<int>(timer.elapsed());
        if (remainingMs <= 0) {
            break;
        }

        m_workerProcess->waitForReadyRead(remainingMs);
        handleWorkerStdout();
        handleWorkerStderr();
        if (!m_workerProcess) {
            break;
        }
        if (m_workerProcess->state() != QProcess::Running) {
            break;
        }
    }

    if (!m_workerReady) {
        const QString message = withStderrSuffix(
            QStringLiteral("python worker did not become ready"),
            m_workerStderrBuffer);
        if (errorResult) {
            *errorResult = startFailurePayload(QStringLiteral("rule_template_bridge_start_failed"), message);
        }
        resetWorkerProcess();
        return false;
    }

    return true;
}

bool RuleTemplateSuggestionService::sendRequestToWorker(const QVariantMap& request, QVariantMap* errorResult)
{
    if (!m_workerProcess || m_workerProcess->state() != QProcess::Running || !m_workerReady) {
        if (errorResult) {
            *errorResult = startFailurePayload(
                QStringLiteral("rule_template_bridge_worker_unavailable"),
                QStringLiteral("python worker is not available"));
        }
        return false;
    }

    QByteArray requestJson = QJsonDocument::fromVariant(request).toJson(QJsonDocument::Compact);
    requestJson.append('\n');
    if (m_workerProcess->write(requestJson) == -1) {
        if (errorResult) {
            *errorResult = startFailurePayload(
                QStringLiteral("rule_template_bridge_write_failed"),
                withStderrSuffix(m_workerProcess->errorString(), m_workerStderrBuffer));
        }
        handleWorkerTransportFailure(QStringLiteral("rule_template_bridge_write_failed"), m_workerProcess->errorString());
        return false;
    }

    if (!m_workerProcess->waitForBytesWritten(1000)) {
        if (errorResult) {
            *errorResult = startFailurePayload(
                QStringLiteral("rule_template_bridge_write_failed"),
                withStderrSuffix(QStringLiteral("python worker write timed out"), m_workerStderrBuffer));
        }
        handleWorkerTransportFailure(
            QStringLiteral("rule_template_bridge_write_failed"),
            QStringLiteral("python worker write timed out"));
        return false;
    }

    return true;
}

std::shared_ptr<RuleTemplateSuggestionService::PendingRequest>
RuleTemplateSuggestionService::createPendingRequest(const QVariantMap& request, bool emitSignals)
{
    auto pendingRequest = std::make_shared<PendingRequest>();
    pendingRequest->requestId = request.value(QStringLiteral("requestId")).toString();
    pendingRequest->correlationId = request.value(QStringLiteral("correlationId")).toString();
    pendingRequest->emitSignals = emitSignals;
    pendingRequest->timeoutTimer = new QTimer(this);
    pendingRequest->timeoutTimer->setSingleShot(true);
    connect(pendingRequest->timeoutTimer,
            &QTimer::timeout,
            this,
            [this]() {
                handleWorkerTransportFailure(
                    QStringLiteral("rule_template_bridge_timeout"),
                    QStringLiteral("python worker timed out"));
            });
    return pendingRequest;
}

void RuleTemplateSuggestionService::registerPendingRequest(const std::shared_ptr<PendingRequest>& pendingRequest)
{
    {
        QMutexLocker locker(&m_mutex);
        m_pendingRequests.insert(pendingRequest->requestId, pendingRequest);
        ++m_activeRequestCount;
    }
    pendingRequest->timeoutTimer->start(kRequestTimeoutMs);
    emit busyChanged();
}

void RuleTemplateSuggestionService::handleWorkerStdout()
{
    if (!m_workerProcess) {
        return;
    }

    m_workerStdoutBuffer.append(m_workerProcess->readAllStandardOutput());
    while (true) {
        const int newlineIndex = m_workerStdoutBuffer.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }

        QByteArray line = m_workerStdoutBuffer.left(newlineIndex).trimmed();
        m_workerStdoutBuffer.remove(0, newlineIndex + 1);
        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (!document.isObject()) {
            if (!m_workerReady) {
                continue;
            }
            handleWorkerTransportFailure(
                QStringLiteral("rule_template_bridge_parse_error"),
                QStringLiteral("worker returned invalid JSON: %1").arg(parseError.errorString()));
            return;
        }

        const QVariantMap message = document.object().toVariantMap();
        const QString kind = message.value(QStringLiteral("kind")).toString();
        if (kind == QStringLiteral("ready")) {
            m_workerReady = true;
            continue;
        }
        if (kind != QStringLiteral("response") && kind != QStringLiteral("error")) {
            if (!m_workerReady) {
                continue;
            }
            handleWorkerTransportFailure(
                QStringLiteral("rule_template_bridge_protocol_error"),
                QStringLiteral("worker returned unsupported message kind"));
            return;
        }

        QVariantMap payload = message.value(QStringLiteral("payload")).toMap();
        QString requestId = payload.value(QStringLiteral("requestId")).toString().trimmed();
        if (requestId.isEmpty()) {
            requestId = message.value(QStringLiteral("requestId")).toString().trimmed();
        }
        if (requestId.isEmpty()) {
            handleWorkerTransportFailure(
                QStringLiteral("rule_template_bridge_protocol_error"),
                QStringLiteral("worker response missing requestId"));
            return;
        }

        std::shared_ptr<PendingRequest> pendingRequest;
        {
            QMutexLocker locker(&m_mutex);
            pendingRequest = m_pendingRequests.value(requestId);
        }
        if (!pendingRequest) {
            continue;
        }

        completeRequest(payload, pendingRequest, kind == QStringLiteral("response"));
    }
}

void RuleTemplateSuggestionService::handleWorkerStderr()
{
    if (!m_workerProcess) {
        return;
    }

    m_workerStderrBuffer.append(m_workerProcess->readAllStandardError());
    if (m_workerStderrBuffer.size() > 16384) {
        m_workerStderrBuffer = m_workerStderrBuffer.right(16384);
    }
}

void RuleTemplateSuggestionService::handleWorkerTransportFailure(const QString& errorCode,
                                                                 const QString& errorMessage)
{
    QList<std::shared_ptr<PendingRequest>> pendingRequests;
    {
        QMutexLocker locker(&m_mutex);
        pendingRequests = m_pendingRequests.values();
    }

    const QString fullMessage = withStderrSuffix(errorMessage, m_workerStderrBuffer);
    for (const auto& pendingRequest : pendingRequests) {
        QVariantMap payload = startFailurePayload(errorCode, fullMessage);
        payload.insert(QStringLiteral("requestId"), pendingRequest->requestId);
        payload.insert(QStringLiteral("correlationId"), pendingRequest->correlationId);
        completeRequest(payload, pendingRequest, false);
    }

    if (pendingRequests.isEmpty()) {
        setLastError(fullMessage);
    }

    resetWorkerProcess();
}

void RuleTemplateSuggestionService::resetWorkerProcess()
{
    if (!m_workerProcess) {
        m_workerReady = false;
        m_workerStdoutBuffer.clear();
        m_workerStderrBuffer.clear();
        return;
    }

    disconnect(m_workerProcess, nullptr, this, nullptr);
    if (m_workerProcess->state() != QProcess::NotRunning) {
        const QByteArray shutdownCommand = QByteArrayLiteral("{\"command\":\"shutdown\"}\n");
        m_workerProcess->write(shutdownCommand);
        m_workerProcess->waitForBytesWritten(250);
        m_workerProcess->terminate();
        if (!m_workerProcess->waitForFinished(500)) {
            m_workerProcess->kill();
            m_workerProcess->waitForFinished(500);
        }
    }

    m_workerProcess->deleteLater();
    m_workerProcess = nullptr;
    m_workerReady = false;
    m_workerStdoutBuffer.clear();
    m_workerStderrBuffer.clear();
}

void RuleTemplateSuggestionService::completeRequest(const QVariantMap& payload,
                                                    const std::shared_ptr<PendingRequest>& pendingRequest,
                                                    bool success)
{
    if (!pendingRequest || pendingRequest->finished) {
        return;
    }

    QVariantMap result = payload;
    if (!result.contains(QStringLiteral("requestId"))) {
        result.insert(QStringLiteral("requestId"), pendingRequest->requestId);
    }
    if (!result.contains(QStringLiteral("correlationId"))) {
        result.insert(QStringLiteral("correlationId"), pendingRequest->correlationId);
    }
    result.insert(QStringLiteral("success"), success);

    pendingRequest->finished = true;
    pendingRequest->result = result;
    if (pendingRequest->timeoutTimer) {
        pendingRequest->timeoutTimer->stop();
        pendingRequest->timeoutTimer->deleteLater();
        pendingRequest->timeoutTimer = nullptr;
    }

    if (success) {
        setLastError(QString());
    } else {
        setLastError(result.value(QStringLiteral("error")).toString());
    }

    if (pendingRequest->waitLoop && pendingRequest->waitLoop->isRunning()) {
        pendingRequest->waitLoop->quit();
    }

    releasePendingRequest(pendingRequest->requestId);

    if (!pendingRequest->emitSignals) {
        return;
    }

    if (success) {
        emit suggestionReady(result);
    } else {
        emit suggestionFailed(result);
    }
}

void RuleTemplateSuggestionService::releasePendingRequest(const QString& requestId)
{
    bool busyChangedNeeded = false;
    {
        QMutexLocker locker(&m_mutex);
        const int removedCount = m_pendingRequests.remove(requestId);
        if (removedCount != 0 && m_activeRequestCount > 0) {
            --m_activeRequestCount;
            busyChangedNeeded = true;
        }
    }

    if (busyChangedNeeded) {
        emit busyChanged();
    }
}

void RuleTemplateSuggestionService::setLastError(const QString& errorMessage)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_lastError != errorMessage) {
            m_lastError = errorMessage;
            changed = true;
        }
    }

    if (changed) {
        emit lastErrorChanged();
    }
}