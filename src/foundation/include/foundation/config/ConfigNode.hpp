
#pragma once
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <shared_mutex>
#include "foundation/json/json_facade.h"
#include "foundation/yaml/yaml_facade.h"

namespace foundation {
namespace config {

class ConfigNode {
public:
    using Ptr = std::shared_ptr<ConfigNode>;
    struct SourceInfo {
        std::string path;
        std::string provider;
        std::chrono::system_clock::time_point loadTime;
        size_t size = 0;
        std::string toString() const {
            return path + " (via " + provider + ")";
        }
    };

    ConfigNode();
    ~ConfigNode();
    ConfigNode(const ConfigNode& other);
    ConfigNode& operator=(const ConfigNode& other);
    explicit ConfigNode(const foundation::json::JsonFacade& json);
    explicit ConfigNode(const foundation::yaml::YamlFacade& yaml);
    explicit ConfigNode(bool value);
    explicit ConfigNode(int value);
    explicit ConfigNode(double value);
    explicit ConfigNode(const std::string& value);
    explicit ConfigNode(const char* value);
    ConfigNode(ConfigNode&&) noexcept;
    ConfigNode& operator=(ConfigNode&&) noexcept;
    ConfigNode clone() const;
    bool isNull() const;
    bool isBool() const;
    bool isNumber() const;
    bool isInt() const;
    bool isDouble() const;
    bool isString() const;
    bool isArray() const;
    bool isObject() const;
    bool asBool(bool defaultValue = false) const;
    int asInt(int defaultValue = 0) const;
    double asDouble(double defaultValue = 0.0) const;
    std::string asString(const std::string& defaultValue = "") const;
    template<typename T>
    T as() const;
    template<typename T>
    T as(const T& defaultValue) const;
    size_t size() const;
    ConfigNode at(size_t index) const;
    std::vector<ConfigNode> asArray() const;
    bool has(const std::string& key) const;
    ConfigNode get(const std::string& key) const;
    std::vector<std::string> getKeys() const;
    template<typename T>
    T get(const std::string& key, const T& defaultValue) const;
    ConfigNode getPath(const std::string& path, char delimiter = '.') const;
    template<typename T>
    T get(const std::string& path, const T& defaultValue, char delimiter = '.') const;
    void merge(const ConfigNode& other, bool overwrite = true);
    void overlay(const ConfigNode& other);
    std::string toString(bool pretty = false) const;
    std::string toJsonString(bool pretty = true) const;
    std::string toYamlString() const;
    void convertToYaml(yaml::YamlFacade& yaml, const std::string& path) const;
    const SourceInfo& getSourceInfo() const;
    void setSourceInfo(const SourceInfo& info);
    bool isEmpty() const { return isNull(); }
    bool isNotEmpty() const { return !isNull(); }
    bool saveToFile(const std::string& filename) const;
    bool saveToJsonFile(const std::string& filename, bool pretty = true) const;
    bool saveToYamlFile(const std::string& filename) const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace config
} // namespace foundation