#include <QGuiApplication>
#include <QQuickWindow>
#include <QApplication>
#include <QObject>
#include <iostream>
#include "AppBootstrap.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

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
