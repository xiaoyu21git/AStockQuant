// config/ConfigNode.cpp
#include "foundation/config/ConfigNode.hpp"
#include "foundation/core/Exception.hpp"
#include "foundation/utils/String.hpp"
#include "foundation/utils/Time.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <type_traits>
#include <variant>
namespace foundation {
namespace config {
    
// ============ ConfigNode::Impl 实现 ============
// ============ 线程局部缓冲区，避免重复分配 ============
namespace {
    thread_local std::vector<std::string_view> g_split_buffer;
    thread_local std::string g_lookup_buffer;
    
    void ensureBufferCapacity(size_t required_size) {
        if (g_lookup_buffer.capacity() < required_size) {
            g_lookup_buffer.reserve(std::max(required_size * 2, size_t(64)));
        }
    }
}
class ConfigNode::Impl {
public:
    // 内部数据类型
    enum class DataType {
        NULL_VALUE,
        BOOL,
        INT,
        DOUBLE,
        STRING,
        ARRAY,
        OBJECT
    };

    DataType type = DataType::NULL_VALUE;
    std::variant<
        bool,
        int64_t,
        double,
        std::string,
        std::vector<ConfigNode>,
        std::map<std::string, ConfigNode>
    > value;
    
    SourceInfo sourceInfo;
    
        // 从 JsonFacade 构造
    void fromJson(const foundation::json::JsonFacade& json) {
        if (json.isNull()) {
            type = DataType::NULL_VALUE;
        } else if (json.isBool()) {
            type = DataType::BOOL;
            value = json.asBool();
        } else if (json.isNumber()) {
            // 尝试判断是整数还是浮点数
            try {
                int intVal = json.asInt();
                type = DataType::INT;
                value = static_cast<int64_t>(intVal);
            } catch (...) {
                type = DataType::DOUBLE;
                value = json.asDouble();
            }
        } else if (json.isString()) {
            type = DataType::STRING;
            value = json.asString();
        } else if (json.isArray()) {
            type = DataType::ARRAY;
            std::vector<ConfigNode> arr;
            for (size_t i = 0; i < json.size(); ++i) {
                auto node = json.at(i);
                ConfigNode child;
                child.impl_->fromJson(node);
                arr.push_back(std::move(child));
            }
            value = std::move(arr);
        } else if (json.isObject()) {
            type = DataType::OBJECT;
            std::map<std::string, ConfigNode> obj;
            // 这里需要根据 JsonFacade 的实际接口遍历对象
            // 简化实现：假设可以通过某种方式获取所有键
            // obj["key"] = ConfigNode(json.get("key"));
            value = std::move(obj);
        }
    }

    // 转换为 JsonFacade
    foundation::json::JsonFacade toJson() const {
        switch (type) {
            case DataType::NULL_VALUE:
                return foundation::json::JsonFacade::createNull();
            case DataType::BOOL:
                return foundation::json::JsonFacade::createBool(
                    std::get<bool>(value)
                );
            case DataType::INT:
                return foundation::json::JsonFacade::createInt(
                    static_cast<int>(std::get<int64_t>(value))
                );
            case DataType::DOUBLE:
                return foundation::json::JsonFacade::createDouble(
                    std::get<double>(value)
                );
            case DataType::STRING:
                return foundation::json::JsonFacade::createString(
                    std::get<std::string>(value)
                );
            case DataType::ARRAY: {
                auto arr = foundation::json::JsonFacade::createArray();
                const auto& vec = std::get<std::vector<ConfigNode>>(value);
                for (const auto& item : vec) {
                    arr.push_back(item.impl_->toJson());
                }
                return arr;
            }
            case DataType::OBJECT: {
                auto obj = foundation::json::JsonFacade::createObject();
                const auto& map = std::get<std::map<std::string, ConfigNode>>(value);
                for (const auto& [key, val] : map) {
                    obj.set(key, val.impl_->toJson());
                }
                return obj;
            }
            default:
                throw foundation::Exception("Unknown data type in ConfigNode");
        }
    }
    // 静态工厂方法
    void fromYaml(const foundation::yaml::YamlFacade& yaml)
    {
        // 简化实现：假设我们只关心根节点的类型
    
        // 尝试获取根节点作为各种类型
    
        // 1. 尝试作为映射
        try {
            auto stringMap = yaml.getStringMap("");
            if (!stringMap.empty()) {
                type = DataType::OBJECT;
                std::map<std::string, ConfigNode> obj;
            for (const auto& [key, strValue] : stringMap) {
                // 这里简化处理：所有值都作为字符串
                obj[key] = ConfigNode(strValue);
            }
            
                value = std::move(obj);
                return;
            }
        } catch (...) {}
    
        // 2. 尝试作为数组
        try {
            auto stringArray = yaml.getStringArray("");
            if (!stringArray.empty()) {
                type = DataType::ARRAY;
                std::vector<ConfigNode> arr;
            
                for (const auto& strValue : stringArray) {
                    arr.push_back(ConfigNode(strValue));
                }
            
                value = std::move(arr);
                return;
            }
        } catch (...) {}
    
        // 3. 尝试作为各种标量类型
        try {
            bool boolVal = yaml.getBool("", false);
            type = DataType::BOOL;
            value = boolVal;
            return;
        } catch (...) {}
    
        try {
            int intVal = yaml.getInt("", 0);
            type = DataType::INT;
            value = static_cast<int64_t>(intVal);
            return;
        } catch (...) {}
    
        try {
            double doubleVal = yaml.getDouble("", 0.0);
            type = DataType::DOUBLE;
            value = doubleVal;
            return;
        } catch (...) {}
    
    try {
        std::string strVal = yaml.getString("", "");
        type = DataType::STRING;
        value = strVal;
        return;
    } catch (...) {}
    
    // 4. 默认返回 null
    type = DataType::NULL_VALUE;
    }
    // 克隆实现
    std::unique_ptr<Impl> clone() const {
        auto newImpl = std::make_unique<Impl>();
        newImpl->type = type;
        newImpl->sourceInfo = sourceInfo;
        
        switch (type) {
            case DataType::NULL_VALUE:
                break;
            case DataType::BOOL:
                newImpl->value = std::get<bool>(value);
                break;
            case DataType::INT:
                newImpl->value = std::get<int64_t>(value);
                break;
            case DataType::DOUBLE:
                newImpl->value = std::get<double>(value);
                break;
            case DataType::STRING:
                newImpl->value = std::get<std::string>(value);
                break;
            case DataType::ARRAY: {
                const auto& srcVec = std::get<std::vector<ConfigNode>>(value);
                std::vector<ConfigNode> newVec;
                for (const auto& node : srcVec) {
                    //newVec.push_back(ConfigNode(*node.impl_));
                    newVec.push_back(node);
                }
                newImpl->value = std::move(newVec);
                break;
            }
            case DataType::OBJECT: {
                const auto& srcMap = std::get<std::map<std::string, ConfigNode>>(value);
                std::map<std::string, ConfigNode> newMap;
                for (const auto& [key, node] : srcMap) {
                    //newMap[key] = ConfigNode(*node.impl_);
                    newMap[key] = node;
                }
                newImpl->value = std::move(newMap);
                break;
            }
        }
        
        return newImpl;
    }
    
    // 路径访问
    ConfigNode getByPath(const std::string& path, char delimiter) const {
        if (path.empty()) {
            ConfigNode result;
            if (result.impl_) {
                result.impl_ = std::make_unique<Impl>(*this);
            }
            return result;
        }
        // 使用优化的 splitInto 方法
        foundation::utils::String::splitInto(path, delimiter, g_split_buffer);
    
        const Impl* current = this;
    
        // 遍历分割后的每一部分
        for (const auto& part : g_split_buffer) {
            if (current->type != DataType::OBJECT) {
                return ConfigNode();
            }
        const auto& obj = std::get<std::map<std::string, ConfigNode>>(current->value);
        
        // 使用线程局部缓冲区，避免创建临时 string
        ensureBufferCapacity(part.size());
        g_lookup_buffer.assign(part.data(), part.size());
        
        auto it = obj.find(g_lookup_buffer);
        if (it == obj.end()) {
            return ConfigNode();
        }
        
        current = it->second.impl_.get();
    }
    
        ConfigNode result;
        result.impl_ = std::make_unique<Impl>(*current);
        return result;
    }
    
    // 合并操作
    void merge(const Impl& other, bool overwrite) {
        if (type != DataType::OBJECT || other.type != DataType::OBJECT) {
            return;
        }
        
        auto& thisObj = std::get<std::map<std::string, ConfigNode>>(value);
        const auto& otherObj = std::get<std::map<std::string, ConfigNode>>(other.value);
        
        for (const auto& [key, otherNode] : otherObj) {
            auto it = thisObj.find(key);
            if (it == thisObj.end()) {
                // 新键，直接添加
                thisObj[key] = otherNode;
            } else if (overwrite) {
                // 覆盖现有值
                it->second = otherNode;
            } else {
                // 递归合并
                if (it->second.impl_->type == DataType::OBJECT && 
                    otherNode.impl_->type == DataType::OBJECT) {
                    it->second.impl_->merge(*otherNode.impl_, false);
                }
            }
        }
    }

    void overlay(const Impl& other) {
        merge(other, true);
    }
};
// ConfigNode.cpp
ConfigNode::ConfigNode(const ConfigNode& other) {
    if (other.impl_) {
        impl_ = std::make_unique<Impl>(*other.impl_);  // 深拷贝 Impl
    }
    // 如果 other.impl_ 为空，impl_ 会保持默认值（nullptr）
}
// 移动赋值运算符
ConfigNode& ConfigNode::operator=(ConfigNode&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
    }
    return *this;
}
ConfigNode& ConfigNode::operator=(const ConfigNode& other) {
    if (this != &other) {
        if (other.impl_) {
            impl_ = std::make_unique<Impl>(*other.impl_);
        } else {
            impl_.reset();  // 设置为空
        }
    }
    return *this;
}
// ============ ConfigNode 公有方法实现 ============
// 模板方法实现
template<typename T>
T ConfigNode::as() const {
    if constexpr (std::is_same_v<T, bool>) {
        return asBool();
    } else if constexpr (std::is_integral_v<T>) {
        return static_cast<T>(asInt());
    } else if constexpr (std::is_floating_point_v<T>) {
        return static_cast<T>(asDouble());
    } else if constexpr (std::is_same_v<T, std::string>) {
        return asString();
    } else {
        throw std::runtime_error("Unsupported type conversion");
    }
}

// 显式实例化常用类型
template bool ConfigNode::as<bool>() const;
template int ConfigNode::as<int>() const;
template double ConfigNode::as<double>() const;
template std::string ConfigNode::as<std::string>() const;

template<typename T>
T ConfigNode::as(const T& defaultValue) const {
    if (isNull()) return defaultValue;
    try {
        return as<T>();
    } catch (...) {
        return defaultValue;
    }
}

template bool ConfigNode::as<bool>(const bool&) const;
template int ConfigNode::as<int>(const int&) const;
template double ConfigNode::as<double>(const double&) const;
template std::string ConfigNode::as<std::string>(const std::string&) const;

template<typename T>
T ConfigNode::get(const std::string& key, const T& defaultValue) const {
    auto node = this->get(key);
    return node.as<T>(defaultValue);
}

template bool ConfigNode::get<bool>(const std::string&, const bool&) const;
template int ConfigNode::get<int>(const std::string&, const int&) const;
template double ConfigNode::get<double>(const std::string&, const double&) const;
template std::string ConfigNode::get<std::string>(const std::string&, const std::string&) const;

template<typename T>
T ConfigNode::get(const std::string& path, const T& defaultValue, char delimiter) const {
    auto node = this->getPath(path, delimiter);
    return node.as<T>(defaultValue);
}

template bool ConfigNode::get<bool>(const std::string&, const bool&, char) const;
template int ConfigNode::get<int>(const std::string&, const int&, char) const;
template double ConfigNode::get<double>(const std::string&, const double&, char) const;
template std::string ConfigNode::get<std::string>(const std::string&, const std::string&, char) const;
// 构造函数
ConfigNode::ConfigNode() : impl_(std::make_unique<Impl>()) {}

ConfigNode::ConfigNode(const foundation::json::JsonFacade& json) 
    : impl_(std::make_unique<Impl>()) {
    impl_->fromJson(json);
}
// 添加 YamlFacade 构造函数
ConfigNode::ConfigNode(const foundation::yaml::YamlFacade& yaml):impl_(std::make_unique<Impl>())
{
    impl_->fromYaml(yaml);
}
ConfigNode::ConfigNode(bool value) : impl_(std::make_unique<Impl>()) {
    impl_->type = Impl::DataType::BOOL;
    impl_->value = value;
}

ConfigNode::ConfigNode(int value) : impl_(std::make_unique<Impl>()) {
    impl_->type = Impl::DataType::INT;
    impl_->value = static_cast<int64_t>(value);
}

ConfigNode::ConfigNode(double value) : impl_(std::make_unique<Impl>()) {
    impl_->type = Impl::DataType::DOUBLE;
    impl_->value = value;
}

ConfigNode::ConfigNode(const std::string& value) : impl_(std::make_unique<Impl>()) {
    impl_->type = Impl::DataType::STRING;
    impl_->value = value;
}

ConfigNode::ConfigNode(const char* value) : impl_(std::make_unique<Impl>()) {
    impl_->type = Impl::DataType::STRING;
    impl_->value = std::string(value);
}

// 拷贝构造函数
// ConfigNode::ConfigNode(const ConfigNode& other) 
//     : impl_(other.impl_->clone()) {}

// 移动构造函数
ConfigNode::ConfigNode(ConfigNode&& other) noexcept
    : impl_(std::move(other.impl_)) {}

// 析构函数
ConfigNode::~ConfigNode() = default;

// // 赋值运算符
// ConfigNode& ConfigNode::operator=(const ConfigNode& other) {
//     if (this != &other) {
//         impl_ = other.impl_->clone();
//     }
//     return *this;
// }


// ============ 类型检查方法 ============

bool ConfigNode::isNull() const { 
    return impl_->type == Impl::DataType::NULL_VALUE; 
}

bool ConfigNode::isBool() const { 
    return impl_->type == Impl::DataType::BOOL; 
}

bool ConfigNode::isNumber() const { 
    return impl_->type == Impl::DataType::INT || 
           impl_->type == Impl::DataType::DOUBLE; 
}

bool ConfigNode::isInt() const { 
    return impl_->type == Impl::DataType::INT; 
}

bool ConfigNode::isDouble() const { 
    return impl_->type == Impl::DataType::DOUBLE; 
}

bool ConfigNode::isString() const { 
    return impl_->type == Impl::DataType::STRING; 
}

bool ConfigNode::isArray() const { 
    return impl_->type == Impl::DataType::ARRAY; 
}

bool ConfigNode::isObject() const { 
    return impl_->type == Impl::DataType::OBJECT; 
}

// ============ 值获取方法 ============

bool ConfigNode::asBool(bool defaultValue) const {
    if (isBool()) {
        return std::get<bool>(impl_->value);
    } else if (isInt()) {
        return std::get<int64_t>(impl_->value) != 0;
    } else if (isDouble()) {
        return std::get<double>(impl_->value) != 0.0;
    } else if (isString()) {
        std::string str = std::get<std::string>(impl_->value);
        return str == "true" || str == "1" || str == "yes";
    }
    return defaultValue;
}

int ConfigNode::asInt(int defaultValue) const {
    if (isInt()) {
        return static_cast<int>(std::get<int64_t>(impl_->value));
    } else if (isDouble()) {
        return static_cast<int>(std::get<double>(impl_->value));
    } else if (isBool()) {
        return std::get<bool>(impl_->value) ? 1 : 0;
    } else if (isString()) {
        try {
            return std::stoi(std::get<std::string>(impl_->value));
        } catch (...) {
            return defaultValue;
        }
    }
    return defaultValue;
}

double ConfigNode::asDouble(double defaultValue) const {
    if (isDouble()) {
        return std::get<double>(impl_->value);
    } else if (isInt()) {
        return static_cast<double>(std::get<int64_t>(impl_->value));
    } else if (isBool()) {
        return std::get<bool>(impl_->value) ? 1.0 : 0.0;
    } else if (isString()) {
        try {
            return std::stod(std::get<std::string>(impl_->value));
        } catch (...) {
            return defaultValue;
        }
    }
    return defaultValue;
}

std::string ConfigNode::asString(const std::string& defaultValue) const {
    if (isString()) {
        return std::get<std::string>(impl_->value);
    } else if (isBool()) {
        return std::get<bool>(impl_->value) ? "true" : "false";
    } else if (isInt()) {
        return std::to_string(std::get<int64_t>(impl_->value));
    } else if (isDouble()) {
        std::ostringstream oss;
        oss << std::get<double>(impl_->value);
        return oss.str();
    } else if (isNull()) {
        return "null";
    }
    return defaultValue;
}

// ============ 数组操作 ============

size_t ConfigNode::size() const {
    if (isArray()) {
        return std::get<std::vector<ConfigNode>>(impl_->value).size();
    } else if (isObject()) {
        return std::get<std::map<std::string, ConfigNode>>(impl_->value).size();
    }
    return 0;
}

ConfigNode ConfigNode::at(size_t index) const {
     if (!isArray()) {
        return ConfigNode();  // 空节点
    }
    
    const auto& arr = std::get<std::vector<ConfigNode>>(impl_->value);
    if (index < arr.size()) {
        // 手动创建拷贝，不依赖拷贝构造函数
        ConfigNode result;
        const auto& src = arr[index];
        if (src.impl_) {
            result.impl_ = std::make_unique<Impl>(*src.impl_);
        }
        return result;
    }
    
    return ConfigNode();  // 空节点
}

std::vector<ConfigNode> ConfigNode::asArray() const {
    if (!isArray()) {
        return {};
    }
    
    return std::get<std::vector<ConfigNode>>(impl_->value);
}

// ============ 对象操作 ============

bool ConfigNode::has(const std::string& key) const {
    if (!isObject()) {
        return false;
    }
    
    const auto& obj = std::get<std::map<std::string, ConfigNode>>(impl_->value);
    return obj.find(key) != obj.end();
}

ConfigNode ConfigNode::get(const std::string& key) const {
    if (!isObject()) {
        return ConfigNode();
    }
    
    const auto& obj = std::get<std::map<std::string, ConfigNode>>(impl_->value);
    auto it = obj.find(key);
    if (it != obj.end()) {
        return it->second;
    }
    
    return ConfigNode();
}

// 路径访问方法
ConfigNode ConfigNode::getPath(const std::string& path, char delimiter) const {
    return impl_->getByPath(path, delimiter);
}

// ============ 合并操作 ============

void ConfigNode::merge(const ConfigNode& other, bool overwrite) {
    impl_->merge(*other.impl_, overwrite);
}

void ConfigNode::overlay(const ConfigNode& other) {
    impl_->overlay(*other.impl_);
}

// ============ 序列化方法 ============

std::string ConfigNode::toString(bool pretty) const {
    return toJsonString(pretty);
}

std::string ConfigNode::toJsonString(bool pretty) const {
    try {
        auto json = impl_->toJson();
        return pretty ? json.toPrettyString() : json.toString();
    } catch (const std::exception& e) {
        return foundation::utils::String::format(
            "{\"error\": \"Failed to serialize: %s\"}", e.what()
        );
    }
}

std::string ConfigNode::toYamlString() const {
    try {
        // 使用您提供的 YamlFacade 实现
        foundation::yaml::YamlFacade yamlFacade;
        
        // 递归将 ConfigNode 转换为 YAML
        convertToYaml(yamlFacade, "");
        
        return yamlFacade.toString();
        
    } catch (const std::exception& e) {
        // 如果转换失败，回退到JSON格式
        ("Failed to convert to YAML: {}, falling back to JSON", e.what());
        return toJsonString(true);
    }

}
// 添加辅助方法：将 ConfigNode 转换为 YAML
void ConfigNode::convertToYaml(foundation::yaml::YamlFacade& yaml, const std::string& path) const {
    auto impl = impl_.get();  // 获取内部实现
    
    switch (impl->type) {
        case ConfigNode::Impl::DataType::NULL_VALUE:
            // YAML 中 null 表示为空，这里设置为空字符串
            if (!path.empty()) {
                yaml.setString(path, "");
            }
            break;
            
        case ConfigNode::Impl::DataType::BOOL:
            yaml.setBool(path, std::get<bool>(impl->value));
            break;
            
        case ConfigNode::Impl::DataType::INT:
            yaml.setInt(path, static_cast<int>(std::get<int64_t>(impl->value)));
            break;
            
        case ConfigNode::Impl::DataType::DOUBLE:
            yaml.setDouble(path, std::get<double>(impl->value));
            break;
            
        case ConfigNode::Impl::DataType::STRING:
            yaml.setString(path, std::get<std::string>(impl->value));
            break;
            
        case ConfigNode::Impl::DataType::ARRAY: {
            const auto& arr = std::get<std::vector<ConfigNode>>(impl->value);
            
            // 检查数组元素类型是否一致
            bool allStrings = true;
            bool allInts = true;
            bool allDoubles = true;
            
            for (const auto& node : arr) {
                if (!node.isString()) allStrings = false;
                if (!node.isInt()) allInts = false;
                if (!node.isDouble()) allDoubles = false;
            }
            
            if (allStrings) {
                std::vector<std::string> stringArr;
                for (const auto& node : arr) {
                    stringArr.push_back(node.asString());
                }
                yaml.setStringArray(path, stringArr);
            } else if (allInts) {
                std::vector<int> intArr;
                for (const auto& node : arr) {
                    intArr.push_back(node.asInt());
                }
                yaml.setIntArray(path, intArr);
            } else if (allDoubles) {
                std::vector<double> doubleArr;
                for (const auto& node : arr) {
                    doubleArr.push_back(node.asDouble());
                }
                yaml.setDoubleArray(path, doubleArr);
            } else {
                // 混合类型数组，需要递归处理
                // 由于 YamlFacade 的限制，这里简化为字符串数组
                std::vector<std::string> stringArr;
                for (const auto& node : arr) {
                    stringArr.push_back(node.asString());
                }
                yaml.setStringArray(path, stringArr);
            }
            break;
        }
            
        case ConfigNode::Impl::DataType::OBJECT: {
            const auto& obj = std::get<std::map<std::string, ConfigNode>>(impl->value);
            for (const auto& [key, node] : obj) {
                std::string childPath = path.empty() ? key : path + "." + key;
                // 递归处理子节点
                node.convertToYaml(yaml, childPath);
            }
            break;
        }
    }
}

// ========== 文件保存 =================

bool ConfigNode::saveToFile(const std::string& filename) const {
    // 根据文件扩展名决定保存格式
    if (filename.size() >= 5 && 
        filename.substr(filename.size() - 5) == ".yaml" ||
        filename.substr(filename.size() - 4) == ".yml") {
        return saveToYamlFile(filename);
    } else {
        // 默认保存为 JSON
    return saveToJsonFile(filename);
}
}

bool ConfigNode::saveToJsonFile(const std::string& filename, bool pretty) const {
    try {
        std::ofstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        file << toJsonString(pretty);
        return file.good();
    } catch (const std::exception& e) {
        // 日志记录错误
        return false;
    }
}

bool ConfigNode::saveToYamlFile(const std::string& filename) const {
    try {
        yaml::YamlFacade yaml;
        convertToYaml(yaml, "/");
        
        // 假设 YamlFacade 有保存功能
        // 如果没有，你可能需要先转换为字符串再写入文件
        std::ofstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        file << toYamlString();
        return file.good();
    } catch (const std::exception& e) {
        // 日志记录错误
        return false;
    }
}
// ============ 源信息方法 ============

const ConfigNode::SourceInfo& ConfigNode::getSourceInfo() const {
    return impl_->sourceInfo;
}

void ConfigNode::setSourceInfo(const SourceInfo& info) {
    impl_->sourceInfo = info;
}

} // namespace config
} // namespace foundation
