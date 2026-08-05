#include "PythonEventBridge.h"
#include "foundation/log/logging.hpp"

#include <QDir>
#include <QFileInfo>

#ifdef _WIN32
#include <windows.h>
#endif

namespace app {

static std::string getExeDir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return ".";
    std::string path(buf, len);
    auto pos = path.rfind('\\');
    return (pos != std::string::npos) ? path.substr(0, pos) : ".";
#else
    return ".";
#endif
}

PythonEventBridge& PythonEventBridge::instance() {
    static PythonEventBridge s;
    return s;
}

PythonEventBridge::PythonEventBridge() {
    m_pythonExe = findPython();
    m_projectRoot = findProjectRoot();
}

PythonEventBridge::~PythonEventBridge() { stop(); }

QString PythonEventBridge::findPython() const {
#ifdef PYTHON_EXECUTABLE
    QString py = QString::fromLocal8Bit(PYTHON_EXECUTABLE);
    if (QFileInfo::exists(py)) return py;
#endif
    return QStringLiteral("python");  // PATH 中查找
}

QString PythonEventBridge::findProjectRoot() const {
    QString exeDir = QString::fromStdString(getExeDir());
    // exeDir = G:\C++\AStockQuantEngine\bin\Release
    // projectRoot = exeDir/../../
    QDir dir(exeDir);
    dir.cdUp();  // bin
    dir.cdUp();  // project root
    return dir.absolutePath();
}

bool PythonEventBridge::start() {
    if (m_started && m_process && m_process->state() == QProcess::Running)
        return true;

    if (m_process) {
        delete m_process;
        m_process = nullptr;
    }

    m_process = new QProcess(this);

    // 转发 Python 输出到 C++ 日志 (lambda 连接, 无需 MOC)
    QObject::connect(m_process, &QProcess::readyReadStandardOutput,
                     this, &PythonEventBridge::onReadyReadStdout);
    QObject::connect(m_process, &QProcess::readyReadStandardError,
                     this, &PythonEventBridge::onReadyReadStderr);
    QObject::connect(m_process, &QProcess::finished,
                     this, &PythonEventBridge::onFinished);
    QObject::connect(m_process, &QProcess::errorOccurred,
                     this, &PythonEventBridge::onError);

    // 设置环境: PYTHONPATH 指向项目根目录, 使 astock_engine 包可被导入
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONPATH", m_projectRoot);
    env.insert("PYTHONIOENCODING", "utf-8");   // 强制 Python 输出 UTF-8
    env.insert("PYTHONUTF8", "1");             // Python 3.7+ UTF-8 模式
    env.insert("TQDM_DISABLE", "1");           // 禁用 akshare 进度条
    // 禁用 HanLP GPU 加速，避免启动时 CUDA 初始化延迟
    env.insert("HANLP_USE_CUDA", "0");
    m_process->setProcessEnvironment(env);
    m_process->setWorkingDirectory(m_projectRoot);

    INTERNAL_INFO_STREAM << "[PyBridge] 启动子进程: " << m_pythonExe.toStdString()
                         << " -m astock_engine.events.scheduler"
                         << "  PYTHONPATH=" << m_projectRoot.toStdString();

    m_process->start(m_pythonExe, {"-m", "astock_engine.events.scheduler"});

    if (!m_process->waitForStarted(5000)) {
        INTERNAL_ERROR_STREAM << "[PyBridge] 启动失败: "
                              << m_process->errorString().toStdString();
        delete m_process;
        m_process = nullptr;
        return false;
    }

    m_started = true;
    INTERNAL_INFO_STREAM << "[PyBridge] 子进程已启动 PID=" << m_process->processId();
    return true;
}

void PythonEventBridge::stop() {
    if (!m_process) return;

    m_started = false;

    if (m_process->state() == QProcess::Running) {
        INTERNAL_INFO_STREAM << "[PyBridge] 正在终止子进程...";
        m_process->terminate();  // SIGTERM
        if (!m_process->waitForFinished(5000)) {
            INTERNAL_WARN_STREAM << "[PyBridge] 进程未响应 SIGTERM, 强制终止";
            m_process->kill();
            m_process->waitForFinished(3000);
        }
        INTERNAL_INFO_STREAM << "[PyBridge] 子进程已终止";
    }

    delete m_process;
    m_process = nullptr;
}

bool PythonEventBridge::isRunning() const {
    return m_started && m_process && m_process->state() == QProcess::Running;
}

// ── Slots ──

void PythonEventBridge::onReadyReadStdout() {
    if (!m_process) return;
    QString text = QString::fromUtf8(m_process->readAllStandardOutput()).trimmed();
    if (!text.isEmpty()) {
        // 逐行输出，挂到 C++ 日志流
        const QStringList lines = text.split('\n');
        for (const auto& line : lines) {
            if (!line.trimmed().isEmpty())
                INTERNAL_INFO_STREAM << "[PyScheduler] " << line.toStdString();
        }
    }
}

void PythonEventBridge::onReadyReadStderr() {
    if (!m_process) return;
    QString text = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
    if (!text.isEmpty()) {
        const QStringList lines = text.split('\n');
        for (const auto& line : lines) {
            const QString t = line.trimmed();
            if (t.isEmpty()) continue;
            // 过滤第三方库噪音
            if (t.contains("FutureWarning") || t.contains("pynvml") ||
                t.contains("clean_up_tokenization") || t.contains("Building model") ||
                t.contains("%|") || t.contains("it/s"))  // tqdm progress bar
                continue;
            // WARNING/ERROR → WARN; 其余 → DEBUG
            if (t.contains("WARNING") || t.contains("ERROR") || t.contains("异常") || t.contains("失败"))
                INTERNAL_WARN_STREAM << "[Py] " << t.toStdString();
            else
                INTERNAL_DEBUG_STREAM << "[Py] " << t.toStdString();
        }
    }
}

void PythonEventBridge::onFinished(int exitCode, QProcess::ExitStatus status) {
    if (status == QProcess::CrashExit) {
        INTERNAL_ERROR_STREAM << "[PyBridge] 进程崩溃 exitCode=" << exitCode;
    } else if (exitCode != 0) {
        INTERNAL_WARN_STREAM << "[PyBridge] 进程退出 exitCode=" << exitCode;
    } else {
        INTERNAL_INFO_STREAM << "[PyBridge] 进程正常退出";
    }
    m_started = false;
}

void PythonEventBridge::onError(QProcess::ProcessError error) {
    QString msg = m_process ? m_process->errorString() : QStringLiteral("unknown");
    INTERNAL_ERROR_STREAM << "[PyBridge] 进程错误: " << error
                          << " " << msg.toStdString();
}

} // namespace app
