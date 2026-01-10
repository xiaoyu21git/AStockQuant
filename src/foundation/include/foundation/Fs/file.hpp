// fs/file.hpp - 文件系统模块头文件
#pragma once

#include <string>
#include <vector>
#include <memory>

namespace foundation {
namespace fs {

// ============ FileReader ============
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
    FileReader& operator>>(T& value);
};

// ============ FileWriter ============
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
    FileWriter& operator<<(const T& value);

    void flush();
    bool isOpen() const;
    std::string filename() const;
};

// ============ 文件工具函数 ============
class File {
public:
    // 读取
    static std::string readText(const std::string& path);
    static std::vector<std::string> readLines(const std::string& path);
    static std::vector<uint8_t> readBinary(const std::string& path);
    
    // 写入
    static bool writeText(const std::string& path, const std::string& content);
    static bool writeLines(const std::string& path, const std::vector<std::string>& lines);
    static bool writeBinary(const std::string& path, const std::vector<uint8_t>& data);
    static bool appendText(const std::string& path, const std::string& content);
    
    // 文件信息
    static bool exists(const std::string& path);
    static bool isFile(const std::string& path);
    static bool isDirectory(const std::string& path);
    static size_t size(const std::string& path);
    static std::string extension(const std::string& path);
    static std::string filename(const std::string& path);
    static std::string directory(const std::string& path);
    
    // 目录操作
    static bool createDirectory(const std::string& path);
    static bool createDirectories(const std::string& path);
    static std::vector<std::string> listFiles(const std::string& path);
    static std::vector<std::string> listDirectories(const std::string& path);
    
    // 文件操作
    static bool copy(const std::string& src, const std::string& dst);
    static bool move(const std::string& src, const std::string& dst);
    static bool remove(const std::string& path);
    static bool removeDirectory(const std::string& path);
    
    // 临时文件
    static std::string createTempFile();
    static std::string createTempFile(const std::string& content);
};

} // namespace fs
} // namespace foundation