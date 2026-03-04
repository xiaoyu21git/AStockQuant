#include "registerQmlTypes.hpp"

#include <QQmlEngine>
#include <QStringList>
//#include "BacktestController.h"
#include "DataService.h"
#include "DataSourceService.h"
#include "DataRuleService.h"
#include "DataPreviewService.h"
#include "DataCleaningService.h"
#include "PreviewDataModel.h"
#include "DataFetchController.h"  // 添加DataFetchController头文件

namespace wang{

   void registerQmlTypes()
   {
      static const char* url = "AStock.Bridge";

    // qmlRegisterType<BacktestController>(url, 1, 0, "BacktestController");
      // 预览数据模型 - 专为预览窗口设计
      qmlRegisterType<PreviewDataModel>(url, 1, 0, "PreviewDataModel");
          
      // 数据服务（替代旧的DataFetchController）- 使用新的极简DataService
      qmlRegisterType<DataService>(url, 1, 0, "DataService");
      
      // 数据源服务
      qmlRegisterType<DataSourceService>(url, 1, 0, "DataSourceService");
      
      // 规则服务
      qmlRegisterType<DataRuleService>(url, 1, 0, "DataRuleService");
      
      // 预览服务
      qmlRegisterType<DataPreviewService>(url, 1, 0, "DataPreviewService");
      
      // 清洗服务
      qmlRegisterType<DataCleaningService>(url, 1, 0, "DataCleaningService");
      
      // DataFetchController - 用于数据获取和清洗，遵循不在QML中操作数据的原则
      qmlRegisterType<DataFetchController>(url, 1, 0, "DataFetchController");
   }
}
