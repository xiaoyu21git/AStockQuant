#include "VasAurora.hpp"
#include "registerQmlTypes.hpp"

#include <QQmlEngine>

namespace wang {

VasAurora::VasAurora(QQmlApplicationEngine* engine)
    : engineM(engine)
{
    wang::registerQmlTypes();
    engineM->addImportPath("qrc:/");
    engineM->load(QUrl(QStringLiteral("qrc:/main.qml")));
}

}