#include "registerQmlTypes.hpp"

#include <QQmlEngine>
#include <QStringList>
#include <QTimer>
#include "DataService.h"
#include "DataRuleService.h"
#include "CacheDetailPreviewModel.h"
#include "PreviewDataModel.h"
#include "DataFetchController.h"  // 添加DataFetchController头文件
#include "FactorViewModel.h"       // 因子视图模型（只负责视图）
#include "FactorDebugController.h" // 新增：因子调试控制器
#include "FactorMetaService.h"        // 新增：因子元数据服务
#include "CleanedDataController.h"    // 新增：清洗后数据控制器
#include "MarketDataBridge.h"         // 新增：行情桥接服务
#include "RiskControlBridge.h"        // 新增：风险控制服务
#include "PositionAccountBridge.h"   // 新增：持仓账户服务
#include "TradeExecutionBridge.h"    // 新增：交易执行服务
#include "TradingConnectionConfigService.h" // 新增：交易连接配置服务
#include "TradingMarketCalendarService.h" // 新增：交易市场日历桥接服务
#include "TradingFormPanelHelper.h"
#include "RuleTemplateDetailHelper.h"
#include "RuleTemplateSuggestionService.h" // 新增：规则模板建议桥接服务
#include "TradingRuntimeStatusService.h" // 新增：交易运行时状态桥接服务
//#include "UiLifecycleCoordinator.h"
#include "StrategyBridge.h"
#include "FactorService.h"
#include "FactorBacktestBridge.h"
#include "StrategyBacktestBridge.h"

namespace wang{

   void registerQmlTypes()
   {
      static const char* url = "AStock.Bridge";

      // 预览数据模型 - 专为预览窗口设计
      qmlRegisterType<PreviewDataModel>(url, 1, 0, "PreviewDataModel");
      qmlRegisterType<CacheDetailPreviewModel>(url, 1, 0, "CacheDetailPreviewModel");
          
      // 数据服务（替代旧的DataFetchController）- 使用新的极简DataService
      qmlRegisterType<DataService>(url, 1, 0, "DataService");
      
      // 规则服务
      qmlRegisterType<DataRuleService>(url, 1, 0, "DataRuleService");
      
      // DataFetchController - 用于数据获取和清洗，遵循不在QML中操作数据的原则
      qmlRegisterType<DataFetchController>(url, 1, 0, "DataFetchController");
      
      // FactorViewModel - 因子视图模型（只负责视图更新）
      qmlRegisterType<FactorViewModel>(url, 1, 0, "FactorViewModel");
      
      // FactorDebugController - 因子调试控制器
      qmlRegisterType<FactorDebugController>(url, 1, 0, "FactorDebugController");
       
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

      // RiskConfigService - 风险配置服务（单例模式，复用 RiskControlBridge）
      qmlRegisterSingletonType<RiskControlBridge>(
         url, 1, 0, "RiskConfigService",
         [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            return RiskControlBridge::instance();
         }
      );

      qmlRegisterSingletonType<MarketDataBridge>(
         url, 1, 0, "MarketDataBridge",
         [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            // MarketDataBridge 由 AppBootstrap 通过 setFacade 注入，无单例
            auto* bridge = new MarketDataBridge();
            return bridge;
         }
      );

      qmlRegisterSingletonType<RiskControlBridge>(
         url, 1, 0, "RiskControlBridge",
         [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            return RiskControlBridge::instance();
         }
      );

      qmlRegisterSingletonType<PositionAccountBridge>(
         url, 1, 0, "PositionAccountBridge",
         [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            return PositionAccountBridge::instance();
         }
      );

      qmlRegisterSingletonType<TradeExecutionBridge>(
         url, 1, 0, "TradeExecutionBridge",
         [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            return TradeExecutionBridge::instance();
         }
      );

      qmlRegisterSingletonType<TradingConnectionConfigService>(
         url, 1, 0, "TradingConnectionConfigService",
         [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            return TradingConnectionConfigService::instance();
         }
      );

      qmlRegisterSingletonType<TradingMarketCalendarService>(
         url, 1, 0, "TradingMarketCalendarService",
         [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            return TradingMarketCalendarService::instance();
         }
      );

      qmlRegisterSingletonType<TradingFormPanelHelper>(
         url, 1, 0, "TradingFormPanelHelper",
         [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            return TradingFormPanelHelper::instance();
         }
      );

      qmlRegisterSingletonType<RuleTemplateSuggestionService>(
         url, 1, 0, "RuleTemplateSuggestionService",
         [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            return RuleTemplateSuggestionService::instance();
         }
      );

      qmlRegisterSingletonType<RuleTemplateDetailHelper>(
         url, 1, 0, "RuleTemplateDetailHelper",
         [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            return RuleTemplateDetailHelper::instance();
         }
      );

      qmlRegisterSingletonType<TradingRuntimeStatusService>(
         url, 1, 0, "TradingRuntimeStatusService",
         [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            return TradingRuntimeStatusService::instance();
         }
      );

      // UiLifecycleCoordinator - UI生命周期协调器（待实现）
      // qmlRegisterSingletonType<UiLifecycleCoordinator>(...);

       qmlRegisterType<StrategyBridge>(url, 1, 0, "StrategyBridge");

      // FactorService - 因子服务桥接层（单例模式）
      qmlRegisterSingletonType<FactorService>(
         url, 1, 0, "FactorService",
         [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            auto* service = FactorService::instance();
            QTimer::singleShot(0, [service]() {
                service->initialize();
            });
            return service;
         }
      );

       // FactorBacktestController - 因子回测控制器（QML 内联组件）
       qmlRegisterType<FactorBacktestBridge>(
          url, 1, 0, "FactorBacktestController");

       // StrategyBacktestController - 策略回测控制器（QML 内联组件）
       qmlRegisterType<StrategyBacktestBridge>(
          url, 1, 0, "StrategyBacktestController");
   }
}
