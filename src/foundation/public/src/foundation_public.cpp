// foundation_public.cpp
#include "foundation_public.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <algorithm>
#include <random>
#include <filesystem>  // 必须加
#include <functional>
#include <cstdlib>     // getenv, setenv
#include <vector>
#include <string>

namespace fs = std::filesystem;

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>  // open
#endif

namespace foundation {

// ================= 异常类 =================

Exception::Exception(const std::string& message, const std::string& file, int line)
    : message_(message), file_(file), line_(line) {}

const char* Exception::what() const noexcept {
    return message_.c_str();
}

std::string Exception::fullMessage() const {
    if (file_.empty()) return message_;
    return message_ + " [" + file_ + ":" + std::to_string(line_) + "]";
}

RuntimeException::RuntimeException(const std::string& msg, const std::string& file, int line)
    : Exception(msg, file, line) {}
LogicException::LogicException(const std::string& msg, const std::string& file, int line)
    : Exception(msg, file, line) {}
FileException::FileException(const std::string& msg, const std::string& file, int line)
    : Exception(msg, file, line) {}
NetworkException::NetworkException(const std::string& msg, const std::string& file, int line)
    : Exception(msg, file, line) {}
ParseException::ParseException(const std::string& msg, const std::string& file, int line)
    : Exception(msg, file, line) {}
ConfigException::ConfigException(const std::string& msg, const std::string& file, int line)
    : Exception(msg, file, line) {}

// ================= FileReader =================

class FileReader::Impl {
private:
    std::ifstream stream_;
    std::string filename_;
public:
    explicit Impl(const std::string& filename) : filename_(filename) {
        stream_.open(filename, std::ios::binary);
        if (!stream_.is_open()) throw FileException("无法打开文件: " + filename);
    }
    ~Impl() { if (stream_.is_open()) stream_.close(); }

    std::string readAll() {
        if (!stream_.is_open()) return "";
        stream_.seekg(0, std::ios::end);
        size_t size = static_cast<size_t>(stream_.tellg());
        stream_.seekg(0, std::ios::beg);
        std::string content(size, '\0');
        stream_.read(&content[0], size);
        return content;
    }

    std::vector<std::string> readLines() {
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(stream_, line)) lines.push_back(line);
        stream_.clear();
        stream_.seekg(0);
        return lines;
    }

    bool readLine(std::string& line) { return static_cast<bool>(std::getline(stream_, line)); }
    bool isOpen() const { return stream_.is_open(); }
    bool eof() const { return stream_.eof(); }
    size_t size() { auto pos = stream_.tellg(); stream_.seekg(0, std::ios::end); auto s = stream_.tellg(); stream_.seekg(pos); return static_cast<size_t>(s); }
    std::string filename() const { return filename_; }
    std::ifstream& stream() { return stream_; }
};

FileReader::FileReader(const std::string& filename) : impl_(std::make_unique<Impl>(filename)) {}
FileReader::~FileReader() = default;
FileReader::FileReader(FileReader&& other) noexcept : impl_(std::move(other.impl_)) {}
FileReader& FileReader::operator=(FileReader&& other) noexcept { impl_ = std::move(other.impl_); return *this; }
std::string FileReader::readAll() { return impl_->readAll(); }
std::vector<std::string> FileReader::readLines() { return impl_->readLines(); }
bool FileReader::readLine(std::string& line) { return impl_->readLine(line); }
bool FileReader::isOpen() const { return impl_->isOpen(); }
bool FileReader::eof() const { return impl_->eof(); }
//size_t FileReader::size() { return impl_->size(); }
std::string FileReader::filename() const { return impl_->filename(); }
//template<typename T> FileReader& FileReader::operator>>(T& value) { impl_->stream() >> value; return *this; }
// 显式实例化
template FileReader& FileReader::operator>> <int>(int&);
template FileReader& FileReader::operator>> <double>(double&);
template FileReader& FileReader::operator>> <std::string>(std::string&);

// ================= FileWriter =================

class FileWriter::Impl {
private:
    std::ofstream stream_;
    std::string filename_;
public:
    Impl(const std::string& filename, bool append, bool createDirs) : filename_(filename) {
        if (createDirs) { auto dir = fs::path(filename).parent_path(); if (!dir.empty()) fs::create_directories(dir); }
        std::ios::openmode mode = std::ios::binary | (append ? std::ios::app : std::ios::trunc);
        stream_.open(filename, mode);
        if (!stream_.is_open()) throw FileException("无法打开文件: " + filename);
    }
    ~Impl() { if (stream_.is_open()) stream_.close(); }

    bool write(const std::string& content) { if (!stream_.is_open()) return false; stream_ << content; return true; }
    bool writeLine(const std::string& line) { if (!stream_.is_open()) return false; stream_ << line << '\n'; return true; }
    bool writeLines(const std::vector<std::string>& lines) { if (!stream_.is_open()) return false; for (auto& l : lines) stream_ << l << '\n'; return true; }
    void flush() { if (stream_.is_open()) stream_.flush(); }
    bool isOpen() const { return stream_.is_open(); }
    std::string filename() const { return filename_; }
    std::ofstream& stream() { return stream_; }
};

FileWriter::FileWriter(const std::string& filename, bool append, bool createDirs) : impl_(std::make_unique<Impl>(filename, append, createDirs)) {}
FileWriter::~FileWriter() = default;
FileWriter::FileWriter(FileWriter&& other) noexcept : impl_(std::move(other.impl_)) {}
FileWriter& FileWriter::operator=(FileWriter&& other) noexcept { impl_ = std::move(other.impl_); return *this; }
bool FileWriter::write(const std::string& content) { return impl_->write(content); }
bool FileWriter::writeLine(const std::string& line) { return impl_->writeLine(line); }
bool FileWriter::writeLines(const std::vector<std::string>& lines) { return impl_->writeLines(lines); }
void FileWriter::flush() { impl_->flush(); }
bool FileWriter::isOpen() const { return impl_->isOpen(); }
std::string FileWriter::filename() const { return impl_->filename(); }
//template<typename T> FileWriter& FileWriter::operator<<(const T& value) { impl_->stream() << value; return *this; }
template FileWriter& FileWriter::operator<< <int>(const int&);
template FileWriter& FileWriter::operator<< <double>(const double&);
template FileWriter& FileWriter::operator<< <std::string>(const std::string&);
template FileWriter& FileWriter::operator<< <const char*>(const char* const&);

// ================= TempFile =================

class TempFile::Impl {
private:
    fs::path tempPath_;
public:
    Impl() { tempPath_ = fs::temp_directory_path() / ("foundation_temp_" + std::to_string(std::rand()) + ".tmp"); std::ofstream(tempPath_).close(); }
    explicit Impl(const std::string& prefix) { tempPath_ = fs::temp_directory_path() / (prefix + "_" + std::to_string(std::rand()) + ".tmp"); std::ofstream(tempPath_).close(); }
    ~Impl() { if (fs::exists(tempPath_)) fs::remove(tempPath_); }

    std::string path() const { return tempPath_.string(); }
    bool remove() { if (fs::exists(tempPath_)) return fs::remove(tempPath_); return false; }
};

TempFile::TempFile() : impl_(std::make_unique<Impl>()) {}
TempFile::TempFile(const std::string& prefix) : impl_(std::make_unique<Impl>(prefix)) {}
TempFile::~TempFile() = default;
TempFile::TempFile(TempFile&& other) noexcept : impl_(std::move(other.impl_)) {}
TempFile& TempFile::operator=(TempFile&& other) noexcept { impl_ = std::move(other.impl_); return *this; }
std::string TempFile::path() const { return impl_->path(); }
FileReader TempFile::openForReading() const { return FileReader(impl_->path()); }
FileWriter TempFile::openForWriting(bool append) const { return FileWriter(impl_->path(), append, false); }
bool TempFile::remove() { return impl_->remove(); }

// ================= FileLock =================
// 注意：这里只做 POSIX 版本，如果需要 Windows 可用 CreateFile + LockFileEx
#ifndef _WIN32
class FileLock::Impl {
private:
    std::string filename_;
    int fd_;
    bool locked_;
public:
    explicit Impl(const std::string& filename) : filename_(filename), fd_(-1), locked_(false) {
        fd_ = ::open(filename.c_str(), O_RDWR | O_CREAT, 0644);
        if (fd_ < 0) throw FileException("无法打开文件锁: " + filename);
    }
    ~Impl() { if (locked_) unlock(); if (fd_ >= 0) ::close(fd_); }
    bool lock() { if (locked_) return true; if (::flock(fd_, LOCK_EX) == 0) { locked_ = true; return true; } return false; }
    bool tryLock() { if (locked_) return true; if (::flock(fd_, LOCK_EX | LOCK_NB) == 0) { locked_ = true; return true; } return false; }
    bool unlock() { if (!locked_) return true; if (::flock(fd_, LOCK_UN) == 0) { locked_ = false; return true; } return false; }
    bool isLocked() const { return locked_; }
};
FileLock::FileLock(const std::string& filename) : impl_(std::make_unique<Impl>(filename)) {}
FileLock::~FileLock() = default;
FileLock::FileLock(FileLock&& other) noexcept : impl_(std::move(other.impl_)) {}
FileLock& FileLock::operator=(FileLock&& other) noexcept { impl_ = std::move(other.impl_); return *this; }
bool FileLock::lock() { return impl_->lock(); }
bool FileLock::tryLock() { return impl_->tryLock(); }
bool FileLock::unlock() { return impl_->unlock(); }
bool FileLock::isLocked() const { return impl_->isLocked(); }
#endif
// ============ Utils实现 ============

// 文件系统工具
bool Utils::fileExists(const std::string& path) {
    return fs::exists(path);
}

bool Utils::isFile(const std::string& path) {
    return fs::is_regular_file(path);
}

bool Utils::isDirectory(const std::string& path) {
    return fs::is_directory(path);
}

size_t Utils::fileSize(const std::string& path) {
    return fs::file_size(path);
}

std::string Utils::fileExtension(const std::string& path) {
    return fs::path(path).extension().string();
}

std::string Utils::fileName(const std::string& path) {
    return fs::path(path).filename().string();
}

std::string Utils::directoryName(const std::string& path) {
    return fs::path(path).parent_path().string();
}

bool Utils::createDirectory(const std::string& path) {
    return fs::create_directory(path);
}

bool Utils::createDirectories(const std::string& path) {
    return fs::create_directories(path);
}

bool Utils::copyFile(const std::string& src, const std::string& dst) {
    try {
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        return true;
    } catch (...) {
        return false;
    }
}

bool Utils::moveFile(const std::string& src, const std::string& dst) {
    try {
        fs::rename(src, dst);
        return true;
    } catch (...) {
        return false;
    }
}

bool Utils::deleteFile(const std::string& path) {
    return fs::remove(path);
}

bool Utils::deleteDirectory(const std::string& path) {
    return fs::remove_all(path) > 0;
}

std::string Utils::readTextFile(const std::string& path) {
    try {
        FileReader reader(path);
        return reader.readAll();
    } catch (...) {
        return "";
    }
}

bool Utils::writeTextFile(const std::string& path, const std::string& content) {
    try {
        FileWriter writer(path, false, true);
        return writer.write(content);
    } catch (...) {
        return false;
    }
}

std::vector<uint8_t> Utils::readBinaryFile(const std::string& path) {
    try {
        FileReader reader(path);
        std::string content = reader.readAll();
        
        std::vector<uint8_t> data(content.size());
        std::copy(content.begin(), content.end(), data.begin());
        return data;
    } catch (...) {
        return {};
    }
}

bool Utils::writeBinaryFile(const std::string& path, const std::vector<uint8_t>& data) {
    try {
        FileWriter writer(path, false, true);
        std::string content(data.begin(), data.end());
        return writer.write(content);
    } catch (...) {
        return false;
    }
}

void Utils::walkDirectory(const std::string& path,
                         std::function<void(const std::string&)> fileCallback,
                         std::function<void(const std::string&)> dirCallback) {
    if (!fs::exists(path)) return;
    
    try {
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (entry.is_regular_file() && fileCallback) {
                fileCallback(entry.path().string());
            } else if (entry.is_directory() && dirCallback) {
                dirCallback(entry.path().string());
            }
        }
    } catch (...) {
        // 忽略权限错误等
    }
}

// 字符串工具
std::string Utils::trim(const std::string& str) {
    return trimRight(trimLeft(str));
}

std::string Utils::trimLeft(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r\f\v");
    return (start == std::string::npos) ? "" : str.substr(start);
}

std::string Utils::trimRight(const std::string& str) {
    size_t end = str.find_last_not_of(" \t\n\r\f\v");
    return (end == std::string::npos) ? "" : str.substr(0, end + 1);
}

std::string Utils::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

std::string Utils::toUpper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

bool Utils::startsWith(const std::string& str, const std::string& prefix) {
    return str.size() >= prefix.size() && 
           str.compare(0, prefix.size(), prefix) == 0;
}

bool Utils::endsWith(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() && 
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> Utils::split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string token;
    
    while (std::getline(iss, token, delimiter)) {
        tokens.push_back(token);
    }
    
    return tokens;
}

std::vector<std::string> Utils::split(const std::string& str, 
                                     const std::string& delimiter) {
    std::vector<std::string> tokens;
    size_t start = 0, end = 0;
    
    while ((end = str.find(delimiter, start)) != std::string::npos) {
        tokens.push_back(str.substr(start, end - start));
        start = end + delimiter.length();
    }
    
    tokens.push_back(str.substr(start));
    return tokens;
}

std::string Utils::join(const std::vector<std::string>& strings, 
                       const std::string& delimiter) {
    std::ostringstream oss;
    for (size_t i = 0; i < strings.size(); ++i) {
        if (i > 0) oss << delimiter;
        oss << strings[i];
    }
    return oss.str();
}

// 编码转换（简化实现）
std::string Utils::urlEncode(const std::string& str) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    
    for (char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
        }
    }
    
    return escaped.str();
}

std::string Utils::urlDecode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int value;
            std::istringstream hexStream(str.substr(i + 1, 2));
            hexStream >> std::hex >> value;
            result += static_cast<char>(value);
            i += 2;
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

std::string Utils::base64Encode(const std::string& str) {
    static const std::string base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    
    for (unsigned char c : str) {
        char_array_3[i++] = c;
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + 
                              ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + 
                              ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for (i = 0; i < 4; i++) {
                ret += base64_chars[char_array_4[i]];
            }
            i = 0;
        }
    }
    
    if (i) {
        for (j = i; j < 3; j++) {
            char_array_3[j] = '\0';
        }
        
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + 
                          ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + 
                          ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;
        
        for (j = 0; j < i + 1; j++) {
            ret += base64_chars[char_array_4[j]];
        }
        
        while (i++ < 3) {
            ret += '=';
        }
    }
    
    return ret;
}

std::string Utils::base64Decode(const std::string& str) {
    static const std::string base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    
    std::string ret;
    int i = 0;
    int j = 0;
    int in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    
    for (char c : str) {
        if (c == '=') break;
        
        size_t pos = base64_chars.find(c);
        if (pos == std::string::npos) continue;
        
        char_array_4[i++] = static_cast<unsigned char>(pos);
        if (i == 4) {
            for (i = 0; i < 4; i++) {
                char_array_4[i] = static_cast<unsigned char>(
                    base64_chars.find(char_array_4[i]));
            }
            
            char_array_3[0] = (char_array_4[0] << 2) + 
                              ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + 
                              ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            
            for (i = 0; i < 3; i++) {
                ret += char_array_3[i];
            }
            i = 0;
        }
    }
    
    if (i) {
        for (j = i; j < 4; j++) {
            char_array_4[j] = 0;
        }
        
        for (j = 0; j < 4; j++) {
            char_array_4[j] = static_cast<unsigned char>(
                base64_chars.find(char_array_4[j]));
        }
        
        char_array_3[0] = (char_array_4[0] << 2) + 
                          ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + 
                          ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
        
        for (j = 0; j < i - 1; j++) {
            ret += char_array_3[j];
        }
    }
    
    return ret;
}

// 哈希和摘要（简化实现）
std::string Utils::md5(const std::string& str) {
    return "md5_" + std::to_string(std::hash<std::string>{}(str));
}

std::string Utils::sha1(const std::string& str) {
    return "sha1_" + std::to_string(std::hash<std::string>{}(str));
}

std::string Utils::sha256(const std::string& str) {
    return "sha256_" + std::to_string(std::hash<std::string>{}(str));
}

// 时间工具
std::string Utils::currentTimeString() {
    return currentTimeString("%Y-%m-%d %H:%M:%S");
}

std::string Utils::currentTimeString(const std::string& format) {
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

int64_t Utils::currentTimeMillis() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

int64_t Utils::currentTimeMicros() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
}

// 随机数
int Utils::randomInt(int min, int max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(min, max);
    return distrib(gen);
}

std::string Utils::randomString(size_t length) {
    static const std::string chars = 
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    
    std::string result;
    result.reserve(length);
    
    for (size_t i = 0; i < length; ++i) {
        result.push_back(chars[randomInt(0, chars.size() - 1)]);
    }
    
    return result;
}

std::string Utils::uuid() {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    
    for (int i = 0; i < 16; ++i) {
        oss << std::setw(2) << randomInt(0, 255);
        if (i == 3 || i == 5 || i == 7 || i == 9) {
            oss << '-';
        }
    }
    
    return oss.str();
}

// 系统工具
std::string Utils::getEnv(const std::string& name, 
                         const std::string& defaultValue) {
    const char* value = std::getenv(name.c_str());
    return value ? value : defaultValue;
}

bool Utils::setEnv(const std::string& name, const std::string& value) {
#ifdef _WIN32
    return _putenv_s(name.c_str(), value.c_str()) == 0;
#else
    return setenv(name.c_str(), value.c_str(), 1) == 0;
#endif
}

std::string Utils::getCurrentDirectory() {
    return fs::current_path().string();
}

bool Utils::setCurrentDirectory(const std::string& path) {
    try {
        fs::current_path(path);
        return true;
    } catch (...) {
        return false;
    }
}

std::string Utils::getHomeDirectory() {
    const char* home = std::getenv("HOME");
    if (home) return home;
    
#ifdef _WIN32
    home = std::getenv("USERPROFILE");
    if (home) return home;
#endif
    
    return ".";
}

std::string Utils::getTempDirectory() {
    return fs::temp_directory_path().string();
}

} // namespace foundation