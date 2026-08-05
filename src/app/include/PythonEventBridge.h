#pragma once
// PythonEventBridge — QProcess 启动 Python 金融事件感知调度器
// App 启动时以子进程方式运行 astock_engine.events.scheduler,
// 输出通过 C++ 日志系统记录。

#include <QObject>
#include <QProcess>
#include <QString>

namespace app {

class PythonEventBridge : public QObject {
public:
    static PythonEventBridge& instance();

    /// @brief 启动 Python 事件调度子进程
    bool start();

    /// @brief 终止子进程
    void stop();

    bool isRunning() const;

private:
    PythonEventBridge();
    ~PythonEventBridge() override;

    void onReadyReadStdout();
    void onReadyReadStderr();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onError(QProcess::ProcessError error);

    QString findPython() const;
    QString findProjectRoot() const;

    QProcess* m_process = nullptr;
    QString m_pythonExe;
    QString m_projectRoot;
    bool m_started = false;
};

} // namespace app
