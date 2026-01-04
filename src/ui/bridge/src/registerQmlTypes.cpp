#include "registerQmlTypes.hpp"
#include <QQmlEngine>
#include <QStringList>
#include "TradeRecordModel.h"
namespace wang{

   void registerQmlTypes()
   {
      static const char* url = "AStock.Engine";
      qmlRegisterType<TradeRecordModel>(url, 1, 0, "TradeRecordModel");
   }
}