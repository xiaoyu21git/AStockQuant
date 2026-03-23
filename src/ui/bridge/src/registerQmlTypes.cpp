#include "registerQmlTypes.hpp"

#include <QQmlEngine>
#include <QStringList>
#include <QTimer>
//#include "BacktestController.h"
#include "DataService.h"
#include "DataSourceService.h"
#include "DataRuleService.h"
#include "DataPreviewService.h"
#include "DataCleaningService.h"
#include "PreviewDataModel.h"
#include "DataFetchController.h"  // 添加DataFetchController头文件
#include "FactorParamController.h" // 添加因子参数控制器头文件
#include "GlobalDataService.h"     // 新增：全局数据服务
#include "FactorViewModel.h"       // 因子视图模型（只负责视图）
#include "FactorService.h"         // 因子服务（业务逻辑）
#include "FactorDebugController.h" // 新增：因子调试控制器
#include "FactorBacktestController.h" // 新增：因子回测控制器
#include "FactorMetaService.h"        // 新增：因子元数据服务
#include "CleanedDataController.h"    // 新增：清洗后数据控制器
#include "DataCleaningEngine.h"       // 新增：数据清洗引擎
#include "StrategyBacktestController.h" // 新增：策略回测控制器
#include "StrategyService.h"           // 新增：策略服务
#include "StrategyViewModel.h"        // 新增：策略视图模型

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
      
      // FactorParamController - 因子参数配置控制器
      qmlRegisterType<FactorParamController>(url, 1, 0, "FactorParamController");
      
      // GlobalDataService - 全局数据服务（单例模式）
      qmlRegisterSingletonType<GlobalDataService>(
         url, 1, 0, "GlobalDataService",
         [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            return GlobalDataService::instance();
         }
      );
      
      // FactorViewModel - 因子视图模型（只负责视图更新）
      qmlRegisterType<FactorViewModel>(url, 1, 0, "FactorViewModel");
      
      // FactorService - 因子服务（负责业务逻辑）- 改为单例模式
      qmlRegisterSingletonType<FactorService>(
         url, 1, 0, "FactorService",
         [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            return FactorService::instance();
         }
      );
      
      // FactorDebugController - 因子调试控制器
      qmlRegisterType<FactorDebugController>(url, 1, 0, "FactorDebugController");
       
      // FactorBacktestController - 因子回测控制器
      qmlRegisterType<FactorBacktestController>(url, 1, 0, "FactorBacktestController");
      
      // FactorMetaService - 因子元数据服务（单例模式）
      qmlRegisterSingletonType<FactorMetaService>(
         url, 1, 0, "FactorMetaService",
         [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            auto* service = new FactorMetaService();
            service->initialize();
            return service;
         }
      );
      
      // CleanedDataController - 清洗后数据控制器（单例模式）
      qmlRegisterSingletonType<ui::bridge::CleanedDataController>(
         url, 1, 0, "CleanedDataController",
         [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            auto* controller = new ui::bridge::CleanedDataController();
            // 异步初始化，避免阻塞UI
            QTimer::singleShot(0, [controller]() {
                controller->initialize();
            });
            return controller;
         }
      );
      
      // DataCleaningEngine - 数据清洗引擎（单例模式）
      qmlRegisterSingletonType<DataCleaningEngine>(
         url, 1, 0, "DataCleaningEngine",
         [](QQmlEngine* qmlEngine, QJSEngine* scriptEngine) -> QObject* {
            Q_UNUSED(qmlEngine)
            Q_UNUSED(scriptEngine)
            auto* cleaningEngine = new DataCleaningEngine();
            return cleaningEngine;
         }
      );
      
      // StrategyBacktestController - 策略回测控制器
      qmlRegisterType<StrategyBacktestController>(url, 1, 0, "StrategyBacktestController");
      
      // StrategyService - 策略服务（单例模式）
      qmlRegisterSingletonType<StrategyService>(
         url, 1, 0, "StrategyService",
         [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            auto* service = StrategyService::instance();
            // 异步初始化，避免阻塞UI
            QTimer::singleShot(0, [service]() {
                service->initialize();
            });
            return service;
         }
      );
      
      // StrategyViewModel - 策略视图模型（只负责视图更新）
      qmlRegisterType<StrategyViewModel>(url, 1, 0, "StrategyViewModel");
   }
}
