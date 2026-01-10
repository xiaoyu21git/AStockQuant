// core/config.hpp
#pragma once

#include <string>
#include <map>
#include <chrono>

namespace foundation {

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
        size_t maxFileSize = 10 * 1024 * 1024; // 10MB
        size_t maxFiles = 10;
    } log;
};

} // namespace foundation