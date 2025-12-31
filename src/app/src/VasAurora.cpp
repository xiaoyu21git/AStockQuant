#include "VasAurora.hpp"

// #include "VasNode.hpp"
 #include "registerQmlTypes.hpp"
 #include <QQmlEngine>  // 必须包含
namespace wang{
    VasAurora::VasAurora():engineM(std::make_unique<QQmlApplicationEngine>())
    {
        
        wang::qmgr::registerQmlTypes();
        engineM->addImportPath("qrc:/"); 
        engineM->load(QUrl(QStringLiteral("qrc:/main.qml")));
    }
}