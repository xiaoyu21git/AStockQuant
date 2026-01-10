// log/file_logger.hpp
#pragma once

#include "logger.hpp"
#include <fstream>
#include <mutex>
#include <filesystem>

namespace foundation {
namespace log {

class FileLogger : public Logger {
private:
    LogLevel level_ = LogLevel::INFO;
    std::string filename_;
    std::ofstream file_;
    std::mutex mutex_;
    
public:
    explicit FileLogger(const std::string& filename) : filename_(filename) {
        // 确保目录存在
        auto dir = std::filesystem::path(filename).parent_path();
        if (!dir.empty()) {
            std::filesystem::create_directories(dir);
        }
        
        file_.open(filename, std::ios::app);
        if (!file_.is_open()) {
            throw std::runtime_error("无法打开日志文件: " + filename);
        }
    }
    
    ~FileLogger() override {
        if (file_.is_open()) {
            file_.close();
        }
    }
    
    void log(LogLevel level, const std::string& message,
            const std::string& file = "", int line = 0) override {
        if (level < level_) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!file_.is_open()) {
            file_.open(filename_, std::ios::app);
        }
        
        LogEntry entry{level, message, file, line, std::time(nullptr)};
        file_ << entry.toString() << std::endl;
    }
    
    void setLevel(LogLevel level) override { level_ = level; }
    LogLevel getLevel() const override { return level_; }
    
    void flush() override {
        if (file_.is_open()) {
            file_.flush();
        }
    }
    
    std::string getFilename() const { return filename_; }
    
    void reopen() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.close();
        }
        file_.open(filename_, std::ios::app);
    }
};

} // namespace log
} // namespace foundation