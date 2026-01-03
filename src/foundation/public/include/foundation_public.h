#pragma once

#include <string>
#include <memory>
#include <vector>
#include <map>
#include <functional>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <random>
#include <cctype>
#include <cstdlib>
#include <exception>

namespace foundation {

namespace fs = std::filesystem;

// ================= 异常类 =================
class Exception : public std::exception {
private:
    std::string message_;
    std::string file_;
    int line_;

public:
    Exception(const std::string& message, const std::string& file = "", int line = 0);
    const char* what() const noexcept override;
    std::string fullMessage() const;
    std::string file() const;
    int line() const;
};

class RuntimeException : public Exception {
public:
    RuntimeException(const std::string& message, const std::string& file = "", int line = 0);
};

class LogicException : public Exception {
public:
    LogicException(const std::string& message, const std::string& file = "", int line = 0);
};

class FileException : public Exception {
public:
    FileException(const std::string& message, const std::string& file = "", int line = 0);
};

class NetworkException : public Exception {
public:
    NetworkException(const std::string& message, const std::string& file = "", int line = 0);
};

class ParseException : public Exception {
public:
    ParseException(const std::string& message, const std::string& file = "", int line = 0);
};

class ConfigException : public Exception {
public:
    ConfigException(const std::string& message, const std::string& file = "", int line = 0);
};

// ================= FileReader =================
class FileReader {
private:
    class Impl;
    std::unique_ptr<Impl> impl_;

public:
    explicit FileReader(const std::string& filename);
    ~FileReader();

    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;
    FileReader(FileReader&&) noexcept;
    FileReader& operator=(FileReader&&) noexcept;

    std::string readAll();
    std::vector<std::string> readLines();
    bool readLine(std::string& line);

    bool isOpen() const;
    bool eof() const;
    size_t size() const;
    std::string filename() const;

    template<typename T>
    FileReader& operator>>(T& value) {
        impl_->stream() >> value;
        return *this;
    }
};

// ================= FileWriter =================
class FileWriter {
private:
    class Impl;
    std::unique_ptr<Impl> impl_;

public:
    explicit FileWriter(const std::string& filename, bool append = false, bool createDirs = true);
    ~FileWriter();

    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;
    FileWriter(FileWriter&&) noexcept;
    FileWriter& operator=(FileWriter&&) noexcept;

    bool write(const std::string& content);
    bool writeLine(const std::string& line);
    bool writeLines(const std::vector<std::string>& lines);

    template<typename T>
    FileWriter& operator<<(const T& value) {
        impl_->stream() << value;
        return *this;
    }

    void flush();
    bool isOpen() const;
    std::string filename() const;
};

// ================= TempFile =================
class TempFile {
private:
    class Impl;
    std::unique_ptr<Impl> impl_;

public:
    TempFile();
    explicit TempFile(const std::string& prefix);
    ~TempFile();

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&&) noexcept;
    TempFile& operator=(TempFile&&) noexcept;

    std::string path() const;
    FileReader openForReading() const;
    FileWriter openForWriting(bool append = false) const;
    bool remove();
};

// ================= FileLock =================
class FileLock {
private:
    class Impl;
    std::unique_ptr<Impl> impl_;

public:
    explicit FileLock(const std::string& filename);
    ~FileLock();

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    FileLock(FileLock&&) noexcept;
    FileLock& operator=(FileLock&&) noexcept;

    bool lock();
    bool tryLock();
    bool unlock();
    bool isLocked() const;
};

// ================= Utils =================
class Utils {
public:
    static bool fileExists(const std::string& path);
    static bool isFile(const std::string& path);
    static bool isDirectory(const std::string& path);
    static size_t fileSize(const std::string& path);
    static std::string fileExtension(const std::string& path);
    static std::string fileName(const std::string& path);
    static std::string directoryName(const std::string& path);
    static bool createDirectory(const std::string& path);
    static bool createDirectories(const std::string& path);
    static bool copyFile(const std::string& src, const std::string& dst);
    static bool moveFile(const std::string& src, const std::string& dst);
    static bool deleteFile(const std::string& path);
    static bool deleteDirectory(const std::string& path);
    static std::string readTextFile(const std::string& path);
    static bool writeTextFile(const std::string& path, const std::string& content);
    static std::vector<uint8_t> readBinaryFile(const std::string& path);
    static bool writeBinaryFile(const std::string& path, const std::vector<uint8_t>& data);
    static void walkDirectory(const std::string& path,
                              std::function<void(const std::string&)> fileCallback,
                              std::function<void(const std::string&)> dirCallback = nullptr);
    static std::string trim(const std::string& str);
    static std::string trimLeft(const std::string& str);
    static std::string trimRight(const std::string& str);
    static std::string toLower(const std::string& str);
    static std::string toUpper(const std::string& str);
    static bool startsWith(const std::string& str, const std::string& prefix);
    static bool endsWith(const std::string& str, const std::string& suffix);
    static std::vector<std::string> split(const std::string& str, char delimiter);
    static std::vector<std::string> split(const std::string& str, const std::string& delimiter);
    static std::string join(const std::vector<std::string>& strings, const std::string& delimiter);
    static std::string urlEncode(const std::string& str);
    static std::string urlDecode(const std::string& str);
    static std::string base64Encode(const std::string& str);
    static std::string base64Decode(const std::string& str);
    static std::string md5(const std::string& str);
    static std::string sha1(const std::string& str);
    static std::string sha256(const std::string& str);
    static std::string currentTimeString();
    static std::string currentTimeString(const std::string& format);
    static int64_t currentTimeMillis();
    static int64_t currentTimeMicros();
    static int randomInt(int min, int max);
    static std::string randomString(size_t length);
    static std::string uuid();
    static std::string getEnv(const std::string& name, const std::string& defaultValue = "");
    static bool setEnv(const std::string& name, const std::string& value);
    static std::string getCurrentDirectory();
    static bool setCurrentDirectory(const std::string& path);
    static std::string getHomeDirectory();
    static std::string getTempDirectory();

    template<typename T>
    static std::string toString(const T& value) {
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }

    template<typename T>
    static T fromString(const std::string& str) {
        std::istringstream iss(str);
        T value{};
        iss >> value;
        return value;
    }

    template<typename Container, typename Predicate>
    static bool contains(const Container& container, Predicate pred) {
        return std::any_of(container.begin(), container.end(), pred);
    }

    template<typename Container, typename T>
    static bool containsValue(const Container& container, const T& value) {
        return std::find(container.begin(), container.end(), value) != container.end();
    }

    template<typename Map, typename Key>
    static typename Map::mapped_type getValueOrDefault(const Map& map, const Key& key, const typename Map::mapped_type& defaultValue) {
        auto it = map.find(key);
        return it != map.end() ? it->second : defaultValue;
    }
};

// ============ 宏定义 ============

// 异常宏
#define FOUNDATION_THROW(exc, msg) \
    throw exc(msg, __FILE__, __LINE__)

#define FOUNDATION_THROW_IF(condition, exc, msg) \
    if (condition) FOUNDATION_THROW(exc, msg)

#define FOUNDATION_CHECK(condition, exc, msg) \
    FOUNDATION_THROW_IF(!(condition), exc, msg)

#define FOUNDATION_ASSERT(condition, msg) \
    FOUNDATION_CHECK(condition, foundation::LogicException, msg)

// 文件操作检查宏
#define FOUNDATION_CHECK_FILE_EXISTS(path) \
    FOUNDATION_CHECK(foundation::Utils::fileExists(path), \
                    foundation::FileException, "文件不存在: " + std::string(path))

#define FOUNDATION_CHECK_FILE_READABLE(path) \
    { \
        FOUNDATION_CHECK_FILE_EXISTS(path); \
        FOUNDATION_CHECK(foundation::Utils::isFile(path), \
                        foundation::FileException, "不是文件: " + std::string(path)); \
    }

// 日志宏
#define FOUNDATION_DEBUG(logger, msg) \
    if (logger) logger->debug(msg)

#define FOUNDATION_INFO(logger, msg) \
    if (logger) logger->info(msg)

#define FOUNDATION_WARNING(logger, msg) \
    if (logger) logger->warning(msg)

#define FOUNDATION_ERROR(logger, msg) \
    if (logger) logger->error(msg)

#define FOUNDATION_FATAL(logger, msg) \
    if (logger) logger->fatal(msg)

// 带位置的日志宏
#define FOUNDATION_DEBUG_L(logger, msg) \
    if (logger) logger->debug(msg + " [" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]")

#define FOUNDATION_INFO_L(logger, msg) \
    if (logger) logger->info(msg + " [" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]")

#define FOUNDATION_WARNING_L(logger, msg) \
    if (logger) logger->warning(msg + " [" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]")

#define FOUNDATION_ERROR_L(logger, msg) \
    if (logger) logger->error(msg + " [" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]")

#define FOUNDATION_FATAL_L(logger, msg) \
    if (logger) logger->fatal(msg + " [" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]")

// 快速文件操作宏
#define FOUNDATION_READ_FILE(path) \
    foundation::Utils::readTextFile(path)

#define FOUNDATION_WRITE_FILE(path, content) \
    foundation::Utils::writeTextFile(path, content)

#define FOUNDATION_WITH_FILE_READER(path, var_name) \
    foundation::FileReader var_name(path)

#define FOUNDATION_WITH_FILE_WRITER(path, var_name) \
    foundation::FileWriter var_name(path)

} // namespace foundation