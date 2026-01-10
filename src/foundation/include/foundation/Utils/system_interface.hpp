// modules/system_interface.hpp - 系统工具独立接口
#pragma once

#include <string>
#include <cstdint>
#include <chrono>

namespace foundation {

// 前向声明异常类
class Exception;

// ============ 系统工具接口（独立，不依赖完整Foundation）============
class ISystemUtils {
public:
    virtual ~ISystemUtils() = default;
    
    // 时间相关
    virtual std::string getCurrentTime() = 0;
    virtual std::string getCurrentTime(const std::string& format) = 0;
    virtual int64_t getCurrentTimestamp() = 0;
    virtual int64_t getCurrentMicroTimestamp() = 0;
    
    // 随机数生成
    virtual int getRandomInt(int min, int max) = 0;
    virtual std::string getRandomString(size_t length) = 0;
    virtual std::string generateUuid() = 0;
    
    // 环境变量
    virtual std::string getEnvironmentVariable(const std::string& name, 
                                              const std::string& defaultValue = "") = 0;
    virtual bool setEnvironmentVariable(const std::string& name, const std::string& value) = 0;
    
    // 文件系统路径
    virtual std::string getCurrentDirectory() = 0;
    virtual bool setCurrentDirectory(const std::string& path) = 0;
    virtual std::string getHomeDirectory() = 0;
    virtual std::string getTempDirectory() = 0;
    
    // 系统信息
    virtual int getProcessId() = 0;
    virtual std::string getHostname() = 0;
};

} // namespace foundation