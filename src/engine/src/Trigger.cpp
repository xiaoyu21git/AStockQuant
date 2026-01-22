#define NOMINMAX
#include <windows.h>
#include "ITrigger.h"
#include "Event.h"
#include "BaseInterface.h"
#include <foundation/log/logging.hpp>
#include <foundation/Utils/Uuid.h>
#include <memory>
#include <utility>
#include <sstream>

namespace engine {

// ============================================================================
// TriggerConditionImpl - 触发条件基类的通用实现
// ============================================================================

class TriggerConditionImpl : public TriggerCondition {
protected:
    std::string description_;
    
public:
    explicit TriggerConditionImpl(std::string description) 
        : description_(std::move(description)) {}
    
    ~TriggerConditionImpl() override = default;
    
    std::string description() const override {
        return description_;
    }
};

// ============================================================================
// EventTypeCondition - 事件类型触发条件
// ============================================================================

class EventTypeCondition : public TriggerConditionImpl {
private:
    Event::Type event_type_;
    
public:
    explicit EventTypeCondition(Event::Type event_type)
        : TriggerConditionImpl(std::string("EventTypeCondition: ") + Event::type_to_string(event_type))
        , event_type_(event_type) {}
    
    bool check(const Event& event, foundation::Timestamp current_time)override  {
        return event.type() == event_type_;
    }
    
    std::unique_ptr<TriggerCondition> clone() const override {
        return std::make_unique<EventTypeCondition>(event_type_);
    }
};

// ============================================================================
// TimeCondition - 时间触发条件
// ============================================================================

class TimeCondition : public TriggerConditionImpl {
public:
    enum class TimeConditionType {
        BEFORE,        // 在指定时间之前
        AFTER,         // 在指定时间之后
        BETWEEN,       // 在时间区间内
        AT_TIME,       // 在指定时间点
        EVERY_INTERVAL // 每隔指定时间间隔
    };
    
private:
    TimeConditionType condition_type_;
    foundation::Timestamp time1_;
    foundation::Timestamp time2_;
    Duration interval_;
    foundation::Timestamp last_trigger_time_;
    mutable std::mutex mutex_;
    
public:
    // 构造函数：单一时间点
    explicit TimeCondition(TimeConditionType type, foundation::Timestamp time)
        : TriggerConditionImpl("TimeCondition: " + time_condition_type_to_string(type))
        , condition_type_(type)
        , time1_(time)
        , time2_()
        , interval_(Duration::zero())
        , last_trigger_time_(foundation::Timestamp::from_seconds(0)) {}
    
    // 构造函数：时间区间
    explicit TimeCondition(foundation::Timestamp start_time, foundation::Timestamp end_time)
        : TriggerConditionImpl("TimeCondition: BETWEEN")
        , condition_type_(TimeConditionType::BETWEEN)
        , time1_(start_time)
        , time2_(end_time)
        , interval_(Duration::zero())
        , last_trigger_time_(foundation::Timestamp::from_seconds(0)) {}
    
    // 构造函数：时间间隔
    explicit TimeCondition(Duration interval)
        : TriggerConditionImpl("TimeCondition: EVERY_INTERVAL")
        , condition_type_(TimeConditionType::EVERY_INTERVAL)
        , time1_()
        , time2_()
        , interval_(interval)
        , last_trigger_time_(foundation::Timestamp::from_seconds(0)) {}
    
    bool check(const Event& event, foundation::Timestamp current_time) override  {
        std::lock_guard<std::mutex> lock(mutex_);
        
        switch (condition_type_) {
            case TimeConditionType::BEFORE:
                return current_time < time1_;
                
            case TimeConditionType::AFTER:
                return current_time > time1_;
                
            case TimeConditionType::BETWEEN:
                return current_time >= time1_ && current_time <= time2_;
                
            case TimeConditionType::AT_TIME:
                return current_time == time1_;
                
            case TimeConditionType::EVERY_INTERVAL: {
                if (last_trigger_time_ == foundation::Timestamp::from_seconds(0)) {
                    last_trigger_time_ = current_time;
                    return true;
                }
                
                auto elapsed = current_time - last_trigger_time_;
                if (elapsed >= interval_) {
                    last_trigger_time_ = current_time;
                    return true;
                }
                return false;
            }
                
            default:
                LOG_ERROR("Unknown time condition type: {}", static_cast<int>(condition_type_));
                return false;
        }
    }
    
    std::unique_ptr<TriggerCondition> clone() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        switch (condition_type_) {
            case TimeConditionType::BEFORE:
            case TimeConditionType::AFTER:
            case TimeConditionType::AT_TIME:
                return std::make_unique<TimeCondition>(condition_type_, time1_);
                
            case TimeConditionType::BETWEEN:
                return std::make_unique<TimeCondition>(time1_, time2_);
                
            case TimeConditionType::EVERY_INTERVAL:
                return std::make_unique<TimeCondition>(interval_);
                
            default:
                LOG_ERROR("Cannot clone unknown time condition type");
                return std::make_unique<TimeCondition>(condition_type_, time1_);
        }
    }
    
private:
    static std::string time_condition_type_to_string(TimeConditionType type) {
        switch (type) {
            case TimeConditionType::BEFORE: return "BEFORE";
            case TimeConditionType::AFTER: return "AFTER";
            case TimeConditionType::BETWEEN: return "BETWEEN";
            case TimeConditionType::AT_TIME: return "AT_TIME";
            case TimeConditionType::EVERY_INTERVAL: return "EVERY_INTERVAL";
            default: return "UNKNOWN";
        }
    }
};

// ============================================================================
// LogicalCondition - 逻辑组合条件
// ============================================================================

class LogicalCondition : public TriggerConditionImpl {
public:
    enum class LogicalOperator {
        AND,
        OR,
        NOT
    };
    
private:
    LogicalOperator operator_;
    std::unique_ptr<TriggerCondition> left_;
    std::unique_ptr<TriggerCondition> right_; // 仅用于AND和OR
    
public:
    // AND 或 OR 运算
    explicit LogicalCondition(LogicalOperator op, 
                             std::unique_ptr<TriggerCondition> left,
                             std::unique_ptr<TriggerCondition> right)
        : TriggerConditionImpl(logical_operator_to_string(op) + " condition")
        , operator_(op)
        , left_(std::move(left))
        , right_(std::move(right)) {}
    
    // NOT 运算
    explicit LogicalCondition(std::unique_ptr<TriggerCondition> condition)
        : TriggerConditionImpl("NOT condition")
        , operator_(LogicalOperator::NOT)
        , left_(std::move(condition))
        , right_(nullptr) {}
    
    bool check(const Event& event, foundation::Timestamp current_time)override  {
        switch (operator_) {
            case LogicalOperator::AND:
                return left_->check(event, current_time) && 
                       right_->check(event, current_time);
                
            case LogicalOperator::OR:
                return left_->check(event, current_time) || 
                       right_->check(event, current_time);
                
            case LogicalOperator::NOT:
                return !left_->check(event, current_time);
                
            default:
                LOG_ERROR("Unknown logical operator: {}", static_cast<int>(operator_));
                return false;
        }
    }
    
    std::unique_ptr<TriggerCondition> clone() const override {
        switch (operator_) {
            case LogicalOperator::AND:
            case LogicalOperator::OR:
                return std::make_unique<LogicalCondition>(
                    operator_, left_->clone(), right_->clone());
                
            case LogicalOperator::NOT:
                return std::make_unique<LogicalCondition>(left_->clone());
                
            default:
                LOG_ERROR("Cannot clone unknown logical operator");
                return std::make_unique<LogicalCondition>(left_->clone());
        }
    }
    
private:
    static std::string logical_operator_to_string(LogicalOperator op) {
        switch (op) {
            case LogicalOperator::AND: return "AND";
            case LogicalOperator::OR: return "OR";
            case LogicalOperator::NOT: return "NOT";
            default: return "UNKNOWN";
        }
    }
};

// ============================================================================
// EventDataCondition - 事件数据条件
// ============================================================================

class EventDataCondition : public TriggerConditionImpl {
private:
    std::string data_key_;
    std::string expected_value_;
    
public:
    explicit EventDataCondition(const std::string& data_key, const std::string& expected_value)
        : TriggerConditionImpl("EventDataCondition: " + data_key + " == " + expected_value)
        , data_key_(data_key)
        , expected_value_(expected_value) {}
    
    bool check(const Event& event, foundation::Timestamp /*current_time*/) override {
        const auto& attrs = event.attributes();

    auto it = attrs.find(data_key_);
    if (it == attrs.end()) {
        return false;
    }

    return it->second == expected_value_;
}

    
    std::unique_ptr<TriggerCondition> clone() const override {
        return std::make_unique<EventDataCondition>(data_key_, expected_value_);
    }
};

// ============================================================================
// CompositeCondition - 复合条件
// ============================================================================

class CompositeCondition : public TriggerConditionImpl {
private:
    std::vector<std::unique_ptr<TriggerCondition>> conditions_;
    
public:
    explicit CompositeCondition(std::vector<std::unique_ptr<TriggerCondition>> conditions)
        : TriggerConditionImpl("CompositeCondition")
        , conditions_(std::move(conditions)) {}
    
    bool check(const Event& event, foundation::Timestamp current_time)override  {
        for (const auto& condition : conditions_) {
            if (!condition->check(event, current_time)) {
                return false;
            }
        }
        return true;
    }
    
    std::unique_ptr<TriggerCondition> clone() const override {
        std::vector<std::unique_ptr<TriggerCondition>> cloned_conditions;
        for (const auto& condition : conditions_) {
            cloned_conditions.push_back(condition->clone());
        }
        return std::make_unique<CompositeCondition>(std::move(cloned_conditions));
    }
};

// ============================================================================
// TriggerActionImpl - 触发动作基类实现
// ============================================================================

class TriggerActionImpl : public TriggerAction {
protected:
    std::string description_;
    
public:
    explicit TriggerActionImpl(std::string description) 
        : description_(std::move(description)) {}
    
    ~TriggerActionImpl() override = default;
    
    std::string description() const override {
        return description_;
    }
};

// ============================================================================
// LogAction - 日志记录动作
// ============================================================================

class LogAction : public TriggerActionImpl {
private:
    std::string message_template_;
    foundation::LogLevel log_level_;
    
public:
    explicit LogAction(const std::string& message, foundation::LogLevel level = foundation::LogLevel::INFO)
        : TriggerActionImpl("LogAction: " + message)
        , message_template_(message)
        , log_level_(level) {}
    
    Error execute(const Event& triggering_event, foundation::Timestamp current_time) override {
        try {
            // 简单实现，将事件信息插入到消息模板中
            std::string message = message_template_;
            
            // 可以在这里添加更复杂的模板替换逻辑
            // 例如: "Event {{event_id}} triggered at {{time}}"
            
            switch (log_level_) {
                case foundation::LogLevel::DEBUG: LOG_DEBUG("{}", message); break;
                case foundation::LogLevel::INFO: LOG_INFO("{}", message); break;
                case foundation::LogLevel::WARN: LOG_WARN("{}", message); break;
                case foundation::LogLevel::Found_ERROR: LOG_ERROR("{}", message); break;
                default: LOG_INFO("{}", message); break;
            }
            
            return Error{0, "Log action executed successfully"};
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to execute log action: {}", e.what());
            return Error{-1, std::string("Failed to execute log action: ") + e.what()};
        }
    }
    
    std::unique_ptr<TriggerAction> clone() const override {
        return std::make_unique<LogAction>(message_template_, log_level_);
    }
};

// ============================================================================
// EventEmitAction - 发送新事件动作
// ============================================================================

class EventEmitAction : public TriggerActionImpl {
private:
    Event::Type event_type_;
    std::map<std::string, std::string> event_data_;
    
public:
    explicit EventEmitAction(Event::Type event_type, 
                            std::map<std::string, std::string> data = {})
        : TriggerActionImpl(std::string("EventEmitAction: ") + Event::type_to_string(event_type))
        , event_type_(event_type)
        , event_data_(std::move(data)) {}
    
    Error execute(const Event& triggering_event, foundation::Timestamp current_time) override {
        try {
            // 创建新事件
            auto new_event = Event::create(event_type_, current_time, event_data_);
            
            // TODO: 这里需要访问EventBus来发布事件
            // 当前设计需要在Trigger实现中传入EventBus引用
            // 或者在Engine层面处理事件分发
            
            LOG_INFO("EventEmitAction: Created event of type {}", 
                    Event::type_to_string(event_type_));
            
            return Error{0, "Event emit action completed (event created but not published)"};
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to execute event emit action: {}", e.what());
            return Error{-1, std::string("Failed to execute event emit action: ") + e.what()};
        }
    }
    
    std::unique_ptr<TriggerAction> clone() const override {
        return std::make_unique<EventEmitAction>(event_type_, event_data_);
    }
};

// ============================================================================
// CallbackAction - 回调函数动作
// ============================================================================

class CallbackAction : public TriggerActionImpl {
private:
    std::function<Error(const Event&, foundation::Timestamp)> callback_;
    
public:
    explicit CallbackAction(std::function<Error(const Event&, foundation::Timestamp)> callback)
        : TriggerActionImpl("CallbackAction")
        , callback_(std::move(callback)) {}
    
    Error execute(const Event& triggering_event, foundation::Timestamp current_time) override {
        try {
            return callback_(triggering_event, current_time);
        } catch (const std::exception& e) {
            LOG_ERROR("Callback action execution failed: {}", e.what());
            return Error{-1, std::string("Callback execution failed: ") + e.what()};
        }
    }
    
    std::unique_ptr<TriggerAction> clone() const override {
        // 注意：函数对象不能安全地克隆
        // 在实际使用中可能需要其他机制
        LOG_WARN("CallbackAction clone not fully implemented");
        return std::make_unique<CallbackAction>(callback_);
    }
};

// ============================================================================
// TriggerImpl - 触发器实现
// ============================================================================

class TriggerImpl : public Trigger {
private:
    std::string name_;
    foundation::Uuid id_;
    bool enabled_;
    std::unique_ptr<TriggerCondition> condition_;
    std::unique_ptr<TriggerAction> action_;
    mutable std::mutex mutex_;
    
public:
   // ✅ 显式构造函数（不提供默认构造函数）
    TriggerImpl(std::string name,
                std::unique_ptr<TriggerCondition> condition,
                std::unique_ptr<TriggerAction> action)
        : Trigger(name, std::move(condition), std::move(action))
        , name_(std::move(name))
        , id_(foundation::Uuid_create())
        , enabled_(true)
        , condition_(std::move(condition))
        , action_(std::move(action)) 
    {
        LOG_DEBUG("Created trigger: {}", name_);
    }
    
    ~TriggerImpl() override {
        LOG_DEBUG("Destroyed trigger: {}", name_);
    }
    
    Error evaluate(const Event& event, foundation::Timestamp current_time) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!enabled_) {
            return Error{0, "Trigger disabled"};
        }
        
        try {
            // 检查条件
            if (!condition_->check(event, current_time)) {
                return Error{0, "Condition not satisfied"};
            }
            
            LOG_DEBUG("Trigger '{}' condition satisfied, executing action", name_);
            
            // 执行动作
            Error result = action_->execute(event, current_time);
            
            if (result.code != 0) {
                LOG_ERROR("Trigger '{}' action failed: {}", name_, result.message);
            } else {
                LOG_DEBUG("Trigger '{}' action executed successfully", name_);
            }
            
            return result;
            
        } catch (const std::exception& e) {
            LOG_ERROR("Trigger '{}' evaluation failed: {}", name_, e.what());
            return Error{-1, std::string("Trigger evaluation failed: ") + e.what()};
        }
    }
    
    std::string name() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return name_;
    }
    
    foundation::Uuid id() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return id_;
    }
    
    void set_enabled(bool enabled) override {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_ = enabled;
        LOG_DEBUG("Trigger '{}' {}", name_, enabled_ ? "enabled" : "disabled");
    }
    
    bool is_enabled() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return enabled_;
    }
    
    const TriggerCondition* condition() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return condition_.get();
    }
    
    const TriggerAction* action() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return action_.get();
    }
    
    std::unique_ptr<Trigger> clone() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::make_unique<TriggerImpl>(
            name_, condition_->clone(), action_->clone());
    }
};

// ============================================================================
// 工厂函数实现
// ============================================================================

std::unique_ptr<Trigger> Trigger::create(
    std::string name,
    std::unique_ptr<TriggerCondition> condition,
    std::unique_ptr<TriggerAction> action) {
    
    return std::make_unique<TriggerImpl>(
        std::move(name),
        std::move(condition),
        std::move(action)
    );
}

// ============================================================================
// 工厂函数创建标准条件和动作
// ============================================================================

// 注意：以下工厂函数不在原始头文件中声明，但提供会更方便使用
// 可以根据需要添加到头文件或保持为内部实现

namespace trigger_factory {

std::unique_ptr<TriggerCondition> create_event_type_condition(Event::Type event_type) {
    return std::make_unique<EventTypeCondition>(event_type);
}

std::unique_ptr<TriggerCondition> create_time_before_condition(foundation::Timestamp time) {
    return std::make_unique<TimeCondition>(TimeCondition::TimeConditionType::BEFORE, time);
}

std::unique_ptr<TriggerCondition> create_time_after_condition(foundation::Timestamp time) {
    return std::make_unique<TimeCondition>(TimeCondition::TimeConditionType::AFTER, time);
}

std::unique_ptr<TriggerCondition> create_time_between_condition(foundation::Timestamp start, foundation::Timestamp end) {
    return std::make_unique<TimeCondition>(start, end);
}

std::unique_ptr<TriggerCondition> create_interval_condition(Duration interval) {
    return std::make_unique<TimeCondition>(interval);
}

std::unique_ptr<TriggerCondition> create_event_data_condition(const std::string& key, 
                                                              const std::string& value) {
    return std::make_unique<EventDataCondition>(key, value);
}

std::unique_ptr<TriggerCondition> create_and_condition(
    std::unique_ptr<TriggerCondition> left,
    std::unique_ptr<TriggerCondition> right) {
    return std::make_unique<LogicalCondition>(
        LogicalCondition::LogicalOperator::AND, std::move(left), std::move(right));
}

std::unique_ptr<TriggerCondition> create_or_condition(
    std::unique_ptr<TriggerCondition> left,
    std::unique_ptr<TriggerCondition> right) {
    return std::make_unique<LogicalCondition>(
        LogicalCondition::LogicalOperator::OR, std::move(left), std::move(right));
}

std::unique_ptr<TriggerCondition> create_not_condition(
    std::unique_ptr<TriggerCondition> condition) {
    return std::make_unique<LogicalCondition>(std::move(condition));
}

std::unique_ptr<TriggerAction> create_log_action(const std::string& message, 
                                                foundation::LogLevel level = foundation::LogLevel::INFO) {
    return std::make_unique<LogAction>(message, level);
}

std::unique_ptr<TriggerAction> create_event_emit_action(Event::Type event_type,
                                                       std::map<std::string, std::string> data = {}) {
    return std::make_unique<EventEmitAction>(event_type, std::move(data));
}

std::unique_ptr<TriggerAction> create_callback_action(
    std::function<Error(const Event&, foundation::Timestamp)> callback) {
    return std::make_unique<CallbackAction>(std::move(callback));
}

} // namespace trigger_factory

} // namespace engine