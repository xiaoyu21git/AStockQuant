// json_impl.cpp
#include "json_facade.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>

namespace foundation::json {
    
// ============ 适配器实现 ============

// 第三方的JSON库适配器（这里以nlohmann/json为例）
class ThirdPartyJsonAdapter : public JsonValue {
private:
    // 假设这是第三方JSON库的类型
    void* thirdPartyValue_;
    
    // 私有构造函数
    ThirdPartyJsonAdapter(void* value);
    
public:
    ~ThirdPartyJsonAdapter() override;
    
    static ThirdPartyJsonAdapter* createNull();
    static ThirdPartyJsonAdapter* createBool(bool value);
    static ThirdPartyJsonAdapter* createInt(int value);
    static ThirdPartyJsonAdapter* createDouble(double value);
    static ThirdPartyJsonAdapter* createString(const std::string& value);
    static ThirdPartyJsonAdapter* createArray();
    static ThirdPartyJsonAdapter* createObject();
    
    static ThirdPartyJsonAdapter* parse(const std::string& json);
    static ThirdPartyJsonAdapter* parseFile(const std::string& filename);
    
    // 实现抽象接口
    std::string toString() const override;
    std::string toPrettyString(int indent = 2) const override;
    bool isNull() const override;
    bool isBool() const override;
    bool isNumber() const override;
    bool isString() const override;
    bool isArray() const override;
    bool isObject() const override;
    
    bool asBool() const override;
    int asInt() const override;
    double asDouble() const override;
    std::string asString() const override;
    
    size_t size() const override;
    JsonValue* at(size_t index) override;
    const JsonValue* at(size_t index) const override;
    void push_back(JsonValue* value) override;
    
    bool has(const std::string& key) const override;
    JsonValue* get(const std::string& key) override;
    const JsonValue* get(const std::string& key) const override;
    void set(const std::string& key, JsonValue* value) override;
    
private:
    // 转换为第三方库类型
    void* toThirdParty() const { return thirdPartyValue_; }
};

// ============ Facade实现 ============

// JsonValue工厂方法
JsonValue* JsonValue::createNull() {
    return ThirdPartyJsonAdapter::createNull();
}

JsonValue* JsonValue::createBool(bool value) {
    return ThirdPartyJsonAdapter::createBool(value);
}

JsonValue* JsonValue::createInt(int value) {
    return ThirdPartyJsonAdapter::createInt(value);
}

JsonValue* JsonValue::createDouble(double value) {
    return ThirdPartyJsonAdapter::createDouble(value);
}

JsonValue* JsonValue::createString(const std::string& value) {
    return ThirdPartyJsonAdapter::createString(value);
}

JsonValue* JsonValue::createArray() {
    return ThirdPartyJsonAdapter::createArray();
}

JsonValue* JsonValue::createObject() {
    return ThirdPartyJsonAdapter::createObject();
}

JsonValue* JsonValue::parse(const std::string& json) {
    return ThirdPartyJsonAdapter::parse(json);
}

JsonValue* JsonValue::parseFile(const std::string& filename) {
    return ThirdPartyJsonAdapter::parseFile(filename);
}

// JsonFacade实现
JsonFacade::JsonFacade() 
    : root_(nullptr) {}

JsonFacade::JsonFacade(JsonValue* value) 
    : root_(value) {}

JsonFacade::~JsonFacade() = default;
// 复制构造函数
JsonFacade::JsonFacade(const JsonFacade& other) {
    if (other.root_) {
        // 需要实现 JsonValue 的深拷贝
        // 这需要在 JsonValue 接口中添加 clone() 方法
        root_ = other.root_->clone();
    }
}

// 复制赋值操作符
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
JsonValue* JsonFacade::getValue() const {
    return root_.get();
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

// 代理方法
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

size_t JsonFacade::size() const {
    return root_ ? root_->size() : 0;
}

JsonFacade JsonFacade::at(size_t index) const {
    if (!root_) throw std::runtime_error("JsonFacade is empty");
    return JsonFacade(root_->at(index));
}

void JsonFacade::push_back(const JsonFacade& value) {
    if (!root_) {
        root_.reset(JsonValue::createArray());
    }
    // 需要复制value的内部值
    root_->push_back(value.getValue());
}

bool JsonFacade::has(const std::string& key) const {
    return root_ && root_->has(key);
}

JsonFacade JsonFacade::get(const std::string& key) const {
    if (!root_) throw std::runtime_error("JsonFacade is empty");
    return JsonFacade(root_->get(key));
}

void JsonFacade::set(const std::string& key, const JsonFacade& value) {
    if (!root_) {
        root_.reset(JsonValue::createObject());
    }
    root_->set(key, value.getValue());
}

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

} // namespace foundation::json