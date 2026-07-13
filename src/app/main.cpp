#include <QGuiApplication>
#include <QQuickWindow>
#include <QApplication>
#include <QIcon>
#include <QObject>
#include <iostream>
#include "AppBootstrap.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // CRT也走UTF-8，修复Foundation logger输出乱码
    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stderr), _O_U8TEXT);
#endif
    QCoreApplication::setOrganizationName("AStock");
    QCoreApplication::setOrganizationDomain("astock.com");
    QCoreApplication::setApplicationName("AStockQuantEngine");
    QApplication app(argc, argv);
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
