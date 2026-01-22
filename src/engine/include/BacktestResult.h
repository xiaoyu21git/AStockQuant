// engine/include/engine/backtest_result.h
#pragma once 
#include "BaseInterface.h"
#include "foundation.h"
namespace engine {
using Timestamp = foundation::Timestamp;
using Duration = foundation::Duration;

class BacktestResult {
public:
    // 基础信息
    struct RunInfo {
        foundation::Uuid backtest_id;
        foundation::Timestamp start_time;
        foundation::Timestamp end_time;
        Duration duration;
        std::string strategy_name;
        std::map<std::string, std::string> parameters;
    };
    
    // 交易统计
    struct TradeStats {
        int total_trades;
        int winning_trades;
        int losing_trades;
        double win_rate;           // 胜率
        double profit_factor;      // 盈亏比
        double total_profit;       // 总盈利
        double total_loss;         // 总亏损
        double net_profit;         // 净利润
        double max_profit;         // 单笔最大盈利
        double max_loss;           // 单笔最大亏损
        double avg_profit;         // 平均盈利
        double avg_loss;           // 平均亏损
    };
    
    // 风险指标
    struct RiskMetrics {
        double max_drawdown;       // 最大回撤
        double sharpe_ratio;       // 夏普比率
        double sortino_ratio;      // 索提诺比率
        double calmar_ratio;       // 卡玛比率
        double var_95;             // 95% VaR
        double expected_shortfall; // 预期损失
        double volatility;         // 波动率
    };
    
    // 绩效分析
    struct Performance {
        double total_return;       // 总收益率
        double annual_return;      // 年化收益率
        double monthly_return;     // 月均收益率
        double daily_return;       // 日均收益率
        double benchmark_return;   // 基准收益率
        double alpha;              // Alpha
        double beta;               // Beta
        double information_ratio;  // 信息比率
    };
    
    // 交易记录
    struct TradeRecord {
        foundation::Uuid trade_id;
        foundation::Timestamp entry_time;
        foundation::Timestamp exit_time;
        std::string symbol;
        std::string direction;     // "BUY" / "SELL"
        double entry_price;
        double exit_price;
        double quantity;
        double commission;
        double profit;
        double profit_pct;
        std::string notes;
    };
    
    // 权益曲线
    struct EquityPoint {
        foundation::Timestamp timestamp;
        double equity;     // 权益
        double balance;    // 余额
        double floating;   // 浮动盈亏
        double drawdown;   // 回撤
    };
    
public:
    // 添加交易记录
    void add_trade_record(const TradeRecord& record);
    
    // 更新权益曲线
    void update_equity_curve(foundation::Timestamp time, double equity);
    
    // 计算所有统计指标
    void calculate_all_metrics();
    
    // 序列化为JSON
   // std::string to_json() const;
    
    // 从JSON反序列化
    //static Result<BacktestResult> from_json(const std::string& json);
    
    // 生成报告
    std::string generate_report() const;
    
    // 获取指标值
    const RunInfo& run_info() const { return run_info_; }
    const TradeStats& trade_stats() const { return trade_stats_; }
    const RiskMetrics& risk_metrics() const { return risk_metrics_; }
    const Performance& performance() const { return performance_; }
    const std::vector<TradeRecord>& trades() const { return trades_; }
    const std::vector<EquityPoint>& equity_curve() const { return equity_curve_; }
    
private:
    RunInfo run_info_;
    TradeStats trade_stats_;
    RiskMetrics risk_metrics_;
    Performance performance_;
    std::vector<TradeRecord> trades_;
    std::vector<EquityPoint> equity_curve_;
    std::map<std::string, std::vector<double>> custom_metrics_;
};

} // namespace engine