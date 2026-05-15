#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>

namespace foundation::json {
    
// Forward declaration
class JsonValue;

// Simplified JSON facade interface
class JsonFacade {
private:
    std::unique_ptr<JsonValue> root_;
    
public:
    JsonFacade();
    ~JsonFacade();
    
    // Construction
    explicit JsonFacade(std::unique_ptr<JsonValue> value);
    
    // Copy operations
    JsonFacade(const JsonFacade& other);
    JsonFacade& operator=(const JsonFacade& other);
    
    // Move operations
    JsonFacade(JsonFacade&&) = default;
    JsonFacade& operator=(JsonFacade&&) = default;
    
    // Simple factories
    static JsonFacade createNull();
    static JsonFacade createBool(bool value);
    static JsonFacade createInt(int value);
    static JsonFacade createDouble(double value);
    static JsonFacade createString(const std::string& value);
    static JsonFacade createArray();
    static JsonFacade createObject();
    static JsonFacade createfloat(float value);
    static JsonFacade createlong(long value);
    
    // Parsing
    static JsonFacade parse(const std::string& json);
    static JsonFacade parseFile(const std::string& filename);
    
    // Type inspection
    bool isNull() const;
    bool isBool() const;
    bool isNumber() const;
    bool isInteger() const;
    bool isString() const;
    bool isArray() const;
    bool isObject() const;
    
    // Value access
    bool asBool() const;
    int asInt() const;
    double asDouble() const;
    std::string asString() const;
    
    // Array access
    size_t size() const;
    JsonFacade at(size_t index) const;
    void push_back(const JsonFacade& value);
    
    // Object access
    bool has(const std::string& key) const;
    JsonFacade get(const std::string& key) const;
    std::vector<std::string> keys() const;
    void set(const std::string& key, const JsonFacade& value);
    
    // Serialization
    std::string toString() const;
    std::string toPrettyString(int indent = 2) const;
    bool saveToFile(const std::string& filename) const;
    
    // Internal access
    JsonValue* getValue() const;
    bool empty() const { return !root_; }
    
private:
    // Private constructor used by factories
    JsonFacade(JsonValue* value);
};

// Abstract interface
class JsonValue {
public:
    virtual ~JsonValue() = default;
    
    // Serialization
    virtual std::string toString() const = 0;
    virtual std::string toPrettyString(int indent = 2) const = 0;
    
    // Type inspection
    virtual bool isNull() const = 0;
    virtual bool isBool() const = 0;
    virtual bool isNumber() const = 0;
    virtual bool isInteger() const = 0;
    virtual bool isString() const = 0;
    virtual bool isArray() const = 0;
    virtual bool isObject() const = 0;
    
    // Deep copy
    virtual std::unique_ptr<JsonValue> clone() const = 0;
    
    // Value access
    virtual bool asBool() const = 0;
    virtual int asInt() const = 0;
    virtual double asDouble() const = 0;
    virtual std::string asString() const = 0;
    
    // Array access
    virtual size_t size() const = 0;
    virtual std::unique_ptr<JsonValue> at(size_t index) = 0;
    virtual std::unique_ptr<JsonValue> at(size_t index) const = 0;
    virtual void push_back(std::unique_ptr<JsonValue> value) = 0;
    
    // Object access
    virtual bool has(const std::string& key) const = 0;
    virtual std::unique_ptr<JsonValue> get(const std::string& key) = 0;
    virtual std::unique_ptr<JsonValue> get(const std::string& key) const = 0;
    virtual std::vector<std::string> keys() const = 0;
    virtual void set(const std::string& key, std::unique_ptr<JsonValue> value) = 0;
    
    // Factory helpers
    static std::unique_ptr<JsonValue> createNull();
    static std::unique_ptr<JsonValue> createBool(bool value);
    static std::unique_ptr<JsonValue> createInt(int value);
    static std::unique_ptr<JsonValue> createDouble(double value);
    static std::unique_ptr<JsonValue> createString(const std::string& value);
    static std::unique_ptr<JsonValue> createArray();
    static std::unique_ptr<JsonValue> createObject();
    static std::unique_ptr<JsonValue> createfloat(float value);
    static std::unique_ptr<JsonValue> createlong(long value);
    
    // Parsing helpers
    static std::unique_ptr<JsonValue> parse(const std::string& json);
    static std::unique_ptr<JsonValue> parseFile(const std::string& filename);
};

} // namespace foundation::json