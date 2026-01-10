// modules/fs.hpp - 文件系统模块头文件
#pragma once

#include "fs_interface.hpp"
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <memory>
#include <mutex>

namespace foundation {
namespace modules {

class FileSystemImpl : public IFileSystem {
private:
    // 文件读写器实现类
    class FileReaderImpl {
    private:
        std::ifstream stream_;
        std::string filename_;
        size_t size_ = 0;
        bool isOpen_ = false;
        
    public:
        FileReaderImpl(const std::string& filename);
        ~FileReaderImpl();
        
        bool isOpen() const { return isOpen_; }
        bool eof() const { return stream_.eof(); }
        size_t size() const { return size_; }
        std::string filename() const { return filename_; }
        
        std::string readAll();
        std::vector<std::string> readLines();
        bool readLine(std::string& line);
        std::vector<uint8_t> readBytes(size_t count);
        
        template<typename T>
        FileReaderImpl& operator>>(T& value) {
            stream_ >> value;
            return *this;
        }
    };
    
    class FileWriterImpl {
    private:
        std::ofstream stream_;
        std::string filename_;
        bool isOpen_ = false;
        
    public:
        FileWriterImpl(const std::string& filename, bool append = false, bool createDirs = true);
        ~FileWriterImpl();
        
        bool isOpen() const { return isOpen_; }
        std::string filename() const { return filename_; }
        
        bool write(const std::string& content);
        bool writeLine(const std::string& line);
        bool writeLines(const std::vector<std::string>& lines);
        bool writeBytes(const uint8_t* data, size_t size);
        
        template<typename T>
        FileWriterImpl& operator<<(const T& value) {
            stream_ << value;
            return *this;
        }
        
        void flush();
        void close();
    };
    
    // 临时文件管理器
    class TempFileManager {
    private:
        std::vector<std::string> tempFiles_;
        std::mutex mutex_;
        
    public:
        ~TempFileManager();
        
        std::string createTempFile(const std::string& prefix = "", 
                                  const std::string& suffix = "");
        void registerTempFile(const std::string& path);
        bool removeTempFile(const std::string& path);
        void cleanupAll();
    };
    
    TempFileManager tempManager_;
    
public:
    FileSystemImpl();
    ~FileSystemImpl() override;
    
    // IFileSystem 接口实现
    std::string readText(const std::string& path) override;
    std::vector<std::string> readLines(const std::string& path) override;
    std::vector<uint8_t> readBinary(const std::string& path) override;
    
    bool writeText(const std::string& path, const std::string& content) override;
    bool writeLines(const std::string& path, const std::vector<std::string>& lines) override;
    bool writeBinary(const std::string& path, const std::vector<uint8_t>& data) override;
    bool appendText(const std::string& path, const std::string& content) override;
    
    bool exists(const std::string& path) override;
    bool isFile(const std::string& path) override;
    bool isDirectory(const std::string& path) override;
    size_t size(const std::string& path) override;
    std::string getExtension(const std::string& path) override;
    std::string getFilename(const std::string& path) override;
    std::string getDirectory(const std::string& path) override;
    
    bool createDirectory(const std::string& path) override;
    bool createDirectories(const std::string& path) override;
    std::vector<std::string> listFiles(const std::string& path) override;
    std::vector<std::string> listDirectories(const std::string& path) override;
    
    bool copy(const std::string& src, const std::string& dst) override;
    bool move(const std::string& src, const std::string& dst) override;
    bool remove(const std::string& path) override;
    bool removeDirectory(const std::string& path) override;
    
    std::string createTempFile() override;
    std::string createTempFile(const std::string& content) override;
    
    // 额外功能（不在接口中，但实现提供）
    std::unique_ptr<FileReaderImpl> openForReading(const std::string& filename);
    std::unique_ptr<FileWriterImpl> openForWriting(const std::string& filename, 
                                                  bool append = false, 
                                                  bool createDirs = true);
    
    bool isSymlink(const std::string& path);
    std::string absolutePath(const std::string& path);
    std::string normalizePath(const std::string& path);
    std::time_t lastModified(const std::string& path);
    std::time_t lastAccessed(const std::string& path);
    bool setPermissions(const std::string& path, uint32_t mode);
    uint32_t getPermissions(const std::string& path);
    
private:
    void ensureParentDirectory(const std::filesystem::path& path) const;
    std::filesystem::path resolvePath(const std::string& path) const;
};

} // namespace modules
} // namespace foundation