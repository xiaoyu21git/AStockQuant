#include <QGuiApplication>
#include <QQuickWindow>
#include <QApplication>
#include <QIcon>
#include <QObject>
#include <iostream>
#include <locale>
#include <codecvt>
#include "AppBootstrap.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // 强制C++流也用UTF-8 (Windows 10 1903+)
    try { std::locale::global(std::locale(".UTF-8")); } catch(...) {}
    try { std::cout.imbue(std::locale(".UTF-8")); } catch(...) {}
    try { std::cerr.imbue(std::locale(".UTF-8")); } catch(...) {}
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
