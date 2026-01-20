#pragma once
#include "BaseInterface.h"
namespace engine {

class Clock {
public:
    virtual ~Clock() = default;
    
    /**
     * @brief 获取当前时间
     * @return 当前时间戳
     * 
     * 根据时钟模式返回不同的时间：
     * - 回测模式：返回模拟时间
     * - 实时模式：返回系统时间
     * - 加速模式：返回加速后的时间
     */
    virtual Timestamp current_time() const = 0;
    
    /**
     * @brief 推进时间（回测/单步模式使用）
     * @param target_time 目标时间
     * @return 成功或错误
     * 
     * 将时钟推进到指定时间，会触发时间推进事件
     * 仅在回测模式和单步模式下有效
     */
    virtual Error advance_to(Timestamp target_time) = 0;
    
    /**
     * @brief 启动时钟
     * @return 成功或错误
     * 
     * 对于实时/加速时钟，开始计时
     * 对于回测时钟，开始模拟
     */
    virtual Error start() = 0;
    
    /**
     * @brief 停止时钟
     * @return 成功或错误
     * 
     * 停止时钟运行，暂停所有时间相关活动
     */
    virtual Error stop() = 0;
    
    /**
     * @brief 重置时钟
     * @param start_time 重置后的起始时间
     * @return 成功或错误
     * 
     * 将时钟重置到指定时间，清空所有状态
     */
    virtual Error reset(Timestamp start_time) = 0;
    
    /**
     * @brief 判断时钟是否正在运行
     * @return true如果时钟正在运行
     */
    virtual bool is_running() const = 0;
    
    /**
     * @brief 获取时钟模式
     * @return 当前模式
     */
    enum class Mode { Backtest, Realtime, Accelerated, SingleStep };
    virtual Mode mode() const = 0;
    
    // 创建不同类型的时钟
    static std::unique_ptr<Clock> create_backtest_clock(
        Timestamp start_time,
        Timestamp end_time,
        Duration step_interval);
    
    static std::unique_ptr<Clock> create_realtime_clock();
    
    static std::unique_ptr<Clock> create_accelerated_clock(
        Duration acceleration_factor);
};

} // namespace engine