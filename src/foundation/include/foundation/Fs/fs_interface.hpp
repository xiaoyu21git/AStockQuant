// modules/fs_interface.hpp - 文件系统模块独立接口
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace foundation {

// 前向声明异常类
class Exception;
class FileException;

// ============ 文件系统接口（独立，不依赖完整Foundation）============
class IFileSystem {
public:
    virtual ~IFileSystem() = default;
    
    // 读取操作
    virtual std::string readText(const std::string& path) = 0;
    virtual std::vector<std::string> readLines(const std::string& path) = 0;
    virtual std::vector<uint8_t> readBinary(const std::string& path) = 0;
    
    // 写入操作
    virtual bool writeText(const std::string& path, const std::string& content) = 0;
    virtual bool writeLines(const std::string& path, const std::vector<std::string>& lines) = 0;
    virtual bool writeBinary(const std::string& path, const std::vector<uint8_t>& data) = 0;
    virtual bool appendText(const std::string& path, const std::string& content) = 0;
    
    // 文件信息
    virtual bool exists(const std::string& path) = 0;
    virtual bool isFile(const std::string& path) = 0;
    virtual bool isDirectory(const std::string& path) = 0;
    virtual size_t size(const std::string& path) = 0;
    virtual std::string getExtension(const std::string& path) = 0;
    virtual std::string getFilename(const std::string& path) = 0;
    virtual std::string getDirectory(const std::string& path) = 0;
    
    // 目录操作
    virtual bool createDirectory(const std::string& path) = 0;
    virtual bool createDirectories(const std::string& path) = 0;
    virtual std::vector<std::string> listFiles(const std::string& path) = 0;
    virtual std::vector<std::string> listDirectories(const std::string& path) = 0;
    
    // 文件操作
    virtual bool copy(const std::string& src, const std::string& dst) = 0;
    virtual bool move(const std::string& src, const std::string& dst) = 0;
    virtual bool remove(const std::string& path) = 0;
    virtual bool removeDirectory(const std::string& path) = 0;
    
    // 临时文件
    virtual std::string createTempFile() = 0;
    virtual std::string createTempFile(const std::string& content) = 0;
};

} // namespace foundation