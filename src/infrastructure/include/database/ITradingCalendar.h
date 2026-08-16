#pragma once
// ITradingCalendar — 交易日历抽象 (P4, 实盘路径)
// 替换实盘路径两处 gmsdk 日历调用 (管道 prevTradingDayFn / 调度器交易日查询),
// 回测路径 gmsdk 保持不动 (runBacktestLoop 内, §12 回测不改)
// 注意: 与 domain::backtest 模块的局部 ITradingCalendar 语义不同
// (后者为回测窗口构建: isTradingDay(TradingDay)/shiftTradingDays), 不合并

#include <cstdint>
#include <string>

namespace astock::infrastructure::database {

class ITradingCalendar {
public:
    virtual ~ITradingCalendar() = default;

    /// @brief 当前交易日 (YYYYMMDD int); 非交易日返回最近的前一个交易日, 查不到返回 0
    [[nodiscard]] virtual std::int64_t currentTradingDay() = 0;

    /// @brief 上一交易日 (输入 YYYYMMDD string → YYYYMMDD string), 查不到返回空串
    [[nodiscard]] virtual std::string previousTradingDay(const std::string& date) = 0;
};

} // namespace astock::infrastructure::database
