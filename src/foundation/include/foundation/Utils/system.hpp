// modules/system.hpp - 系统工具接口声明
#pragma once

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <direct.h>
#include <process.h>
#else
#include <unistd.h>
#include <pwd.h>
#include <sys/utsname.h>
#endif

#include "system_interface.hpp"
#include <chrono>
#include <random>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <cstdlib>
#include <string>
#include <vector>
#include <cstdint>

namespace foundation {
namespace utils  {

class SystemUtilsImpl : public ISystemUtils {
private:
    mutable std::mutex rngMutex_;
    mutable std::random_device rd_;
    mutable std::mt19937 rng_;
    
    static constexpr const char* ALPHANUMERIC = 
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    
    static constexpr const char* HEX_CHARS = "0123456789abcdef";
    
public:
    SystemUtilsImpl();
    ~SystemUtilsImpl() override = default;
    
    // ============ ISystemUtils 接口实现 ============
    std::string getCurrentTime() override;
    std::string getCurrentTime(const std::string& format) override;
    int64_t getCurrentTimestamp() override;
    int64_t getCurrentMicroTimestamp() override;
    
    int getRandomInt(int min, int max) override;
    std::string getRandomString(size_t length) override;
    std::string getRandomString(size_t length, const std::string& charset);
    std::string generateUuid() override;
    
    std::string getEnvironmentVariable(const std::string& name, 
                                      const std::string& defaultValue) override;
    bool setEnvironmentVariable(const std::string& name, const std::string& value) override;
    
    std::string getCurrentDirectory() override;
    bool setCurrentDirectory(const std::string& path) override;
    std::string getHomeDirectory() override;
    std::string getTempDirectory() override;
    
    int getProcessId() override;
    std::string getHostname() override;
    
    // ============ 新增方法（不属于接口） ============
    struct SystemInfo {
        std::string osName;
        std::string osVersion;
        std::string architecture;
        int processorCount;
        size_t totalMemory;
        size_t availableMemory;
    };
    
    struct DiskInfo {
        std::string name;
        std::string mountPoint;
        uint64_t totalBytes;
        uint64_t freeBytes;
        uint64_t usedBytes;
    };
    
    struct NetworkInfo {
        std::string interfaceName;
        std::string ipAddress;
        std::string macAddress;
        uint64_t bytesSent;
        uint64_t bytesReceived;
    };
    
    SystemInfo getSystemInfo();
    double getCpuUsage();
    std::vector<DiskInfo> getDiskInfo();
    std::vector<NetworkInfo> getNetworkInfo();
    
    bool isWindows() const;
    bool isLinux() const;
    bool isMacOS() const;
    
    std::vector<std::string> getCommandLineArgs();
    int executeCommand(const std::string& command, std::string* output = nullptr);
    
    void sleepMilliseconds(int milliseconds);
    void sleepSeconds(int seconds);
    
    std::chrono::steady_clock::time_point getProgramStartTime();
    int64_t getProgramUptime();
};

} // namespace modules
} // namespace foundation