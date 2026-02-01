#include "BacktestEngine.h"

#include <foundation.h>

namespace engine {

BacktestResult BacktestEngine::run(
    const std::vector<domain::model::Bar>& bars,
    double initial_capital,
    const std::string& strategy_name,
    double max_position_ratio,
    double commission_rate,
    double slippage_rate,
    double min_volume)
{
    BacktestResult result;
    if (bars.empty()) {
        return result;
    }

    double cash = initial_capital;
    double position = 0.0;
    double entry_price = 0.0;

    for (std::size_t i = 0; i < bars.size(); ++i) {
        const auto& bar = bars[i];
        double price = bar.close;

        bool should_buy  = false;
        bool should_sell = false;

        // 根据策略名选择不同的简单 demo 逻辑
        if (strategy_name == "\xE7\xA7\xBB\xE5\x8A\xA8\xE5\xB9\xB3\xE5\x9D\x87\xE7\xBA\xBF\xE7\xAD\x96\xE7\x95\xA5") {
            // 趋势跟随：收盘价>开盘价且空仓则开多；收盘价<开盘价且有仓则平多
            if (bar.close > bar.open && position == 0.0) {
                should_buy = true;
            } else if (bar.close < bar.open && position > 0.0) {
                should_sell = true;
            }
        } else if (strategy_name == "RSI\xE7\xAD\x96\xE7\x95\xA5") {
            // 每3根K线切一次方向
            if (i % 3 == 0) {
                if (position > 0.0) {
                    should_sell = true;
                } else {
                    should_buy = true;
                }
            }
        } else if (strategy_name == "\xE5\xB8\x83\xE6\x9E\x97\xE5\xB8\xA6\xE7\xAD\x96\xE7\x95\xA5") {
            // 交替进出场：偶数bar开多，奇数bar平多
            if (i % 2 == 0 && position == 0.0) {
                should_buy = true;
            } else if (i % 2 == 1 && position > 0.0) {
                should_sell = true;
            }
        } else {
            // 未知策略名：使用交替进出场 demo
            if (i % 2 == 0 && position == 0.0) {
                should_buy = true;
            } else if (i % 2 == 1 && position > 0.0) {
                should_sell = true;
            }
        }

        // 将 domain::model::Bar 的毫秒时间戳转成 foundation::Timestamp（按秒）
        foundation::Timestamp ts = foundation::Timestamp::from_seconds(bar.time / 1000);

        // 简单流动性约束：当成交量低于阈值时，不开仓也不平仓
        if (min_volume > 0.0 && bar.volume > 0.0 && bar.volume < min_volume) {
            should_buy  = false;
            should_sell = false;
        }

        if (should_buy && price > 0.0 && cash > 0.0) {
            double tradePrice = price;
            if (slippage_rate > 0.0) {
                tradePrice = price * (1.0 + slippage_rate); // 买入向上滑点
            }
            // 应用最大仓位比例限制：单次开仓金额不超过 initial_capital * max_position_ratio
            double max_invest_cash = initial_capital * max_position_ratio;
            if (max_invest_cash <= 0.0) {
                max_invest_cash = cash;
            }
            double invest_cash = (cash < max_invest_cash) ? cash : max_invest_cash;
            if (invest_cash <= 0.0) {
                continue;
            }
            // 按整数手数下单，这里简单假设一手=100股
            const double lot_size = 100.0;
            double raw_qty = invest_cash / tradePrice;
            double lots = std::floor(raw_qty / lot_size);
            if (lots <= 0.0) {
                // 资金不足以买入一手，则本次不交易
                continue;
            }
            double qty = lots * lot_size;

            // 开仓手续费
            double entryCommission = 0.0;
            if (commission_rate > 0.0) {
                entryCommission = tradePrice * qty * commission_rate;
            }

            BacktestResult::TradeRecord rec{};
            rec.trade_id	= foundation::Uuid{};
            rec.entry_time	= ts;
            rec.exit_time	= ts;
            rec.symbol		= bar.symbol;
            rec.direction	= "BUY";
            rec.entry_price = tradePrice;
            rec.exit_price	= tradePrice;
            rec.quantity	= qty;
            rec.commission = entryCommission;
            rec.profit	= 0.0;
            rec.profit_pct = 0.0;
            rec.notes		= "open long";
            result.add_trade_record(rec);

            position	= qty;
            cash		= cash - invest_cash - entryCommission;
            entry_price = tradePrice;
        } else if (should_sell && price > 0.0 && position > 0.0) {
            double tradePrice = price;
            if (slippage_rate > 0.0) {
                tradePrice = price * (1.0 - slippage_rate); // 卖出向下滑点
            }

            // 计算手续费
            double entryCommission = 0.0;
            double exitCommission  = 0.0;
            if (commission_rate > 0.0) {
                entryCommission = entry_price * position * commission_rate;
                exitCommission  = tradePrice  * position * commission_rate;
            }
            double totalCommission = entryCommission + exitCommission;

            double grossProfit = (tradePrice - entry_price) * position;
            double profit = grossProfit - totalCommission;

            BacktestResult::TradeRecord rec{};
            rec.trade_id	= foundation::Uuid{};
            rec.entry_time	= ts;
            rec.exit_time	= ts;
            rec.symbol		= bar.symbol;
            rec.direction	= "SELL";
            rec.entry_price = entry_price;
            rec.exit_price	= tradePrice;
            rec.quantity	= position;
            rec.commission = totalCommission;
            rec.profit	= profit;
            rec.profit_pct = (entry_price > 0.0 ? profit / (entry_price * position) : 0.0);
            rec.notes		= "close long";
            result.add_trade_record(rec);

            // 卖出回笼资金，扣除卖出腿手续费（买入腿手续费已在开仓时扣除）
            cash		+= position * tradePrice - exitCommission;
            position	= 0.0;
            entry_price = 0.0;
        }

        double equity = cash + position * price;
        result.update_equity_curve(ts, equity);
    }

    result.calculate_all_metrics();
    return result;
}

} // namespace engine
