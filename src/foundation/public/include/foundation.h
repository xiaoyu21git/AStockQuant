// foundation_unified.h (修正版)
#pragma once
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <chrono>

namespace foundation {
    
// 前向声明现有模块的类型
class JsonFacade;
class YamlFacade;
class HttpClient;
class WebSocketConnection;

// ============ 统一配置结构 ============
struct UnifiedConfig {
    struct HttpConfig {
        std::chrono::milliseconds timeout = std::chrono::seconds(30);
        std::multimap<std::string, std::string> defaultHeaders;
        bool enableProxy = false;
        std::string proxyUrl;
    } http;
    
    struct FileConfig {
        bool createDirectories = true;
        size_t maxFileSize = 1024 * 1024 * 100;
        std::string tempDirectory;
    } file;
    
    struct JsonConfig {
        bool prettyPrint = true;
        int indentSize = 2;
    } json;
    
    struct YamlConfig {
        bool allowDuplicates = false;
        bool throwOnError = true;
    } yaml;
    
    struct LogConfig {
        bool enableConsole = true;
        bool enableFile = false;
        std::string logFile = "foundation.log";
        std::string logLevel = "INFO";
    } log;
};

// ============ 统一访问门面类 ============
class Foundation {
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    
    Foundation();
    ~Foundation();
    
    Foundation(const Foundation&) = delete;
    Foundation& operator=(const Foundation&) = delete;
    Foundation(Foundation&&) = delete;
    Foundation& operator=(Foundation&&) = delete;
    
public:
    static Foundation& instance();
    
    // ============ 初始化配置 ============
    void initialize(const UnifiedConfig& config);
    void initialize(const std::string& configFile);
    
    // ============ JSON 操作接口 ============
    class Json {
    private:
        friend class Foundation;
        Json() = default;
        
    public:
        // 创建
        std::shared_ptr<foundation::json::JsonFacade> createObject() const;
        std::shared_ptr<foundation::json::JsonFacade> createArray() const;
        std::shared_ptr<foundation::json::JsonFacade> createNull() const;
        std::shared_ptr<foundation::json::JsonFacade> createBool(bool value) const;
        std::shared_ptr<foundation::json::JsonFacade> createInt(int value) const;
        std::shared_ptr<foundation::json::JsonFacade> createDouble(double value) const;
        std::shared_ptr<foundation::json::JsonFacade> createString(const std::string& value) const;
        
        // 解析
        std::shared_ptr<foundation::json::JsonFacade> parse(const std::string& json) const;
        std::shared_ptr<foundation::json::JsonFacade> parseFile(const std::string& filename) const;
        
        // 工具
        std::string prettyPrint(const foundation::json::JsonFacade& json) const;
        bool validate(const std::string& json) const;
    };
    
    // ============ YAML 操作接口 ============
    class Yaml {
    private:
        friend class Foundation;
        Yaml() = default;
        
    public:
        std::shared_ptr<foundation::yaml::YamlFacade> createEmpty() const;
        std::shared_ptr<foundation::yaml::YamlFacade> loadFromFile(const std::string& filename) const;
        std::shared_ptr<foundation::yaml::YamlFacade> parse(const std::string& yaml) const;
        bool saveToFile(const foundation::yaml::YamlFacade& yaml, const std::string& filename) const;
        std::string toString(const foundation::yaml::YamlFacade& yaml) const;
    };
    
    // ============ HTTP 操作接口 ============
    class Http {
    private:
        friend class Foundation;
        Http() = default;
        
    public:
        std::shared_ptr<foundation::net::HttpClient> createClient() const;
        std::string get(const std::string& url) const;
        std::string post(const std::string& url, const std::string& data) const;
        std::string put(const std::string& url, const std::string& data) const;
        bool download(const std::string& url, const std::string& savePath) const;
        std::vector<std::string> batchGet(const std::vector<std::string>& urls) const;
        std::shared_ptr<foundation::net::WebSocketConnection> createWebSocket() const;
    };
    
    // ============ 文件操作接口 ============
    class File {
    private:
        friend class Foundation;
        File() = default;
        
    public:
        // 读取
        std::string readText(const std::string& filename) const;
        std::vector<std::string> readLines(const std::string& filename) const;
        std::vector<uint8_t> readBinary(const std::string& filename) const;
        
        // 写入
        bool writeText(const std::string& filename, const std::string& content) const;
        bool writeLines(const std::string& filename, const std::vector<std::string>& lines) const;
        bool writeBinary(const std::string& filename, const std::vector<uint8_t>& data) const;
        bool appendText(const std::string& filename, const std::string& content) const;
        
        // 文件信息
        bool exists(const std::string& path) const;
        bool isFile(const std::string& path) const;
        bool isDirectory(const std::string& path) const;
        size_t size(const std::string& path) const;
        std::string extension(const std::string& path) const;
        std::string filename(const std::string& path) const;
        std::string directory(const std::string& path) const;
        
        // 目录操作
        bool createDirectory(const std::string& path) const;
        bool createDirectories(const std::string& path) const;
        std::vector<std::string> listFiles(const std::string& path, bool recursive = false) const;
        std::vector<std::string> listDirectories(const std::string& path, bool recursive = false) const;
        
        // 文件操作
        bool copy(const std::string& src, const std::string& dst) const;
        bool move(const std::string& src, const std::string& dst) const;
        bool remove(const std::string& path) const;
        bool removeDirectory(const std::string& path) const;
        
        // 临时文件
        std::string createTempFile() const;
        std::string createTempFile(const std::string& content) const;
        
        // 文件锁
        class Lock {
        private:
            class Impl;
            std::unique_ptr<Impl> impl_;
            
        public:
            explicit Lock(const std::string& filename);
            ~Lock();
            
            bool acquire();
            bool tryAcquire();
            bool release();
        };
    };
    
    // ============ 系统工具接口 ============
    class System {
    private:
        friend class Foundation;
        System() = default;
        
    public:
        std::string currentTime() const;
        std::string currentTime(const std::string& format) const;
        int64_t currentTimestamp() const;
        int64_t currentMicroTimestamp() const;
        
        int randomInt(int min, int max) const;
        std::string randomString(size_t length) const;
        std::string uuid() const;
        
        std::string getEnv(const std::string& name, const std::string& defaultValue = "") const;
        bool setEnv(const std::string& name, const std::string& value) const;
        
        std::string currentDirectory() const;
        bool setCurrentDirectory(const std::string& path) const;
        std::string homeDirectory() const;
        std::string tempDirectory() const;
        
        int getProcessId() const;
        std::string getHostname() const;
    };
    
    // ============ 字符串工具接口 ============
    class String {
    private:
        friend class Foundation;
        String() = default;
        
    public:
        std::string trim(const std::string& str) const;
        std::string trimLeft(const std::string& str) const;
        std::string trimRight(const std::string& str) const;
        
        std::string toLower(const std::string& str) const;
        std::string toUpper(const std::string& str) const;
        
        bool startsWith(const std::string& str, const std::string& prefix) const;
        bool endsWith(const std::string& str, const std::string& suffix) const;
        
        std::vector<std::string> split(const std::string& str, char delimiter) const;
        std::vector<std::string> split(const std::string& str, const std::string& delimiter) const;
        std::string join(const std::vector<std::string>& strings, const std::string& delimiter) const;
        
        std::string urlEncode(const std::string& str) const;
        std::string urlDecode(const std::string& str) const;
        std::string base64Encode(const std::string& str) const;
        std::string base64Decode(const std::string& str) const;
        
        std::string md5(const std::string& str) const;
        std::string sha1(const std::string& str) const;
        std::string sha256(const std::string& str) const;
        
        std::string replace(const std::string& str, const std::string& from, 
                           const std::string& to) const;
        std::string replaceAll(const std::string& str, const std::string& from, 
                              const std::string& to) const;
    };
    
    // ============ 日志系统接口 ============
    class Logger {
    public:
        enum Level { DEBUG, INFO, WARNING, ERROR, FATAL };
        
        virtual ~Logger() = default;
        virtual void log(Level level, const std::string& message) = 0;
        virtual void log(Level level, const std::string& message, 
                        const std::string& file, int line) = 0;
        
        virtual void debug(const std::string& message) = 0;
        virtual void info(const std::string& message) = 0;
        virtual void warning(const std::string& message) = 0;
        virtual void error(const std::string& message) = 0;
        virtual void fatal(const std::string& message) = 0;
        
        virtual void setLevel(Level level) = 0;
        virtual Level getLevel() const = 0;
        
        static std::shared_ptr<Logger> createDefault();
    };
    
    // ============ 配置管理接口 ============
    class Config {
    private:
        friend class Foundation;
        Config() = default;
        
    public:
        std::shared_ptr<foundation::json::JsonFacade> loadJsonConfig(const std::string& filename) const;
        std::shared_ptr<foundation::yaml::YamlFacade> loadYamlConfig(const std::string& filename) const;
        
        template<typename T>
        T get(const std::string& key, const T& defaultValue = T{}) const;
        
        template<typename T>
        void set(const std::string& key, const T& value);
        
        bool saveJsonConfig(const foundation::json::JsonFacade& config, const std::string& filename) const;
        bool saveYamlConfig(const foundation::yaml::YamlFacade& config, const std::string& filename) const;
    };
    
    // ============ 访问器方法 ============
    Json& json();
    Yaml& yaml();
    Http& http();
    File& file();
    System& system();
    String& string();
    Logger& logger();
    Config& config();
};

// ============ 便捷宏定义 ============
#define FOUNDATION foundation::Foundation::instance()

#define F_JSON FOUNDATION.json()
#define F_YAML FOUNDATION.yaml()
#define F_HTTP FOUNDATION.http()
#define F_FILE FOUNDATION.file()
#define F_SYSTEM FOUNDATION.system()
#define F_STRING FOUNDATION.string()
#define F_LOGGER FOUNDATION.logger()
#define F_CONFIG FOUNDATION.config()

#define F_LOG_DEBUG(msg) F_LOGGER.debug(msg)
#define F_LOG_INFO(msg) F_LOGGER.info(msg)
#define F_LOG_WARNING(msg) F_LOGGER.warning(msg)
#define F_LOG_ERROR(msg) F_LOGGER.error(msg)
#define F_LOG_FATAL(msg) F_LOGGER.fatal(msg)

#define F_LOG_DEBUG_L(msg) F_LOGGER.log(foundation::Foundation::Logger::DEBUG, msg, __FILE__, __LINE__)
#define F_LOG_INFO_L(msg) F_LOGGER.log(foundation::Foundation::Logger::INFO, msg, __FILE__, __LINE__)
#define F_LOG_WARNING_L(msg) F_LOGGER.log(foundation::Foundation::Logger::WARNING, msg, __FILE__, __LINE__)
#define F_LOG_ERROR_L(msg) F_LOGGER.log(foundation::Foundation::Logger::ERROR, msg, __FILE__, __LINE__)
#define F_LOG_FATAL_L(msg) F_LOGGER.log(foundation::Foundation::Logger::FATAL, msg, __FILE__, __LINE__)

#define F_READ_FILE(path) F_FILE.readText(path)
#define F_WRITE_FILE(path, content) F_FILE.writeText(path, content)
#define F_APPEND_FILE(path, content) F_FILE.appendText(path, content)

#define F_HTTP_GET(url) F_HTTP.get(url)
#define F_HTTP_POST(url, data) F_HTTP.post(url, data)

} // namespace foundation