#pragma once
#include <memory>
#include <string>
#include <vector>
#include <map>

namespace foundation::json {
    
// 前向声明
class JsonValue;

// Facade：简化的JSON接口
class JsonFacade {
private:
    std::unique_ptr<JsonValue> root_;
    
public:
    JsonFacade();
    ~JsonFacade();
    
    // 构造接口
    explicit JsonFacade(std::unique_ptr<JsonValue> value);
    
    // 拷贝操作
    JsonFacade(const JsonFacade& other);
    JsonFacade& operator=(const JsonFacade& other);
    
    // 移动操作
    JsonFacade(JsonFacade&&) = default;
    JsonFacade& operator=(JsonFacade&&) = default;
    
    // 简单构造接口
    static JsonFacade createNull();
    static JsonFacade createBool(bool value);
    static JsonFacade createInt(int value);
    static JsonFacade createDouble(double value);
    static JsonFacade createString(const std::string& value);
    static JsonFacade createArray();
    static JsonFacade createObject();
    static JsonFacade createfloat(float value);
    static JsonFacade createlong(long value);
    
    // 解析接口
    static JsonFacade parse(const std::string& json);
    static JsonFacade parseFile(const std::string& filename);
    
    // 类型检查
    bool isNull() const;
    bool isBool() const;
    bool isNumber() const;
    bool isString() const;
    bool isArray() const;
    bool isObject() const;
    
    // 值获取
    bool asBool() const;
    int asInt() const;
    double asDouble() const;
    std::string asString() const;
    
    // 数组操作
    size_t size() const;
    JsonFacade at(size_t index) const;
    void push_back(const JsonFacade& value);
    
    // 对象操作
    bool has(const std::string& key) const;
    JsonFacade get(const std::string& key) const;
    void set(const std::string& key, const JsonFacade& value);
    
    // 序列化
    std::string toString() const;
    std::string toPrettyString(int indent = 2) const;
    bool saveToFile(const std::string& filename) const;
    
    // 内部访问
    JsonValue* getValue() const;
    bool empty() const { return !root_; }
    
private:
    // 私有构造函数，供工厂方法使用
    JsonFacade(JsonValue* value);
};

// 抽象接口
class JsonValue {
public:
    virtual ~JsonValue() = default;
    
    // 序列化
    virtual std::string toString() const = 0;
    virtual std::string toPrettyString(int indent = 2) const = 0;
    
    // 类型检查
    virtual bool isNull() const = 0;
    virtual bool isBool() const = 0;
    virtual bool isNumber() const = 0;
    virtual bool isString() const = 0;
    virtual bool isArray() const = 0;
    virtual bool isObject() const = 0;
    
    // 深拷贝
    virtual std::unique_ptr<JsonValue> clone() const = 0;
    
    // 类型转换
    virtual bool asBool() const = 0;
    virtual int asInt() const = 0;
    virtual double asDouble() const = 0;
    virtual std::string asString() const = 0;
    
    // 数组操作
    virtual size_t size() const = 0;
    virtual std::unique_ptr<JsonValue> at(size_t index) = 0;
    virtual std::unique_ptr<JsonValue> at(size_t index) const = 0;
    virtual void push_back(std::unique_ptr<JsonValue> value) = 0;
    
    // 对象操作
    virtual bool has(const std::string& key) const = 0;
    virtual std::unique_ptr<JsonValue> get(const std::string& key) = 0;
    virtual std::unique_ptr<JsonValue> get(const std::string& key) const = 0;
    virtual void set(const std::string& key, std::unique_ptr<JsonValue> value) = 0;
    
    // 工厂方法 - 需要添加 createNull 和 createDouble
    static std::unique_ptr<JsonValue> createNull();
    static std::unique_ptr<JsonValue> createBool(bool value);
    static std::unique_ptr<JsonValue> createInt(int value);
    static std::unique_ptr<JsonValue> createDouble(double value);
    static std::unique_ptr<JsonValue> createString(const std::string& value);
    static std::unique_ptr<JsonValue> createArray();
    static std::unique_ptr<JsonValue> createObject();
    static std::unique_ptr<JsonValue> createfloat(float value);
    static std::unique_ptr<JsonValue> createlong(long value);
    
    // 解析和序列化 - 修正返回类型
    static std::unique_ptr<JsonValue> parse(const std::string& json);
    static std::unique_ptr<JsonValue> parseFile(const std::string& filename);
};

} // namespace foundation::json