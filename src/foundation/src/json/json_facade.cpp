#include "foundation/json/json_facade.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>

namespace foundation::json {
    
// ============ ThirdPartyJsonAdapter 实现 ============

class ThirdPartyJsonAdapter : public JsonValue {
private:
    nlohmann::json value_;
    
    // 私有构造函数
    explicit ThirdPartyJsonAdapter(const nlohmann::json& value) : value_(value) {}
    explicit ThirdPartyJsonAdapter(nlohmann::json&& value) : value_(std::move(value)) {}
    
public:
    ~ThirdPartyJsonAdapter() override = default;
    
    // ============ 静态工厂方法 ============
    static std::unique_ptr<JsonValue> createNull() {
        return std::unique_ptr<JsonValue>(new ThirdPartyJsonAdapter(nlohmann::json()));
    }
    
    static std::unique_ptr<JsonValue> createBool(bool value) {
        return std::unique_ptr<JsonValue>(new ThirdPartyJsonAdapter(value));
    }
    
    static std::unique_ptr<JsonValue> createInt(int value) {
        return std::unique_ptr<JsonValue>(new ThirdPartyJsonAdapter(value));
    }
    
    static std::unique_ptr<JsonValue> createDouble(double value) {
        return std::unique_ptr<JsonValue>(new ThirdPartyJsonAdapter(value));
    }
    //添加 float 和long 
    static std::unique_ptr<JsonValue> createfloat(float value) {
        return std::unique_ptr<JsonValue>(new ThirdPartyJsonAdapter(value));
    }
    static std::unique_ptr<JsonValue> createlong(long value) {
        return std::unique_ptr<JsonValue>(new ThirdPartyJsonAdapter(value));
    }
    static std::unique_ptr<JsonValue> createString(const std::string& value) {
        return std::unique_ptr<JsonValue>(new ThirdPartyJsonAdapter(value));
    }
    
    static std::unique_ptr<JsonValue> createArray() {
        return std::unique_ptr<JsonValue>(new ThirdPartyJsonAdapter(nlohmann::json::array()));
    }
    
    static std::unique_ptr<JsonValue> createObject() {
        return std::unique_ptr<JsonValue>(new ThirdPartyJsonAdapter(nlohmann::json::object()));
    }
    
    static std::unique_ptr<JsonValue> parse(const std::string& json) {
        try {
            nlohmann::json parsed = nlohmann::json::parse(json);
            return std::unique_ptr<JsonValue>(new ThirdPartyJsonAdapter(std::move(parsed)));
        } catch (const std::exception& e) {
            // 解析失败返回 null
            return createNull();
        }
    }
    
    static std::unique_ptr<JsonValue> parseFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return createNull();
        }
        
        try {
            nlohmann::json parsed = nlohmann::json::parse(file);
            return std::unique_ptr<JsonValue>(new ThirdPartyJsonAdapter(std::move(parsed)));
        } catch (const std::exception& e) {
            return createNull();
        }
    }
    
    // ============ 实现 JsonValue 接口 ============
    
    // 序列化
    std::string toString() const override {
        return value_.dump();
    }
    
    std::string toPrettyString(int indent = 2) const override {
        return value_.dump(indent);
    }
    
    // 类型检查
    bool isNull() const override {
        return value_.is_null();
    }
    
    bool isBool() const override {
        return value_.is_boolean();
    }
    
    bool isNumber() const override {
        return value_.is_number();
    }
    
    bool isString() const override {
        return value_.is_string();
    }
    
    bool isArray() const override {
        return value_.is_array();
    }
    
    bool isObject() const override {
        return value_.is_object();
    }
    
    // 深拷贝
    std::unique_ptr<JsonValue> clone() const override {
        return std::unique_ptr<JsonValue>(new ThirdPartyJsonAdapter(value_));
        
    }
    
    // 类型转换
    bool asBool() const override {
        if (!isBool()) {
            throw std::runtime_error("Not a boolean value");
        }
        return value_.get<bool>();
    }
    
    int asInt() const override {
        if (!isNumber()) {
            throw std::runtime_error("Not a number value");
        }
        return value_.get<int>();
    }
    
    double asDouble() const override {
        if (!isNumber()) {
            throw std::runtime_error("Not a number value");
        }
        return value_.get<double>();
    }
    
    std::string asString() const override {
        if (!isString()) {
            throw std::runtime_error("Not a string value");
        }
        return value_.get<std::string>();
    }
    
    // 数组操作
    size_t size() const override {
        if (!isArray()) {
            return 0;
        }
        return value_.size();
    }
    
    virtual std::unique_ptr<JsonValue> at(size_t index) override {
        if (!isArray() || index >= value_.size()) {
            return nullptr;
        }
        return std::unique_ptr<JsonValue>(new ThirdPartyJsonAdapter(value_[index]));
    }
    
    virtual std::unique_ptr<JsonValue> at(size_t index) const override {
        if (!isArray() || index >= value_.size()) {
            return nullptr;
        }
        return std::unique_ptr<JsonValue>(new ThirdPartyJsonAdapter(value_[index]));
    }
    
    void push_back(std::unique_ptr<JsonValue> value) override {
        if (!isArray()) {
            throw std::runtime_error("Not an array");
        }
        
        if (ThirdPartyJsonAdapter* adapter = dynamic_cast<ThirdPartyJsonAdapter*>(value.get())) {
            value_.push_back(adapter->value_);
            // value 的所有权已经被转移
            value.release(); // 注意：这里 release 是因为我们要接管所有权
        } else {
            throw std::runtime_error("Incompatible JsonValue type");
        }
    }
    
    // 对象操作
    bool has(const std::string& key) const override {
        if (!isObject()) {
            return false;
        }
        return value_.contains(key);
    }
    
    virtual std::unique_ptr<JsonValue> get(const std::string& key) override {
         if (!isObject() || !value_.contains(key)) {
            return nullptr;
        }
        return std::unique_ptr<JsonValue>(new ThirdPartyJsonAdapter(value_[key]));
    }
    
    virtual std::unique_ptr<JsonValue> get(const std::string& key) const override {
       if (!isObject() || !value_.contains(key)) {
            return nullptr;
        }
        return std::unique_ptr<JsonValue>(new ThirdPartyJsonAdapter(value_[key]));
    }
    
    void set(const std::string& key, std::unique_ptr<JsonValue> value) override {
        if (!isObject()) {
            throw std::runtime_error("Not an object");
        }
        
        if (ThirdPartyJsonAdapter* adapter = dynamic_cast<ThirdPartyJsonAdapter*>(value.get())) {
            value_[key] = adapter->value_;
            // value 的所有权已经被转移
            value.release(); // 注意：这里 release 是因为我们要接管所有权
        } else {
            throw std::runtime_error("Incompatible JsonValue type");
        }
    }
};

// ============ JsonValue 工厂方法实现 ============

std::unique_ptr<JsonValue> JsonValue::createNull() {
    return ThirdPartyJsonAdapter::createNull();
}

std::unique_ptr<JsonValue> JsonValue::createBool(bool value) {
    return ThirdPartyJsonAdapter::createBool(value);
}

std::unique_ptr<JsonValue> JsonValue::createInt(int value) {
    return ThirdPartyJsonAdapter::createInt(value);
}

std::unique_ptr<JsonValue> JsonValue::createDouble(double value) {
    return ThirdPartyJsonAdapter::createDouble(value);
}

std::unique_ptr<JsonValue> JsonValue::createString(const std::string& value) {
    return ThirdPartyJsonAdapter::createString(value);
}

std::unique_ptr<JsonValue> JsonValue::createArray() {
    return ThirdPartyJsonAdapter::createArray();
}

std::unique_ptr<JsonValue> JsonValue::createObject() {
    return ThirdPartyJsonAdapter::createObject();
}

std::unique_ptr<JsonValue> JsonValue::parse(const std::string& json) {
    return ThirdPartyJsonAdapter::parse(json);
}

std::unique_ptr<JsonValue> JsonValue::parseFile(const std::string& filename) {
    return ThirdPartyJsonAdapter::parseFile(filename);
}
//添加long 和float
std::unique_ptr<JsonValue> JsonValue::createfloat(float value) {
    return ThirdPartyJsonAdapter::createfloat(value);
}
std::unique_ptr<JsonValue> JsonValue::createlong(long value) {
    return ThirdPartyJsonAdapter::createlong(value);
}
// ============ JsonFacade 实现 ============

JsonFacade::JsonFacade() : root_(nullptr) {}

JsonFacade::JsonFacade(std::unique_ptr<JsonValue> value) : root_(std::move(value)) {}

JsonFacade::JsonFacade(JsonValue* value) : root_(value) {}

JsonFacade::~JsonFacade() = default;

JsonFacade::JsonFacade(const JsonFacade& other) {
    if (other.root_) {
        root_ = other.root_->clone();
    }
}

JsonFacade& JsonFacade::operator=(const JsonFacade& other) {
    if (this != &other) {
        if (other.root_) {
            root_ = other.root_->clone();
        } else {
            root_.reset();
        }
    }
    return *this;
}

// 静态工厂方法
JsonFacade JsonFacade::createNull() {
    return JsonFacade(JsonValue::createNull());
}

JsonFacade JsonFacade::createBool(bool value) {
    return JsonFacade(JsonValue::createBool(value));
}

JsonFacade JsonFacade::createInt(int value) {
    return JsonFacade(JsonValue::createInt(value));
}

JsonFacade JsonFacade::createDouble(double value) {
    return JsonFacade(JsonValue::createDouble(value));
}

JsonFacade JsonFacade::createString(const std::string& value) {
    return JsonFacade(JsonValue::createString(value));
}
JsonFacade JsonFacade::createfloat(float value){
    return JsonFacade(JsonValue::createfloat(value));
}
JsonFacade JsonFacade::createlong(long value){
    return JsonFacade(JsonValue::createlong(value));
}
JsonFacade JsonFacade::createArray() {
    return JsonFacade(JsonValue::createArray());
}

JsonFacade JsonFacade::createObject() {
    return JsonFacade(JsonValue::createObject());
}

JsonFacade JsonFacade::parse(const std::string& json) {
    return JsonFacade(JsonValue::parse(json));
}

JsonFacade JsonFacade::parseFile(const std::string& filename) {
    return JsonFacade(JsonValue::parseFile(filename));
}

// 类型检查
bool JsonFacade::isNull() const {
    return root_ ? root_->isNull() : true;
}

bool JsonFacade::isBool() const {
    return root_ && root_->isBool();
}

bool JsonFacade::isNumber() const {
    return root_ && root_->isNumber();
}

bool JsonFacade::isString() const {
    return root_ && root_->isString();
}

bool JsonFacade::isArray() const {
    return root_ && root_->isArray();
}

bool JsonFacade::isObject() const {
    return root_ && root_->isObject();
}

// 值获取
bool JsonFacade::asBool() const {
    if (!root_) throw std::runtime_error("JsonFacade is empty");
    return root_->asBool();
}

int JsonFacade::asInt() const {
    if (!root_) throw std::runtime_error("JsonFacade is empty");
    return root_->asInt();
}

double JsonFacade::asDouble() const {
    if (!root_) throw std::runtime_error("JsonFacade is empty");
    return root_->asDouble();
}

std::string JsonFacade::asString() const {
    if (!root_) throw std::runtime_error("JsonFacade is empty");
    return root_->asString();
}

// 数组操作
size_t JsonFacade::size() const {
    return root_ ? root_->size() : 0;
}

JsonFacade JsonFacade::at(size_t index) const {
    if (!root_) throw std::runtime_error("JsonFacade is empty");
     std::unique_ptr<JsonValue> value = root_->at(index);
    if (!value) {
        return JsonFacade(); // 返回空的 JsonFacade
    }
    return JsonFacade(std::move(value));
}

void JsonFacade::push_back(const JsonFacade& value) {
    if (!root_) {
        root_ = JsonValue::createArray();
    }
    root_->push_back(value.root_->clone());
}

// 对象操作
bool JsonFacade::has(const std::string& key) const {
    return root_ && root_->has(key);
}

JsonFacade JsonFacade::get(const std::string& key) const {
    if (!root_) throw std::runtime_error("JsonFacade is empty");
    std::unique_ptr<JsonValue> value= root_->get(key);
    if (!value) {
        return JsonFacade(); // 返回空的 JsonFacade
    }
    return JsonFacade(std::move(value));
}

void JsonFacade::set(const std::string& key, const JsonFacade& value) {
    if (!root_) {
        root_ = JsonValue::createObject();
    }
    root_->set(key, value.root_->clone());
}

// 序列化
std::string JsonFacade::toString() const {
    return root_ ? root_->toString() : "null";
}

std::string JsonFacade::toPrettyString(int indent) const {
    return root_ ? root_->toPrettyString(indent) : "null";
}

bool JsonFacade::saveToFile(const std::string& filename) const {
    if (!root_) return false;
    
    std::ofstream file(filename);
    if (!file.is_open()) return false;
    
    file << toPrettyString();
    return true;
}

JsonValue* JsonFacade::getValue() const {
    return root_.get();
}

} // namespace foundation::json