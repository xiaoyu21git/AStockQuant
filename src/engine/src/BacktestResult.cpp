// engine/src/BacktestResult.cpp
#include "BacktestResult.h"
#include "foundation/log/logger.hpp"
#include <foundation/json/json_facade.h>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace engine {

// 添加交易记录
void BacktestResult::add_trade_record(const TradeRecord& record) {
    trades_.push_back(record);
}

// 更新权益曲线
void BacktestResult::update_equity_curve(Timestamp time, double equity) {
    equity_curve_.push_back({time, equity, equity, 0.0, 0.0});
    
    // 计算当前回撤
    if (!equity_curve_.empty()) {
        double max_equity = 0.0;
        for (const auto& point : equity_curve_) {
            if (point.equity > max_equity) {
                max_equity = point.equity;
            }
        }
        
        if (max_equity > 0.0) {
            equity_curve_.back().drawdown = (max_equity - equity) / max_equity * 100.0;
        }
    }
}

// 计算所有统计指标
void BacktestResult::calculate_all_metrics() {
    // 重置统计
    trade_stats_ = TradeStats{};
    risk_metrics_ = RiskMetrics{};
    performance_ = Performance{};
    
    // 基本交易统计
    trade_stats_.total_trades = static_cast<int>(trades_.size());
    
    for (const auto& trade : trades_) {
        if (trade.profit > 0) {
            trade_stats_.winning_trades++;
            trade_stats_.total_profit += trade.profit;
            if (trade.profit > trade_stats_.max_profit) {
                trade_stats_.max_profit = trade.profit;
            }
        } else {
            trade_stats_.losing_trades++;
            trade_stats_.total_loss += trade.profit; // profit为负
            if (trade.profit < trade_stats_.max_loss) {
                trade_stats_.max_loss = trade.profit;
            }
        }
    }
    
    // 计算胜率
    if (trade_stats_.total_trades > 0) {
        trade_stats_.win_rate = static_cast<double>(trade_stats_.winning_trades) 
                              / trade_stats_.total_trades;
    }
    
    // 计算平均盈亏
    if (trade_stats_.winning_trades > 0) {
        trade_stats_.avg_profit = trade_stats_.total_profit / trade_stats_.winning_trades;
    }
    
    if (trade_stats_.losing_trades > 0) {
        trade_stats_.avg_loss = trade_stats_.total_loss / trade_stats_.losing_trades;
    }
    
    // 计算盈亏比
    if (std::abs(trade_stats_.total_loss) > 1e-10) {
        trade_stats_.profit_factor = std::abs(trade_stats_.total_profit / trade_stats_.total_loss);
    }
    
    // 计算净利润
    trade_stats_.net_profit = trade_stats_.total_profit + trade_stats_.total_loss;
    
    // 计算回测期总时长
    if (run_info_.start_time.to_seconds() > 0 && 
        run_info_.end_time.to_seconds() > 0) { 
        auto duration = run_info_.end_time - run_info_.start_time;  // 得到 foundation::Duration
        run_info_.duration = duration;  // 直接赋值
    }
}

// 生成报告
std::string BacktestResult::generate_report() const {
    std::string report;
    
    report += "=== 回测报告 ===\n\n";
    
    // 运行信息
    report += "运行信息:\n";
    report += "策略名称: " + run_info_.strategy_name + "\n";
    report += "回测ID: " + run_info_.backtest_id.to_string() + "\n";
    report += "开始时间: " + std::to_string(run_info_.start_time.to_seconds()) + "\n";
    report += "结束时间: " + std::to_string(run_info_.end_time.to_seconds()) + "\n";
    report += "持续时间: " + std::to_string(run_info_.duration.to_seconds()) + "秒\n\n";
    
    // 交易统计
    report += "交易统计:\n";
    report += "总交易次数: " + std::to_string(trade_stats_.total_trades) + "\n";
    report += "盈利交易: " + std::to_string(trade_stats_.winning_trades) + "\n";
    report += "亏损交易: " + std::to_string(trade_stats_.losing_trades) + "\n";
    report += "胜率: " + std::to_string(trade_stats_.win_rate * 100) + "%\n";
    report += "净利润: " + std::to_string(trade_stats_.net_profit) + "\n";
    report += "总盈利: " + std::to_string(trade_stats_.total_profit) + "\n";
    report += "总亏损: " + std::to_string(trade_stats_.total_loss) + "\n";
    report += "盈亏比: " + std::to_string(trade_stats_.profit_factor) + "\n\n";
    
    // 简要交易记录
    if (!trades_.empty()) {
        report += "交易记录 (前10笔):\n";
        int count = 0;
        for (const auto& trade : trades_) {
            if (count++ >= 10) break;
            report += trade.symbol + " " + trade.direction + " 盈亏: " 
                    + std::to_string(trade.profit) + "\n";
        }
    }
    
    return report;
}

} // namespace engine