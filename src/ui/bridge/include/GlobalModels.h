// GlobalModels.h
#pragma once

#include "TradeRecordModel.h"
#include "BacktestEngine.h"
#include "Bar.h"

#include <vector>
#include <string>

class GlobalModels {
public:
    // TradeRecordModel 单例
    static TradeRecordModel* tradeModel() {
        static TradeRecordModel* model = new TradeRecordModel();
        return model;
    }

    // BacktestEngine 单例
    static engine::BacktestEngine* backtestEngine() {
        static engine::BacktestEngine* engine = new engine::BacktestEngine();
        return engine;
    }

    // 全局 Bar 数据
    static const std::vector<domain::model::Bar>& bars() {
        static std::vector<domain::model::Bar> s_bars;
        if (s_bars.empty()) {
            loadBars(s_bars);
        }
        return s_bars;
    }

private:
    // 从 CSV 或 JSON 文件加载 Bar
    static void loadBars(std::vector<domain::model::Bar>& outBars) {
        // 示例：硬编码生成
        using namespace domain::model;
        outBars.push_back(Bar{"AAPL", 1673075400000, 150.0, 151.0, 149.5, 150.5, 1000});
        outBars.push_back(Bar{"AAPL", 1673075460000, 150.5, 151.2, 150.0, 151.0, 1200});
        outBars.push_back(Bar{"AAPL", 1673075520000, 151.0, 151.5, 150.5, 151.2, 900});

        // TODO: 以后可改为从 CSV / 数据库 / API 加载
    }
};
