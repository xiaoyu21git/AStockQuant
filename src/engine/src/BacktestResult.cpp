// engine/src/BacktestResult.cpp
#include "BacktestResult.h"
#include "foundation/log/logger.hpp"
#include <foundation/json/json_facade.h>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace {

double calculateMean(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    const double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return sum / static_cast<double>(values.size());
}

double calculateStdDev(const std::vector<double>& values, double mean) {
    if (values.size() < 2) {
        return 0.0;
    }

    double variance = 0.0;
    for (double value : values) {
        const double diff = value - mean;
        variance += diff * diff;
    }
    variance /= static_cast<double>(values.size() - 1);
    return std::sqrt((std::max)(variance, 0.0));
}

double annualize(double cumulativeReturn, std::size_t periods) {
    if (periods == 0) {
        return 0.0;
    }
    if (cumulativeReturn <= -1.0) {
        return -1.0;
    }
    return std::pow(1.0 + cumulativeReturn, 252.0 / static_cast<double>(periods)) - 1.0;
}

double percentileValue(std::vector<double> values, double percentile) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double position = percentile * static_cast<double>(values.size() - 1);
    const std::size_t lowerIndex = static_cast<std::size_t>(std::floor(position));
    const std::size_t upperIndex = static_cast<std::size_t>(std::ceil(position));
    if (lowerIndex == upperIndex) {
        return values[lowerIndex];
    }
    const double weight = position - static_cast<double>(lowerIndex);
    return values[lowerIndex] * (1.0 - weight) + values[upperIndex] * weight;
}

} // namespace

namespace engine {

// 添加交易记录
void BacktestResult::add_trade_record(const TradeRecord& record) {
    trades_.push_back(record);
}

// 更新权益曲线
void BacktestResult::update_equity_curve(Timestamp time, double equity) {
    equity_curve_.push_back({time, equity, equity, 0.0, 0.0});

    if (equity > rolling_max_equity_) {
        rolling_max_equity_ = equity;
    }
    if (rolling_max_equity_ > 0.0) {
        equity_curve_.back().drawdown = (rolling_max_equity_ - equity) / rolling_max_equity_;
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

    if (equity_curve_.size() >= 2) {
        std::vector<double> returns;
        returns.reserve(equity_curve_.size() - 1);

        for (std::size_t index = 1; index < equity_curve_.size(); ++index) {
            const double previousEquity = equity_curve_[index - 1].equity;
            const double currentEquity = equity_curve_[index].equity;
            if (previousEquity > 0.0) {
                returns.push_back(currentEquity / previousEquity - 1.0);
            }
        }

        const double firstEquity = equity_curve_.front().equity;
        const double lastEquity = equity_curve_.back().equity;
        if (firstEquity > 0.0) {
            performance_.total_return = lastEquity / firstEquity - 1.0;
            performance_.annual_return = annualize(performance_.total_return, returns.size());
            performance_.daily_return = returns.empty() ? 0.0 : calculateMean(returns);
            performance_.monthly_return = performance_.daily_return * 21.0;
        }

        const double returnsMean = calculateMean(returns);
        const double returnsStdDev = calculateStdDev(returns, returnsMean);
        risk_metrics_.volatility = returnsStdDev * std::sqrt(252.0);
        if (returnsStdDev > 0.0) {
            risk_metrics_.sharpe_ratio = returnsMean / returnsStdDev * std::sqrt(252.0);
        }

        std::vector<double> downsideReturns;
        downsideReturns.reserve(returns.size());
        for (double value : returns) {
            if (value < 0.0) {
                downsideReturns.push_back(value);
            }
        }

        const double downsideStdDev = calculateStdDev(downsideReturns, calculateMean(downsideReturns));
        if (downsideStdDev > 0.0) {
            risk_metrics_.sortino_ratio = returnsMean / downsideStdDev * std::sqrt(252.0);
        }

        for (const auto& point : equity_curve_) {
            risk_metrics_.max_drawdown = (std::max)(risk_metrics_.max_drawdown, point.drawdown);
        }
        if (risk_metrics_.max_drawdown > 0.0) {
            risk_metrics_.calmar_ratio = performance_.annual_return / risk_metrics_.max_drawdown;
        }

        if (!returns.empty()) {
            const double varThreshold = percentileValue(returns, 0.05);
            risk_metrics_.var_95 = -varThreshold;

            double tailSum = 0.0;
            int tailCount = 0;
            for (double value : returns) {
                if (value <= varThreshold) {
                    tailSum += value;
                    ++tailCount;
                }
            }
            if (tailCount > 0) {
                risk_metrics_.expected_shortfall = -(tailSum / static_cast<double>(tailCount));
            }
        }
    }

    performance_.alpha = 0.0;
    performance_.beta = 0.0;
    performance_.information_ratio = 0.0;
    
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