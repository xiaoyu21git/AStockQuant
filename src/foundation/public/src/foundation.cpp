// foundation_unified.cpp (简化实现)
#include "foundation_public.h"
#include "json_facade.h"
#include "yaml_facade.h"
#include "net_facade.h"
#include "foundation.h"
#include <memory>
#include <mutex>
#include <iostream>
#include <string>       // std::string::ends_with
#include <string_view>  // std::string_view::ends_with
#ifdef _WIN32
    #include <windows.h>
    #include <processthreadsapi.h>  // Windows 10+ 可能需要这
#endif

namespace foundation {
    
// ============ Foundation::Impl 实现 ============

class Foundation::Impl {
private:
    UnifiedConfig config_;
    std::mutex mutex_;
    
public:
    Impl() {
        // 默认配置
        config_.http.timeout = std::chrono::seconds(30);
        config_.file.createDirectories = true;
        config_.json.prettyPrint = true;
        config_.json.indentSize = 2;
        config_.log.enableConsole = true;
        config_.log.logLevel = "INFO";
    }
    
    ~Impl() = default;
    static bool endsWith(const std::string& str, const std::string& suffix) {
        if (str.size() < suffix.size()) return false;
        return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin());
    }
    void initialize(const UnifiedConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
    }
    
    void initialize(const std::string& configFile) {
        std::lock_guard<std::mutex> lock(mutex_);
        try {
            // 根据文件扩展名选择解析器
            if (endsWith(configFile, ".json")) {
                auto json = json::JsonFacade::parseFile(configFile);
                // 从JSON加载配置...
            } else if (endsWith(configFile, ".yaml") || endsWith(configFile, ".yml")) {
                //auto yaml = yaml::YamlFacade::loadFromFile(configFile);
                foundation::yaml::YamlFacade yaml;
                yaml.loadFromFile(configFile); // 非静态函数需要对象调用
                // 从YAML加载配置...
            }
        }catch (...) {
            // 保持默认配置
        }
    }
    
    const UnifiedConfig& config() const { return config_; }
};

// ============ Foundation 单例实现 ============

Foundation& Foundation::instance() {
    static Foundation instance;
    return instance;
}

Foundation::Foundation() : impl_(std::make_unique<Impl>()) {}
Foundation::~Foundation() = default;

void Foundation::initialize(const UnifiedConfig& config) {
    impl_->initialize(config);
}

void Foundation::initialize(const std::string& configFile) {
    impl_->initialize(configFile);
}

// ============ 子模块实现（重用现有模块） ============

// Json 实现 - 直接调用现有的 JsonFacade
Foundation::Json& Foundation::json() {
    static Json instance;
    return instance;
}

std::shared_ptr<foundation::json::JsonFacade> Foundation::Json::createObject() const {
    return std::make_shared<foundation::json::JsonFacade>(foundation::json::JsonFacade::createObject());
}

std::shared_ptr<foundation::json::JsonFacade> Foundation::Json::createArray() const {
    return std::make_shared<foundation::json::JsonFacade>(foundation::json::JsonFacade::createArray());
}

std::shared_ptr<foundation::json::JsonFacade> Foundation::Json::createNull() const {
    return std::make_shared<foundation::json::JsonFacade>(foundation::json::JsonFacade::createNull());
}

std::shared_ptr<foundation::json::JsonFacade> Foundation::Json::createBool(bool value) const {
    return std::make_shared<foundation::json::JsonFacade>(foundation::json::JsonFacade::createBool(value));
}

std::shared_ptr<foundation::json::JsonFacade> Foundation::Json::createInt(int value) const {
    return std::make_shared<foundation::json::JsonFacade>(foundation::json::JsonFacade::createInt(value));
}

std::shared_ptr<foundation::json::JsonFacade> Foundation::Json::createDouble(double value) const {
    return std::make_shared<foundation::json::JsonFacade>(foundation::json::JsonFacade::createDouble(value));
}

std::shared_ptr<foundation::json::JsonFacade> Foundation::Json::createString(const std::string& value) const {
    return std::make_shared<foundation::json::JsonFacade>(foundation::json::JsonFacade::createString(value));
}

std::shared_ptr<foundation::json::JsonFacade> Foundation::Json::parse(const std::string& json) const {
    try {
        return std::make_shared<foundation::json::JsonFacade>(foundation::json::JsonFacade::parse(json));
    } catch (const std::exception& e) {
        throw ParseException("JSON解析失败: " + std::string(e.what()));
    }
}

std::shared_ptr<foundation::json::JsonFacade> Foundation::Json::parseFile(const std::string& filename) const {
    try {
        return std::make_shared<foundation::json::JsonFacade>(foundation::json::JsonFacade::parseFile(filename));
    } catch (const std::exception& e) {
        throw FileException("读取JSON文件失败: " + std::string(e.what()));
    }
}

std::string Foundation::Json::prettyPrint(const foundation::json::JsonFacade& json) const {
    return json.toPrettyString();
}

bool Foundation::Json::validate(const std::string& json) const {
    try {
        auto parsed = json::JsonFacade::parse(json);
        return true;
    } catch (...) {
        return false;
    }
}

// Yaml 实现 - 直接调用现有的 YamlFacade
Foundation::Yaml& Foundation::yaml() {
    static Yaml instance;
    return instance;
}

std::shared_ptr<foundation::yaml::YamlFacade> Foundation::Yaml::createEmpty() const {
    return std::make_shared<foundation::yaml::YamlFacade>(foundation::yaml::YamlFacade::createEmpty());
}

std::shared_ptr<foundation::yaml::YamlFacade> Foundation::Yaml::loadFromFile(const std::string& filename) const {
    try {
        foundation::yaml::YamlFacade yaml;
        yaml.loadFromFile(filename); // 非静态函数需要对象调用
       // auto yaml = yaml::YamlFacade::loadFromFile(filename);
        return std::make_shared<yaml::YamlFacade>(std::move(yaml));
    } catch (const std::exception& e) {
        throw FileException("读取YAML文件失败: " + std::string(e.what()));
    }
}

std::shared_ptr<foundation::yaml::YamlFacade> Foundation::Yaml::parse(const std::string& yaml) const {
    try {
        auto parsed = foundation::yaml::YamlFacade::parse(yaml);
        return std::make_shared<foundation::yaml::YamlFacade>(std::move(parsed));
    } catch (const std::exception& e) {
        throw ParseException("YAML解析失败: " + std::string(e.what()));
    }
}

bool Foundation::Yaml::saveToFile(const foundation::yaml::YamlFacade& yaml, const std::string& filename) const {
    try {
        const auto& concrete = static_cast<const foundation::yaml::YamlFacade&>(yaml);
        return concrete.saveToFile(filename);
    } catch (const std::exception& e) {
        throw FileException("保存YAML文件失败: " + std::string(e.what()));
    }
}

std::string Foundation::Yaml::toString(const foundation::yaml::YamlFacade& yaml) const {
    try {
        const auto& concrete = static_cast<const foundation::yaml::YamlFacade&>(yaml);
        return concrete.toString();
    } catch (...) {
        return "";
    }
}

// Http 实现 - 调用现有的 NetFacade
Foundation::Http& Foundation::http() {
    static Http instance;
    return instance;
}

std::shared_ptr<foundation::net::HttpClient> Foundation::Http::createClient() const {
    auto client = net::NetFacade::createHttpClient();
    // 从单例获取配置
    auto& config = Foundation::instance().impl_->config();
    client->setDefaultTimeout(config.http.timeout);
    
    for (const auto& [key, value] : config.http.defaultHeaders) {
        client->addDefaultHeader(key, value);
    }
    
    return client;
}

std::string Foundation::Http::get(const std::string& url) const {
    auto client = createClient();
    try {
        auto response = client->get(url);
        if (response.isSuccess()) {
            return response.body;
        }
        throw NetworkException("HTTP GET失败: " + response.statusText);
    } catch (const std::exception& e) {
        throw NetworkException("HTTP请求失败: " + std::string(e.what()));
    }
}

std::string Foundation::Http::post(const std::string& url, const std::string& data) const {
    auto client = createClient();
    try {
        auto response = client->post(url, data);
        if (response.isSuccess()) {
            return response.body;
        }
        throw NetworkException("HTTP POST失败: " + response.statusText);
    } catch (const std::exception& e) {
        throw NetworkException("HTTP请求失败: " + std::string(e.what()));
    }
}

std::string Foundation::Http::put(const std::string& url, const std::string& data) const {
    auto client = createClient();
    try {
        auto response = client->put(url, data);
        if (response.isSuccess()) {
            return response.body;
        }
        throw NetworkException("HTTP PUT失败: " + response.statusText);
    } catch (const std::exception& e) {
        throw NetworkException("HTTP请求失败: " + std::string(e.what()));
    }
}

bool Foundation::Http::download(const std::string& url, const std::string& savePath) const {
    auto client = createClient();
    try {
        auto response = client->get(url);
        if (response.isSuccess()) {
            return Utils::writeBinaryFile(savePath, 
                std::vector<uint8_t>(response.body.begin(), response.body.end()));
        }
        return false;
    } catch (...) {
        return false;
    }
}

std::vector<std::string> Foundation::Http::batchGet(const std::vector<std::string>& urls) const {
    auto client = createClient();
    std::vector<std::string> results;
    results.reserve(urls.size());
    
    for (const auto& url : urls) {
        try {
            results.push_back(get(url));
        } catch (...) {
            results.push_back("");
        }
    }
    
    return results;
}

std::shared_ptr<foundation::net::WebSocketConnection> Foundation::Http::createWebSocket() const {
    return foundation::net::NetFacade::createWebSocket();
}

// File 实现 - 调用现有的 foundation::Utils 和 RAII 类
Foundation::File& Foundation::file() {
    static File instance;
    return instance;
}

std::string Foundation::File::readText(const std::string& filename) const {
    try {
        return Utils::readTextFile(filename);
    } catch (const std::exception& e) {
        throw FileException("读取文件失败: " + std::string(e.what()));
    }
}

std::vector<std::string> Foundation::File::readLines(const std::string& filename) const {
    try {
        FileReader reader(filename);
        return reader.readLines();
    } catch (const std::exception& e) {
        throw FileException("读取文件失败: " + std::string(e.what()));
    }
}

std::vector<uint8_t> Foundation::File::readBinary(const std::string& filename) const {
    try {
        return Utils::readBinaryFile(filename);
    } catch (const std::exception& e) {
        throw FileException("读取二进制文件失败: " + std::string(e.what()));
    }
}

bool Foundation::File::writeText(const std::string& filename, const std::string& content) const {
    auto& config = Foundation::instance().impl_->config();
    try {
        return Utils::writeTextFile(filename, content);
    } catch (const std::exception& e) {
        throw FileException("写入文件失败: " + std::string(e.what()));
    }
}

bool Foundation::File::writeLines(const std::string& filename, const std::vector<std::string>& lines) const {
    auto& config = Foundation::instance().impl_->config();
    try {
        FileWriter writer(filename, false, config.file.createDirectories);
        return writer.writeLines(lines);
    } catch (const std::exception& e) {
        throw FileException("写入文件失败: " + std::string(e.what()));
    }
}

bool Foundation::File::writeBinary(const std::string& filename, const std::vector<uint8_t>& data) const {
    try {
        return Utils::writeBinaryFile(filename, data);
    } catch (const std::exception& e) {
        throw FileException("写入二进制文件失败: " + std::string(e.what()));
    }
}

bool Foundation::File::appendText(const std::string& filename, const std::string& content) const {
    auto& config = Foundation::instance().impl_->config();
    try {
        FileWriter writer(filename, true, config.file.createDirectories);
        return writer.write(content);
    } catch (const std::exception& e) {
        throw FileException("追加文件失败: " + std::string(e.what()));
    }
}

bool Foundation::File::exists(const std::string& path) const {
    return Utils::fileExists(path);
}

bool Foundation::File::isFile(const std::string& path) const {
    return Utils::isFile(path);
}

bool Foundation::File::isDirectory(const std::string& path) const {
    return Utils::isDirectory(path);
}

size_t Foundation::File::size(const std::string& path) const {
    return Utils::fileSize(path);
}

std::string Foundation::File::extension(const std::string& path) const {
    return Utils::fileExtension(path);
}

std::string Foundation::File::filename(const std::string& path) const {
    return Utils::fileName(path);
}

std::string Foundation::File::directory(const std::string& path) const {
    return Utils::directoryName(path);
}

bool Foundation::File::createDirectory(const std::string& path) const {
    return Utils::createDirectory(path);
}

bool Foundation::File::createDirectories(const std::string& path) const {
    return Utils::createDirectories(path);
}

std::vector<std::string> Foundation::File::listFiles(const std::string& path, bool recursive) const {
    std::vector<std::string> files;
    Utils::walkDirectory(path,
        [&files](const std::string& file) { files.push_back(file); },
        nullptr
    );
    return files;
}

std::vector<std::string> Foundation::File::listDirectories(const std::string& path, bool recursive) const {
    std::vector<std::string> dirs;
    Utils::walkDirectory(path,
        nullptr,
        [&dirs](const std::string& dir) { dirs.push_back(dir); }
    );
    return dirs;
}

bool Foundation::File::copy(const std::string& src, const std::string& dst) const {
    return Utils::copyFile(src, dst);
}

bool Foundation::File::move(const std::string& src, const std::string& dst) const {
    return Utils::moveFile(src, dst);
}

bool Foundation::File::remove(const std::string& path) const {
    return Utils::deleteFile(path);
}

bool Foundation::File::removeDirectory(const std::string& path) const {
    return Utils::deleteDirectory(path);
}

std::string Foundation::File::createTempFile() const {
    TempFile temp;
    return temp.path();
}

std::string Foundation::File::createTempFile(const std::string& content) const {
    TempFile temp;
    writeText(temp.path(), content);
    return temp.path();
}

// System 实现 - 调用现有的 Utils
Foundation::System& Foundation::system() {
    static System instance;
    return instance;
}

std::string Foundation::System::currentTime() const {
    return Utils::currentTimeString();
}

std::string Foundation::System::currentTime(const std::string& format) const {
    return Utils::currentTimeString(format);
}

int64_t Foundation::System::currentTimestamp() const {
    return Utils::currentTimeMillis();
}

int64_t Foundation::System::currentMicroTimestamp() const {
    return Utils::currentTimeMicros();
}

int Foundation::System::randomInt(int min, int max) const {
    return Utils::randomInt(min, max);
}

std::string Foundation::System::randomString(size_t length) const {
    return Utils::randomString(length);
}

std::string Foundation::System::uuid() const {
    return Utils::uuid();
}

std::string Foundation::System::getEnv(const std::string& name, const std::string& defaultValue) const {
    return Utils::getEnv(name, defaultValue);
}

bool Foundation::System::setEnv(const std::string& name, const std::string& value) const {
    return Utils::setEnv(name, value);
}

std::string Foundation::System::currentDirectory() const {
    return Utils::getCurrentDirectory();
}

bool Foundation::System::setCurrentDirectory(const std::string& path) const {
    return Utils::setCurrentDirectory(path);
}

std::string Foundation::System::homeDirectory() const {
    return Utils::getHomeDirectory();
}

std::string Foundation::System::tempDirectory() const {
    return Utils::getTempDirectory();
}

int Foundation::System::getProcessId() const {
#ifdef _WIN32
    return static_cast<int>(GetCurrentProcessId());
#else
    return static_cast<int>(getpid());
#endif
}

std::string Foundation::System::getHostname() const {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return hostname;
    }
    return "unknown";
}

// String 实现 - 调用现有的 Utils
Foundation::String& Foundation::string() {
    static String instance;
    return instance;
}

std::string Foundation::String::trim(const std::string& str) const {
    return Utils::trim(str);
}

std::string Foundation::String::trimLeft(const std::string& str) const {
    return Utils::trimLeft(str);
}

std::string Foundation::String::trimRight(const std::string& str) const {
    return Utils::trimRight(str);
}

std::string Foundation::String::toLower(const std::string& str) const {
    return Utils::toLower(str);
}

std::string Foundation::String::toUpper(const std::string& str) const {
    return Utils::toUpper(str);
}

bool Foundation::String::startsWith(const std::string& str, const std::string& prefix) const {
    return Utils::startsWith(str, prefix);
}

bool Foundation::String::endsWith(const std::string& str, const std::string& suffix) const {
    return Utils::endsWith(str, suffix);
}

std::vector<std::string> Foundation::String::split(const std::string& str, char delimiter) const {
    return Utils::split(str, delimiter);
}

std::vector<std::string> Foundation::String::split(const std::string& str, const std::string& delimiter) const {
    return Utils::split(str, delimiter);
}

std::string Foundation::String::join(const std::vector<std::string>& strings, const std::string& delimiter) const {
    return Utils::join(strings, delimiter);
}

std::string Foundation::String::urlEncode(const std::string& str) const {
    return Utils::urlEncode(str);
}

std::string Foundation::String::urlDecode(const std::string& str) const {
    return Utils::urlDecode(str);
}

std::string Foundation::String::base64Encode(const std::string& str) const {
    return Utils::base64Encode(str);
}

std::string Foundation::String::base64Decode(const std::string& str) const {
    return Utils::base64Decode(str);
}

std::string Foundation::String::md5(const std::string& str) const {
    return Utils::md5(str);
}

std::string Foundation::String::sha1(const std::string& str) const {
    return Utils::sha1(str);
}

std::string Foundation::String::sha256(const std::string& str) const {
    return Utils::sha256(str);
}

std::string Foundation::String::replace(const std::string& str, const std::string& from, 
                                       const std::string& to) const {
    std::string result = str;
    size_t pos = result.find(from);
    if (pos != std::string::npos) {
        result.replace(pos, from.length(), to);
    }
    return result;
}

std::string Foundation::String::replaceAll(const std::string& str, const std::string& from, 
                                          const std::string& to) const {
    std::string result = str;
    size_t pos = 0;
    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, from.length(), to);
        pos += to.length();
    }
    return result;
}

// Config 实现
Foundation::Config& Foundation::config() {
    static Config instance;
    return instance;
}

std::shared_ptr<foundation::json::JsonFacade> Foundation::Config::loadJsonConfig(const std::string& filename) const {
    return Foundation::instance().json().parseFile(filename);
}

std::shared_ptr<foundation::yaml::YamlFacade> Foundation::Config::loadYamlConfig(const std::string& filename) const {
    return Foundation::instance().yaml().loadFromFile(filename);
}

bool Foundation::Config::saveJsonConfig(const foundation::json::JsonFacade& config, const std::string& filename) const {
    try {
        const auto& concrete = static_cast<const foundation::json::JsonFacade&>(config);
        return concrete.saveToFile(filename);
    } catch (...) {
        return false;
    }
}

bool Foundation::Config::saveYamlConfig(const foundation::yaml::YamlFacade& config, const std::string& filename) const {
    try {
        const auto& concrete = static_cast<const yaml::YamlFacade&>(config);
        return concrete.saveToFile(filename);
    } catch (...) {
        return false;
    }
}

// Logger 实现 - 使用现有的 Logger 接口
std::shared_ptr<Foundation::Logger> Foundation::Logger::createDefault() {
    // 这里可以返回现有的 Logger 实现
    // 例如：return std::make_shared<foundation::ConsoleLogger>();
    return nullptr; // 简化为空实现
}

} // namespace foundation