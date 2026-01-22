// engine/src/Clock.cpp
#include "IClock.h"
#include <thread>
#include <chrono>

namespace engine {


// 回测时钟实现（保持不变）
class BacktestClock : public Clock {
public:
    BacktestClock(foundation::Timestamp start_time, foundation::Timestamp end_time, Duration step_interval)
        : start_time_(start_time)
        , end_time_(end_time)
        , step_interval_(step_interval)
        , current_time_(start_time)
        , running_(false) {
    }
    
    foundation::Timestamp current_time() const override {
        return current_time_;
    }
    
    Error advance_to(foundation::Timestamp target_time) override {
        if (target_time < current_time_) {
            return Error::fail(1, "Cannot advance to past time");
        }
        
        if (target_time > end_time_) {
            return Error::fail(2, "Target time exceeds end time");
        }
        
        current_time_ = target_time;
        return Error::success();
    }
    
    Error start() override {
        if (running_) {
            return Error::fail(3, "Clock is already running");
        }
        
        running_ = true;
        current_time_ = start_time_;
        return Error::success();
    }
    
    Error stop() override {
        if (!running_) {
            return Error::fail(4, "Clock is not running");
        }
        
        running_ = false;
        return Error::success();
    }
    
    Error reset(foundation::Timestamp start_time) override {
        start_time_ = start_time;
        current_time_ = start_time;
        running_ = false;
        return Error::success();
    }
    
    bool is_running() const override {
        return running_;
    }
    
    Mode mode() const override {
        return Mode::Backtest;
    }
    
private:
    foundation::Timestamp start_time_;
    foundation::Timestamp end_time_;
    Duration step_interval_;
    foundation::Timestamp current_time_;
    bool running_;
};

// 实时时钟实现（使用foundation函数）
class RealtimeClock : public Clock {
public:
    RealtimeClock() : running_(false) {
    }
    
    foundation::Timestamp current_time() const override {
        // 使用 foundation 的时间戳函数
       
        return foundation::timestamp_now();
    }
    
    Error advance_to(foundation::Timestamp target_time) override {
        // 实时时钟不支持手动推进时间
        return Error::fail(5, "Realtime clock does not support manual time advancement");
    }
    
    Error start() override {
        if (running_) {
            return Error::fail(3, "Clock is already running");
        }
        
        running_ = true;
        return Error::success();
    }
    
    Error stop() override {
        if (!running_) {
            return Error::fail(4, "Clock is not running");
        }
        
        running_ = false;
        return Error::success();
    }
    
    Error reset(foundation::Timestamp start_time) override {
        // 实时时钟不支持重置到特定时间
        return Error::fail(6, "Realtime clock does not support reset");
    }
    
    bool is_running() const override {
        return running_;
    }
    
    Mode mode() const override {
        return Mode::Realtime;
    }
    
private:
    bool running_;
};

// 加速时钟实现（使用foundation函数）
class AcceleratedClock : public Clock {
public:
    AcceleratedClock(Duration acceleration_factor)
        : acceleration_factor_(std::chrono::duration<double>(acceleration_factor.to_seconds_double()))
        , running_(false)
        , start_real_time_(std::chrono::system_clock::from_time_t(foundation::Foundation::timestamp()))
        , start_sim_time_(std::chrono::system_clock::from_time_t(foundation::Foundation::timestamp())) {
    }
    
    foundation::Timestamp current_time() const override {
        if (!running_) {
            return start_sim_time_;
        }
        using namespace std::chrono;

        // 当前真实时间（ms）
        int64_t now_real_ms = foundation::Foundation::timestamp_ms();

        // 启动时的真实时间（ms）
        int64_t start_real_ms =start_real_time_.to_milliseconds();

        // 真实经过时间（ms）
        int64_t real_elapsed_ms = now_real_ms - start_real_ms;

        // 真实经过时间（duration）
        duration<double> real_elapsed_sec =
            duration<double, std::milli>(real_elapsed_ms);

        // 加速后的模拟时间（秒）
        duration<double> sim_elapsed_sec =
            real_elapsed_sec * acceleration_factor_.count();

        // 转换为 Timestamp 使用的 duration 类型（100ns）
       foundation::Duration sim_elapsed = foundation::Duration::from_seconds(sim_elapsed_sec.count());

        return start_sim_time_ + sim_elapsed;
    }

    
    Error advance_to(foundation::Timestamp target_time) override {
        // 加速时钟不支持手动推进时间
        return Error::fail(5, "Accelerated clock does not support manual time advancement");
    }
    
    Error start() override {
        if (running_) {
            return Error::fail(3, "Clock is already running");
        }
        
        running_ = true;
        // 使用foundation的时间函数
        auto timestamp_seconds = foundation::Foundation::timestamp();
        start_real_time_ = foundation::Timestamp::from_seconds(timestamp_seconds);
        start_sim_time_ = foundation::Timestamp::from_seconds(timestamp_seconds);
        return Error::success();
    }
    
    Error stop() override {
        if (!running_) {
            return Error::fail(4, "Clock is not running");
        }
        
        running_ = false;
        // 保存当前模拟时间
        start_sim_time_ = current_time();
        return Error::success();
    }
    
    Error reset(foundation::Timestamp start_time) override {
        start_sim_time_ = start_time;
        auto timestamp_seconds = foundation::Foundation::timestamp();
        start_real_time_ = foundation::Timestamp::from_seconds(timestamp_seconds);
        running_ = false;
        return Error::success();
    }
    
    bool is_running() const override {
        return running_;
    }
    
    Mode mode() const override {
        return Mode::Accelerated;
    }
    
private:
    std::chrono::duration<double> acceleration_factor_;
    bool running_;
    foundation::Timestamp start_real_time_;
    foundation::Timestamp start_sim_time_;
};

// 工厂方法实现
std::unique_ptr<Clock> Clock::create_backtest_clock(
    foundation::Timestamp start_time,
    foundation::Timestamp end_time,
    Duration step_interval) {
    
    return std::make_unique<BacktestClock>(start_time, end_time, step_interval);
}

std::unique_ptr<Clock> Clock::create_realtime_clock() {
    return std::make_unique<RealtimeClock>();
}

std::unique_ptr<Clock> Clock::create_accelerated_clock(Duration acceleration_factor) {
    return std::make_unique<AcceleratedClock>(acceleration_factor);
}

} // namespace engine