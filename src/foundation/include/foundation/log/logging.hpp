// foundation/internal/logging.hpp - 内部专用日志头文件
#ifndef FOUNDATION_INTERNAL_LOGGING_HPP
#define FOUNDATION_INTERNAL_LOGGING_HPP

#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <memory>
#include <atomic>

// ============ 编译时配置 ============
#ifndef FOUNDATION_INTERNAL_LOG_LEVEL
    #define FOUNDATION_INTERNAL_LOG_LEVEL 1  // 默认 DEBUG 级别
    // 0: TRACE, 1: DEBUG, 2: INFO, 3: WARN, 4: ERROR, 5: OFF
#endif

#ifndef FOUNDATION_INTERNAL_LOG_ENABLE_COLOR
    #define FOUNDATION_INTERNAL_LOG_ENABLE_COLOR 1
#endif

namespace foundation {
namespace internal {

// ============ 内部日志级别 ============
enum class InternalLogLevel : int {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERR = 4,
    OFF = 5
};

// ============ 编译时级别检查 ============
template<InternalLogLevel Level>
struct InternalLogEnabled {
    static constexpr bool value = 
        (static_cast<int>(Level) >= FOUNDATION_INTERNAL_LOG_LEVEL) &&
        (static_cast<int>(Level) < static_cast<int>(InternalLogLevel::OFF));
};

// ============ 轻量级内部日志记录器 ============
class InternalLogger {
public:
    static InternalLogger& instance() {
        static InternalLogger logger;
        return logger;
    }
    
    // 设置输出流
    void setOutputStream(std::ostream* stream) {
        std::lock_guard<std::mutex> lock(mutex_);
        output_stream_ = stream ? stream : &std::clog;
    }
    
    // 设置是否启用颜色
    void setColorEnabled(bool enabled) {
        color_enabled_ = enabled;
    }

    // 设置运行时级别
    void setLevel(InternalLogLevel level) {
        runtime_level_ = level;
    }

    // 获取当前级别
    InternalLogLevel getLevel() const {
        return runtime_level_;
    }

    // 启用文件日志: logs/system/system_YYYY-MM-DD.log
    void enableFileLogging(const std::string& logDir) {
        std::lock_guard<std::mutex> lock(mutex_);
        m_logDir = logDir;
        m_fileLogEnabled = true;
        ensureLogFile();
    }

    void disableFileLogging() {
        std::lock_guard<std::mutex> lock(mutex_);
        m_fileLogEnabled = false;
        if (m_logFile.is_open()) m_logFile.close();
    }
    
    // 原始日志接口
    void log(InternalLogLevel level, 
             const std::string& message,
             const std::string& file = "",
             int line = 0,
             const std::string& function = "") {
        
        // 运行时级别检查
        if (static_cast<int>(level) < static_cast<int>(runtime_level_) ||
            level == InternalLogLevel::OFF) {
            return;
        }
        
        // 格式化并输出
        std::string formatted = formatMessage(level, message, file, line, function);
        
        std::lock_guard<std::mutex> lock(mutex_);
        if (output_stream_ && output_stream_->good()) {
            *output_stream_ << formatted << std::endl;
        }
        // 同步写入文件
        if (m_fileLogEnabled) {
            ensureLogFile();
            if (m_logFile.is_open()) {
                // 文件输出去掉颜色代码
                m_logFile << stripColors(formatted) << std::endl;
                m_logFile.flush();
            }
        }
    }
    
    // 各级别便捷方法
    void trace(const std::string& message,
               const std::string& file = "",
               int line = 0,
               const std::string& function = "") {
        log(InternalLogLevel::TRACE, message, file, line, function);
    }
    
    void debug(const std::string& message,
               const std::string& file = "",
               int line = 0,
               const std::string& function = "") {
        log(InternalLogLevel::DEBUG, message, file, line, function);
    }
    
    void info(const std::string& message,
              const std::string& file = "",
              int line = 0,
              const std::string& function = "") {
        log(InternalLogLevel::INFO, message, file, line, function);
    }
    
    void warn(const std::string& message,
              const std::string& file = "",
              int line = 0,
              const std::string& function = "") {
        log(InternalLogLevel::WARN, message, file, line, function);
    }
    
    void error(const std::string& message,
               const std::string& file = "",
               int line = 0,
               const std::string& function = "") {
        log(InternalLogLevel::ERR, message, file, line, function);
    }
    
private:
    InternalLogger() 
        : output_stream_(&std::clog)
        , runtime_level_(static_cast<InternalLogLevel>(FOUNDATION_INTERNAL_LOG_LEVEL))
        , color_enabled_(FOUNDATION_INTERNAL_LOG_ENABLE_COLOR != 0) {
    }
    
    ~InternalLogger() = default;
    
    // 禁止拷贝
    InternalLogger(const InternalLogger&) = delete;
    InternalLogger& operator=(const InternalLogger&) = delete;
    
    // 格式化消息
    std::string formatMessage(InternalLogLevel level,
                             const std::string& message,
                             const std::string& file,
                             int line,
                             const std::string& function) const {
        std::stringstream ss;
        
        // 时间戳
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        char time_buffer[32];
        std::strftime(time_buffer, sizeof(time_buffer), 
                     "%H:%M:%S", std::localtime(&time));
        
        ss << time_buffer << "." 
           << std::setfill('0') << std::setw(3) << ms.count() << " ";
        
        // 级别（带颜色）
        if (color_enabled_) {
            ss << getLevelColor(level);
        }
        
        ss << "[" << levelToString(level) << "]";
        
        if (color_enabled_) {
            ss << "\033[0m";  // 重置颜色
        }
        
        ss << " ";
        
        // 位置信息（如果有）
        if (!file.empty()) {
            // 提取文件名
            size_t slash_pos = file.find_last_of("/\\");
            std::string filename = (slash_pos != std::string::npos) 
                                 ? file.substr(slash_pos + 1) 
                                 : file;
            
            ss << "[" << filename;
            if (line > 0) {
                ss << ":" << line;
            }
            if (!function.empty()) {
                ss << " " << function;
            }
            ss << "] ";
        }
        
        // 消息
        ss << message;
        
        return ss.str();
    }
    
    // 级别转字符串
    std::string levelToString(InternalLogLevel level) const {
        switch (level) {
            case InternalLogLevel::TRACE: return "TRACE";
            case InternalLogLevel::DEBUG: return "DEBUG";
            case InternalLogLevel::INFO:  return "INFO";
            case InternalLogLevel::WARN:  return "WARN";
            case InternalLogLevel::ERR: return "ERROR";
            case InternalLogLevel::OFF:   return "OFF";
            default: return "UNKNOWN";
        }
    }
    
    // 获取颜色代码
    std::string getLevelColor(InternalLogLevel level) const {
        if (!color_enabled_) {
            return "";
        }
        
        switch (level) {
            case InternalLogLevel::TRACE: return "\033[36m";  // 青色
            case InternalLogLevel::DEBUG: return "\033[34m";  // 蓝色
            case InternalLogLevel::INFO:  return "\033[32m";  // 绿色
            case InternalLogLevel::WARN:  return "\033[33m";  // 黄色
            case InternalLogLevel::ERR: return "\033[31m";  // 红色
            default: return "\033[0m";  // 默认
        }
    }
    
private:
    void ensureLogFile() const {
        if (m_logDir.empty()) return;
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char dateBuf[16];
        std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &tm);
        std::string today(dateBuf);

        if (today == m_currentLogDate && m_logFile.is_open()) return;

        if (m_logFile.is_open()) m_logFile.close();
        m_currentLogDate = today;

        std::filesystem::path dir = std::filesystem::path(m_logDir) / "system";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        std::string path = (dir / ("system_" + today + ".log")).string();
        m_logFile.open(path, std::ios::app | std::ios::out | std::ios::binary);
    }

    static std::string stripColors(const std::string& msg) {
        std::string result;
        result.reserve(msg.size());
        bool inEscape = false;
        for (char c : msg) {
            if (c == '\033') { inEscape = true; continue; }
            if (inEscape) {
                if (c == 'm') inEscape = false;
                continue;
            }
            result += c;
        }
        return result;
    }

    std::ostream* output_stream_{&std::clog};
    InternalLogLevel runtime_level_{static_cast<InternalLogLevel>(FOUNDATION_INTERNAL_LOG_LEVEL)};
    bool color_enabled_{FOUNDATION_INTERNAL_LOG_ENABLE_COLOR != 0};
    bool m_fileLogEnabled{false};
    std::string m_logDir;
    mutable std::string m_currentLogDate;
    mutable std::ofstream m_logFile;
    mutable std::mutex mutex_;
};

// ============ 流式日志包装器 ============
template<InternalLogLevel Level>
class InternalLogStream {
public:
    InternalLogStream(const std::string& file = "", 
                     int line = 0,
                     const std::string& function = "")
        : file_(file), line_(line), function_(function) {
    }
    
    ~InternalLogStream() {
        // 在析构时输出日志
        if constexpr (InternalLogEnabled<Level>::value) {
            if (ss_.tellp() > 0) {
                InternalLogger::instance().log(Level, ss_.str(), 
                                              file_, line_, function_);
            }
        }
    }
    
    // 流式输出
    template<typename T>
    InternalLogStream& operator<<(const T& value) {
        if constexpr (InternalLogEnabled<Level>::value) {
            ss_ << value;
        }
        return *this;
    }
    
    // 支持 manipulators
    InternalLogStream& operator<<(std::ostream& (*manip)(std::ostream&)) {
        if constexpr (InternalLogEnabled<Level>::value) {
            ss_ << manip;
        }
        return *this;
    }
    
private:
    std::string file_;
    int line_;
    std::string function_;
    std::stringstream ss_;
};

} // namespace internal
} // namespace foundation

// ============ 内部日志宏 ============

// 基础宏
#define INTERNAL_LOG_STREAM(level, func) \
    foundation::internal::InternalLogStream< \
        foundation::internal::InternalLogLevel::level>( \
        __FILE__, __LINE__, func)

// 流式日志宏（全项目统一，仅此一套）
#define INTERNAL_TRACE_STREAM \
    INTERNAL_LOG_STREAM(TRACE, __FUNCTION__)

#define INTERNAL_DEBUG_STREAM \
    INTERNAL_LOG_STREAM(DEBUG, __FUNCTION__)

#define INTERNAL_INFO_STREAM \
    INTERNAL_LOG_STREAM(INFO, __FUNCTION__)

#define INTERNAL_WARN_STREAM \
    INTERNAL_LOG_STREAM(WARN, __FUNCTION__)

#define INTERNAL_ERROR_STREAM \
    INTERNAL_LOG_STREAM(ERR, __FUNCTION__)

#endif // FOUNDATION_INTERNAL_LOGGING_HPP