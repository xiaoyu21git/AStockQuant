// foundation/src/foundation.cpp
#include "foundation.h"

// 包含各模块头文件

namespace foundation {

// ============ 内部辅助类 ============

// ============ Foundation类实现 ============

Foundation::~Foundation() {
    if (initialized_) {
        shutdown();
    }
}

Foundation& Foundation::instance() {
    static Foundation instance;
    return instance;
}

bool Foundation::initialize(const Config& config) {
    if (initialized_) {
        return true;
    }
    
    try {
        // 设置随机种子
        if (config.random_seed == 0) {
            auto now = std::chrono::system_clock::now();
            auto seed = static_cast<unsigned int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()).count());
            utils::Random::seed(seed);
        } else {
            utils::Random::seed(config.random_seed);
        }
        
        // 初始化日志系统
        auto logger_impl = std::make_unique<log::LoggerImpl>();
        logger_impl->setLevel(config.log_level);
        
        if (config.enable_console_log) {
            auto console_handler = std::make_unique<log::ConsoleHandler>(true);
            logger_impl->addHandler(std::move(console_handler));
        }
        
        if (config.enable_file_log && !config.log_file.empty()) {
            auto file_handler = std::make_unique<log::FileHandler>(
                config.log_file, true);
            logger_impl->addHandler(std::move(file_handler));
        }
        
        logger_ = std::move(logger_impl);
        
        // 初始化文件系统模块
        filesystem_ = std::make_unique<fs::File>();
        
        // 初始化JSON模块
        json_ = std::make_unique<json::JsonFacade>();
        
        // 初始化YAML模块
        yaml_ = std::make_unique<yaml::YamlFacade>();
        
        // 初始化网络模块
        http_client_ = net::NetFacade::createHttpClient();
        if (http_client_) {
            http_client_->setDefaultTimeout(config.http_timeout);
        }
        
        websocket_ = net::NetFacade::createWebSocket();
        
        // 初始化线程池
        thread_pool_ = thread::ThreadPoolFactory::create_fixed(config.thread_pool_size);
        
        // 初始化工具模块
        random_ = std::make_unique<utils::Random>();
        time_ = std::make_unique<utils::Time>();
        string_ = std::make_unique<utils::String>();
        system_ = std::make_unique<utils::SystemUtilsImpl>();
        
        initialized_ = true;
        
        // 记录初始化成功
        logger_->info("Foundation initialized successfully");
        logger_->info("Thread pool size: " + std::to_string(config.thread_pool_size));
        logger_->info("Log level: " + std::to_string(static_cast<int>(config.log_level)));
        
        return true;
        
    } catch (const Exception& e) {
        std::cerr << "Failed to initialize Foundation: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize Foundation: " << e.what() << std::endl;
        return false;
    }
}

void Foundation::shutdown() {
    if (!initialized_) {
        return;
    }
    
    try {
        logger_->info("Shutting down Foundation...");
        
        // 关闭网络连接
        if (websocket_) {
            websocket_->disconnect();
        }
        
        // 等待线程池任务完成
        if (thread_pool_) {
            // 这里可以添加线程池的优雅关闭逻辑
        }
        
        // 刷新日志
        logger_->flush();
        
        // 重置所有资源（按依赖顺序）
        websocket_.reset();
        http_client_.reset();
        thread_pool_.reset();
        yaml_.reset();
        json_.reset();
        filesystem_.reset();
        system_.reset();
        string_.reset();
        time_.reset();
        random_.reset();
        logger_.reset();
        
        initialized_ = false;
        
        std::cout << "Foundation shutdown completed" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error during Foundation shutdown: " << e.what() << std::endl;
    }
}

// ============ 模块访问器实现 ============

ILogger& Foundation::logger() {
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
        throw RuntimeException("JSON module not initialized");
    }
    return *json_;
}

yaml::YamlFacade& Foundation::yaml() {
    if (!yaml_) {
        throw RuntimeException("YAML module not initialized");
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
        throw RuntimeException("Random module not initialized");
    }
    return *random_;
}

utils::Time& Foundation::time() {
    if (!time_) {
        throw RuntimeException("Time module not initialized");
    }
    return *time_;
}

utils::String& Foundation::string() {
    if (!string_) {
        throw RuntimeException("String module not initialized");
    }
    return *string_;
}

utils::SystemUtilsImpl& Foundation::system() {
    if (!system_) {
        throw RuntimeException("System module not initialized");
    }
    return *system_;
}

// ============ 静态便捷方法实现 ============

// 日志静态方法
void Foundation::log_trace(const std::string& message, const std::string& file, int line) {
    if (instance().is_initialized()) {
        instance().logger().log(LogLevel::TRACE, message, file, line);
    } else {
        std::cout << "[TRACE] " << message << std::endl;
    }
}

void Foundation::log_debug(const std::string& message, const std::string& file, int line) {
    if (instance().is_initialized()) {
        instance().logger().log(LogLevel::DEBUG, message, file, line);
    } else {
        std::cout << "[DEBUG] " << message << std::endl;
    }
}

void Foundation::log_info(const std::string& message, const std::string& file, int line) {
    if (instance().is_initialized()) {
        instance().logger().log(LogLevel::INFO, message, file, line);
    } else {
        std::cout << "[INFO] " << message << std::endl;
    }
}

void Foundation::log_warn(const std::string& message, const std::string& file, int line) {
    if (instance().is_initialized()) {
        instance().logger().log(LogLevel::WARN, message, file, line);
    } else {
        std::cout << "[WARN] " << message << std::endl;
    }
}

void Foundation::log_error(const std::string& message, const std::string& file, int line) {
    if (instance().is_initialized()) {
        instance().logger().log(LogLevel::ERR, message, file, line);
    } else {
        std::cerr << "[ERROR] " << message << std::endl;
    }
}

void Foundation::log_fatal(const std::string& message, const std::string& file, int line) {
    if (instance().is_initialized()) {
        instance().logger().log(LogLevel::FATAL, message, file, line);
    } else {
        std::cerr << "[FATAL] " << message << std::endl;
    }
}

// 文件操作
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

// JSON操作
json::JsonFacade Foundation::parse_json(const std::string& json_str) {
    return json::JsonFacade::parse(json_str);
}

json::JsonFacade Foundation::load_json(const std::string& file_path) {
    auto content = read_file(file_path);
    return json::JsonFacade::parse(content);
}

bool Foundation::save_json(const json::JsonFacade& json, const std::string& file_path) {
    return json.saveToFile(file_path);
}

// YAML操作
yaml::YamlFacade Foundation::parse_yaml(const std::string& yaml_str) {
    return yaml::YamlFacade::parse(yaml_str);
}

yaml::YamlFacade Foundation::load_yaml(const std::string& file_path) {
    auto yaml = yaml::YamlFacade::createEmpty();
    if (yaml.loadFromFile(file_path)) {
        return yaml;
    }
    throw FileException("Failed to load YAML file: " + file_path);
}

bool Foundation::save_yaml(const yaml::YamlFacade& yaml, const std::string& file_path) {
    return yaml.saveToFile(file_path);
}

// HTTP操作
net::HttpResponse Foundation::http_get(const std::string& url) {
    auto client = net::NetFacade::createHttpClient();
    if (!client) {
        throw NetworkException("Failed to create HTTP client");
    }
    return client->get(url);
}

net::HttpResponse Foundation::http_post(const std::string& url, const std::string& body) {
    auto client = net::NetFacade::createHttpClient();
    if (!client) {
        throw NetworkException("Failed to create HTTP client");
    }
    return client->post(url, body);
}

net::HttpResponse Foundation::http_put(const std::string& url, const std::string& body) {
    auto client = net::NetFacade::createHttpClient();
    if (!client) {
        throw NetworkException("Failed to create HTTP client");
    }
    return client->put(url, body);
}

net::HttpResponse Foundation::http_delete(const std::string& url) {
    auto client = net::NetFacade::createHttpClient();
    if (!client) {
        throw NetworkException("Failed to create HTTP client");
    }
    return client->delete_(url);
}

// 随机数
int Foundation::random_int(int min, int max) {
    return utils::Random::getInt(min, max);
}

double Foundation::random_double(double min, double max) {
    return utils::Random::getFloat(min, max);
}

std::string Foundation::random_string(size_t length) {
    return utils::Random::getString(length);
}

std::string Foundation::generate_uuid() {
    return utils::Random::generateUuid();
}

// 时间
int64_t Foundation::timestamp() {
    return utils::Time::getTimestamp();
}

int64_t Foundation::timestamp_ms() {
    return utils::Time::getTimestampMilliseconds();
}

std::string Foundation::current_time_string() {
    return utils::Time::getCurrentTimeString();
}

std::string Foundation::current_time_string(const std::string& format) {
    return utils::Time::getCurrentTimeString(format);
}

// 字符串
std::string Foundation::trim(const std::string& str) {
    return utils::String::trim(str);
}

std::string Foundation::to_lower(const std::string& str) {
    return utils::String::toLower(str);
}

std::string Foundation::to_upper(const std::string& str) {
    return utils::String::toUpper(str);
}

std::vector<std::string> Foundation::split(const std::string& str, char delimiter) {
    return utils::String::split(str, delimiter);
}

std::string Foundation::join(const std::vector<std::string>& strings, const std::string& delimiter) {
    return utils::String::join(strings, delimiter);
}

// std::string Foundation::format_string(const std::string& fmt, ...) {
//     va_list args;
//     va_start(args, fmt);
//     std::string result = utils::String::formatv(fmt, args);
//     va_end(args);
//     return result;
// }
std::string Foundation::format_string(const std::string fmt, ...) {
    va_list args;
    va_start(args, fmt);  // 现在 fmt 不是引用类型
    std::string result = utils::String::formatv(fmt, args);
    va_end(args);
    return result;
}
// 系统
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

std::string Foundation::env(const std::string& name, const std::string& defaultValue) {
    return instance().system().getEnvironmentVariable(name, defaultValue);
}

bool Foundation::set_env(const std::string& name, const std::string& value) {
    return instance().system().setEnvironmentVariable(name, value);
}

// ============ JsonBuilder实现 ============

JsonBuilder::JsonBuilder() : json_(json::JsonFacade::createObject()) {}

JsonBuilder::JsonBuilder(const json::JsonFacade& json) : json_(json) {}

JsonBuilder& JsonBuilder::set(const std::string& key, const std::string& value) {
    json_.set(key, json::JsonFacade::createString(value));
    return *this;
}

JsonBuilder& JsonBuilder::set(const std::string& key, int value) {
    json_.set(key, json::JsonFacade::createInt(value));
    return *this;
}

JsonBuilder& JsonBuilder::set(const std::string& key, double value) {
    json_.set(key, json::JsonFacade::createDouble(value));
    return *this;
}

JsonBuilder& JsonBuilder::set(const std::string& key, bool value) {
    json_.set(key, json::JsonFacade::createBool(value));
    return *this;
}

JsonBuilder& JsonBuilder::set(const std::string& key, const JsonBuilder& value) {
    json_.set(key, value.json_);
    return *this;
}

json::JsonFacade JsonBuilder::build() const {
    return json_;
}

std::string JsonBuilder::to_string() const {
    return json_.toString();
}

std::string JsonBuilder::to_pretty_string() const {
    return json_.toPrettyString();
}

// ============ HttpRequestBuilder实现 ============

HttpRequestBuilder::HttpRequestBuilder(const std::string& url) {
    request_.url = url;
}

HttpRequestBuilder& HttpRequestBuilder::method(const std::string& method) {
    request_.method = method;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::body(const std::string& body) {
    request_.body = body;
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::header(const std::string& name, const std::string& value) {
    request_.headers.emplace(name, value);
    return *this;
}

HttpRequestBuilder& HttpRequestBuilder::timeout(std::chrono::milliseconds timeout) {
    request_.timeout = timeout;
    return *this;
}

net::HttpRequest HttpRequestBuilder::build() const {
    return request_;
}

net::HttpResponse HttpRequestBuilder::execute() const {
    auto client = net::NetFacade::createHttpClient();
    if (!client) {
        throw NetworkException("Failed to create HTTP client");
    }
    return client->send(request_);
}

} // namespace foundation