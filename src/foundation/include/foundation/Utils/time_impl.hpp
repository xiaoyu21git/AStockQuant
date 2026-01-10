// utils/time_impl.hpp - 时间工具内联实现
#pragma once

#include "time.hpp"
#include <thread>
namespace foundation {
namespace utils {

// ============ 内联函数实现 ============

inline std::chrono::system_clock::time_point Time::now() {
    return std::chrono::system_clock::now();
}

inline std::chrono::high_resolution_clock::time_point Time::nowHighResolution() {
    return std::chrono::high_resolution_clock::now();
}

inline std::chrono::steady_clock::time_point Time::nowSteady() {
    return std::chrono::steady_clock::now();
}

inline int64_t Time::getTimestamp() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

inline int64_t Time::getTimestampMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

inline int64_t Time::getTimestampMicroseconds() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

inline int64_t Time::getTimestampNanoseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

inline std::string Time::getCurrentTimeString() {
    return getCurrentTimeString("%Y-%m-%d %H:%M:%S");
}

inline std::string Time::getCurrentTimeString(const std::string& format) {
    auto now = std::chrono::system_clock::now();
    return formatTime(now, format);
}

template<typename TimePoint1, typename TimePoint2>
inline double Time::durationSeconds(TimePoint1 start, TimePoint2 end) {
    return std::chrono::duration<double>(end - start).count();
}

template<typename TimePoint1, typename TimePoint2>
inline double Time::durationMilliseconds(TimePoint1 start, TimePoint2 end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

template<typename TimePoint1, typename TimePoint2>
inline double Time::durationMicroseconds(TimePoint1 start, TimePoint2 end) {
    return std::chrono::duration<double, std::micro>(end - start).count();
}

inline std::string Time::getTodayDate() {
    return getCurrentTimeString("%Y-%m-%d");
}

inline Time::Timer::Timer() {
    start();
}

inline void Time::Timer::start() {
    startTime_ = std::chrono::high_resolution_clock::now();
    isRunning_ = true;
}

inline void Time::Timer::stop() {
    if (isRunning_) {
        endTime_ = std::chrono::high_resolution_clock::now();
        isRunning_ = false;
    }
}

inline void Time::Timer::restart() {
    start();
}

inline double Time::Timer::elapsedSeconds() const {
    auto end = isRunning_ ? std::chrono::high_resolution_clock::now() : endTime_;
    return std::chrono::duration<double>(end - startTime_).count();
}

inline double Time::Timer::elapsedMilliseconds() const {
    auto end = isRunning_ ? std::chrono::high_resolution_clock::now() : endTime_;
    return std::chrono::duration<double, std::milli>(end - startTime_).count();
}

inline double Time::Timer::elapsedMicroseconds() const {
    auto end = isRunning_ ? std::chrono::high_resolution_clock::now() : endTime_;
    return std::chrono::duration<double, std::micro>(end - startTime_).count();
}

inline int64_t Time::Timer::elapsedNanoseconds() const {
    auto end = isRunning_ ? std::chrono::high_resolution_clock::now() : endTime_;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - startTime_).count();
}

template<typename Func, typename... Args>
inline double Time::measureExecutionTime(Func&& func, Args&&... args) {
    auto start = std::chrono::high_resolution_clock::now();
    std::forward<Func>(func)(std::forward<Args>(args)...);
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double>(end - start).count();
}

template<typename Func, typename... Args>
inline double Time::measureExecutionTimeMs(Func&& func, Args&&... args) {
    auto start = std::chrono::high_resolution_clock::now();
    std::forward<Func>(func)(std::forward<Args>(args)...);
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

inline void Time::sleep(double seconds) {
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}

inline void Time::sleepMs(int64_t milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

inline void Time::sleepUs(int64_t microseconds) {
    std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
}

} // namespace utils
} // namespace foundation