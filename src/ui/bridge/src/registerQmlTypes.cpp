#include "registerQmlTypes.hpp"
#include <QQmlEngine>
#include <QStringList>
#
namespace wang{

   void registerQmlTypes()
   {
      // vas::Component::getInstance()->publisher()->init(true);
      static const char* url = "wang.qapi";
      // qmlRegisterType<QmlMediumlCtrl>(url,1,0,"MainCtrl");
      // qmlRegisterType<QmlWebToHtmlEdit>(url,1,0,"QmlWebToHtmlEdit");
   }
}