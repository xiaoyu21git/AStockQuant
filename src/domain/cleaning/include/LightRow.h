// LightRow.h — 轻量对象行：按 schema 一次性分配列槽，列数无上限
// 替代 JsonFacade 用于清洗热路径；行池通过 resetObject() 原地复用列槽，避免反复分配
#pragma once
#include <string>
#include <variant>
#include <vector>
#include <unordered_map>

namespace cleaning {

struct LightValue {
    enum Type : uint8_t { Null, Bool, Double, String } type = Null;
    double d = 0.0; bool b = false; std::string s;
    LightValue() = default;
    LightValue(double v) : type(Double), d(v) {}
    LightValue(bool v)   : type(Bool),   b(v) {}
    LightValue(std::string v) : type(String), s(std::move(v)) {}
    LightValue(const char* v): type(String), s(v) {}
    LightValue(std::monostate) : type(Null) {}
    bool isNull()   const { return type == Null; }
    bool isBool()   const { return type == Bool; }
    bool isNumber() const { return type == Double; }
    bool isString() const { return type == String; }
    double asDouble() const { return d; }
    bool   asBool()   const { return b; }
    int    asInt()    const { return static_cast<int>(d); }
    const std::string& asString() const { return s; }
};

class LightSchema {
public:
    static LightSchema& instance() { static LightSchema s; return s; }
    // 注册全部字段名（无列数上限）；幂等：首次注册生效，重复调用忽略
    void init(const std::vector<std::string>& ns) {
        if (built_) return;
        for (size_t i = 0; i < ns.size(); ++i) nameToIdx_[ns[i]] = static_cast<int>(i);
        size_ = static_cast<int>(ns.size()); built_ = true;
    }
    int index(const char* n) const { if(!built_)return -1; auto it=nameToIdx_.find(n); return it!=nameToIdx_.end()?it->second:-1; }
    int size() const { return size_; } bool built() const { return built_; }
private:
    LightSchema() = default;
    std::unordered_map<std::string, int> nameToIdx_;
    int size_ = 0; bool built_ = false;
};

class LightRow {
public:
    // 对象行：按已注册 schema 一次性分配列槽（schema 未注册时列槽为空，写入全部忽略）
    LightRow() : isSingle_(false), values_(static_cast<size_t>(LightSchema::instance().size())) {}
    explicit LightRow(LightValue v) : isSingle_(true), single_(std::move(v)) {}

    static LightRow createNull()   { return LightRow(LightValue(std::monostate{})); }
    static LightRow createBool(bool v){ return LightRow(LightValue(v)); }
    static LightRow createInt(int v)  { return LightRow(LightValue(static_cast<double>(v))); }
    static LightRow createDouble(double v){ return LightRow(LightValue(v)); }
    static LightRow createString(const std::string& v){ return LightRow(LightValue(v)); }
    static LightRow createObject(){ LightRow r; return r; }
    static LightRow createArray() { return LightRow(LightValue{}); }

    // 原地重置为全新对象行（列槽复用不释放，供行池批间复用）
    void resetObject() {
        isSingle_ = false;
        for (auto& v : values_) v = LightValue{};
    }

    bool isNull()   const { return isSingle_ && single_.isNull(); }
    bool isBool()   const { return isSingle_ && single_.isBool(); }
    bool isNumber() const { return isSingle_ && single_.isNumber(); }
    bool isString() const { return isSingle_ && single_.isString(); }
    bool isObject() const { return !isSingle_; }
    bool isArray()  const { return false; }
    double asDouble() const { return single_.asDouble(); }
    bool asBool() const { return single_.asBool(); }
    int asInt() const { return single_.asInt(); }
    std::string asString() const { return single_.asString(); }

    bool has(const char* key) const {
        if(isSingle_)return false;
        int idx=LightSchema::instance().index(key);
        return idx>=0 && static_cast<size_t>(idx)<values_.size() && values_[static_cast<size_t>(idx)].type!=LightValue::Null;
    }
    LightRow get(const char* key) const {
        if(isSingle_)return LightRow(LightValue{});
        int idx=LightSchema::instance().index(key);
        return (idx>=0 && static_cast<size_t>(idx)<values_.size()) ? LightRow(values_[static_cast<size_t>(idx)]) : LightRow(LightValue{});
    }
    void setNull(const char* key){
        if(isSingle_)return;
        int idx=LightSchema::instance().index(key);
        if(idx>=0 && static_cast<size_t>(idx)<values_.size())values_[static_cast<size_t>(idx)]=LightValue(std::monostate{});
    }
    void setDouble(const char* key, double v){
        if(isSingle_)return;
        int idx=LightSchema::instance().index(key);
        if(idx>=0 && static_cast<size_t>(idx)<values_.size())values_[static_cast<size_t>(idx)]=LightValue(v);
    }
    void setString(const char* key, const std::string& v){
        if(isSingle_)return;
        int idx=LightSchema::instance().index(key);
        if(idx>=0 && static_cast<size_t>(idx)<values_.size())values_[static_cast<size_t>(idx)]=LightValue(v);
    }
    void setBool(const char* key, bool v){
        if(isSingle_)return;
        int idx=LightSchema::instance().index(key);
        if(idx>=0 && static_cast<size_t>(idx)<values_.size())values_[static_cast<size_t>(idx)]=LightValue(v);
    }
    void set(const char* key, const LightRow& value){
        if(isSingle_)return;
        int idx=LightSchema::instance().index(key);
        if(idx>=0 && static_cast<size_t>(idx)<values_.size())values_[static_cast<size_t>(idx)]=value.asInnerValue();
    }
    void remove(const char* key) { setNull(key); }
    size_t size() const { return 0; }
    LightRow at(size_t) const { return LightRow(LightValue{}); }
    void push_back(const LightRow&) {}
    std::vector<std::string> keys() const { return {}; }

    LightValue asInnerValue() const { return isSingle_?single_:LightValue{}; }
private:
    bool isSingle_;
    LightValue single_;
    std::vector<LightValue> values_;
};

} // namespace cleaning
