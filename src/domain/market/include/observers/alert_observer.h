// alert_observer.h
#pragma once

#include "data_observer_base.h"
#include <map>
#include <vector>
#include <functional>

namespace astock {
namespace market {
namespace observer {

/**
 * @brief 报警规则类型
 */
enum class AlertRuleType {
    PRICE_THRESHOLD,      // 价格阈值
    VOLUME_SPIKE,         // 成交量突增
    PRICE_CHANGE_PERCENT, // 价格变化百分比
    TECHNICAL_INDICATOR,  // 技术指标
    CUSTOM_CONDITION      // 自定义条件
};

/**
 * @brief 报警条件
 */
struct AlertCondition {
    AlertRuleType type;
    std::string symbol;
    std::string parameter;  // JSON格式的参数
    
    // 条件检查函数
    using ConditionChecker = std::function<bool(const DataEvent&, const std::string&)>;
    ConditionChecker checker;
    
    // 触发条件
    bool check(const DataEvent& event) const;
};

/**
 * @brief 报警动作
 */
struct AlertAction {
    enum class ActionType {
        LOG_MESSAGE,      // 记录日志
        SEND_EMAIL,       // 发送邮件
        SEND_SMS,         // 发送短信
        WEBHOOK,          // Webhook通知
        EXECUTE_COMMAND,  // 执行命令
        CUSTOM_ACTION     // 自定义动作
    };
    
    ActionType type;
    std::string target;   // 目标地址/命令
    std::string message_template;
    
    // 动作执行函数
    using ActionExecutor = std::function<void(const DataEvent&, const std::string&, const std::string&)>;
    ActionExecutor executor;
    
    void execute(const DataEvent& event) const;
};

/**
 * @brief 报警规则
 */
struct AlertRule {
    std::string name;
    std::string description;
    AlertCondition condition;
    AlertAction action;
    bool enabled = true;
    uint64_t last_triggered = 0;
    size_t trigger_count = 0;
    
    // 冷却时间（避免重复触发）
    std::chrono::seconds cooldown = std::chrono::seconds(60);
    
    bool can_trigger() const;
    void trigger(const DataEvent& event);
};

/**
 * @brief 报警观察者 - 监控数据并触发报警
 */
class AlertObserver : public BaseDataObserver {
public:
    AlertObserver(const std::string& name,
                 const std::string& description = "");
    ~AlertObserver() override;
    
    /**
     * @brief 添加报警规则
     */
    void add_rule(const AlertRule& rule);
    
    /**
     * @brief 移除报警规则
     */
    bool remove_rule(const std::string& rule_name);
    
    /**
     * @brief 启用/禁用规则
     */
    bool set_rule_enabled(const std::string& rule_name, bool enabled);
    
    /**
     * @brief 获取所有规则
     */
    std::vector<AlertRule> get_all_rules() const;
    
    /**
     * @brief 获取触发历史
     */
    struct TriggerHistory {
        std::string rule_name;
        uint64_t timestamp;
        std::string symbol;
        DataEventType event_type;
        std::string event_data;
    };
    
    std::vector<TriggerHistory> get_trigger_history(size_t limit = 100) const;
    
    /**
     * @brief 清除触发历史
     */
    void clear_trigger_history();
    
    /**
     * @brief 添加内置条件检查器
     */
    void add_builtin_condition_checker(AlertRuleType type, 
                                      AlertCondition::ConditionChecker checker);
    
    /**
     * @brief 添加内置动作执行器
     */
    void add_builtin_action_executor(AlertAction::ActionType type,
                                    AlertAction::ActionExecutor executor);
    
    /**
     * @brief 测试规则
     */
    bool test_rule(const std::string& rule_name, const DataEvent& test_event);
    
protected:
    void process_event(const DataEvent& event) override;
    
private:
    // 规则检查
    void check_rules(const DataEvent& event);
    
    // 内置条件检查器
    bool check_price_threshold(const DataEvent& event, const std::string& params);
    bool check_volume_spike(const DataEvent& event, const std::string& params);
    bool check_price_change(const DataEvent& event, const std::string& params);
    
    // 内置动作执行器
    void execute_log_action(const DataEvent& event, const std::string& target, const std::string& template_str);
    void execute_webhook_action(const DataEvent& event, const std::string& target, const std::string& template_str);
    void execute_command_action(const DataEvent& event, const std::string& target, const std::string& template_str);
    
    // 数据存储
    std::map<std::string, AlertRule> rules_;
    mutable std::shared_mutex rules_mutex_;
    
    std::deque<TriggerHistory> trigger_history_;
    mutable std::mutex history_mutex_;
    size_t max_history_size_ = 1000;
    
    // 内置检查器和执行器
    std::map<AlertRuleType, AlertCondition::ConditionChecker> condition_checkers_;
    std::map<AlertAction::ActionType, AlertAction::ActionExecutor> action_executors_;
    
    // 统计
    struct AlertStats {
        size_t total_events_checked = 0;
        size_t total_rules_triggered = 0;
        size_t active_rules_count = 0;
    };
    
    mutable std::mutex stats_mutex_;
    AlertStats stats_;
};

} // namespace observer
} // namespace market
} // namespace astock