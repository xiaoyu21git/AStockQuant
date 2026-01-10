#include "foundation/config/ConfigParser.h"
#include "foundation/config/JsonParser.h"
#include "foundation/config/YamlParser.h"

namespace foundation::config {

ConfigParser::ConfigParser(Type type) {
    switch(type) {
        case Type::JSON:
            impl_ = std::make_unique<JsonParser>();
            break;
        case Type::YAML:
            impl_ = std::make_unique<YamlParser>();
            break;
        default:
            throw std::runtime_error("Unsupported config type");
    }
}

void ConfigParser::loadString(const std::string& text) {
    impl_->parseString(text);
}

bool ConfigParser::contains(const std::string& key) const {
    return impl_->contains(key);
}

template<typename T>
T ConfigParser::getValue(const std::string& key) const {
    return impl_->getValue<T>(key);
}

// 显式实例化常用类型（可选）
template int ConfigParser::getValue<int>(const std::string&) const;
template std::string ConfigParser::getValue<std::string>(const std::string&) const;

} // namespace foundation::config
