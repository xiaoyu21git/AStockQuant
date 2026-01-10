#include "VasAurora.hpp"
#include "registerQmlTypes.hpp"

#include <QQmlEngine>

namespace wang {

VasAurora::VasAurora(QQmlApplicationEngine* engine)
    : engineM(engine)
{
    wang::registerQmlTypes();
    //qDebug() << engineM->importPathList();
    engineM->addImportPath("qrc:/");
    engineM->load(QUrl(QStringLiteral("qrc:/ConsoleUi/Qml/main.qml"))); 
}

}