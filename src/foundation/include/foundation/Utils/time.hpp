// utils/time.hpp - 时间工具头文件（独立模块，也可通过Foundation使用）
#pragma once

#include <string>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstdint>

namespace foundation {
namespace utils {

class Time {
public:
    // ============ 时间点操作 ============
    
    // 获取当前时间点
    static std::chrono::system_clock::time_point now();
    
    // 获取高精度当前时间点
    static std::chrono::high_resolution_clock::time_point nowHighResolution();
    
    // 获取稳定时钟时间点
    static std::chrono::steady_clock::time_point nowSteady();
    
    // ============ 时间戳操作 ============
    
    // 获取秒级时间戳
    static int64_t getTimestamp();
    
    // 获取毫秒级时间戳
    static int64_t getTimestampMilliseconds();
    
    // 获取微秒级时间戳
    static int64_t getTimestampMicroseconds();
    
    // 获取纳秒级时间戳
    static int64_t getTimestampNanoseconds();
    
    // ============ 时间格式化 ============
    
    // 获取当前时间的字符串表示（默认格式：YYYY-MM-DD HH:MM:SS）
    static std::string getCurrentTimeString();
    
    // 按指定格式获取当前时间
    static std::string getCurrentTimeString(const std::string& format);
    
    // 格式化时间点
    static std::string formatTime(const std::chrono::system_clock::time_point& tp,
                                 const std::string& format = "%Y-%m-%d %H:%M:%S");
    
    // 格式化时间戳（秒）
    static std::string formatTimestamp(int64_t timestamp,
                                      const std::string& format = "%Y-%m-%d %H:%M:%S");
    
    // ============ 时间解析 ============
    
    // 从字符串解析时间
    static std::chrono::system_clock::time_point parseTime(
        const std::string& timeStr,
        const std::string& format = "%Y-%m-%d %H:%M:%S");
    
    // 从时间戳解析时间
    static std::chrono::system_clock::time_point parseTimestamp(int64_t timestamp);
    
    // ============ 时间计算 ============
    
    // 时间差（秒）
    template<typename TimePoint1, typename TimePoint2>
    static double durationSeconds(TimePoint1 start, TimePoint2 end);
    
    // 时间差（毫秒）
    template<typename TimePoint1, typename TimePoint2>
    static double durationMilliseconds(TimePoint1 start, TimePoint2 end);
    
    // 时间差（微秒）
    template<typename TimePoint1, typename TimePoint2>
    static double durationMicroseconds(TimePoint1 start, TimePoint2 end);
    
    // ============ 日期操作 ============
    
    // 获取今天的日期（YYYY-MM-DD）
    static std::string getTodayDate();
    
    // 获取当前年份
    static int getCurrentYear();
    
    // 获取当前月份（1-12）
    static int getCurrentMonth();
    
    // 获取当前日（1-31）
    static int getCurrentDay();
    
    // 获取当前星期几（0-6，0=周日）
    static int getCurrentWeekday();
    
    // 获取当前小时（0-23）
    static int getCurrentHour();
    
    // 获取当前分钟（0-59）
    static int getCurrentMinute();
    
    // 获取当前秒（0-59）
    static int getCurrentSecond();
    
    // ============ 计时器类 ============
    class Timer {
    private:
        std::chrono::high_resolution_clock::time_point startTime_;
        std::chrono::high_resolution_clock::time_point endTime_;
        bool isRunning_ = false;
        
    public:
        Timer();
        
        // 开始计时
        void start();
        
        // 停止计时
        void stop();
        
        // 重新开始
        void restart();
        
        // 获取经过的时间（秒）
        double elapsedSeconds() const;
        
        // 获取经过的时间（毫秒）
        double elapsedMilliseconds() const;
        
        // 获取经过的时间（微秒）
        double elapsedMicroseconds() const;
        
        // 获取经过的时间（纳秒）
        int64_t elapsedNanoseconds() const;
        
        // 检查是否正在运行
        bool isRunning() const { return isRunning_; }
        
        // 获取开始时间
        std::chrono::high_resolution_clock::time_point getStartTime() const { return startTime_; }
    };
    
    // ============ 性能测量工具 ============
    
    // 测量函数执行时间
    template<typename Func, typename... Args>
    static double measureExecutionTime(Func&& func, Args&&... args);
    
    // 测量函数执行时间（毫秒）
    template<typename Func, typename... Args>
    static double measureExecutionTimeMs(Func&& func, Args&&... args);
    
    // ============ 睡眠函数 ============
    
    // 睡眠指定秒数
    static void sleep(double seconds);
    
    // 睡眠指定毫秒数
    static void sleepMs(int64_t milliseconds);
    
    // 睡眠指定微秒数
    static void sleepUs(int64_t microseconds);
    
    // ============ 时区相关 ============
    
    // 获取本地时区偏移（小时）
    static int getLocalTimezoneOffset();
    
    // 转换到UTC时间
    static std::chrono::system_clock::time_point toUtc(
        const std::chrono::system_clock::time_point& localTime);
    
    // 转换到本地时间
    static std::chrono::system_clock::time_point toLocal(
        const std::chrono::system_clock::time_point& utcTime);
    
    // ============ 时间常量 ============
    static constexpr int64_t SECONDS_PER_MINUTE = 60;
    static constexpr int64_t SECONDS_PER_HOUR = 3600;
    static constexpr int64_t SECONDS_PER_DAY = 86400;
    static constexpr int64_t MILLISECONDS_PER_SECOND = 1000;
    static constexpr int64_t MICROSECONDS_PER_SECOND = 1000000;
    static constexpr int64_t NANOSECONDS_PER_SECOND = 1000000000;
    
private:
    // 辅助函数：将tm结构转换为时间点
    static std::chrono::system_clock::time_point tmToTimePoint(const std::tm& tm);
    
    // 辅助函数：将时间点转换为tm结构
    static std::tm timePointToTm(const std::chrono::system_clock::time_point& tp);
};

} // namespace utils
} // namespace foundation

// 内联函数实现
#include "time_impl.hpp"