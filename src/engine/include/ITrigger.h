#pragma once
#include "BaseInterface.h"
#include "Event.h"
#include "foundation.h"
using Duration = foundation::Duration;
using Timestamp = foundation::Timestamp;
namespace engine {
class TriggerCondition {
public:
    
    virtual ~TriggerCondition() = default;
    
    /**
     * @brief 检查条件是否满足
     * @param event 触发事件
     * @param current_time 当前时间
     * @return 是否满足条件
     */
    virtual bool check(const Event& event, foundation::Timestamp current_time) = 0;
    
    /**
     * @brief 获取条件描述
     * @return 描述字符串
     */
    virtual std::string description() const = 0;
    
    /**
     * @brief 克隆条件
     * @return 条件的深度拷贝
     */
    virtual std::unique_ptr<TriggerCondition> clone() const = 0;
};

class TriggerAction {
public:
    virtual ~TriggerAction() = default;
    
    /**
     * @brief 执行触发动作
     * @param triggering_event 触发事件
     * @param current_time 当前时间
     * @return 成功或错误
     */
    virtual Error execute(const Event& triggering_event, foundation::Timestamp current_time) = 0;
    
    /**
     * @brief 获取动作描述
     * @return 描述字符串
     */
    virtual std::string description() const = 0;
    
    /**
     * @brief 克隆动作
     * @return 动作的深度拷贝
     */
    virtual std::unique_ptr<TriggerAction> clone() const = 0;
};

class Trigger {
public:
    /**
     * @brief 构造函数
     * @param name 触发器名称
     * @param condition 触发条件
     * @param action 触发动作
     */
    Trigger(std::string name, 
            std::unique_ptr<TriggerCondition> condition,
            std::unique_ptr<TriggerAction> action){
                
            };
    // 添加这行代码（在构造函数声明之前）
    Trigger() = delete;
    virtual ~Trigger() = default;
    
    /**
     * @brief 评估触发器
     * @param event 事件
     * @param current_time 当前时间
     * @return 是否触发成功
     * 
     * 检查条件是否满足，如果满足则执行动作
     * 由Engine在分发事件时调用
     */
    virtual Error evaluate(const Event& event, foundation::Timestamp current_time) = 0;
    
    /**
     * @brief 获取触发器名称
     * @return 名称字符串
     */
    virtual std::string name() const = 0;
    
    /**
     * @brief 获取触发器ID
     * @return 唯一ID
     */
    virtual foundation::Uuid id() const = 0;
    
    /**
     * @brief 启用/禁用触发器
     * @param enabled 是否启用
     */
    virtual void set_enabled(bool enabled) = 0;
    
    /**
     * @brief 检查是否启用
     * @return 是否启用
     */
    virtual bool is_enabled() const = 0;
    
    /**
     * @brief 获取触发条件
     * @return 条件指针
     */
    virtual const TriggerCondition* condition() const = 0;
    
    /**
     * @brief 获取触发动作
     * @return 动作指针
     */
    virtual const TriggerAction* action() const = 0;
    
    /**
     * @brief 创建触发器实例
     * @param name 触发器名称
     * @param condition 触发条件
     * @param action 触发动作
     * @return 触发器指针
     */
    static  std::unique_ptr<Trigger> Trigger::create(std::string name
        ,std::unique_ptr<TriggerCondition> condition
        ,std::unique_ptr<TriggerAction> action);
};

} // namespace engine