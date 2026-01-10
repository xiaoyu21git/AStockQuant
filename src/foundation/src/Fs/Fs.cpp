// modules/fs.cpp - 文件系统模块实现
#include "foundation/Fs/fs.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include "foundation/core/exception.hpp"
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace foundation {
namespace modules {

namespace fs = std::filesystem;

// ============ FileReaderImpl 实现 ============
FileSystemImpl::FileReaderImpl::FileReaderImpl(const std::string& filename) 
    : filename_(filename) {
    stream_.open(filename, std::ios::binary);
    isOpen_ = stream_.is_open();
    if (isOpen_) {
        stream_.seekg(0, std::ios::end);
        size_ = stream_.tellg();
        stream_.seekg(0, std::ios::beg);
    }
}

FileSystemImpl::FileReaderImpl::~FileReaderImpl() {
    if (stream_.is_open()) {
        stream_.close();
    }
}

std::string FileSystemImpl::FileReaderImpl::readAll() {
    std::ostringstream oss;
    oss << stream_.rdbuf();
    return oss.str();
}

std::vector<std::string> FileSystemImpl::FileReaderImpl::readLines() {
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(stream_, line)) {
        lines.push_back(line);
    }
    return lines;
}

bool FileSystemImpl::FileReaderImpl::readLine(std::string& line) {
    return bool(std::getline(stream_, line));
}

std::vector<uint8_t> FileSystemImpl::FileReaderImpl::readBytes(size_t count) {
    std::vector<uint8_t> buffer(count);
    stream_.read(reinterpret_cast<char*>(buffer.data()), count);
    buffer.resize(stream_.gcount());
    return buffer;
}

// ============ FileWriterImpl 实现 ============
FileSystemImpl::FileWriterImpl::FileWriterImpl(const std::string& filename, 
                                              bool append, bool createDirs) 
    : filename_(filename) {
    if (createDirs) {
        auto dir = fs::path(filename).parent_path();
        if (!dir.empty()) {
            fs::create_directories(dir);
        }
    }
    
    // auto mode = std::ios::binary;
    // if (append) {
    //     mode |= std::ios::app;
    // } else {
    //     mode |= std::ios::trunc;
    // }
    
    std::ios::openmode mode = std::ios::binary;
    if (append) {
        mode = mode | std::ios::app;
    } else {
        mode = mode | std::ios::trunc;
    }
    stream_.open(filename, mode);
    isOpen_ = stream_.is_open();
}

FileSystemImpl::FileWriterImpl::~FileWriterImpl() {
    if (stream_.is_open()) {
        stream_.close();
    }
}

bool FileSystemImpl::FileWriterImpl::write(const std::string& content) {
    stream_ << content;
    return !stream_.fail();
}

bool FileSystemImpl::FileWriterImpl::writeLine(const std::string& line) {
    stream_ << line << '\n';
    return !stream_.fail();
}

bool FileSystemImpl::FileWriterImpl::writeLines(const std::vector<std::string>& lines) {
    for (const auto& line : lines) {
        if (!writeLine(line)) {
            return false;
        }
    }
    return true;
}

bool FileSystemImpl::FileWriterImpl::writeBytes(const uint8_t* data, size_t size) {
    stream_.write(reinterpret_cast<const char*>(data), size);
    return !stream_.fail();
}

void FileSystemImpl::FileWriterImpl::flush() {
    stream_.flush();
}

void FileSystemImpl::FileWriterImpl::close() {
    if (stream_.is_open()) {
        stream_.close();
        isOpen_ = false;
    }
}

// ============ TempFileManager 实现 ============
FileSystemImpl::TempFileManager::~TempFileManager() {
    cleanupAll();
}

std::string FileSystemImpl::TempFileManager::createTempFile(const std::string& prefix, 
                                                           const std::string& suffix) {
    std::string pattern = prefix + "XXXXXX" + suffix;
    std::string tempPath;
    
#ifdef _WIN32
    char tmpPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpPath);
    
    char tmpFile[MAX_PATH];
    if (GetTempFileNameA(tmpPath, "tmp", 0, tmpFile)) {
        tempPath = tmpFile;
        if (!suffix.empty()) {
            std::string newPath = tempPath + suffix;
            std::rename(tempPath.c_str(), newPath.c_str());
            tempPath = newPath;
        }
    }
#else
    char tmpFile[] = "/tmp/XXXXXX";
    std::string fullPattern = "/tmp/" + pattern;
    
    // 复制模式到可修改的缓冲区
    std::vector<char> buffer(fullPattern.begin(), fullPattern.end());
    buffer.push_back('\0');
    
    int fd = mkstemps(buffer.data(), suffix.size());
    if (fd != -1) {
        close(fd);
        tempPath = buffer.data();
    }
#endif
    
    if (!tempPath.empty()) {
        registerTempFile(tempPath);
    }
    
    return tempPath;
}

void FileSystemImpl::TempFileManager::registerTempFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    tempFiles_.push_back(path);
}

bool FileSystemImpl::TempFileManager::removeTempFile(const std::string& path) {
    bool removed = fs::remove(path);
    if (removed) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find(tempFiles_.begin(), tempFiles_.end(), path);
        if (it != tempFiles_.end()) {
            tempFiles_.erase(it);
        }
    }
    return removed;
}

void FileSystemImpl::TempFileManager::cleanupAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& file : tempFiles_) {
        std::remove(file.c_str());
    }
    tempFiles_.clear();
}

// ============ FileSystemImpl 主类实现 ============
FileSystemImpl::FileSystemImpl() = default;

FileSystemImpl::~FileSystemImpl() {
    tempManager_.cleanupAll();
}

std::string FileSystemImpl::readText(const std::string& path) {
    FileReaderImpl reader(path);
    if (!reader.isOpen()) {
        throw FileException("无法打开文件: " + path);
    }
    return reader.readAll();
}

std::vector<std::string> FileSystemImpl::readLines(const std::string& path) {
    FileReaderImpl reader(path);
    if (!reader.isOpen()) {
        throw FileException("无法打开文件: " + path);
    }
    return reader.readLines();
}

std::vector<uint8_t> FileSystemImpl::readBinary(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw FileException("无法打开文件: " + path);
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        throw FileException("读取文件失败: " + path);
    }
    
    return buffer;
}

bool FileSystemImpl::writeText(const std::string& path, const std::string& content) {
    FileWriterImpl writer(path, false, true);
    if (!writer.isOpen()) {
        return false;
    }
    return writer.write(content);
}

bool FileSystemImpl::writeLines(const std::string& path, const std::vector<std::string>& lines) {
    FileWriterImpl writer(path, false, true);
    if (!writer.isOpen()) {
        return false;
    }
    return writer.writeLines(lines);
}

bool FileSystemImpl::writeBinary(const std::string& path, const std::vector<uint8_t>& data) {
    FileWriterImpl writer(path, false, true);
    if (!writer.isOpen()) {
        return false;
    }
    return writer.writeBytes(data.data(), data.size());
}

bool FileSystemImpl::appendText(const std::string& path, const std::string& content) {
    FileWriterImpl writer(path, true, true);
    if (!writer.isOpen()) {
        return false;
    }
    return writer.write(content);
}

bool FileSystemImpl::exists(const std::string& path) {
    return fs::exists(path);
}

bool FileSystemImpl::isFile(const std::string& path) {
    return fs::is_regular_file(path);
}

bool FileSystemImpl::isDirectory(const std::string& path) {
    return fs::is_directory(path);
}

size_t FileSystemImpl::size(const std::string& path) {
    return fs::file_size(path);
}

std::string FileSystemImpl::getExtension(const std::string& path) {
    return fs::path(path).extension().string();
}

std::string FileSystemImpl::getFilename(const std::string& path) {
    return fs::path(path).filename().string();
}

std::string FileSystemImpl::getDirectory(const std::string& path) {
    return fs::path(path).parent_path().string();
}

bool FileSystemImpl::createDirectory(const std::string& path) {
    return fs::create_directory(path);
}

bool FileSystemImpl::createDirectories(const std::string& path) {
    return fs::create_directories(path);
}

std::vector<std::string> FileSystemImpl::listFiles(const std::string& path) {
    std::vector<std::string> files;
    
    if (!fs::exists(path) || !fs::is_directory(path)) {
        return files;
    }
    
    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path().string());
        }
    }
    
    return files;
}

std::vector<std::string> FileSystemImpl::listDirectories(const std::string& path) {
    std::vector<std::string> dirs;
    
    if (!fs::exists(path) || !fs::is_directory(path)) {
        return dirs;
    }
    
    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.is_directory()) {
            dirs.push_back(entry.path().string());
        }
    }
    
    return dirs;
}

bool FileSystemImpl::copy(const std::string& src, const std::string& dst) {
    try {
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        return true;
    } catch (...) {
        return false;
    }
}

bool FileSystemImpl::move(const std::string& src, const std::string& dst) {
    try {
        fs::rename(src, dst);
        return true;
    } catch (...) {
        return false;
    }
}

bool FileSystemImpl::remove(const std::string& path) {
    return fs::remove(path);
}

bool FileSystemImpl::removeDirectory(const std::string& path) {
    return fs::remove_all(path) > 0;
}

std::string FileSystemImpl::createTempFile() {
    return tempManager_.createTempFile();
}

std::string FileSystemImpl::createTempFile(const std::string& content) {
    std::string tempPath = createTempFile();
    if (!tempPath.empty()) {
        writeText(tempPath, content);
    }
    return tempPath;
}

// ============ 额外功能实现 ============
std::unique_ptr<FileSystemImpl::FileReaderImpl> 
FileSystemImpl::openForReading(const std::string& filename) {
    auto reader = std::make_unique<FileReaderImpl>(filename);
    if (!reader->isOpen()) {
        return nullptr;
    }
    return reader;
}

std::unique_ptr<FileSystemImpl::FileWriterImpl> 
FileSystemImpl::openForWriting(const std::string& filename, bool append, bool createDirs) {
    auto writer = std::make_unique<FileWriterImpl>(filename, append, createDirs);
    if (!writer->isOpen()) {
        return nullptr;
    }
    return writer;
}

bool FileSystemImpl::isSymlink(const std::string& path) {
    return fs::is_symlink(path);
}

std::string FileSystemImpl::absolutePath(const std::string& path) {
    return fs::absolute(path).string();
}

std::string FileSystemImpl::normalizePath(const std::string& path) {
    return fs::canonical(fs::absolute(path)).string();
}

std::time_t FileSystemImpl::lastModified(const std::string& path) {
        auto ftime = std::filesystem::last_write_time(path);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    return std::chrono::system_clock::to_time_t(sctp);
}

std::time_t FileSystemImpl::lastAccessed(const std::string& path) {
    // C++17 filesystem 没有直接提供最后访问时间
    // 使用平台特定方法
#ifdef _WIN32
    struct _stat64 stat_buf;
    if (_stat64(path.c_str(), &stat_buf) == 0) {
        return stat_buf.st_atime;
    }
#else
    struct stat stat_buf;
    if (stat(path.c_str(), &stat_buf) == 0) {
        return stat_buf.st_atime;
    }
#endif
    return 0;
}

bool FileSystemImpl::setPermissions(const std::string& path, uint32_t mode) {
    try {
        fs::permissions(path, static_cast<fs::perms>(mode));
        return true;
    } catch (...) {
        return false;
    }
}

uint32_t FileSystemImpl::getPermissions(const std::string& path) {
    try {
        return static_cast<uint32_t>(fs::status(path).permissions());
    } catch (...) {
        return 0;
    }
}

// ============ 私有方法实现 ============
void FileSystemImpl::ensureParentDirectory(const std::filesystem::path& path) const {
    auto parent = path.parent_path();
    if (!parent.empty() && !fs::exists(parent)) {
        fs::create_directories(parent);
    }
}

std::filesystem::path FileSystemImpl::resolvePath(const std::string& path) const {
    return fs::absolute(fs::path(path));
}

} // namespace modules
} // namespace foundation