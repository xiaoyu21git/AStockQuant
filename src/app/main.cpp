#include <QGuiApplication>
#include <QQuickWindow>
#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QObject>
#include <iostream>
#include "AppBootstrap.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <streambuf>
#include <string>
#include <mutex>

namespace {

// 仅对「真控制台」生效的 UTF-8 → UTF-16 → WriteConsoleW 输出缓冲。
// 目的：让 std::cout / std::clog(日志)的中文在控制台正确显示，
// 同时避开历史上两个大锤修复各自踩的雷：
//   - std::locale(UTF-8)  → 曾搞坏 libpq 连接串解析(commit e5cc17c 撤销)
//   - _setmode(_O_U8TEXT) → 曾搞坏 CRT 管道导致无法启动(commit 9ad8813 撤销)
// 本方案两者都不用：只在 GetConsoleMode 成功(真控制台)时接管 ostream；
// 管道/重定向/Git Bash 场景一个字节都不改，原样输出 UTF-8。
class Utf8ConsoleStreambuf : public std::streambuf {
public:
    explicit Utf8ConsoleStreambuf(HANDLE handle) : handle_(handle) {}

protected:
    int_type overflow(int_type c) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (c != traits_type::eof()) {
            char ch = static_cast<char>(c);
            buffer_.push_back(ch);
            if (ch == '\n') flushLocked();
        }
        return c;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.append(s, static_cast<size_t>(n));
        if (buffer_.find('\n') != std::string::npos || buffer_.size() >= 8192) flushLocked();
        return n;
    }
    int sync() override {
        std::lock_guard<std::mutex> lock(mutex_);
        flushLocked();
        return 0;
    }

private:
    void flushLocked() {
        if (buffer_.empty()) return;
        int wlen = MultiByteToWideChar(CP_UTF8, 0, buffer_.data(),
                                       static_cast<int>(buffer_.size()), nullptr, 0);
        if (wlen > 0) {
            std::wstring wide(static_cast<size_t>(wlen), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, buffer_.data(),
                                static_cast<int>(buffer_.size()), &wide[0], wlen);
            DWORD written = 0;
            WriteConsoleW(handle_, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr);
        }
        buffer_.clear();
    }

    HANDLE handle_;
    std::string buffer_;
    std::mutex mutex_;
};

// 仅当标准句柄是真正的控制台(非管道/重定向/文件)时返回 true
bool isRealConsole(DWORD stdHandleId) {
    HANDLE h = GetStdHandle(stdHandleId);
    if (h == nullptr || h == INVALID_HANDLE_VALUE) return false;
    DWORD mode = 0;
    return GetConsoleMode(h, &mode) != 0;
}

void enableUtf8ConsoleIfAttached() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // streambuf 对象刻意 new 且不释放：需活到程序结束(晚于 std::cout/std::clog 析构)，
    // 避免静态析构顺序导致悬垂。全程仅泄漏 1~2 个小对象。
    if (isRealConsole(STD_OUTPUT_HANDLE)) {
        std::cout.rdbuf(new Utf8ConsoleStreambuf(GetStdHandle(STD_OUTPUT_HANDLE)));
    }
    if (isRealConsole(STD_ERROR_HANDLE)) {
        auto* errBuf = new Utf8ConsoleStreambuf(GetStdHandle(STD_ERROR_HANDLE));
        std::cerr.rdbuf(errBuf);
        std::clog.rdbuf(errBuf);  // InternalLogger 默认写 std::clog
    }
}

} // namespace
#endif

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    enableUtf8ConsoleIfAttached();
#endif
    QCoreApplication::setOrganizationName("AStock");
    QCoreApplication::setOrganizationDomain("astock.com");
    QCoreApplication::setApplicationName("AStockQuantEngine");
    QApplication app(argc, argv);
    QDir::setCurrent(QCoreApplication::applicationDirPath());  // 工作目录 = exe 目录
    app.setWindowIcon(QIcon(QStringLiteral(":/resources/icons/app.ico")));

    AppBootstrap bootstrap;
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&bootstrap]() {
        std::cout << "[main] aboutToQuit: bootstrap.initialized=" << (bootstrap.isInitialized() ? "true" : "false")
                  << " bootstrap.started=" << (bootstrap.isStarted() ? "true" : "false")
                  << " lastError=" << bootstrap.lastError() << "\n";
    });
    QObject::connect(&app, &QGuiApplication::lastWindowClosed, []() {
        std::cout << "[main] lastWindowClosed emitted\n";
    });

    bootstrap.init();
    std::cout << "[main] bootstrap.init finished: initialized=" << (bootstrap.isInitialized() ? "true" : "false")
              << " lastError=" << bootstrap.lastError() << "\n";
    bootstrap.start();
    std::cout << "[main] bootstrap.start finished: started=" << (bootstrap.isStarted() ? "true" : "false")
              << " lastError=" << bootstrap.lastError() << "\n";

    int ret = app.exec();
    std::cout << "[main] app.exec returned: " << ret << "\n";

    bootstrap.shutdown();
    std::cout << "[main] shutdown complete\n";
    return ret;
}
