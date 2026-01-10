// log/console_logger.hpp
#pragma once

#include "logger.hpp"
#include <iostream>
#include <mutex>

namespace foundation {
namespace log {

class ConsoleLogger : public Logger {
private:
    LogLevel level_ = LogLevel::INFO;
    std::mutex mutex_;
    bool colored_ = true;
    
    std::string getColorCode(LogLevel level) const {
        if (!colored_) return "";
        
        switch (level) {
            case LogLevel::DEBUG:   return "\033[36m";  // Cyan
            case LogLevel::INFO:    return "\033[32m";  // Green
            case LogLevel::WARNING: return "\033[33m";  // Yellow
            case LogLevel::ERROR:   return "\033[31m";  // Red
            case LogLevel::FATAL:   return "\033[35m";  // Magenta
            default:                return "\033[0m";   // Reset
        }
    }
    
public:
    ConsoleLogger(bool colored = true) : colored_(colored) {}
    
    void log(LogLevel level, const std::string& message,
            const std::string& file = "", int line = 0) override {
        if (level < level_) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        LogEntry entry{level, message, file, line, std::time(nullptr)};
        std::string logStr = entry.toString();
        
        if (colored_) {
            std::cout << getColorCode(level) << logStr << "\033[0m" << std::endl;
        } else {
            std::cout << logStr << std::endl;
        }
    }
    
    void setLevel(LogLevel level) override { level_ = level; }
    LogLevel getLevel() const override { return level_; }
    
    void setColored(bool colored) { colored_ = colored; }
    bool isColored() const { return colored_; }
};

} // namespace log
} // namespace foundation