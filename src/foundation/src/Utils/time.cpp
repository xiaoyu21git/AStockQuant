// utils/time.cpp - 时间工具非内联实现
#include "foundation/Utils/time.hpp"
#include <ctime>

namespace foundation {
namespace utils {

std::string Time::formatTime(const std::chrono::system_clock::time_point& tp,
                            const std::string& format) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    
    std::ostringstream oss;
    oss << std::put_time(&tm, format.c_str());
    return oss.str();
}

std::string Time::formatTimestamp(int64_t timestamp,
                                 const std::string& format) {
    std::time_t time = static_cast<std::time_t>(timestamp);
    
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    
    std::ostringstream oss;
    oss << std::put_time(&tm, format.c_str());
    return oss.str();
}

std::chrono::system_clock::time_point Time::parseTime(
    const std::string& timeStr,
    const std::string& format) {
    
    std::tm tm = {};
    std::istringstream iss(timeStr);
    iss >> std::get_time(&tm, format.c_str());
    
    if (iss.fail()) {
        throw std::runtime_error("Failed to parse time string: " + timeStr);
    }
    
    return tmToTimePoint(tm);
}

std::chrono::system_clock::time_point Time::parseTimestamp(int64_t timestamp) {
    return std::chrono::system_clock::from_time_t(timestamp);
}

int Time::getCurrentYear() {
    auto now = std::chrono::system_clock::now();
    auto tm = timePointToTm(now);
    return tm.tm_year + 1900;
}

int Time::getCurrentMonth() {
    auto now = std::chrono::system_clock::now();
    auto tm = timePointToTm(now);
    return tm.tm_mon + 1;
}

int Time::getCurrentDay() {
    auto now = std::chrono::system_clock::now();
    auto tm = timePointToTm(now);
    return tm.tm_mday;
}

int Time::getCurrentWeekday() {
    auto now = std::chrono::system_clock::now();
    auto tm = timePointToTm(now);
    return tm.tm_wday;
}

int Time::getCurrentHour() {
    auto now = std::chrono::system_clock::now();
    auto tm = timePointToTm(now);
    return tm.tm_hour;
}

int Time::getCurrentMinute() {
    auto now = std::chrono::system_clock::now();
    auto tm = timePointToTm(now);
    return tm.tm_min;
}

int Time::getCurrentSecond() {
    auto now = std::chrono::system_clock::now();
    auto tm = timePointToTm(now);
    return tm.tm_sec;
}

int Time::getLocalTimezoneOffset() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    
    std::tm local_tm;
    std::tm gmt_tm;
    
#ifdef _WIN32
    localtime_s(&local_tm, &time);
    gmtime_s(&gmt_tm, &time);
#else
    localtime_r(&time, &local_tm);
    gmtime_r(&time, &gmt_tm);
#endif
    
    // 计算时差（小时）
    int diff = local_tm.tm_hour - gmt_tm.tm_hour;
    if (local_tm.tm_mday != gmt_tm.tm_mday) {
        diff += (local_tm.tm_mday > gmt_tm.tm_mday) ? 24 : -24;
    }
    
    return diff;
}

std::chrono::system_clock::time_point Time::toUtc(
    const std::chrono::system_clock::time_point& localTime) {
    auto time = std::chrono::system_clock::to_time_t(localTime);
    
    std::tm tm;
#ifdef _WIN32
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    
    return tmToTimePoint(tm);
}

std::chrono::system_clock::time_point Time::toLocal(
    const std::chrono::system_clock::time_point& utcTime) {
    auto time = std::chrono::system_clock::to_time_t(utcTime);
    
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    
    return tmToTimePoint(tm);
}

// 私有辅助函数
std::chrono::system_clock::time_point Time::tmToTimePoint(const std::tm& tm) {
    std::time_t time = std::mktime(const_cast<std::tm*>(&tm));
    return std::chrono::system_clock::from_time_t(time);
}

std::tm Time::timePointToTm(const std::chrono::system_clock::time_point& tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    
    return tm;
}

} // namespace utils
} // namespace foundation