// modules/system.cpp - 系统工具实现
#include "foundation/Utils/system.hpp"
#include <fstream>
#include <algorithm>
#include <cctype>

namespace foundation {
namespace utils  {

// ============ 构造函数 ============
SystemUtilsImpl::SystemUtilsImpl() : rng_(rd_()) {}

// ============ 时间相关方法 ============
std::string SystemUtilsImpl::getCurrentTime() {
    return getCurrentTime("%Y-%m-%d %H:%M:%S");
}

std::string SystemUtilsImpl::getCurrentTime(const std::string& format) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    
    std::ostringstream oss;
    oss << std::put_time(&tm, format.c_str());
    return oss.str();
}

int64_t SystemUtilsImpl::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
}

int64_t SystemUtilsImpl::getCurrentMicroTimestamp() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
}

// ============ 随机数方法 ============
int SystemUtilsImpl::getRandomInt(int min, int max) {
    std::lock_guard<std::mutex> lock(rngMutex_);
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng_);
}

std::string SystemUtilsImpl::getRandomString(size_t length) {
    return getRandomString(length, ALPHANUMERIC);
}

// ============ 随机数方法 ============
std::string SystemUtilsImpl::getRandomString(size_t length, const std::string& charset) {
    if (charset.empty()) {
        return "";
    }
    
    std::lock_guard<std::mutex> lock(rngMutex_);
    
    // 修复：使用 unsigned int 而不是 size_t
    std::uniform_int_distribution<unsigned int> dist(0, 
        static_cast<unsigned int>(charset.size() - 1));
    
    std::string result;
    result.reserve(length);
    
    for (size_t i = 0; i < length; ++i) {
        result.push_back(charset[dist(rng_)]);
    }
    
    return result;
}

// ============ UUID 生成 ============
std::string SystemUtilsImpl::generateUuid() {
    std::lock_guard<std::mutex> lock(rngMutex_);
    std::uniform_int_distribution<int> dist(0, 15);
    
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    
    // 前8个字符
    for (int i = 0; i < 8; ++i) {
        ss << std::setw(1) << dist(rng_);
    }
    ss << '-';
    
    // 接下来4个字符
    for (int i = 0; i < 4; ++i) {
        ss << std::setw(1) << dist(rng_);
    }
    ss << "-4";
    
    // 接下来3个字符
    for (int i = 0; i < 3; ++i) {
        ss << std::setw(1) << dist(rng_);
    }
    ss << '-';
    
    std::uniform_int_distribution<int> yDist(8, 11);
    ss << std::setw(1) << yDist(rng_);
    
    // 最后12个字符
    for (int i = 0; i < 12; ++i) {
        ss << std::setw(1) << dist(rng_);
    }
    
    return ss.str();
}

// ============ 环境变量 ============
std::string SystemUtilsImpl::getEnvironmentVariable(const std::string& name, 
                                                   const std::string& defaultValue) {
    const char* value = std::getenv(name.c_str());
    return value ? std::string(value) : defaultValue;
}

bool SystemUtilsImpl::setEnvironmentVariable(const std::string& name, const std::string& value) {
#ifdef _WIN32
    return _putenv_s(name.c_str(), value.c_str()) == 0;
#else
    return setenv(name.c_str(), value.c_str(), 1) == 0;
#endif
}

// ============ 文件系统路径 ============
std::string SystemUtilsImpl::getCurrentDirectory() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    if (GetCurrentDirectoryA(MAX_PATH, buffer)) {
        return buffer;
    }
#else
    char buffer[PATH_MAX];
    if (getcwd(buffer, sizeof(buffer))) {
        return buffer;
    }
#endif
    return "";
}

bool SystemUtilsImpl::setCurrentDirectory(const std::string& path) {
#ifdef _WIN32
    return SetCurrentDirectoryA(path.c_str()) != 0;
#else
    return chdir(path.c_str()) == 0;
#endif
}

std::string SystemUtilsImpl::getHomeDirectory() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PROFILE, nullptr, 0, buffer))) {
        return buffer;
    }
    const char* home = std::getenv("USERPROFILE");
    return home ? home : "";
#else
    const char* home = std::getenv("HOME");
    if (home) return home;
    
    struct passwd* pw = getpwuid(getuid());
    return pw ? pw->pw_dir : "";
#endif
}

std::string SystemUtilsImpl::getTempDirectory() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    if (GetTempPathA(MAX_PATH, buffer)) {
        std::string path(buffer);
        if (!path.empty() && (path.back() == '\\' || path.back() == '/')) {
            path.pop_back();
        }
        return path;
    }
#else
    const char* tmp = std::getenv("TMPDIR");
    if (tmp) return tmp;
    
    const char* tmp2 = std::getenv("TEMP");
    if (tmp2) return tmp2;
    
    const char* tmp3 = std::getenv("TMP");
    if (tmp3) return tmp3;
    
    return "/tmp";
#endif
    return "";
}

// ============ 系统信息 ============
int SystemUtilsImpl::getProcessId() {
#ifdef _WIN32
    return static_cast<int>(GetCurrentProcessId());
#else
    return static_cast<int>(getpid());
#endif
}

std::string SystemUtilsImpl::getHostname() {
#ifdef _WIN32
    char buffer[256];
    DWORD size = sizeof(buffer);
    if (GetComputerNameA(buffer, &size)) {
        return buffer;
    }
#else
    char buffer[256];
    if (gethostname(buffer, sizeof(buffer)) == 0) {
        return buffer;
    }
#endif
    return "unknown";
}

// ============ 新增方法实现 ============
SystemUtilsImpl::SystemInfo SystemUtilsImpl::getSystemInfo() {
    SystemInfo info;
    
#ifdef _WIN32
    OSVERSIONINFOEX osvi = { sizeof(OSVERSIONINFOEX) };
    if (GetVersionEx((OSVERSIONINFO*)&osvi)) {
        info.osName = "Windows";
        info.osVersion = std::to_string(osvi.dwMajorVersion) + "." + 
                        std::to_string(osvi.dwMinorVersion);
    }
    
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    info.processorCount = sysInfo.dwNumberOfProcessors;
    
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        info.totalMemory = memInfo.ullTotalPhys;
        info.availableMemory = memInfo.ullAvailPhys;
    }
    
    switch (sysInfo.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64:
            info.architecture = "x64";
            break;
        case PROCESSOR_ARCHITECTURE_ARM:
            info.architecture = "ARM";
            break;
        case PROCESSOR_ARCHITECTURE_IA64:
            info.architecture = "IA64";
            break;
        case PROCESSOR_ARCHITECTURE_INTEL:
            info.architecture = "x86";
            break;
        default:
            info.architecture = "unknown";
    }
#else
    struct utsname unameData;
    if (uname(&unameData) == 0) {
        info.osName = unameData.sysname;
        info.osVersion = unameData.release;
        info.architecture = unameData.machine;
    }
    
    info.processorCount = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    
    std::ifstream meminfo("/proc/meminfo");
    if (meminfo.is_open()) {
        std::string line;
        while (std::getline(meminfo, line)) {
            if (line.find("MemTotal:") == 0) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string value = line.substr(pos + 1);
                    value.erase(std::remove_if(value.begin(), value.end(), ::isspace), value.end());
                    size_t kb = std::stoull(value);
                    info.totalMemory = kb * 1024;
                }
            } else if (line.find("MemAvailable:") == 0) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    std::string value = line.substr(pos + 1);
                    value.erase(std::remove_if(value.begin(), value.end(), ::isspace), value.end());
                    size_t kb = std::stoull(value);
                    info.availableMemory = kb * 1024;
                }
            }
        }
    }
#endif
    
    return info;
}

double SystemUtilsImpl::getCpuUsage() {
    return 0.0;
}

std::vector<SystemUtilsImpl::DiskInfo> SystemUtilsImpl::getDiskInfo() {
    std::vector<DiskInfo> disks;
    return disks;
}

std::vector<SystemUtilsImpl::NetworkInfo> SystemUtilsImpl::getNetworkInfo() {
    std::vector<NetworkInfo> networks;
    return networks;
}

bool SystemUtilsImpl::isWindows() const {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

bool SystemUtilsImpl::isLinux() const {
#ifdef __linux__
    return true;
#else
    return false;
#endif
}

bool SystemUtilsImpl::isMacOS() const {
#ifdef __APPLE__
    return true;
#else
    return false;
#endif
}

std::vector<std::string> SystemUtilsImpl::getCommandLineArgs() {
    std::vector<std::string> args;
#ifdef _WIN32
    int argc;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 0; i < argc; ++i) {
            int size = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
            if (size > 0) {
                std::string arg(size - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, &arg[0], size, nullptr, nullptr);
                args.push_back(arg);
            }
        }
        LocalFree(argv);
    }
#endif
    return args;
}

int SystemUtilsImpl::executeCommand(const std::string& command, std::string* output) {
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
    if (!pipe) return -1;
    
    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    
    int exitCode = _pclose(pipe);
    if (output) {
        *output = result;
    }
    return exitCode;
#else
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return -1;
    
    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    
    int exitCode = pclose(pipe);
    if (output) {
        *output = result;
    }
    return exitCode;
#endif
}

void SystemUtilsImpl::sleepMilliseconds(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void SystemUtilsImpl::sleepSeconds(int seconds) {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

std::chrono::steady_clock::time_point SystemUtilsImpl::getProgramStartTime() {
    static const std::chrono::steady_clock::time_point startTime = 
        std::chrono::steady_clock::now();
    return startTime;
}

int64_t SystemUtilsImpl::getProgramUptime() {
    auto now = std::chrono::steady_clock::now();
    auto start = getProgramStartTime();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now - start).count();
}

} // namespace modules
} // namespace foundation