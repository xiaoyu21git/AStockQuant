// foundation/include/foundation.h
/**
 * @file foundation.h
 * @brief Foundation库统一对外接口
 * @version 1.0.0
 * @date 2024
 */

#pragma once

#include <string>
#include <memory>
#include <functional>
#include <chrono>
#include <vector>
#include "foundation/Fs/file.hpp"
#include "foundation/json/json_facade.h"
#include "foundation/yaml/yaml_facade.h"
#include "foundation/net/net_facade.h"
#include "foundation/Utils/random.hpp"
#include "foundation/Utils/time.hpp"
#include "foundation/Utils/string.hpp"
#include "foundation/Utils/system.hpp"
#include "foundation/core/exception.hpp"
#include "foundation/log/logger.hpp"
#include "foundation/thread/thread_pool.hpp"
#include "foundation/thread/ThreadPoolExecutor.h"

// ============ 前置声明 ============
namespace foundation {

// 异常类
class Exception;
class RuntimeException;
class FileException;
class NetworkException;
class ParseException;
class ConfigException;

// 各模块前置声明
namespace fs {
    class File;
}

namespace json {
    class JsonFacade;
}

namespace yaml {
    class YamlFacade;
}

namespace net {
    class HttpClient;
    class WebSocketConnection;
    class NetFacade;
    struct HttpRequest;
    struct HttpResponse;
}
namespace thread {

// 添加这行
using ThreadPoolPtr = ThreadPoolExecutorPtr;

} // namespace thread
// ============ 配置结构 ============
struct Config {
    // 日志配置
    std::string log_file = "foundation.log";
    bool enable_console_log = true;
    bool enable_file_log = false;
    LogLevel log_level = LogLevel::INFO;
    
    // 线程池配置
    size_t thread_pool_size = 4;
    
    // 网络配置
    std::chrono::milliseconds http_timeout = std::chrono::seconds(30);
    bool enable_http_proxy = false;
    std::string http_proxy_host;
    uint16_t http_proxy_port = 0;
    
    // 随机数种子
    unsigned int random_seed = 0;
    
    Config() = default;
};

// ============ Foundation主类 ============
class Foundation {
public:
    // 禁止拷贝
    Foundation(const Foundation&) = delete;
    Foundation& operator=(const Foundation&) = delete;
    
    // 单例访问
    static Foundation& instance();
    
    /**
     * @brief 初始化Foundation库
     */
    bool initialize(const Config& config = Config());
    
    /**
     * @brief 销毁Foundation库
     */
    void shutdown();
    
    /**
     * @brief 检查是否已初始化
     */
    bool is_initialized() const { return initialized_; }
    
    // ============ 模块访问器 ============
    ILogger& logger();
    fs::File& filesystem();
    json::JsonFacade& json();
    yaml::YamlFacade& yaml();
    net::HttpClient& http_client();
    net::WebSocketConnection& websocket();
    thread::ThreadPoolExecutor& thread_pool();
    utils::Random& random();
    utils::Time& time();
    utils::String& string();
    utils::SystemUtilsImpl& system();
    
    // ============ 便捷静态方法 ============
    // 日志
    static void log_trace(const std::string& message, 
                         const std::string& file = "", int line = 0);
    static void log_debug(const std::string& message,
                         const std::string& file = "", int line = 0);
    static void log_info(const std::string& message,
                        const std::string& file = "", int line = 0);
    static void log_warn(const std::string& message,
                        const std::string& file = "", int line = 0);
    static void log_error(const std::string& message,
                         const std::string& file = "", int line = 0);
    static void log_fatal(const std::string& message,
                         const std::string& file = "", int line = 0);
    
    // 文件操作
    static std::string read_file(const std::string& path);
    static bool write_file(const std::string& path, const std::string& content);
    static bool file_exists(const std::string& path);
    static std::vector<std::string> list_files(const std::string& path);
    
    // JSON操作
    static json::JsonFacade parse_json(const std::string& json_str);
    static json::JsonFacade load_json(const std::string& file_path);
    static bool save_json(const json::JsonFacade& json, const std::string& file_path);
    
    // YAML操作
    static yaml::YamlFacade parse_yaml(const std::string& yaml_str);
    static yaml::YamlFacade load_yaml(const std::string& file_path);
    static bool save_yaml(const yaml::YamlFacade& yaml, const std::string& file_path);
    
    // HTTP操作
    static net::HttpResponse http_get(const std::string& url);
    static net::HttpResponse http_post(const std::string& url, const std::string& body);
    static net::HttpResponse http_put(const std::string& url, const std::string& body);
    static net::HttpResponse http_delete(const std::string& url);
    
    // 随机数
    static int random_int(int min, int max);
    static double random_double(double min, double max);
    static std::string random_string(size_t length);
    static std::string generate_uuid();
    
    // 时间
    static int64_t timestamp();
    static int64_t timestamp_ms();
    static std::string current_time_string();
    static std::string current_time_string(const std::string& format);
    
    // 字符串
    static std::string trim(const std::string& str);
    static std::string to_lower(const std::string& str);
    static std::string to_upper(const std::string& str);
    static std::vector<std::string> split(const std::string& str, char delimiter);
    static std::string join(const std::vector<std::string>& strings, const std::string& delimiter);
    //static std::string format_string(const std::string& fmt, ...);
    static std::string format_string(const std::string fmt, ...);
    
    // 系统
    static std::string current_directory();
    static std::string home_directory();
    static std::string temp_directory();
    static int process_id();
    static std::string hostname();
    static std::string env(const std::string& name, const std::string& defaultValue = "");
    static bool set_env(const std::string& name, const std::string& value);
    
private:
    Foundation() = default;
    ~Foundation();
    
    // 各模块实例
    std::unique_ptr<ILogger> logger_;
    std::unique_ptr<fs::File> filesystem_;
    std::unique_ptr<json::JsonFacade> json_;
    std::unique_ptr<yaml::YamlFacade> yaml_;
    std::unique_ptr<net::HttpClient> http_client_;
    std::unique_ptr<net::WebSocketConnection> websocket_;
    thread::ThreadPoolPtr thread_pool_;
    std::unique_ptr<utils::Random> random_;
    std::unique_ptr<utils::Time> time_;
    std::unique_ptr<utils::String> string_;
    std::unique_ptr<utils::SystemUtilsImpl> system_;
    
    bool initialized_ = false;
};

// ============ 全局便捷函数 ============

/**
 * @brief 全局Foundation实例访问
 */
inline Foundation& app() {
    return Foundation::instance();
}

/**
 * @brief 快速初始化
 */
inline bool init(const std::string& config_file = "") {
    Config config;
    if (!config_file.empty()) {
        try {
            auto content = Foundation::read_file(config_file);
            // 可根据文件扩展名解析配置
        } catch (...) {
            // 忽略配置解析错误
        }
    }
    return app().initialize(config);
}

/**
 * @brief 快速关闭
 */
inline void shutdown() {
    app().shutdown();
}

// ============ 流畅接口 ============

// JSON构建器
class JsonBuilder {
private:
    json::JsonFacade json_;
    
public:
    JsonBuilder();
    explicit JsonBuilder(const json::JsonFacade& json);
    
    JsonBuilder& set(const std::string& key, const std::string& value);
    JsonBuilder& set(const std::string& key, int value);
    JsonBuilder& set(const std::string& key, double value);
    JsonBuilder& set(const std::string& key, bool value);
    JsonBuilder& set(const std::string& key, const JsonBuilder& value);
    
    json::JsonFacade build() const;
    std::string to_string() const;
    std::string to_pretty_string() const;
};

// HTTP请求构建器
class HttpRequestBuilder {
private:
    net::HttpRequest request_;
    
public:
    explicit HttpRequestBuilder(const std::string& url);
    
    HttpRequestBuilder& method(const std::string& method);
    HttpRequestBuilder& body(const std::string& body);
    HttpRequestBuilder& header(const std::string& name, const std::string& value);
    HttpRequestBuilder& timeout(std::chrono::milliseconds timeout);
    
    net::HttpRequest build() const;
    net::HttpResponse execute() const;
};

} // namespace foundation

// ============ 宏定义 ============

// 版本信息
#define FOUNDATION_VERSION_MAJOR 1
#define FOUNDATION_VERSION_MINOR 0
#define FOUNDATION_VERSION_PATCH 0
#define FOUNDATION_VERSION "1.0.0"

// 模块访问宏
#define FOUNDATION_APP foundation::app()
#define FOUNDATION_LOG FOUNDATION_APP.logger()
#define FOUNDATION_FS FOUNDATION_APP.filesystem()
#define FOUNDATION_JSON FOUNDATION_APP.json()
#define FOUNDATION_YAML FOUNDATION_APP.yaml()
#define FOUNDATION_HTTP FOUNDATION_APP.http_client()
#define FOUNDATION_WEBSOCKET FOUNDATION_APP.websocket()
#define FOUNDATION_THREADS FOUNDATION_APP.thread_pool()
#define FOUNDATION_RANDOM FOUNDATION_APP.random()
#define FOUNDATION_TIME FOUNDATION_APP.time()
#define FOUNDATION_STRING FOUNDATION_APP.string()
#define FOUNDATION_SYSTEM FOUNDATION_APP.system()

// 日志宏
#define LOG_TRACE(msg) foundation::Foundation::log_trace(msg, __FILE__, __LINE__)
#define LOG_DEBUG(msg) foundation::Foundation::log_debug(msg, __FILE__, __LINE__)
#define LOG_INFO(msg)  foundation::Foundation::log_info(msg, __FILE__, __LINE__)
#define LOG_WARN(msg)  foundation::Foundation::log_warn(msg, __FILE__, __LINE__)
#define LOG_ERROR(msg) foundation::Foundation::log_error(msg, __FILE__, __LINE__)
#define LOG_FATAL(msg) foundation::Foundation::log_fatal(msg, __FILE__, __LINE__)

// 文件操作宏
#define FILE_EXISTS(path) foundation::Foundation::file_exists(path)
#define READ_FILE(path) foundation::Foundation::read_file(path)
#define WRITE_FILE(path, content) foundation::Foundation::write_file(path, content)

// HTTP操作宏
#define HTTP_GET(url) foundation::Foundation::http_get(url)
#define HTTP_POST(url, body) foundation::Foundation::http_post(url, body)
#define HTTP_PUT(url, body) foundation::Foundation::http_put(url, body)
#define HTTP_DELETE(url) foundation::Foundation::http_delete(url)

// 工具宏
#define RANDOM_INT(min, max) foundation::Foundation::random_int(min, max)
#define RANDOM_STRING(len) foundation::Foundation::random_string(len)
#define GENERATE_UUID() foundation::Foundation::generate_uuid()
#define TIMESTAMP() foundation::Foundation::timestamp()
#define TIMESTAMP_MS() foundation::Foundation::timestamp_ms()
#define CURRENT_TIME() foundation::Foundation::current_time_string()

// 字符串操作宏
#define TRIM(str) foundation::Foundation::trim(str)
#define TO_LOWER(str) foundation::Foundation::to_lower(str)
#define TO_UPPER(str) foundation::Foundation::to_upper(str)

// 系统宏
#define CURRENT_DIR() foundation::Foundation::current_directory()
#define HOME_DIR() foundation::Foundation::home_directory()
#define TEMP_DIR() foundation::Foundation::temp_directory()
#define PROCESS_ID() foundation::Foundation::process_id()
#define HOSTNAME() foundation::Foundation::hostname()

// 断言宏
#ifdef NDEBUG
    #define FOUNDATION_ASSERT(expr, msg) ((void)0)
#else
    #define FOUNDATION_ASSERT(expr, msg) \
        do { \
            if (!(expr)) { \
                LOG_FATAL("Assertion failed: " msg); \
                std::terminate(); \
            } \
        } while(0)
#endif

// 异常处理宏
#define FOUNDATION_TRY try
#define FOUNDATION_CATCH catch (const foundation::Exception& e) { \
    LOG_ERROR(std::string("Exception: ") + e.what()); \
    throw; \
}

#define FOUNDATION_CATCH_RETURN(value) \
    catch (const foundation::Exception& e) { \
        LOG_ERROR(std::string("Exception: ") + e.what()); \
        return value; \
    }

// 全局便捷函数
inline foundation::JsonBuilder json() {
    return foundation::JsonBuilder();
}

inline foundation::HttpRequestBuilder http_get(const std::string& url) {
    return foundation::HttpRequestBuilder(url).method("GET");
}

inline foundation::HttpRequestBuilder http_post(const std::string& url) {
    return foundation::HttpRequestBuilder(url).method("POST");
}

inline foundation::HttpRequestBuilder http_put(const std::string& url) {
    return foundation::HttpRequestBuilder(url).method("PUT");
}

inline foundation::HttpRequestBuilder http_delete(const std::string& url) {
    return foundation::HttpRequestBuilder(url).method("DELETE");
}