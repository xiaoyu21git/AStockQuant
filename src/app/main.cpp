#include <QGuiApplication>
#include <QQuickWindow>
#include <QApplication>
#include "VasAurora.hpp"
#include <QtWebEngineQuick>

int main(int argc,char**argv) {
    QApplication app(argc, argv);
    wang::VasAurora ui;
    int result =app.exec();
    return result;
    return 0;
}
