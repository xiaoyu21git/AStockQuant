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
#include <map>
#include <unordered_map>
#include <shared_mutex>

// 现有模块
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
#include "foundation/net/http_request.h"
#include "foundation/net/http_response.h"
#include "foundation/Utils/Uuid.h"

// 新增 Config 模块
#include "foundation/config/ConfigManager.hpp"
#include "foundation/config/ConfigLoader.hpp"
#include "foundation/config/ConfigNode.hpp"
#include "foundation/log/logging.hpp"
#include "foundation/config/JsonConfigProvider.hpp"
#include "foundation/config/YamlConfigProvider.hpp"
// 1. 文件监控模块导出
#include "foundation/fs/FileWatcher.h"

// 2. 缓存工具模块导出
#include "foundation/utils/CacheUtils.h"
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
    using ThreadPoolPtr = ThreadPoolExecutorPtr;
}
using Uuid = utils::Uuid;
// ============ Config 模块前置声明 ============
namespace config {
    class ConfigManager;
    class ConfigLoader;
    class ConfigNode;
    class ConfigProvider;
    class JsonConfigProvider;
    class YamlConfigProvider;
    // 使用 ConfigManager 内部的类型
    using Domain = ConfigManager::Domain;
    
    // 监听器类型也需要匹配
    using ConfigChangeListener = ConfigManager::ConfigChangeListener;

} // namespace config

// ============ 配置结构 ============
struct Config {
    // 基础配置
    std::string profile = "default";
    std::string config_dir = "config";
    bool enable_config_cache = true;
    bool enable_config_watch = false;
    
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
    
    // 配置验证
    bool validate_configs = true;
    bool enable_env_vars = true;
    
    Config() = default;
    
    /**
     * @brief 从配置文件加载配置
     */
    static Config loadFromFile(const std::string& file_path);
    
    /**
     * @brief 保存配置到文件
     */
    bool saveToFile(const std::string& file_path) const;
    
    /**
     * @brief 转换为 ConfigNode
     */
    config::ConfigNode toConfigNode() const;
    
    /**
     * @brief 从 ConfigNode 加载
     */
    static Config fromConfigNode(const config::ConfigNode& node);
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
    
    /**
     * @brief 重新加载所有配置
     */
    void reload_configs();
    
    /**
     * @brief 获取当前配置
     */
    const Config& get_config() const { return config_; }
    
    // ============ 模块访问器 ============
    log::LoggerImpl& logger();
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
    
    // ============ 新增：Config模块访问器 ============
    config::ConfigManager& config_manager();
    config::ConfigLoader& config_loader();
     // 新增：FileWatcher 访问器
    fs::FileWatcher& file_watcher() {
        if (!file_watcher_) {
            file_watcher_ = std::make_unique<fs::FileWatcher>();
        }
        return *file_watcher_;
    }
    
    // 新增：CacheUtils 静态方法包装
    static std::string cache_stats_to_json(const utils::CacheStats& stats) {
        return utils::CacheUtils::statsToJson(stats);
    }
    
    static utils::CacheStats cache_json_to_stats(const std::string& json) {
        return utils::CacheUtils::jsonToStats(json);
    }
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
    
    // ============ 新增：Config便捷方法 ============
    // 配置操作
    static config::ConfigNode::Ptr load_config(const std::string& file_path);
    static config::ConfigNode::Ptr load_config_string(const std::string& content, 
                                                     const std::string& format);
    static bool save_config(const config::ConfigNode::Ptr& config, 
                           const std::string& file_path);
    
    
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
    static std::string join(const std::vector<std::string>& strings, 
                           const std::string& delimiter);
    static std::string format_string(const std::string fmt, ...);
    
    // 系统
    static std::string current_directory();
    static std::string home_directory();
    static std::string temp_directory();
    static int process_id();
    static std::string hostname();
    static std::string env(const std::string& name, 
                          const std::string& defaultValue = "");
    static bool set_env(const std::string& name, const std::string& value);
    
private:
    Foundation() = default;
    ~Foundation();
    
    // 各模块实例
    std::unique_ptr<log::LoggerImpl> logger_;
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
    // 新增成员
    std::unique_ptr<fs::FileWatcher> file_watcher_;
    // 新增：Config模块实例
     // 配置值获取（带默认值）
    template<typename T>
    static T get_config_value(const config::ConfigNode::Ptr& config,
                             const std::string& key,
                             const T& default_value);
    std::unique_ptr<config::ConfigManager> config_manager_;
    std::unique_ptr<config::ConfigLoader> config_loader_;
    config::ConfigNode::Ptr app_config_;
    
    Config config_;
    bool initialized_ = false;
};

// ============ Config 模块模板方法实现 ============
template<typename T>
T Foundation::get_config_value(const config::ConfigNode::Ptr& config,
                              const std::string& key,
                              const T& default_value) {
    if (!config || config->isNull()) {
        return default_value;
    }
    
    auto node = config->get(key);
    if (node.isNull()) {
        return default_value;
    }
    
    // 类型转换
    if constexpr (std::is_same_v<T, std::string>) {
        return node.asString();
    } else if constexpr (std::is_same_v<T, int>) {
        return node.asInt();
    } else if constexpr (std::is_same_v<T, double>) {
        return node.asDouble();
    } else if constexpr (std::is_same_v<T, bool>) {
        return node.asBool();
    } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
        if (node.isArray()) {
            std::vector<std::string> result;
            for (size_t i = 0; i < node.size(); ++i) {
                result.push_back(node[i].asString());
            }
            return result;
        }
        return default_value;
    } else {
        static_assert(sizeof(T) == 0, "Unsupported config value type");
    }
}

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
            // 尝试从配置文件加载
            auto content = Foundation::read_file(config_file);
            
            // 根据文件扩展名解析
            if (config_file.find(".json") != std::string::npos) {
                auto json = Foundation::parse_json(content);
                // 从JSON解析配置
                // config.profile = json.get("profile", "default").as_string();
                // 修复方案2：使用三元运算符（更简洁）
                config.profile = json.has("profile") ? json.get("profile").asString() : "default";
                 // 修复方案2：使用三元运算符（更简洁）
                config.profile = json.has("config_dir") ? json.get("config_dir").asString() : "config";
                 // 修复方案2：使用三元运算符（更简洁）
                config.profile = json.has("enable_config_cache") ? json.get("enable_config_cache").asBool() : true;
                //config.config_dir = json.get("config_dir", "config").as_string();
                //config.enable_config_cache = json.get("enable_config_cache", true).as_bool();
                // ... 其他配置
            } else if (config_file.find(".yaml") != std::string::npos || 
                      config_file.find(".yml") != std::string::npos) {
                auto yaml = Foundation::parse_yaml(content);
                // 从YAML解析配置
                // ... 类似JSON的解析
            }
        } catch (const std::exception& e) {
            Foundation::log_error("Failed to load config file: " + std::string(e.what()));
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

/**
 * @brief 重新加载配置
 */
inline void reload_configs() {
    app().reload_configs();
}

// ============ Config 便捷函数 ============

/**
 * @brief 获取应用配置值
 */
template<typename T>
inline T get_config(const std::string& key, const T& default_value = T()) {
    return Foundation::get_config_value(
        app().app_config(), key, default_value
    );
}

/**
 * @brief 设置运行时配置
 */
inline void set_runtime_config(const std::string& key, 
                              const config::ConfigNode& value,
                              bool persist = false) {
    app().config_manager().setRuntimeConfig(key, value, persist);
}

/**
 * @brief 获取模块配置
 */
inline config::ConfigNode::Ptr get_module_config(const std::string& module_name,
                                                const std::string& module_dir = "modules") {
    return app().config_manager().getModuleConfig(module_name, module_dir);
}

/**
 * @brief 监听配置变更
 */ 
inline void add_config_listener(config::ConfigChangeListener listener,
                               config::Domain domain = config::Domain::RUNTIME) {
    app().config_manager().addDomainListener(domain, listener);
}

// ============ 流畅接口 ============

/**
 * @brief HTTP请求构建器，提供流畅的API来构建和配置HTTP请求
 */
class HttpRequestBuilder {
private:
    mutable net::HttpRequest request_;
    mutable std::vector<std::string> errors_;
    
    // 验证相关方法
    bool is_valid_method(const std::string& method) const;
    bool is_valid_url(const std::string& url) const;
    void validate_method() const;
    void validate_url() const;
    
    // 编码工具
    static std::string url_encode(const std::string& str);
    static std::string form_url_encode(const std::map<std::string, std::string>& data);
    
    // 准备请求
    void prepare_headers();
    void prepare_body();
    void prepare_query_string()const ;
    
public:
    // ============ 构造函数 ============
    
    /**
     * @brief 创建HTTP请求构建器
     * @param url 请求URL
     */
    explicit HttpRequestBuilder(const std::string& url);
    
    /**
     * @brief 创建HTTP请求构建器
     * @param url 请求URL
     */
    explicit HttpRequestBuilder(std::string_view url);
    
    // ============ 基本配置方法 ============
    
    /**
     * @brief 设置HTTP方法
     * @param method HTTP方法（GET, POST, PUT, DELETE等）
     * @return 当前构建器引用
     */
    HttpRequestBuilder& method(const std::string& method);
    
    /**
     * @brief 设置请求体（字符串）
     * @param body 请求体内容
     * @return 当前构建器引用
     */
    HttpRequestBuilder& body(const std::string& body);
    
    /**
     * @brief 设置请求体（字符串视图）
     * @param body 请求体内容
     * @return 当前构建器引用
     */
    HttpRequestBuilder& body(std::string_view body);
    
    /**
     * @brief 添加请求头
     * @param name 头名称
     * @param value 头值
     * @return 当前构建器引用
     */
    HttpRequestBuilder& header(const std::string& name, const std::string& value);
    
    /**
     * @brief 添加请求头（字符串视图）
     * @param name 头名称
     * @param value 头值
     * @return 当前构建器引用
     */
    HttpRequestBuilder& header(std::string_view name, std::string_view value);
    
    /**
     * @brief 设置超时时间
     * @param timeout 超时时间（毫秒）
     * @return 当前构建器引用
     */
    HttpRequestBuilder& timeout(std::chrono::milliseconds timeout);
    
    /**
     * @brief 设置代理
     * @param host 代理主机
     * @param port 代理端口
     * @return 当前构建器引用
     */
    HttpRequestBuilder& proxy(const std::string& host, uint16_t port);
    
    // ============ 增强的便捷方法 ============
    
    /**
     * @brief 设置为GET请求
     * @return 当前构建器引用
     */
    HttpRequestBuilder& get();
    
    /**
     * @brief 设置为POST请求
     * @return 当前构建器引用
     */
    HttpRequestBuilder& post();
    
    /**
     * @brief 设置为PUT请求
     * @return 当前构建器引用
     */
    HttpRequestBuilder& put();
    
    /**
     * @brief 设置为DELETE请求
     * @return 当前构建器引用
     */
    HttpRequestBuilder& delete_();
    
    /**
     * @brief 设置为PATCH请求
     * @return 当前构建器引用
     */
    HttpRequestBuilder& patch();
    
    /**
     * @brief 设置为HEAD请求
     * @return 当前构建器引用
     */
    HttpRequestBuilder& head();
    
    // JSON相关方法
    HttpRequestBuilder& json(const std::string& json);
    HttpRequestBuilder& json(std::string_view json);
    
    // 表单数据
    HttpRequestBuilder& form(const std::map<std::string, std::string>& form_data);
    HttpRequestBuilder& form_field(const std::string& name, const std::string& value);
    
    // Cookie管理
    HttpRequestBuilder& cookie(const std::string& name, const std::string& value);
    HttpRequestBuilder& cookies(const std::map<std::string, std::string>& cookies);
    
    // 查询参数
    HttpRequestBuilder& query_param(const std::string& name, const std::string& value);
    HttpRequestBuilder& query_params(const std::map<std::string, std::string>& params);
    
    // 认证相关
    HttpRequestBuilder& authorization(const std::string& scheme, const std::string& credentials);
    HttpRequestBuilder& bearer_token(const std::string& token);
    HttpRequestBuilder& basic_auth(const std::string& username, const std::string& password);
    
    // 内容类型和接受类型
    HttpRequestBuilder& content_type(const std::string& type);
    HttpRequestBuilder& accept(const std::string& type);
    HttpRequestBuilder& accept_json();
    HttpRequestBuilder& accept_xml();
    HttpRequestBuilder& user_agent(const std::string& agent);
    
    // SSL和安全设置
    HttpRequestBuilder& verify_ssl(bool verify = true);
    HttpRequestBuilder& ssl_cert_path(const std::string& cert_path);
    HttpRequestBuilder& ssl_key_path(const std::string& key_path);
    HttpRequestBuilder& ssl_ca_path(const std::string& ca_path);
    
    // 重定向设置
    HttpRequestBuilder& follow_redirects(bool follow = true);
    HttpRequestBuilder& max_redirects(int max_redirects);
    
    // 连接设置
    HttpRequestBuilder& keep_alive(bool keep_alive = true);
    HttpRequestBuilder& compress(bool compress = true);
    
    // 详细的超时设置
    HttpRequestBuilder& connect_timeout(std::chrono::milliseconds timeout);
    HttpRequestBuilder& read_timeout(std::chrono::milliseconds timeout);
    HttpRequestBuilder& write_timeout(std::chrono::milliseconds timeout);
    HttpRequestBuilder& timeout_seconds(int seconds);
    HttpRequestBuilder& timeout_milliseconds(int ms);
    
    // 代理认证
    HttpRequestBuilder& proxy_auth(const std::string& username, const std::string& password);
    
    // ============ 构建和执行 ============
    
    /**
     * @brief 构建HttpRequest对象
     * @return 配置好的HttpRequest对象
     * @throws std::runtime_error 如果验证失败
     */
    net::HttpRequest build()const ;
    
    /**
     * @brief 验证当前配置是否有效
     * @return true如果配置有效
     */
    bool validate() const;
    
    /**
     * @brief 获取验证错误信息
     * @return 错误信息列表
     */
    std::vector<std::string> get_errors() const;
    
    /**
     * @brief 同步执行HTTP请求
     * @return HTTP响应
     * @throws std::runtime_error 如果请求失败
     */
    net::HttpResponse execute() const;
    
    /**
     * @brief 异步执行HTTP请求
     * @return future对象，包含HTTP响应
     */
    std::future<net::HttpResponse> execute_async() const;
    
    /**
     * @brief 使用回调异步执行HTTP请求
     * @param callback 回调函数
     */
    void execute_async(std::function<void(net::HttpResponse)> callback) const;
    
    // ============ 工具方法 ============
    
    /**
     * @brief 重置构建器（保留URL）
     * @return 当前构建器引用
     */
    HttpRequestBuilder& reset();
    
    /**
     * @brief 克隆当前构建器
     * @return 新的构建器实例
     */
    HttpRequestBuilder clone() const;
    
    /**
     * @brief 获取当前URL
     * @return URL字符串
     */
    std::string get_url() const { return request_.url; }
    
    /**
     * @brief 获取当前HTTP方法
     * @return HTTP方法字符串
     */
    std::string get_method() const { return request_.method; }
};

// ============ 工厂函数 ============

/**
 * @brief 创建GET请求构建器
 * @param url 请求URL
 * @return HttpRequestBuilder实例
 */
inline HttpRequestBuilder get(const std::string& url) {
    return HttpRequestBuilder(url).get();
}

/**
 * @brief 创建POST请求构建器
 * @param url 请求URL
 * @return HttpRequestBuilder实例
 */
inline HttpRequestBuilder post(const std::string& url) {
    return HttpRequestBuilder(url).post();
}

/**
 * @brief 创建JSON POST请求构建器
 * @param url 请求URL
 * @param json JSON数据
 * @return HttpRequestBuilder实例
 */
inline HttpRequestBuilder post_json(const std::string& url, const std::string& json) {
    return HttpRequestBuilder(url).post().json(json);
}

/**
 * @brief 创建表单POST请求构建器
 * @param url 请求URL
 * @param form_data 表单数据
 * @return HttpRequestBuilder实例
 */
inline HttpRequestBuilder post_form(const std::string& url, 
                                   const std::map<std::string, std::string>& form_data) {
    return HttpRequestBuilder(url).post().form(form_data);
}
// ===== UUID 便捷创建接口 =====

// 默认生成（v4）
static inline Uuid create() {
    return utils::Uuid::generate_v4();
}

// 显式 v4
static inline Uuid create_uuid_v4() {
    return utils::Uuid::generate_v4();
}

// 从字符串
static inline Uuid uuid_from_string(const std::string& str) {
    return utils::Uuid::from_string(str);
}

// 空 UUID
static inline Uuid null_uuid() {
    return utils::Uuid::null();
}



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

// 新增：Config模块宏
#define FOUNDATION_CONFIG_MANAGER FOUNDATION_APP.config_manager()
#define FOUNDATION_CONFIG_LOADER FOUNDATION_APP.config_loader()
#define FOUNDATION_APP_CONFIG FOUNDATION_APP.app_config()
// 4. 添加相关宏
#define FOUNDATION_FILE_WATCHER FOUNDATION_APP.file_watcher()
#define CACHE_STATS_TO_JSON(stats) foundation::Foundation::cache_stats_to_json(stats)
#define CACHE_JSON_TO_STATS(json) foundation::Foundation::cache_json_to_stats(json)


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

// Config操作宏
#define LOAD_CONFIG(path) foundation::Foundation::load_config(path)
#define SAVE_CONFIG(config, path) foundation::Foundation::save_config(config, path)
#define GET_CONFIG(key, default_value) foundation::get_config(key, default_value)

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

// Config验证宏
#define VALIDATE_CONFIG(domain) FOUNDATION_CONFIG_MANAGER.validate(domain)
#define VALIDATE_APP_CONFIG() FOUNDATION_CONFIG_MANAGER.validateAppConfig()

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

// ============ 全局便捷函数 ============


// 5. 在全局便捷函数中添加
inline foundation::fs::FileWatcher& file_watcher() {
    return FOUNDATION_FILE_WATCHER;
}
// HTTP构建
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
} // namespace foundation

// ============ 配置文件宏 ============

// 常用配置路径宏
#define CONFIG_PATH_APP "app"
#define CONFIG_PATH_SERVER "server"
#define CONFIG_PATH_DATABASE "database"
#define CONFIG_PATH_LOGGING "logging"
#define CONFIG_PATH_FEATURES "features"

// 配置值获取宏（简化版）
#define CONFIG_STRING(key, default_val) \
    foundation::Foundation::get_app_config_string(key, default_val)

#define CONFIG_INT(key, default_val) \
    foundation::Foundation::get_app_config_int(key, default_val)

#define CONFIG_DOUBLE(key, default_val) \
    foundation::Foundation::get_app_config_double(key, default_val)

#define CONFIG_BOOL(key, default_val) \
    foundation::Foundation::get_app_config_bool(key, default_val)

// 配置监听宏
#define ON_CONFIG_CHANGE(domain, callback) \
    foundation::add_config_listener(callback, domain)

#define ON_CONFIG_PATH(path_pattern, callback) \
    app().config_manager().addPathListener(path_pattern, callback)