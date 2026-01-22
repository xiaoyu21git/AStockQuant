// foundation/include/foundation/Timestamp.h
#pragma once

#include <chrono>
#include <string>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace foundation {
namespace utils{
// 前置声明
class Duration;

class Timestamp {
private:
    std::chrono::system_clock::time_point time_point_;
    
    // 辅助函数：解析时间字符串
    static std::chrono::system_clock::time_point parseTimeString(
        const std::string& time_str, 
        const std::string& format = "%Y-%m-%d %H:%M:%S");
    
    // 辅助函数：格式化时间
    static std::string formatTimePoint(
        const std::chrono::system_clock::time_point& tp,
        const std::string& format = "%Y-%m-%d %H:%M:%S");
        
public:
    // ============ 构造函数 ============
    
    // 默认构造函数：当前时间
    Timestamp() : time_point_(std::chrono::system_clock::now()) {}
    
    // 从时间点构造
    explicit Timestamp(const std::chrono::system_clock::time_point& tp) 
        : time_point_(tp) {}
    
    // 从字符串构造
    explicit Timestamp(const std::string& time_str, 
                      const std::string& format = "%Y-%m-%d %H:%M:%S")
        : time_point_(parseTimeString(time_str, format)) {}
    
    // 从时间戳（秒）构造
    explicit Timestamp(int64_t seconds_since_epoch) 
        : time_point_(std::chrono::system_clock::from_time_t(seconds_since_epoch)) {}
    
    // ============ 工厂方法 ============
    
    static Timestamp now() {
        return Timestamp();
    }
    
    static Timestamp from_string(const std::string& time_str, 
                                const std::string& format = "%Y-%m-%d %H:%M:%S") {
        return Timestamp(time_str, format);
    }
    
    static Timestamp from_seconds(int64_t seconds) {
        return Timestamp(seconds);
    }
    
    static Timestamp from_milliseconds(int64_t milliseconds) {
        return Timestamp(std::chrono::system_clock::time_point(
            std::chrono::milliseconds(milliseconds)));
    }
    
    static Timestamp from_microseconds(int64_t microseconds) {
        return Timestamp(std::chrono::system_clock::time_point(
            std::chrono::microseconds(microseconds)));
    }
    static std::string tostring(){
        return now().to_string();
    }

    
    // ============ 转换方法 ============
    
    std::string to_string(const std::string& format = "%Y-%m-%d %H:%M:%S") const {
        return formatTimePoint(time_point_, format);
    }
    
    int64_t to_seconds() const {
        return std::chrono::duration_cast<std::chrono::seconds>(
            time_point_.time_since_epoch()).count();
    }
    
    int64_t to_milliseconds() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            time_point_.time_since_epoch()).count();
    }
    
    int64_t to_microseconds() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            time_point_.time_since_epoch()).count();
    }
    
    std::time_t to_time_t() const {
        return std::chrono::system_clock::to_time_t(time_point_);
    }
    
    // ============ 时间信息获取 ============
    
    int year() const {
        auto time_t = to_time_t();
        std::tm tm;
        #ifdef _WIN32
            localtime_s(&tm, &time_t);
        #else
            localtime_r(&time_t, &tm);
        #endif
        return tm.tm_year + 1900;
    }
    
    int month() const {
        auto time_t = to_time_t();
        std::tm tm;
        #ifdef _WIN32
            localtime_s(&tm, &time_t);
        #else
            localtime_r(&time_t, &tm);
        #endif
        return tm.tm_mon + 1;
    }
    
    int day() const {
        auto time_t = to_time_t();
        std::tm tm;
        #ifdef _WIN32
            localtime_s(&tm, &time_t);
        #else
            localtime_r(&time_t, &tm);
        #endif
        return tm.tm_mday;
    }
    
    int hour() const {
        auto time_t = to_time_t();
        std::tm tm;
        #ifdef _WIN32
            localtime_s(&tm, &time_t);
        #else
            localtime_r(&time_t, &tm);
        #endif
        return tm.tm_hour;
    }
    
    int minute() const {
        auto time_t = to_time_t();
        std::tm tm;
        #ifdef _WIN32
            localtime_s(&tm, &time_t);
        #else
            localtime_r(&time_t, &tm);
        #endif
        return tm.tm_min;
    }
    
    int second() const {
        auto time_t = to_time_t();
        std::tm tm;
        #ifdef _WIN32
            localtime_s(&tm, &time_t);
        #else
            localtime_r(&time_t, &tm);
        #endif
        return tm.tm_sec;
    }
    
    // ============ 运算符重载 ============
    
    bool operator<(const Timestamp& other) const {
        return time_point_ < other.time_point_;
    }
    
    bool operator<=(const Timestamp& other) const {
        return time_point_ <= other.time_point_;
    }
    
    bool operator>(const Timestamp& other) const {
        return time_point_ > other.time_point_;
    }
    
    bool operator>=(const Timestamp& other) const {
        return time_point_ >= other.time_point_;
    }
    
    bool operator==(const Timestamp& other) const {
        return time_point_ == other.time_point_;
    }
    
    bool operator!=(const Timestamp& other) const {
        return time_point_ != other.time_point_;
    }
    
    // ============ 时间运算 ============
    
    Timestamp operator+(const Duration& duration) const;
    Timestamp operator-(const Duration& duration) const;
    Duration operator-(const Timestamp& other) const;
    
    Timestamp& operator+=(const Duration& duration);
    Timestamp& operator-=(const Duration& duration);
    
    // ============ 友元函数 ============
    
    friend std::ostream& operator<<(std::ostream& os, const Timestamp& ts);
};

// ============ Duration 类 ============

class Duration {
private:
    std::chrono::microseconds microseconds_;
    
public:
    // ============ 构造函数 ============
    
    Duration() : microseconds_(0) {}
    
    explicit Duration(int64_t microseconds) : microseconds_(microseconds) {}
    
    explicit Duration(const std::chrono::microseconds& us) : microseconds_(us) {}
    
    // ============ 工厂方法 ============
    
    static Duration zero() { return Duration(); }
    static Duration microseconds(int64_t us) { return Duration(us); }
    static Duration milliseconds(int64_t ms) { return Duration(ms * 1000); }
    static Duration seconds(int64_t sec) { return Duration(sec * 1000000); }
    static Duration minutes(int64_t min) { return seconds(min * 60); }
    static Duration hours(int64_t h) { return minutes(h * 60); }
    static Duration days(int64_t d) { return hours(d * 24); }
    
    static Duration from_seconds(double seconds) {
        return Duration(static_cast<int64_t>(seconds * 1000000));
    }
    
    // ============ 转换方法 ============
    
    int64_t to_microseconds() const { return microseconds_.count(); }
    int64_t to_milliseconds() const { return microseconds_.count() / 1000; }
    int64_t to_seconds() const { return microseconds_.count() / 1000000; }
    double to_seconds_double() const { return microseconds_.count() / 1000000.0; }
    double to_minutes() const { return to_seconds_double() / 60.0; }
    double to_hours() const { return to_minutes() / 60.0; }
    
    // ============ 运算符重载 ============
    
    bool operator==(const Duration& other) const { return microseconds_ == other.microseconds_; }
    bool operator!=(const Duration& other) const { return microseconds_ != other.microseconds_; }
    bool operator<(const Duration& other) const { return microseconds_ < other.microseconds_; }
    bool operator<=(const Duration& other) const { return microseconds_ <= other.microseconds_; }
    bool operator>(const Duration& other) const { return microseconds_ > other.microseconds_; }
    bool operator>=(const Duration& other) const { return microseconds_ >= other.microseconds_; }
    
    Duration operator+(const Duration& other) const;
    Duration operator-(const Duration& other) const;
    Duration operator*(int64_t scalar) const;
    Duration operator*(double scalar) const;
    Duration operator/(int64_t divisor) const;
    double operator/(const Duration& other) const;
    
    Duration& operator+=(const Duration& other);
    Duration& operator-=(const Duration& other);
    Duration& operator*=(int64_t scalar);
    Duration& operator/=(int64_t divisor);
    
    // ============ 友元函数 ============
    
    friend std::ostream& operator<<(std::ostream& os, const Duration& d);
};

// ============ 内联实现 ============

inline std::chrono::system_clock::time_point Timestamp::parseTimeString(
    const std::string& time_str, 
    const std::string& format) {
    
    std::tm tm = {};
    std::istringstream ss(time_str);
    ss >> std::get_time(&tm, format.c_str());
    
    if (ss.fail()) {
        throw std::runtime_error("Failed to parse time string: " + time_str);
    }
    
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

inline std::string Timestamp::formatTimePoint(
    const std::chrono::system_clock::time_point& tp,
    const std::string& format) {
    
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm;
    
    #ifdef _WIN32
        localtime_s(&tm, &time_t);
    #else
        localtime_r(&time_t, &tm);
    #endif
    
    char buffer[80];
    std::strftime(buffer, sizeof(buffer), format.c_str(), &tm);
    return std::string(buffer);
}

inline Timestamp Timestamp::operator+(const Duration& duration) const {
    return Timestamp(time_point_ + std::chrono::microseconds(duration.to_microseconds()));
}

inline Timestamp Timestamp::operator-(const Duration& duration) const {
    return Timestamp(time_point_ - std::chrono::microseconds(duration.to_microseconds()));
}

inline Duration Timestamp::operator-(const Timestamp& other) const {
    auto diff = time_point_ - other.time_point_;
    auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(diff);
    return Duration(microseconds);
}

inline Timestamp& Timestamp::operator+=(const Duration& duration) {
    time_point_ += std::chrono::microseconds(duration.to_microseconds());
    return *this;
}

inline Timestamp& Timestamp::operator-=(const Duration& duration) {
    time_point_ -= std::chrono::microseconds(duration.to_microseconds());
    return *this;
}

inline Duration Duration::operator+(const Duration& other) const {
    return Duration(microseconds_ + other.microseconds_);
}

inline Duration Duration::operator-(const Duration& other) const {
    return Duration(microseconds_ - other.microseconds_);
}

inline Duration Duration::operator*(int64_t scalar) const {
    return Duration(microseconds_ * scalar);
}

inline Duration Duration::operator*(double scalar) const {
    return Duration(static_cast<int64_t>(microseconds_.count() * scalar));
}

inline Duration Duration::operator/(int64_t divisor) const {
    return Duration(microseconds_ / divisor);
}

inline double Duration::operator/(const Duration& other) const {
    return static_cast<double>(microseconds_.count()) / other.microseconds_.count();
}

inline Duration& Duration::operator+=(const Duration& other) {
    microseconds_ += other.microseconds_;
    return *this;
}

inline Duration& Duration::operator-=(const Duration& other) {
    microseconds_ -= other.microseconds_;
    return *this;
}

inline Duration& Duration::operator*=(int64_t scalar) {
    microseconds_ *= scalar;
    return *this;
}

inline Duration& Duration::operator/=(int64_t divisor) {
    microseconds_ /= divisor;
    return *this;
}

inline std::ostream& operator<<(std::ostream& os, const Timestamp& ts) {
    os << ts.to_string();
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Duration& d) {
    if (d.to_hours() >= 1.0) {
        os << std::fixed << std::setprecision(2) << d.to_hours() << "h";
    } else if (d.to_minutes() >= 1.0) {
        os << std::fixed << std::setprecision(2) << d.to_minutes() << "m";
    } else if (d.to_seconds() >= 1.0) {
        os << std::fixed << std::setprecision(3) << d.to_seconds_double() << "s";
    } else if (d.to_milliseconds() >= 1.0) {
        os << d.to_milliseconds() << "ms";
    } else {
        os << d.to_microseconds() << "us";
    }
    return os;
}

// 全局运算符
inline Duration operator*(int64_t scalar, const Duration& d) {
    return d * scalar;
}

inline Duration operator*(double scalar, const Duration& d) {
    return d * scalar;
}
}
} // namespace foundation