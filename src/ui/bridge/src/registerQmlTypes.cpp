#include "registerQmlTypes.hpp"
#include "foundation/log/logging.hpp"

#include <QQmlEngine>
#include <QStringList>
#include <QTimer>
#include "PreviewDataModel.h"
#include "DataFetchController.h"  // 添加DataFetchController头文件
#include "FactorViewModel.h"       // 因子视图模型（只负责视图）
#include "FactorDebugController.h" // 新增：因子调试控制器
#include "FactorMetaService.h"        // 新增：因子元数据服务
#include "CleanedDataController.h"    // 新增：清洗后数据控制器
#include "CandleDataModel.h"
#include "CrosshairManager.h"
#include "MarketDataBridge.h"
#include "StrategyBridge.h"
#include "FactorService.h"
#include "FactorBacktestBridge.h"
#include "StrategyBacktestBridge.h"
#include "TradingBridges.h"
#include "TradingConnectionConfigService.h"
#include "TradingRuntimeStatusService.h"
#include "TradingMarketCalendarService.h"
#include "RiskConfigService.h"
#include "TradingFormPanelHelper.h"
#include "UiLifecycleCoordinator.h"
#include "BacktestAnalyticsService.h"
#include "StrategyPerformanceModel.h"
#include "SymbolSearchModel.h"
#include "RuleTemplateDetailHelper.h"
#include "RuleTemplateSuggestionService.h"
#include "DataCleaningServiceRefactored.h"
#include "StrategyRuleStatsBridge.h"

namespace wang{

   void registerQmlTypes()
   {
      static const char* url = "AStock.Bridge";

      // 预览数据模型 - 专为预览窗口设计
      qmlRegisterType<PreviewDataModel>(url, 1, 0, "PreviewDataModel");
          
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
            // 同步初始化：只读缓存元数据索引，不加载数据文件，不会阻塞 UI
            controller->initialize();
            return controller;
         }
      );

      // 共享 CandleDataModel 实例
      auto* sharedModel = new bridge::CandleDataModel();

      qmlRegisterSingletonType<bridge::CandleDataModel>(
         url, 1, 0, "CandleDataModel",
         [sharedModel](QQmlEngine*, QJSEngine*) -> QObject* {
            return sharedModel;
         }
      );

      // 统一的行情桥接层 — 同时注册到两个 QML 名字, 兼容现有 QML 代码
      auto* marketBridge = new bridge::MarketDataBridge();
      marketBridge->setModel(sharedModel);
      marketBridge->initializeAsync();

      qmlRegisterSingletonInstance<bridge::MarketDataBridge>(
         url, 1, 0, "MarketDataBridge", marketBridge);

      qmlRegisterSingletonInstance<bridge::MarketDataBridge>(
         url, 1, 0, "StockDataLoader", marketBridge);

      // CrosshairManager — 十字光标状态 (单例, 引擎不接管生命周期)
      qmlRegisterSingletonInstance<bridge::CrosshairManager>(
         url, 1, 0, "CrosshairManager",
         &bridge::CrosshairManager::instance()
      );

       qmlRegisterSingletonType<StrategyBridge>(
          url, 1, 0, "StrategyBridge",
          [](QQmlEngine*, QJSEngine*) -> QObject* {
             INTERNAL_INFO_STREAM << "[QML] StrategyBridge singleton created";
             auto* bridge = new StrategyBridge();
             return bridge;
          });

      // FactorService - 因子服务桥接层（单例模式）
      // instance() 内部已同步调用 initialize()，无需 QTimer 延迟
      qmlRegisterSingletonType<FactorService>(
         url, 1, 0, "FactorService",
         [](QQmlEngine* engine, QJSEngine* scriptEngine) -> QObject* {
            Q_UNUSED(engine)
            Q_UNUSED(scriptEngine)
            auto* service = FactorService::instance();
            return service;
         }
      );

       // FactorBacktestController - 因子回测控制器（QML 内联组件）
       qmlRegisterType<FactorBacktestBridge>(
          url, 1, 0, "FactorBacktestController");

       // StrategyBacktestController - 策略回测控制器（QML 内联组件）
       qmlRegisterType<StrategyBacktestBridge>(
          url, 1, 0, "StrategyBacktestController");

       // ── 交易系统桥接层 (单例) ──
       qmlRegisterSingletonType<bridge::TradeExecutionBridge>(
          url, 1, 0, "TradeExecutionBridge",
          [](QQmlEngine*, QJSEngine*) -> QObject* {
             return new bridge::TradeExecutionBridge();
          });

       qmlRegisterSingletonType<bridge::PositionAccountBridge>(
          url, 1, 0, "PositionAccountBridge",
          [](QQmlEngine*, QJSEngine*) -> QObject* {
             return new bridge::PositionAccountBridge();
          });

       qmlRegisterSingletonType<bridge::RiskControlBridge>(
          url, 1, 0, "RiskControlBridge",
          [](QQmlEngine*, QJSEngine*) -> QObject* {
             return new bridge::RiskControlBridge();
          });

       // ── 新增桥接类型 (单例) ──
       qmlRegisterSingletonType<bridge::TradingConnectionConfigService>(
          url, 1, 0, "TradingConnectionConfigService",
          [](QQmlEngine*, QJSEngine*) -> QObject* {
             return bridge::TradingConnectionConfigService::instance();
          });

       qmlRegisterSingletonType<bridge::TradingRuntimeStatusService>(
          url, 1, 0, "TradingRuntimeStatusService",
          [](QQmlEngine*, QJSEngine*) -> QObject* {
             return new bridge::TradingRuntimeStatusService();
          });

       qmlRegisterSingletonType<bridge::TradingMarketCalendarService>(
          url, 1, 0, "TradingMarketCalendarService",
          [](QQmlEngine*, QJSEngine*) -> QObject* {
             return new bridge::TradingMarketCalendarService();
          });

       qmlRegisterSingletonType<bridge::RiskConfigService>(
          url, 1, 0, "RiskConfigService",
          [](QQmlEngine*, QJSEngine*) -> QObject* {
             return new bridge::RiskConfigService();
          });

       qmlRegisterSingletonType<bridge::TradingFormPanelHelper>(
          url, 1, 0, "TradingFormPanelHelper",
          [](QQmlEngine*, QJSEngine*) -> QObject* {
             return new bridge::TradingFormPanelHelper();
          });

       qmlRegisterSingletonType<bridge::UiLifecycleCoordinator>(
          url, 1, 0, "UiLifecycleCoordinator",
          [](QQmlEngine*, QJSEngine*) -> QObject* {
             return new bridge::UiLifecycleCoordinator();
          });

       qmlRegisterType<StrategyPerformanceModel>(
          url, 1, 0, "StrategyPerformanceModel");

       qmlRegisterType<SymbolSearchModel>(
          url, 1, 0, "SymbolSearchModel");

       // QML 侧按 singleton 用法直接调 RuleTemplateDetailHelper.describeBinding(),
       // 必须注册为 singleton (qmlRegisterType 会导致对类型名调方法 → TypeError)
       qmlRegisterSingletonType<RuleTemplateDetailHelper>(
          url, 1, 0, "RuleTemplateDetailHelper",
          [](QQmlEngine*, QJSEngine*) -> QObject* {
             return new RuleTemplateDetailHelper();
          });

       qmlRegisterSingletonType<RuleTemplateSuggestionService>(
          url, 1, 0, "RuleTemplateSuggestionService",
          [](QQmlEngine*, QJSEngine*) -> QObject* {
             return new RuleTemplateSuggestionService();
          });

       // BacktestAnalyticsService — 回测绩效分析
       qmlRegisterSingletonType<ui::bridge::BacktestAnalyticsService>(
          url, 1, 0, "BacktestAnalyticsService",
          [](QQmlEngine*, QJSEngine*) -> QObject* {
             auto* svc = new ui::bridge::BacktestAnalyticsService();
             svc->refreshRunList();
             return svc;
          });

       // DataCleaningServiceRefactored — 数据清洗服务
       qmlRegisterSingletonType<DataCleaningServiceRefactored>(
          url, 1, 0, "DataCleaningServiceRefactored",
          [](QQmlEngine*, QJSEngine*) -> QObject* {
             auto* svc = new DataCleaningServiceRefactored();
             svc->initialize();
             return svc;
          });

       // StrategyRuleStatsBridge — 策略规则统计桥接
       qmlRegisterSingletonType<StrategyRuleStatsBridge>(
          url, 1, 0, "StrategyRuleStatsBridge",
          [](QQmlEngine*, QJSEngine*) -> QObject* {
             return new StrategyRuleStatsBridge();
          });

   }
}
