// GlobalModels.h
#pragma once

#include "TradeRecordModel.h"
#include "EquityCurveModel.h"
#include "LiveAccountModel.h"
#include "LiveActionLogModel.h"
#include "LivePositionModel.h"

class GlobalModels {
public:
    // TradeRecordModel 单例
    static TradeRecordModel* tradeModel() {
        static TradeRecordModel* model = new TradeRecordModel();
        return model;
    }

    // EquityCurveModel 单例
    static EquityCurveModel* equityModel() {
        static EquityCurveModel* model = new EquityCurveModel();
        return model;
    }

    // LiveAccountModel 单例
    static LiveAccountModel* liveAccountModel() {
        static LiveAccountModel* model = new LiveAccountModel();
        return model;
    }

    // 实盘动作日志单例
    static LiveActionLogModel* liveActionLogModel() {
        static LiveActionLogModel* model = new LiveActionLogModel();
        return model;
    }

    // 实盘持仓明细单例
    static LivePositionModel* livePositionModel() {
        static LivePositionModel* model = new LivePositionModel();
        return model;
    }
};
