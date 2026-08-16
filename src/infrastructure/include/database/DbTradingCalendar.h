#pragma once
// DbTradingCalendar — 交易日历 DB 实现 (P4, 实盘路径)
// 完全迁移 StrategyEngineFacade startLiveLoop 的交易日查询 lambda 逻辑
// (含非交易日判定分支/报错日志), 替代实盘路径 gmsdk 日历调用

#include "ITradingCalendar.h"

namespace astock::infrastructure::database {

class DbTradingCalendar final : public ITradingCalendar {
public:
    std::int64_t currentTradingDay() override;
    std::string previousTradingDay(const std::string& date) override;
};

} // namespace astock::infrastructure::database
