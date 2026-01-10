#include "log/Log.h"
#include "config/ConfigParser.h"
#include <iostream>
#include <memory>

using namespace foundation::log;
using namespace foundation::config;

// ------------------------
// 占位 Backend 示例（可替换成 spdlog）
// ------------------------
class TestLogBackend : public ILogBackend {
public:
    void info(const std::string& msg) override  { std::cout << "[TEST INFO] " << msg << "\n"; }
    void warn(const std::string& msg) override  { std::cout << "[TEST WARN] " << msg << "\n"; }
    void error(const std::string& msg) override { std::cout << "[TEST ERROR] " << msg << "\n"; }
    void debug(const std::string& msg) override { std::cout << "[TEST DEBUG] " << msg << "\n"; }
};

// ------------------------
// 占位 fs 功能
// ------------------------
std::string readFileAsString(const std::string& path) {
    // 实际工程这里可以用 fs::readFile
    if (path == "config.json") {
        return R"({"name": "Alice", "age": 30})";
    }
    if (path == "config.yaml") {
        return R"(
name: Bob
age: 25
)";
    }
    throw std::runtime_error("File not found: " + path);
}

int main() {
    // ------------------------
    // 1️⃣ 注入日志 backend
    // ------------------------
    setBackend(std::make_unique<TestLogBackend>());
    info("Logger initialized");

    try {
        // ------------------------
        // 2️⃣ JSON 配置测试
        // ------------------------
        ConfigParser jsonParser(ConfigParser::Type::JSON);
        std::string jsonText = readFileAsString("config.json");
        jsonParser.loadString(jsonText);
        info("JSON config loaded");

        std::string name = jsonParser.getValue<std::string>("name");
        int age = jsonParser.getValue<int>("age");
        info("JSON name: " + name + ", age: " + std::to_string(age));

        // ------------------------
        // 3️⃣ YAML 配置测试
        // ------------------------
        ConfigParser yamlParser(ConfigParser::Type::YAML);
        std::string yamlText = readFileAsString("config.yaml");
        yamlParser.loadString(yamlText);
        info("YAML config loaded");

        std::string yName = yamlParser.getValue<std::string>("name");
        int yAge = yamlParser.getValue<int>("age");
        info("YAML name: " + yName + ", age: " + std::to_string(yAge));

        // ------------------------
        // 4️⃣ 测试异常处理
        // ------------------------
        if (!jsonParser.contains("unknown_key")) {
            warn("Key 'unknown_key' does not exist in JSON config");
        }
        try {
            jsonParser.getValue<int>("unknown_key"); // 应抛异常
        } catch (const std::exception& e) {
            error(std::string("Caught exception as expected: ") + e.what());
        }

    } catch (const std::exception& e) {
        error(std::string("Unexpected error: ") + e.what());
        return 1;
    }

    info("All tests passed");
    return 0;
}
