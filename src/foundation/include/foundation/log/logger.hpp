// internal/logger.hpp - 内部日志实现（不对外暴露）
#pragma once

#include <mutex>
#include <fstream>
#include <memory>
#include <vector>
#include <string>
#include <ctime>
#include <iostream>
#include <algorithm>
#include <sstream>

// 定义日志级别枚举
namespace foundation {
    enum class LogLevel {
        TRACE = 0,
        DEBUG = 1,
        INFO = 2,
        WARN = 3,
        Found_ERROR = 4,
        FATAL = 5
    };

    // ILogger 接口声明
    class ILogger {
    public:
        virtual ~ILogger() = default;
        virtual void log(LogLevel level, const std::string& message,
                        const std::string& file = "", int line = 0) = 0;
        // 添加 trace 方法声明
        virtual void trace(const std::string& message,
                      const std::string& file = "", int line = 0) = 0;
        virtual void debug(const std::string& message,
                          const std::string& file = "", int line = 0) = 0;
        virtual void info(const std::string& message,
                         const std::string& file = "", int line = 0) = 0;
        virtual void warning(const std::string& message,
                            const std::string& file = "", int line = 0) = 0;
        virtual void error(const std::string& message,
                          const std::string& file = "", int line = 0) = 0;
        virtual void fatal(const std::string& message,
                          const std::string& file = "", int line = 0) = 0;
        
        virtual void setLevel(LogLevel level) = 0;
        virtual LogLevel getLevel() const = 0;
        
        virtual void flush() = 0;
    };
} // namespace foundation

namespace foundation {
namespace log {

// 日志格式化器
class LogFormatter {
public:
    virtual ~LogFormatter() = default;
    virtual std::string format(LogLevel level, const std::string& message,
                              const std::string& file, int line, 
                              std::time_t timestamp) = 0;
    virtual std::unique_ptr<LogFormatter> clone() const = 0;
};

// 默认格式化器
class DefaultFormatter : public LogFormatter {
public:
    std::string format(LogLevel level, const std::string& message,
                      const std::string& file, int line,
                      std::time_t timestamp) override;
    std::unique_ptr<LogFormatter> clone() const override;
    
private:
    std::string levelToString(LogLevel level) const;
};

// 日志处理器
class LogHandler {
public:
    virtual ~LogHandler() = default;
    virtual void handle(LogLevel level, const std::string& message,
                       const std::string& file, int line) = 0;
    virtual void flush() = 0;
    virtual void close() = 0;
    
    void setFormatter(std::unique_ptr<LogFormatter> formatter);
    LogFormatter* getFormatter() const;
    
protected:
    std::unique_ptr<LogFormatter> formatter_;
};

// 控制台处理器
class ConsoleHandler : public LogHandler {
public:
    explicit ConsoleHandler(bool useColor = true);
    ~ConsoleHandler() override = default;
    
    void handle(LogLevel level, const std::string& message,
               const std::string& file, int line) override;
    void flush() override {}
    void close() override {}
    
    void setUseColor(bool useColor) { useColor_ = useColor; }
    bool getUseColor() const { return useColor_; }
    
private:
    bool useColor_;
    mutable std::mutex mutex_;
    
    std::string getColorCode(LogLevel level) const;
};

// 文件处理器
class FileHandler : public LogHandler {
public:
    explicit FileHandler(const std::string& filename, bool append = true);
    ~FileHandler() override;
    
    void handle(LogLevel level, const std::string& message,
               const std::string& file, int line) override;
    void flush() override;
    void close() override;
    
    const std::string& getFilename() const { return filename_; }
    void reopen();
    
private:
    std::string filename_;
    std::ofstream file_;
    std::mutex mutex_;
    bool append_;
};

// 内部日志器实现
class LoggerImpl : public ILogger {
public:
    LoggerImpl();
    ~LoggerImpl() override = default;
    
    // ILogger 接口实现
    void log(LogLevel level, const std::string& message,
            const std::string& file = "", int line = 0) override;
    // 添加 trace 方法
    void trace(const std::string& message,
              const std::string& file = "", int line = 0) override {
        log(LogLevel::TRACE, message, file, line);
    }
    void debug(const std::string& message,
              const std::string& file = "", int line = 0) override;
    void info(const std::string& message,
             const std::string& file = "", int line = 0) override;
    void warning(const std::string& message,
                const std::string& file = "", int line = 0) override;
    void error(const std::string& message,
              const std::string& file = "", int line = 0) override;
    void fatal(const std::string& message,
              const std::string& file = "", int line = 0) override;
    
    void setLevel(LogLevel level) override;
    LogLevel getLevel() const override;
    
    void flush() override;
    
    // 内部管理方法
    void addHandler(std::unique_ptr<LogHandler> handler);
    void removeHandler(LogHandler* handler);
    void clearHandlers();
    
private:
    LogLevel level_ = LogLevel::INFO;
    std::vector<std::unique_ptr<LogHandler>> handlers_;
    mutable  std::mutex mutex_;
    
    bool shouldLog(LogLevel level) const;
};

// ============ 内联方法实现 ============

inline void LogHandler::setFormatter(std::unique_ptr<LogFormatter> formatter) {
    formatter_ = std::move(formatter);
}

inline LogFormatter* LogHandler::getFormatter() const {
    return formatter_.get();
}

} // namespace internal
} // namespace foundation