// foundation.cpp
#include "foundation.h"
#include <iostream>
#include <sstream>
#include <cstdarg>
#include "foundation/net/http_request_builder.h"

namespace foundation {

// ============ Foundation 类实现 ============

Foundation& Foundation::instance() {
    static Foundation instance;
    return instance;
}



Foundation::~Foundation() {
    shutdown();
}

bool Foundation::initialize(const Config& config) {
    if (initialized_) {
        return true;
    }
    
    config_ = config;
    
    try {
        // 1. 初始化日志系统
        logger_ = std::make_unique<foundation::log::LoggerImpl>();
        
        // 设置日志级别（注意：方法名是 setLevel，不是 set_level）
        logger_->setLevel(config.log_level);
        
        // 添加控制台处理器（如果需要）
        if (config.enable_console_log) {
            auto console_handler = std::make_unique<foundation::log::ConsoleHandler>();
            console_handler->setFormatter(
                std::make_unique<foundation::log::DefaultFormatter>()
            );
            logger_->addHandler(std::move(console_handler));
        }
        // 添加文件处理器（如果需要）
        if (config.enable_file_log && !config.log_file.empty()) {
            auto file_handler = std::make_unique<foundation::log::FileHandler>(
                config.log_file, true  // append mode
            );
            file_handler->setFormatter(
                std::make_unique<foundation::log::DefaultFormatter>()
            );
            logger_->addHandler(std::move(file_handler));
        }
        
        // 测试日志
        logger_->info("Foundation logger initialized successfully");
        // 2. 初始化文件系统
        filesystem_ = std::make_unique<fs::File>();
        
        // 3. 初始化JSON和YAML
        json_ = std::make_unique<json::JsonFacade>();
        yaml_ = std::make_unique<yaml::YamlFacade>();
        
        // 4. 初始化网络
        http_client_ = std::make_unique<net::HttpClient>();
        websocket_ = std::make_unique<net::WebSocketConnection>();
        using std::max;
        // 5. 初始化线程池
        thread_pool_ = std::make_shared<thread::ThreadPoolExecutor>(
            config.thread_pool_size,      // 核心线程数
            max(config.thread_pool_size * 2, size_t(4)),  // 最大线程数
            std::chrono::seconds(60),     // 线程保活时间
            "Foundation-ThreadPool"       // 线程池名称
        );
        
        // 6. 初始化工具类
        random_ = std::make_unique<utils::Random>(); 
        utils::Random::setGlobalSeed(config.random_seed);
        time_ = std::make_unique<utils::Time>();
        string_ = std::make_unique<utils::String>();
        system_ = std::make_unique<utils::SystemUtilsImpl>();
        
        // 7. 初始化配置系统
        config_manager_ = std::make_unique<config::ConfigManager>();
        config_loader_ = std::make_unique<config::ConfigLoader>();
        
        // 注册配置提供器
        config_loader_->registerProvider(
            "json",
            std::make_unique<config::JsonConfigProvider>()
        );
        config_loader_->registerProvider(
            "yaml",
            std::make_unique<config::YamlConfigProvider>()
        );
        config_loader_->registerProvider(
            "yml",
            std::make_unique<config::YamlConfigProvider>()
        );
        
        // 初始化配置管理器
        config_manager_->initialize(config.profile, config.config_dir);
        
        // 8. 初始化应用配置
        app_config_ = config_manager_->getAppConfig();
        
        // 9. 设置HTTP超时
        http_client_->set_timeout(config.http_timeout);
        
        // 10. 设置HTTP代理
        if (config.enable_http_proxy && !config.http_proxy_host.empty()) {
            http_client_->set_proxy(config.http_proxy_host, config.http_proxy_port);
        }
        
        initialized_ = true;
        log_info("Foundation library initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        log_error(std::string("Failed to initialize Foundation: ") + e.what());
        shutdown();
        return false;
    }
}

void Foundation::shutdown() {
    if (!initialized_) return;
    
    log_info("Shutting down Foundation library");
    
    // 清理顺序很重要：先停止依赖其他模块的模块
    
    // 1. 停止网络连接
    if (websocket_) {
        websocket_->disconnect();
    }
    
    // 2. 停止线程池
    if (thread_pool_) {
        thread_pool_->shutdown();
    }
    
    // 3. 清理各模块
    config_manager_.reset();
    config_loader_.reset();
    app_config_.reset();
    system_.reset();
    string_.reset();
    time_.reset();
    random_.reset();
    thread_pool_.reset();
    websocket_.reset();
    http_client_.reset();
    yaml_.reset();
    json_.reset();
    filesystem_.reset();
    logger_.reset();
    
    initialized_ = false;
    log_info("Foundation library shutdown complete");
}

void Foundation::reload_configs() {
    if (!initialized_) return;
    
    try {
        config_manager_->reloadAll();
        app_config_ = config_manager_->getAppConfig();
        log_info("All configurations reloaded");
    } catch (const std::exception& e) {
        log_error(std::string("Failed to reload configs: ") + e.what());
    }
}

// ============ 模块访问器实现 ============

log::LoggerImpl& Foundation::logger() {
    if (!logger_) {
        throw RuntimeException("Logger not initialized");
    }
    return *logger_;
}

fs::File& Foundation::filesystem() {
    if (!filesystem_) {
        throw RuntimeException("Filesystem not initialized");
    }
    return *filesystem_;
}

json::JsonFacade& Foundation::json() {
    if (!json_) {
        throw RuntimeException("JSON facade not initialized");
    }
    return *json_;
}

yaml::YamlFacade& Foundation::yaml() {
    if (!yaml_) {
        throw RuntimeException("YAML facade not initialized");
    }
    return *yaml_;
}

net::HttpClient& Foundation::http_client() {
    if (!http_client_) {
        throw RuntimeException("HTTP client not initialized");
    }
    return *http_client_;
}

net::WebSocketConnection& Foundation::websocket() {
    if (!websocket_) {
        throw RuntimeException("WebSocket not initialized");
    }
    return *websocket_;
}

thread::ThreadPoolExecutor& Foundation::thread_pool() {
    if (!thread_pool_) {
        throw RuntimeException("Thread pool not initialized");
    }
    return *thread_pool_;
}

utils::Random& Foundation::random() {
    if (!random_) {
        throw RuntimeException("Random not initialized");
    }
    return *random_;
}

utils::Time& Foundation::time() {
    if (!time_) {
        throw RuntimeException("Time not initialized");
    }
    return *time_;
}

utils::String& Foundation::string() {
    if (!string_) {
        throw RuntimeException("String not initialized");
    }
    return *string_;
}

utils::SystemUtilsImpl& Foundation::system() {
    if (!system_) {
        throw RuntimeException("System not initialized");
    }
    return *system_;
}

config::ConfigManager& Foundation::config_manager() {
    if (!config_manager_) {
        throw RuntimeException("Config manager not initialized");
    }
    return *config_manager_;
}

config::ConfigLoader& Foundation::config_loader() {
    if (!config_loader_) {
        throw RuntimeException("Config loader not initialized");
    }
    return *config_loader_;
}


// ============ 静态日志方法实现 ============

void Foundation::log_trace(const std::string& message, 
                          const std::string& file, int line) {
    instance().logger().trace(message, file, line);
}

void Foundation::log_debug(const std::string& message,
                          const std::string& file, int line) {
    instance().logger().debug(message, file, line);
}

void Foundation::log_info(const std::string& message,
                         const std::string& file, int line) {
    instance().logger().info(message, file, line);
}

void Foundation::log_warn(const std::string& message,
                         const std::string& file, int line) {
    instance().logger().warning(message, file, line);
}

void Foundation::log_error(const std::string& message,
                          const std::string& file, int line) {
    instance().logger().error(message, file, line);
}

void Foundation::log_fatal(const std::string& message,
                          const std::string& file, int line) {
    instance().logger().fatal(message, file, line);
}

// ============ 静态文件操作方法实现 ============

std::string Foundation::read_file(const std::string& path) {
    return fs::File::readText(path);
}

bool Foundation::write_file(const std::string& path, const std::string& content) {
    return fs::File::writeText(path, content);
}

bool Foundation::file_exists(const std::string& path) {
    return fs::File::exists(path);
}

std::vector<std::string> Foundation::list_files(const std::string& path) {
    return fs::File::listFiles(path);
}

// ============ 静态JSON操作方法实现 ============

json::JsonFacade Foundation::parse_json(const std::string& json_str) {
    return json::JsonFacade::parse(json_str);
}

json::JsonFacade Foundation::load_json(const std::string& file_path) {
    return json::JsonFacade::parseFile(file_path);
}

bool Foundation::save_json(const json::JsonFacade& json, const std::string& file_path) {
    return json.saveToFile(file_path);
}

// ============ 静态YAML操作方法实现 ============

yaml::YamlFacade Foundation::parse_yaml(const std::string& yaml_str) {
    return yaml::YamlFacade::parse(yaml_str);
}

yaml::YamlFacade Foundation::load_yaml(const std::string& file_path) {
    return yaml::YamlFacade::createFrom(file_path);
}

bool Foundation::save_yaml(const yaml::YamlFacade& yaml, const std::string& file_path) {
    return yaml.saveToFile(file_path);
}

// ============ 静态配置操作方法实现 ============

config::ConfigNode::Ptr Foundation::load_config(const std::string& file_path) {
    try {
        // 根据扩展名判断格式
        if (file_path.find(".json") != std::string::npos) {
            auto content = read_file(file_path);
            auto json = parse_json(content);
            return std::make_shared<config::ConfigNode>(json);
        } else if (file_path.find(".yaml") != std::string::npos ||
                   file_path.find(".yml") != std::string::npos) {
            auto content = read_file(file_path);
            auto yaml = parse_yaml(content);
            return std::make_shared<config::ConfigNode>(yaml);
        } else {
            throw ParseException("Unsupported config file format: " + file_path);
        }
    } catch (const std::exception& e) {
        log_error(std::string("Failed to load config: ") + e.what());
        throw;
    }
}

config::ConfigNode::Ptr Foundation::load_config_string(const std::string& content, 
                                                      const std::string& format) {
    try {
        if (format == "json") {
            auto json = parse_json(content);
            return std::make_shared<config::ConfigNode>(json);
        } else if (format == "yaml" || format == "yml") {
            auto yaml = parse_yaml(content);
            return std::make_shared<config::ConfigNode>(yaml);
        } else {
            throw ParseException("Unsupported config format: " + format);
        }
    } catch (const std::exception& e) {
        log_error(std::string("Failed to parse config string: ") + e.what());
        throw;
    }
}

bool Foundation::save_config(const config::ConfigNode::Ptr& config, 
                            const std::string& file_path) {
    if (!config) return false;
    
    try {
        if (file_path.find(".json") != std::string::npos) {
            return write_file(file_path, config->toJsonString());
        } else if (file_path.find(".yaml") != std::string::npos ||
                   file_path.find(".yml") != std::string::npos) {
            return write_file(file_path, config->toYamlString());
        } else {
            throw ParseException("Unsupported config file format: " + file_path);
        }
    } catch (const std::exception& e) {
        log_error(std::string("Failed to save config: ") + e.what());
        return false;
    }
}


// ============ 静态HTTP操作方法实现 ============

net::HttpResponse Foundation::http_get(const std::string& url) {
    return instance().http_client().get(url);
}

net::HttpResponse Foundation::http_post(const std::string& url, const std::string& body) {
    return instance().http_client().post(url, body);
}

net::HttpResponse Foundation::http_put(const std::string& url, const std::string& body) {
    return instance().http_client().put(url, body);
}

net::HttpResponse Foundation::http_delete(const std::string& url) {
    return instance().http_client().delete_(url);
}

// ============ 静态随机数方法实现 ============

int Foundation::random_int(int min, int max) {
    return 0;//instance().random().next_int(min, max);
}

double Foundation::random_double(double min, double max) {
    return 0;//instance().random().next_double(min, max);
}

std::string Foundation::random_string(size_t length) {
    return 0;//instance().random().next_string(length);
}

std::string Foundation::generate_uuid() {
    return instance().random().generateUuid();
}

// ============ 静态时间方法实现 ============

int64_t Foundation::timestamp() {
    return  0;//instance().time().timestamp();
}

int64_t Foundation::timestamp_ms() {
    return 0;//instance().time().timestamp_ms();
}

std::string Foundation::current_time_string() {
    return 0;//instance().time().current_time_string();
}

std::string Foundation::current_time_string(const std::string& format) {
    return "";//instance().time().format_time(format);
}

// ============ 静态字符串方法实现 ============

std::string Foundation::trim(const std::string& str) {
    return instance().string().trim(str);
}

std::string Foundation::to_lower(const std::string& str) {
    return "";//instance().string().to_lower(str);
}

std::string Foundation::to_upper(const std::string& str) {
    return "";//instance().string().to_upper(str);
}

std::vector<std::string> Foundation::split(const std::string& str, char delimiter) {
    return instance().string().split(str, delimiter);
}

std::string Foundation::join(const std::vector<std::string>& strings, 
                            const std::string& delimiter) {
    return instance().string().join(strings, delimiter);
}

std::string Foundation::format_string(const std::string fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt.c_str(), args);
    
    va_end(args);
    return std::string(buffer);
}

// ============ 静态系统方法实现 ============

std::string Foundation::current_directory() {
    return instance().system().getCurrentDirectory();
}

std::string Foundation::home_directory() {
    return instance().system().getHomeDirectory();
}

std::string Foundation::temp_directory() {
    return instance().system().getTempDirectory();
}

int Foundation::process_id() {
    return instance().system().getProcessId();
}

std::string Foundation::hostname() {
    return instance().system().getHostname();
}

std::string Foundation::env(const std::string& name, 
                           const std::string& defaultValue) {
    return instance().system().getEnvironmentVariable(name, defaultValue);
}

bool Foundation::set_env(const std::string& name, const std::string& value) {
    return instance().system().setEnvironmentVariable(name, value);
}

// ============ Config 结构实现 ============

Config Config::loadFromFile(const std::string& file_path) {
    auto node = Foundation::load_config(file_path);
    return fromConfigNode(*node);
}

bool Config::saveToFile(const std::string& file_path) const {
    auto node = toConfigNode();
    return Foundation::save_config(std::make_shared<config::ConfigNode>(node), file_path);
}

config::ConfigNode Config::toConfigNode() const {
    config::ConfigNode node;
    using foundation::json::JsonFacade;

    JsonFacade root = JsonFacade::createObject();
    // ===== 基础配置 =====
    root.set("profile",JsonFacade::createString(profile));
    root.set("config_dir", JsonFacade::createString(config_dir));
    root.set("enable_config_cache", JsonFacade::createBool(enable_config_cache));
    root.set("enable_config_watch", JsonFacade::createBool(enable_config_watch));

    // ===== 日志配置 =====
    {
        JsonFacade logging = JsonFacade::createObject();
        logging.set("log_file", JsonFacade::createString(log_file));
        logging.set("enable_console_log",JsonFacade::createBool(enable_console_log));
        logging.set("enable_file_log", JsonFacade::createBool(enable_file_log));
        logging.set("log_level", JsonFacade::createInt(static_cast<int>(log_level)));
        root.set("logging", logging);
    }

    // ===== 线程池 =====
    {
        JsonFacade threadPool = JsonFacade::createObject();
        threadPool.set("size", JsonFacade::createInt(static_cast<int>(thread_pool_size)));
        root.set("thread_pool", threadPool);
    }

    // ===== 网络配置 =====
    {
        JsonFacade network = JsonFacade::createObject();
        network.set("http_timeout", JsonFacade::createInt(static_cast<int>(http_timeout.count())));
        network.set("enable_http_proxy", JsonFacade::createBool(enable_http_proxy));
        network.set("http_proxy_host", JsonFacade::createString(http_proxy_host));
        network.set("http_proxy_port", JsonFacade::createInt(static_cast<int>(http_proxy_port)));
        root.set("network", network);
    }

    // ===== 随机数 =====
    {
        JsonFacade random = JsonFacade::createObject();
        random.set("seed", JsonFacade::createInt(static_cast<int>(random_seed)));
        root.set("random", random);
    }

    // ===== 配置系统行为 =====
    {
        JsonFacade config = JsonFacade::createObject();
        config.set("validate_configs",JsonFacade::createBool( validate_configs));
        config.set("enable_env_vars",JsonFacade::createBool (enable_env_vars));
        root.set("config", config);
    }

    // 一次性生成 ConfigNode（值对象）
    return config::ConfigNode(root);
}

namespace {
    const std::vector<std::string> VALID_METHODS = {
        "GET", "POST", "PUT", "DELETE", "PATCH", 
        "HEAD", "OPTIONS", "TRACE", "CONNECT"
    };
    
    const std::vector<std::string> METHODS_WITH_BODY = {
        "POST", "PUT", "PATCH"
    };
}

// ============ 构造函数 ============

HttpRequestBuilder::HttpRequestBuilder(const std::string& url) 
    : request_{} {
    request_.url = url;
    request_.method = "GET";
    request_.timeout = std::chrono::seconds(30);
    request_.follow_redirects = true;
    request_.max_redirects = 10;
    request_.verify_ssl = true;
    request_.keep_alive = true;
}

HttpRequestBuilder::HttpRequestBuilder(std::string_view url)
    : HttpRequestBuilder(std::string(url)) {}

// ============ 基本配置方法 ============

HttpRequestBuilder& HttpRequestBuilder::method(const std::string& method) {
    std::string upper_method = method;
    std::transform(upper_method.begin(), upper_method.end(), 
                   upper_method.begin(), ::toupper);
    
    if (!is_valid_method(upper_method)) {
        errors_.push_back("Invalid HTTP method: " + method);
        return *this;
    }
    
    request_.method = upper_method;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::body(const std::string& body) {
    request_.body = body;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::body(std::string_view body) {
    request_.body = std::string(body);
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::header(const std::string& name, const std::string& value) {
    request_.headers[name] = value;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::header(std::string_view name, std::string_view value) {
    request_.headers[std::string(name)] = std::string(value);
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::timeout(std::chrono::milliseconds timeout) {
    request_.timeout = timeout;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::proxy(const std::string& host, uint16_t port) {
    request_.proxy_host = host;
    request_.proxy_port = port;
    return *this;
}

// ============ 便捷方法 ============

HttpRequestBuilder& HttpRequestBuilder::get() {
    return method("GET");
}

HttpRequestBuilder& HttpRequestBuilder::post() {
    return method("POST");
}

HttpRequestBuilder& HttpRequestBuilder::put() {
    return method("PUT");
}

HttpRequestBuilder& HttpRequestBuilder::delete_() {
    return method("DELETE");
}

HttpRequestBuilder& HttpRequestBuilder::patch() {
    return method("PATCH");
}

HttpRequestBuilder& HttpRequestBuilder::head() {
    return method("HEAD");
}

HttpRequestBuilder& HttpRequestBuilder::json(const std::string& json) {
    request_.body = json;
    request_.headers["Content-Type"] = "application/json";
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::json(std::string_view json) {
    request_.body = std::string(json);
    request_.headers["Content-Type"] = "application/json";
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::form(const std::map<std::string, std::string>& form_data) {
    request_.body = form_url_encode(form_data);
    request_.headers["Content-Type"] = "application/x-www-form-urlencoded";
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::form_field(const std::string& name, const std::string& value) {
    if (request_.headers["Content-Type"] != "application/x-www-form-urlencoded") {
        request_.body.clear();
        request_.headers["Content-Type"] = "application/x-www-form-urlencoded";
    }
    
    if (!request_.body.empty()) {
        request_.body += "&";
    }
    
    request_.body += url_encode(name) + "=" + url_encode(value);
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::cookie(const std::string& name, const std::string& value) {
    std::string cookie_str = url_encode(name) + "=" + url_encode(value);
    
    if (request_.headers.find("Cookie") != request_.headers.end()) {
        request_.headers["Cookie"] += "; " + cookie_str;
    } else {
        request_.headers["Cookie"] = cookie_str;
    }
    
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::cookies(const std::map<std::string, std::string>& cookies) {
    for (const auto& [name, value] : cookies) {
        cookie(name, value);
    }
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::query_param(const std::string& name, const std::string& value) {
    request_.query_params[name] = value;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::query_params(const std::map<std::string, std::string>& params) {
    for (const auto& [name, value] : params) {
        request_.query_params[name] = value;
    }
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::authorization(const std::string& scheme, const std::string& credentials) {
    request_.headers["Authorization"] = scheme + " " + credentials;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::bearer_token(const std::string& token) {
    return authorization("Bearer", token);
}

HttpRequestBuilder& HttpRequestBuilder::basic_auth(const std::string& username, const std::string& password) {
    std::string credentials = username + ":" + password;
    // 注意：实际应该进行base64编码，这里简化处理
    return authorization("Basic", credentials);
}

HttpRequestBuilder& HttpRequestBuilder::content_type(const std::string& type) {
    request_.headers["Content-Type"] = type;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::accept(const std::string& type) {
    request_.headers["Accept"] = type;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::accept_json() {
    return accept("application/json");
}

HttpRequestBuilder& HttpRequestBuilder::accept_xml() {
    return accept("application/xml");
}

HttpRequestBuilder& HttpRequestBuilder::user_agent(const std::string& agent) {
    request_.headers["User-Agent"] = agent;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::verify_ssl(bool verify) {
    request_.verify_ssl = verify;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::ssl_cert_path(const std::string& cert_path) {
    request_.ssl_cert_path = cert_path;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::ssl_key_path(const std::string& key_path) {
    request_.ssl_key_path = key_path;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::ssl_ca_path(const std::string& ca_path) {
    request_.ssl_ca_path = ca_path;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::follow_redirects(bool follow) {
    request_.follow_redirects = follow;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::max_redirects(int max_redirects) {
    request_.max_redirects = max_redirects;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::keep_alive(bool keep_alive) {
    request_.keep_alive = keep_alive;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::compress(bool compress) {
    if (compress) {
        request_.headers["Accept-Encoding"] = "gzip, deflate";
    } else {
        request_.headers.erase("Accept-Encoding");
    }
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::connect_timeout(std::chrono::milliseconds timeout) {
    request_.connect_timeout = timeout;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::read_timeout(std::chrono::milliseconds timeout) {
    request_.read_timeout = timeout;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::write_timeout(std::chrono::milliseconds timeout) {
    request_.write_timeout = timeout;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::timeout_seconds(int seconds) {
    return timeout(std::chrono::seconds(seconds));
}

HttpRequestBuilder& HttpRequestBuilder::timeout_milliseconds(int ms) {
    return timeout(std::chrono::milliseconds(ms));
}

HttpRequestBuilder& HttpRequestBuilder::proxy_auth(const std::string& username, const std::string& password) {
    request_.proxy_username = username;
    request_.proxy_password = password;
    return *this;
}

// ============ 构建和执行 ============

net::HttpRequest HttpRequestBuilder::build() const {
    // 验证配置
    validate();
    
    if (!errors_.empty()) {
        std::string error_msg = "HTTP request validation failed:";
        for (const auto& error : errors_) {
            error_msg += "\n  - " + error;
        }
        throw std::runtime_error(error_msg);
    }
    
    // 创建请求副本
    net::HttpRequest request = request_;
    
    // 准备请求
    prepare_query_string();
    
    // 设置默认User-Agent
    if (request.headers.find("User-Agent") == request.headers.end()) {
        request.headers["User-Agent"] = "Foundation-HttpClient/1.0";
    }
    
    // 如果是POST/PUT/PATCH但没有Content-Type，设置默认值
    bool method_needs_body = std::find(METHODS_WITH_BODY.begin(), METHODS_WITH_BODY.end(), 
                                       request.method) != METHODS_WITH_BODY.end();
    
    if (method_needs_body && 
        request.headers.find("Content-Type") == request.headers.end() &&
        !request.body.empty()) {
        request.headers["Content-Type"] = "text/plain";
    }
    
    // 设置连接头
    if (request.keep_alive) {
        request.headers["Connection"] = "keep-alive";
    } else {
        request.headers["Connection"] = "close";
    }
    
    return request;
}

bool HttpRequestBuilder::validate() const {
    errors_.clear();
    
    validate_method();
    validate_url();
    
    // 检查URL协议
    if (request_.url.find("://") == std::string::npos) {
        errors_.push_back("Invalid URL: missing protocol (http:// or https://)");
    }
    
    // 检查请求体和方法的兼容性
    bool method_needs_body = std::find(METHODS_WITH_BODY.begin(), METHODS_WITH_BODY.end(), 
                                       request_.method) != METHODS_WITH_BODY.end();
    
    if (method_needs_body && !request_.body.empty() && 
        request_.headers.find("Content-Type") == request_.headers.end()) {
        errors_.push_back("Content-Type header is required for request with body");
    }
    
    // 检查必需字段
    if (request_.url.empty()) {
        errors_.push_back("URL cannot be empty");
    }
    
    return errors_.empty();
}

std::vector<std::string> HttpRequestBuilder::get_errors() const {
    return errors_;
}

net::HttpResponse HttpRequestBuilder::execute() const {
    net::HttpRequest request = build();
    auto& http_client = foundation::app().http_client();
    return http_client.send(request);
}

std::future<net::HttpResponse> HttpRequestBuilder::execute_async() const {
    return std::async(std::launch::async, [this]() {
        return execute();
    });
}

void HttpRequestBuilder::execute_async(std::function<void(net::HttpResponse)> callback) const {
    std::thread([this, callback = std::move(callback)]() {
        callback(execute());
    }).detach();
}

// ============ 工具方法 ============

HttpRequestBuilder& HttpRequestBuilder::reset() {
    std::string url = std::move(request_.url);
    request_ = net::HttpRequest{};
    request_.url = std::move(url);
    request_.method = "GET";
    request_.timeout = std::chrono::seconds(30);
    request_.follow_redirects = true;
    request_.max_redirects = 10;
    request_.verify_ssl = true;
    request_.keep_alive = true;
    errors_.clear();
    return *this;
}

HttpRequestBuilder HttpRequestBuilder::clone() const {
    HttpRequestBuilder builder(request_.url);
    builder.request_ = request_;
    return builder;
}

// ============ 私有辅助方法 ============

bool HttpRequestBuilder::is_valid_method(const std::string& method) const {
    return std::find(VALID_METHODS.begin(), VALID_METHODS.end(), method) != VALID_METHODS.end();
}

bool HttpRequestBuilder::is_valid_url(const std::string& url) const {
    // 简单的URL验证
    return !url.empty() && url.find("://") != std::string::npos;
}

void HttpRequestBuilder::validate_method() const {
    if (!is_valid_method(request_.method)) {
        errors_.push_back("Invalid HTTP method: " + request_.method);
    }
}

void HttpRequestBuilder::validate_url() const {
    if (!is_valid_url(request_.url)) {
        errors_.push_back("Invalid URL: " + request_.url);
    }
}

void HttpRequestBuilder::prepare_headers() {
    // 已经整合到build方法中
}

void HttpRequestBuilder::prepare_body() {
    // 已经整合到build方法中
}

void HttpRequestBuilder::prepare_query_string()const {
    if (request_.query_params.empty()) {
        return;
    }
    
    std::string query_string;
    for (const auto& [name, value] : request_.query_params) {
        if (!query_string.empty()) {
            query_string += "&";
        }
        query_string += url_encode(name) + "=" + url_encode(value);
    }
    
    if (request_.url.find('?') == std::string::npos) {
        request_.url.append("?").append(query_string);
    } else {
        request_.url.append("&").append(query_string);
    }
}

std::string HttpRequestBuilder::url_encode(const std::string& str) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    
    for (char c : str) {
        // 保留字母数字和某些特殊字符
        if (std::isalnum(static_cast<unsigned char>(c)) || 
            c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }
    
    return escaped.str();
}

std::string HttpRequestBuilder::form_url_encode(const std::map<std::string, std::string>& data) {
    std::string result;
    
    for (const auto& [name, value] : data) {
        if (!result.empty()) {
            result += "&";
        }
        result += url_encode(name) + "=" + url_encode(value);
    }
    
    return result;
}


} 