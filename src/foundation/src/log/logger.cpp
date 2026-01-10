// internal/logger.cpp
#include "foundation/log/logger.hpp"
#include <iomanip>

namespace foundation {
namespace log {

// ============ DefaultFormatter 实现 ============

std::string DefaultFormatter::format(LogLevel level, const std::string& message,
                                    const std::string& file, int line,
                                    std::time_t timestamp) {
    std::stringstream ss;
    
    // 格式化时间戳
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &timestamp);
#else
    localtime_r(&timestamp, &tm);
#endif
    
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " ";
    
    // 日志级别
    ss << "[" << levelToString(level) << "] ";
    
    // 文件名和行号
    if (!file.empty()) {
        ss << "[" << file;
        if (line > 0) {
            ss << ":" << line;
        }
        ss << "] ";
    }
    
    // 消息
    ss << message;
    
    return ss.str();
}

std::unique_ptr<LogFormatter> DefaultFormatter::clone() const {
    return std::make_unique<DefaultFormatter>();
}

std::string DefaultFormatter::levelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERR: return "ERR";
        case LogLevel::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

// ============ ConsoleHandler 实现 ============

ConsoleHandler::ConsoleHandler(bool useColor) 
    : useColor_(useColor) {
    formatter_ = std::make_unique<DefaultFormatter>();
}

void ConsoleHandler::handle(LogLevel level, const std::string& message,
                           const std::string& file, int line) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!formatter_) return;
    
    std::time_t now = std::time(nullptr);
    std::string formatted = formatter_->format(level, message, file, line, now);
    
    if (useColor_) {
        std::string color_code = getColorCode(level);
        std::string reset_code = "\033[0m";
        
        if (level == LogLevel::ERR || level == LogLevel::FATAL) {
            std::cerr << color_code << formatted << reset_code << std::endl;
        } else {
            std::cout << color_code << formatted << reset_code << std::endl;
        }
    } else {
        if (level == LogLevel::ERR || level == LogLevel::FATAL) {
            std::cerr << formatted << std::endl;
        } else {
            std::cout << formatted << std::endl;
        }
    }
}

std::string ConsoleHandler::getColorCode(LogLevel level) const {
    switch (level) {
        case LogLevel::TRACE: return "\033[90m";  // 亮黑色/灰色
        case LogLevel::DEBUG: return "\033[36m";  // 青色
        case LogLevel::INFO:  return "\033[32m";  // 绿色
        case LogLevel::WARN:  return "\033[33m";  // 黄色
        case LogLevel::ERR: return "\033[31m";  // 红色
        case LogLevel::FATAL: return "\033[35m";  // 紫色
        default: return "\033[0m";
    }
}

// ============ FileHandler 实现 ============

FileHandler::FileHandler(const std::string& filename, bool append)
    : filename_(filename), append_(append) {
    formatter_ = std::make_unique<DefaultFormatter>();
    reopen();
}

FileHandler::~FileHandler() {
    close();
}

void FileHandler::handle(LogLevel level, const std::string& message,
                        const std::string& file, int line) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!file_.is_open() || !formatter_) return;
    
    std::time_t now = std::time(nullptr);
    std::string formatted = formatter_->format(level, message, file, line, now);
    
    file_ << formatted << std::endl;
}

void FileHandler::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.flush();
    }
}

void FileHandler::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.close();
    }
}

void FileHandler::reopen() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (file_.is_open()) {
        file_.close();
    }
    
    std::ios::openmode mode = std::ios::out;
    if (append_) {
        mode |= std::ios::app;
    } else {
        mode |= std::ios::trunc;
    }
    
    file_.open(filename_, mode);
    if (!file_.is_open()) {
        throw std::runtime_error("Failed to open log file: " + filename_);
    }
}

// ============ LoggerImpl 实现 ============

LoggerImpl::LoggerImpl() {
    // 默认添加控制台处理器
    auto console_handler = std::make_unique<ConsoleHandler>(true);
    addHandler(std::move(console_handler));
}

void LoggerImpl::log(LogLevel level, const std::string& message,
                    const std::string& file, int line) {
    if (!shouldLog(level)) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& handler : handlers_) {
        handler->handle(level, message, file, line);
    }
}

void LoggerImpl::debug(const std::string& message,
                      const std::string& file, int line) {
    log(LogLevel::DEBUG, message, file, line);
}

void LoggerImpl::info(const std::string& message,
                     const std::string& file, int line) {
    log(LogLevel::INFO, message, file, line);
}

void LoggerImpl::warning(const std::string& message,
                        const std::string& file, int line) {
    log(LogLevel::WARN, message, file, line);
}

void LoggerImpl::error(const std::string& message,
                      const std::string& file, int line) {
    log(LogLevel::ERR, message, file, line);
}

void LoggerImpl::fatal(const std::string& message,
                      const std::string& file, int line) {
    log(LogLevel::FATAL, message, file, line);
}

void LoggerImpl::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

    LogLevel LoggerImpl::getLevel() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return level_;
    }

void LoggerImpl::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& handler : handlers_) {
        handler->flush();
    }
}

void LoggerImpl::addHandler(std::unique_ptr<LogHandler> handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_.push_back(std::move(handler));
}

void LoggerImpl::removeHandler(LogHandler* handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_.erase(
        std::remove_if(handlers_.begin(), handlers_.end(),
            [handler](const std::unique_ptr<LogHandler>& h) {
                return h.get() == handler;
            }),
        handlers_.end()
    );
}

void LoggerImpl::clearHandlers() {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_.clear();
}

bool LoggerImpl::shouldLog(LogLevel level) const {
    return static_cast<int>(level) >= static_cast<int>(level_);
}

} // namespace internal
} // namespace foundation