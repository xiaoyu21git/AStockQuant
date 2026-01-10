// fs/file.cpp
#include "foundation/Fs/file.hpp"
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <memory>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <cerrno>
#endif

namespace foundation {
namespace fs {

// ============ FileReader::Impl ============
class FileReader::Impl {
private:
    std::ifstream stream_;
    std::string filename_;
    size_t fileSize_;
    bool isOpen_;

public:
    explicit Impl(const std::string& filename)
        : filename_(filename), fileSize_(0), isOpen_(false) {
        stream_.open(filename, std::ios::in | std::ios::binary);
        if (!stream_.is_open()) {
            return;
        }
        
        isOpen_ = true;
        stream_.seekg(0, std::ios::end);
        fileSize_ = static_cast<size_t>(stream_.tellg());
        stream_.seekg(0, std::ios::beg);
    }
    
    ~Impl() {
        if (stream_.is_open()) {
            stream_.close();
        }
    }
    
    std::string readAll() {
        if (!isOpen_ || fileSize_ == 0) {
            return "";
        }
        
        std::string content;
        content.resize(fileSize_);
        stream_.read(&content[0], fileSize_);
        
        stream_.clear();
        stream_.seekg(0, std::ios::beg);
        return content;
    }
    
    std::vector<std::string> readLines() {
        if (!isOpen_) {
            return {};
        }
        
        std::vector<std::string> lines;
        std::string line;
        
        stream_.clear();
        stream_.seekg(0, std::ios::beg);
        
        while (std::getline(stream_, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(line);
        }
        
        stream_.clear();
        stream_.seekg(0, std::ios::beg);
        return lines;
    }
    
    bool readLine(std::string& line) {
        if (!isOpen_ || stream_.eof()) {
            return false;
        }
        
        if (std::getline(stream_, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return true;
        }
        return false;
    }
    
    bool isOpen() const { return isOpen_; }
    bool eof() const { return !isOpen_ || stream_.eof(); }
    size_t size() const { return fileSize_; }
    std::string filename() const { return filename_; }
    
    template<typename T>
    bool readValue(T& value) {
        if (!isOpen_ || stream_.eof()) {
            return false;
        }
        
        stream_ >> value;
        return !stream_.fail();
    }
};

// ============ FileWriter::Impl ============
class FileWriter::Impl {
private:
    std::ofstream stream_;
    std::string filename_;
    bool isOpen_;

public:
    explicit Impl(const std::string& filename, bool append = false, bool createDirs = true)
        : filename_(filename), isOpen_(false) {
        
        if (createDirs) {
            std::string dir = File::directory(filename);
            if (!dir.empty() && !File::exists(dir)) {
                File::createDirectories(dir);
            }
        }
        
        std::ios::openmode mode = std::ios::out;
        if (append) {
            mode |= std::ios::app;
        }
         // 添加二进制模式，禁止换行符转换
        mode |= std::ios::binary;
        stream_.open(filename, mode);
        isOpen_ = stream_.is_open();
    }
    
    ~Impl() {
        if (stream_.is_open()) {
            stream_.close();
        }
    }
    
    bool write(const std::string& content) {
        if (!isOpen_) return false;
        stream_ << content;
        return !stream_.fail();
    }
    
    bool writeLine(const std::string& line) {
        if (!isOpen_) return false;
        stream_ << line << '\n';
        return !stream_.fail();
    }
    
    bool writeLines(const std::vector<std::string>& lines) {
        if (!isOpen_) return false;
        for (const auto& line : lines) {
            if (!writeLine(line)) {
                return false;
            }
        }
        return true;
    }
    
    void flush() {
        if (isOpen_) {
            stream_.flush();
        }
    }
    
    bool isOpen() const { return isOpen_; }
    std::string filename() const { return filename_; }
    
    template<typename T>
    Impl& operator<<(const T& value) {
        if (isOpen_) {
            stream_ << value;
        }
        return *this;
    }
};

// ============ FileReader 公共接口 ============
FileReader::FileReader(const std::string& filename)
    : impl_(std::make_unique<Impl>(filename)) {}

FileReader::~FileReader() = default;

FileReader::FileReader(FileReader&&) noexcept = default;
FileReader& FileReader::operator=(FileReader&&) noexcept = default;

std::string FileReader::readAll() {
    return impl_->readAll();
}

std::vector<std::string> FileReader::readLines() {
    return impl_->readLines();
}

bool FileReader::readLine(std::string& line) {
    return impl_->readLine(line);
}

bool FileReader::isOpen() const {
    return impl_->isOpen();
}

bool FileReader::eof() const {
    return impl_->eof();
}

size_t FileReader::size() const {
    return impl_->size();
}

std::string FileReader::filename() const {
    return impl_->filename();
}

// 模板方法的显式实例化
template<>
FileReader& FileReader::operator>> <std::string>(std::string& value) {
    impl_->readValue(value);
    return *this;
}

template<>
FileReader& FileReader::operator>> <int>(int& value) {
    impl_->readValue(value);
    return *this;
}

template<>
FileReader& FileReader::operator>> <double>(double& value) {
    impl_->readValue(value);
    return *this;
}

// ============ FileWriter 公共接口 ============
template<>
FileWriter& FileWriter::operator<< <char[7]>(const char (&value)[7]) {
    if (impl_) {
        *impl_ << value;
    }
    return *this;
}

// 对于 "\n" (2个字符)
template<>
FileWriter& FileWriter::operator<< <char[2]>(const char (&value)[2]) {
    if (impl_) {
        *impl_ << value;
    }
    return *this;
}
FileWriter::FileWriter(const std::string& filename, bool append, bool createDirs)
    : impl_(std::make_unique<Impl>(filename, append, createDirs)) {
        
    }

FileWriter::~FileWriter() = default;

FileWriter::FileWriter(FileWriter&&) noexcept = default;
FileWriter& FileWriter::operator=(FileWriter&&) noexcept = default;

bool FileWriter::write(const std::string& content) {
    return impl_->write(content);
}

bool FileWriter::writeLine(const std::string& line) {
    return impl_->writeLine(line);
}

bool FileWriter::writeLines(const std::vector<std::string>& lines) {
    return impl_->writeLines(lines);
}

void FileWriter::flush() {
    impl_->flush();
}

bool FileWriter::isOpen() const {
    return impl_->isOpen();
}

std::string FileWriter::filename() const {
    return impl_->filename();
}

// 模板方法的显式实例化
template<>
FileWriter& FileWriter::operator<< <std::string>(const std::string& value) {
    *impl_ << value;
    return *this;
}

template<>
FileWriter& FileWriter::operator<< <const char*>(const char* const& value) {
    *impl_ << value;
    return *this;
}

template<>
FileWriter& FileWriter::operator<< <char>(const char& value) {
    *impl_ << value;
    return *this;
}

template<>
FileWriter& FileWriter::operator<< <int>(const int& value) {
    *impl_ << value;
    return *this;
}

template<>
FileWriter& FileWriter::operator<< <double>(const double& value) {
    *impl_ << value;
    return *this;
}

template<>
FileWriter& FileWriter::operator<< <float>(const float& value) {
    *impl_ << value;
    return *this;
}

// ============ File 静态方法 ============

// 文件读取
std::string File::readText(const std::string& path) {
    FileReader reader(path);
    if (!reader.isOpen()) {
        return "";
    }
    return reader.readAll();
}

std::vector<std::string> File::readLines(const std::string& path) {
    FileReader reader(path);
    if (!reader.isOpen()) {
        return {};
    }
    return reader.readLines();
}

std::vector<uint8_t> File::readBinary(const std::string& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(size);
    if (size > 0) {
        file.read(reinterpret_cast<char*>(buffer.data()), size);
    }
    
    return buffer;
}

// 文件写入
bool File::writeText(const std::string& path, const std::string& content) {
    FileWriter writer(path, false, true);
    return writer.write(content);
}

bool File::writeLines(const std::string& path, const std::vector<std::string>& lines) {
    FileWriter writer(path, false, true);
    return writer.writeLines(lines);
}

bool File::writeBinary(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream file(path, std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    if (!data.empty()) {
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
    
    return !file.fail();
}

bool File::appendText(const std::string& path, const std::string& content) {
    FileWriter writer(path, true, true);
    return writer.write(content);
}

// 文件信息
bool File::exists(const std::string& path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES);
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0;
#endif
}

bool File::isFile(const std::string& path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return !(attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode);
#endif
}

bool File::isDirectory(const std::string& path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
#endif
}

size_t File::size(const std::string& path) {
    if (!isFile(path)) {
        return 0;
    }
    
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fileInfo)) {
        return 0;
    }
    
    ULARGE_INTEGER size;
    size.LowPart = fileInfo.nFileSizeLow;
    size.HighPart = fileInfo.nFileSizeHigh;
    return static_cast<size_t>(size.QuadPart);
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return 0;
    }
    return static_cast<size_t>(st.st_size);
#endif
}

std::string File::extension(const std::string& path) {
    std::string name = filename(path);
    size_t pos = name.find_last_of('.');
    
    if (pos == std::string::npos || pos == 0) {
        return "";
    }
    
    // 返回带点的扩展名
    return name.substr(pos);  // 包含点，如 ".txt
}

std::string File::filename(const std::string& path) {
#ifdef _WIN32
    char drive[_MAX_DRIVE];
    char dir[_MAX_DIR];
    char fname[_MAX_FNAME];
    char ext[_MAX_EXT];
    
    _splitpath_s(path.c_str(), drive, dir, fname, ext);
    
    std::string result(fname);
    if (ext[0] != '\0') {
        result += ext;
    }
    return result;
#else
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
#endif
}

std::string File::directory(const std::string& path) {
size_t last_slash = path.find_last_of("/\\");
    if (last_slash == std::string::npos) {
        return "";
    }
    
    std::string dir = path.substr(0, last_slash);
    
    // 对于 Windows 的根目录（如 C:\），保留反斜杠
    if (dir.size() == 2 && dir[1] == ':') {
        return dir + "\\";
    }
    
    return dir;
}

// 目录操作
bool File::createDirectory(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    
#ifdef _WIN32
    return CreateDirectoryA(path.c_str(), nullptr) != 0;
#else
    return mkdir(path.c_str(), 0755) == 0;
#endif
}

bool File::createDirectories(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    
    if (isDirectory(path)) {
        return true;
    }
    
    std::string parent = directory(path);
    if (!parent.empty() && !exists(parent)) {
        if (!createDirectories(parent)) {
            return false;
        }
    }
    
    return createDirectory(path);
}

std::vector<std::string> File::listFiles(const std::string& path) {
    std::vector<std::string> result;
    
    if (!isDirectory(path)) {
        return result;
    }
    
#ifdef _WIN32
    std::string pattern = path + "\\*";
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &findData);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        return result;
    }
    
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            result.push_back(findData.cFileName);
        }
    } while (FindNextFileA(hFind, &findData) != 0);
    
    FindClose(hFind);
#else
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return result;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_REG) {
            result.push_back(entry->d_name);
        }
    }
    
    closedir(dir);
#endif
    
    return result;
}

std::vector<std::string> File::listDirectories(const std::string& path) {
    std::vector<std::string> result;
    
    if (!isDirectory(path)) {
        return result;
    }
    
#ifdef _WIN32
    std::string pattern = path + "\\*";
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &findData);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        return result;
    }
    
    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::string name = findData.cFileName;
            if (name != "." && name != "..") {
                result.push_back(name);
            }
        }
    } while (FindNextFileA(hFind, &findData) != 0);
    
    FindClose(hFind);
#else
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return result;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_DIR) {
            std::string name = entry->d_name;
            if (name != "." && name != "..") {
                result.push_back(name);
            }
        }
    }
    
    closedir(dir);
#endif
    
    return result;
}

// 文件操作
bool File::copy(const std::string& src, const std::string& dst) {
    if (!isFile(src)) {
        return false;
    }
    
    std::ifstream source(src, std::ios::binary);
    if (!source.is_open()) {
        return false;
    }
    
    createDirectories(directory(dst));
    
    std::ofstream destination(dst, std::ios::binary);
    if (!destination.is_open()) {
        return false;
    }
    
    destination << source.rdbuf();
    return !destination.fail();
}

bool File::move(const std::string& src, const std::string& dst) {
    if (!exists(src)) {
       // std::cerr << "Move failed: source does not exist: " << src << std::endl;
        return false;
    }
    
    // 确保目标目录存在
    createDirectories(directory(dst));
    
#ifdef _WIN32
    // 方法1: 使用 MoveFileEx 支持跨卷移动和覆盖
    if (MoveFileExA(src.c_str(), dst.c_str(), 
                   MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
        return true;
    }
    
    DWORD error = GetLastError();
    // std::cerr << "MoveFileEx failed (error " << error << "): " 
    //           << src << " -> " << dst << std::endl;
    
    // 方法2: 如果目标已存在，先删除
    if (error == ERROR_ALREADY_EXISTS) {
        if (DeleteFileA(dst.c_str())) {
            return MoveFileA(src.c_str(), dst.c_str()) != 0;
        }
    }
    
    // 方法3: 复制+删除作为最后手段
    if (copy(src, dst)) {
        return remove(src);
    }
    
    return false;
#else
    // Unix/Linux/Mac 版本
    if (rename(src.c_str(), dst.c_str()) == 0) {
        return true;
    }
    
    // 处理跨设备移动（errno == EXDEV）
    if (errno == EXDEV) {
        std::cerr << "Cross-device move, using copy+delete: " 
                  << src << " -> " << dst << std::endl;
        if (copy(src, dst)) {
            return remove(src);
        }
    } else {
        std::cerr << "rename failed (errno " << errno << "): " 
                  << strerror(errno) << std::endl;
    }
    
    return false;
#endif
}

bool File::remove(const std::string& path) {
    if (!exists(path)) {
        return true;
    }
    
#ifdef _WIN32
    if (isDirectory(path)) {
        return RemoveDirectoryA(path.c_str()) != 0;
    } else {
        return DeleteFileA(path.c_str()) != 0;
    }
#else
    if (isDirectory(path)) {
        return rmdir(path.c_str()) == 0;
    } else {
        return ::remove(path.c_str()) == 0;
    }
#endif
}

bool File::removeDirectory(const std::string& path) {
    if (!isDirectory(path)) {
        return false;
    }
    
    auto files = listFiles(path);
    auto dirs = listDirectories(path);
    
    for (const auto& file : files) {
        std::string fullPath = path + 
#ifdef _WIN32
            "\\" + file;
#else
            "/" + file;
#endif
        if (!remove(fullPath)) {
            return false;
        }
    }
    
    for (const auto& dir : dirs) {
        std::string fullPath = path + 
#ifdef _WIN32
            "\\" + dir;
#else
            "/" + dir;
#endif
        if (!removeDirectory(fullPath)) {
            return false;
        }
    }
    
    return remove(path);
}

// 临时文件
std::string File::createTempFile() {
    return createTempFile("");
}

std::string File::createTempFile(const std::string& content) {
#ifdef _WIN32
    char tmpPath[MAX_PATH] = {0};
    char tmpFile[MAX_PATH] = {0};
    
    if (GetTempPathA(MAX_PATH, tmpPath) == 0) {
        return "";
    }
    
    if (GetTempFileNameA(tmpPath, "tmp", 0, tmpFile) == 0) {
        return "";
    }
    
    std::string result(tmpFile);
    
    if (!content.empty()) {
        writeText(result, content);
    }
    
    return result;
#else
    char tmpFile[] = "/tmp/tmp_XXXXXX";
    int fd = mkstemp(tmpFile);
    if (fd == -1) {
        return "";
    }
    
    close(fd);
    std::string result(tmpFile);
    
    if (!content.empty()) {
        writeText(result, content);
    }
    
    return result;
#endif
}

} // namespace fs
} // namespace foundation