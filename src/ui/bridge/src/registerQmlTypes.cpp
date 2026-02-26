#include "registerQmlTypes.hpp"

#include <QQmlEngine>
#include <QStringList>

#include "TradeRecordModel.h"
#include "EquityCurveModel.h"
#include "BacktestController.h"
#include "GlobalModels.h"
#include "HighPositionModel.h"
#include "LiveAccountModel.h"
#include "LiveActionLogModel.h"
#include "LivePositionModel.h"
#include "GlobalState.h"
#include "DataService.h"
#include "DataSourceService.h"
#include "DataRuleService.h"
#include "DataPreviewService.h"
#include "DataCleaningService.h"

namespace wang{

   void registerQmlTypes()
   {
      static const char* url = "AStock.Bridge";
    qmlRegisterType<TradeRecordModel>(url, 1, 0, "TradeRecordModel");
    qmlRegisterType<EquityCurveModel>(url, 1, 0, "EquityCurveModel");
    qmlRegisterType<BacktestController>(url, 1, 0, "BacktestController");
    qmlRegisterType<HighPositionModel>(url, 1, 0, "HighPositionModel");

      // 实盘账户模型（可选）
      qmlRegisterType<LiveAccountModel>(url, 1, 0, "LiveAccountModel");
      qmlRegisterType<LiveActionLogModel>(url, 1, 0, "LiveActionLogModel");
      qmlRegisterType<LivePositionModel>(url, 1, 0, "LivePositionModel");
      qmlRegisterSingletonInstance<GlobalState>(url, 1, 0, "GlobalState",&GlobalState::instance());
      // 向 QML 暴露全局交易记录模型单例，名称为 GlobalTradeModel
      qmlRegisterSingletonInstance<TradeRecordModel>(
          url,
          1,
          0,
          "GlobalTradeModel",
          GlobalModels::tradeModel());

      // 向 QML 暴露全局资金曲线模型单例，名称为 GlobalEquityModel
      qmlRegisterSingletonInstance<EquityCurveModel>(
          url,
          1,
          0,
          "GlobalEquityModel",
          GlobalModels::equityModel());

        // 向 QML 暴露实盘账户单例，名称为 GlobalLiveAccount
        qmlRegisterSingletonInstance<LiveAccountModel>(
          url,
          1,
          0,
          "GlobalLiveAccount",
          GlobalModels::liveAccountModel());

          // 实盘动作日志单例
          qmlRegisterSingletonInstance<LiveActionLogModel>(
            url,
            1,
            0,
            "GlobalLiveActions",
            GlobalModels::liveActionLogModel());

            // 实盘持仓明细单例
            qmlRegisterSingletonInstance<LivePositionModel>(
              url,
              1,
              0,
              "GlobalLivePositions",
              GlobalModels::livePositionModel());
          
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
   }
}
