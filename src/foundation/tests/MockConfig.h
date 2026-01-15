// MockConfig.h
#ifndef MOCKCONFIG_H
#define MOCKCONFIG_H

#include "IConfig.h"
#include <gmock/gmock.h>
#include <optional>

class MockConfig : public IConfig {
public:
    virtual ~MockConfig() = default;
    
    MOCK_METHOD(bool, load, (const std::string& source), (override));
    MOCK_METHOD(bool, save, (const std::string& destination), (override));
    
    // 修复：明确指定返回类型
    MOCK_METHOD((std::optional<std::string>), getString, (const std::string& key), (const, override));
    MOCK_METHOD((std::optional<int>), getInt, (const std::string& key), (const, override));
    MOCK_METHOD((std::optional<double>), getDouble, (const std::string& key), (const, override));
    MOCK_METHOD((std::optional<bool>), getBool, (const std::string& key), (const, override));
    
    MOCK_METHOD(bool, setString, (const std::string& key, const std::string& value), (override));
    MOCK_METHOD(bool, setInt, (const std::string& key, int value), (override));
    MOCK_METHOD(bool, setBool, (const std::string& key, bool value), (override));
    
    MOCK_METHOD(bool, validate, (), (const, override));
    MOCK_METHOD(std::vector<std::string>, getErrors, (), (const, override));
    
    MOCK_METHOD(bool, hasKey, (const std::string& key), (const, override));
    MOCK_METHOD(size_t, count, (), (const, override));
    MOCK_METHOD(void, clear, (), (override));
    
    MOCK_METHOD(void, registerItem, (const ConfigItem& item), (override));
    MOCK_METHOD((std::optional<ConfigItem>), getItem, (const std::string& key), (const, override));
};

#endif // MOCKCONFIG_H