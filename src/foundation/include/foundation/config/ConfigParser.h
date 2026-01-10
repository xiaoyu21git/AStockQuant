#pragma once
#include <memory>
#include <string>
#include "IConfigParser.h"

namespace foundation::config {

class ConfigParser {
public:
    enum class Type {
        JSON,
        YAML
    };

    explicit ConfigParser(Type type);

    void loadString(const std::string& text);
    template<typename T>
    T getValue(const std::string& key) const;

    bool contains(const std::string& key) const;

private:
    std::unique_ptr<IConfigParser> impl_;
};

} // namespace foundation::config
