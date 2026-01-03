#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <memory>
#include <vector>
#include <map>


 // json_facade.h

namespace foundation::json {
    
// 抽象接口
class JsonValue {
public:

    virtual ~JsonValue() = default;
    virtual std::string toString() const = 0;
    virtual std::string toPrettyString(int indent = 2) const = 0;
    virtual bool isNull() const = 0;
    virtual bool isBool() const = 0;
    virtual bool isNumber() const = 0;
    virtual bool isString() const = 0;
    virtual bool isArray() const = 0;
    virtual bool isObject() const = 0;
    // 添加深拷贝方法
    virtual std::unique_ptr<JsonValue> clone() const = 0;
    // 类型转换
    virtual bool asBool() const = 0;
    virtual int asInt() const = 0;
    virtual double asDouble() const = 0;
    virtual std::string asString() const = 0;
    
    // 数组操作
    virtual size_t size() const = 0;
    virtual JsonValue* at(size_t index) = 0;
    virtual const JsonValue* at(size_t index) const = 0;
    virtual void push_back(JsonValue* value) = 0;
    
    // 对象操作
    virtual bool has(const std::string& key) const = 0;
    virtual JsonValue* get(const std::string& key) = 0;
    virtual const JsonValue* get(const std::string& key) const = 0;
    virtual void set(const std::string& key, JsonValue* value) = 0;
    
    // 工厂方法
    static JsonValue* createNull();
    static JsonValue* createBool(bool value);
    static JsonValue* createInt(int value);
    static JsonValue* createDouble(double value);
    static JsonValue* createString(const std::string& value);
    static JsonValue* createArray();
    static JsonValue* createObject();
    
    // 解析和序列化
    static JsonValue* parse(const std::string& json);
    static JsonValue* parseFile(const std::string& filename);
};

// Facade：简化的JSON接口
class JsonFacade {
private:
    std::unique_ptr<JsonValue> root_;
    
public:
    JsonFacade();
    ~JsonFacade();
      // 添加复制构造函数和赋值操作符
    JsonFacade(const JsonFacade& other);
    JsonFacade& operator=(const JsonFacade& other);
     // 保持移动操作
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
    
    // 数组操作（简化）
    size_t size() const;
    JsonFacade at(size_t index) const;
    void push_back(const JsonFacade& value);
    
    // 对象操作（简化）
    bool has(const std::string& key) const;
    JsonFacade get(const std::string& key) const;
    void set(const std::string& key, const JsonFacade& value);
    
    // 序列化
    std::string toString() const;
    std::string toPrettyString(int indent = 2) const;
    bool saveToFile(const std::string& filename) const;
    
private:
    explicit JsonFacade(JsonValue* value);
    JsonValue* getValue() const;
};

} // namespace foundation::json

