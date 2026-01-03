#include <QGuiApplication>
#include <QQuickWindow>
#include <QApplication>
#include "AppBootstrap.h"

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);

    AppBootstrap bootstrap;
    bootstrap.init();
    bootstrap.start();

    int ret = app.exec();

    bootstrap.shutdown();
    return ret;
}
