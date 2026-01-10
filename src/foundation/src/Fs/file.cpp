// fs/file.cpp
#include "foundation/Fs/file.hpp"
#include "../foundation.hpp"
#include <fstream>
#include <filesystem>

namespace foundation {
namespace fs {

namespace fsys = std::filesystem;

std::string File::readText(const std::string& path) {
    FileReader reader(path);
    return reader.readAll();
}

std::vector<std::string> File::readLines(const std::string& path) {
    FileReader reader(path);
    return reader.readLines();
}

std::vector<uint8_t> File::readBinary(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw FileException("无法打开文件: " + path);
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(size);
    if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return buffer;
    }
    
    throw FileException("读取文件失败: " + path);
}

bool File::writeText(const std::string& path, const std::string& content) {
    FileWriter writer(path, false, true);
    return writer.write(content);
}

bool File::writeLines(const std::string& path, const std::vector<std::string>& lines) {
    FileWriter writer(path, false, true);
    return writer.writeLines(lines);
}

bool File::writeBinary(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    return !file.fail();
}

bool File::appendText(const std::string& path, const std::string& content) {
    FileWriter writer(path, true, true);
    return writer.write(content);
}

bool File::exists(const std::string& path) {
    return fsys::exists(path);
}

bool File::isFile(const std::string& path) {
    return fsys::is_regular_file(path);
}

bool File::isDirectory(const std::string& path) {
    return fsys::is_directory(path);
}

size_t File::size(const std::string& path) {
    return fsys::file_size(path);
}

std::string File::extension(const std::string& path) {
    return fsys::path(path).extension().string();
}

std::string File::filename(const std::string& path) {
    return fsys::path(path).filename().string();
}

std::string File::directory(const std::string& path) {
    return fsys::path(path).parent_path().string();
}

bool File::createDirectory(const std::string& path) {
    return fsys::create_directory(path);
}

bool File::createDirectories(const std::string& path) {
    return fsys::create_directories(path);
}

std::vector<std::string> File::listFiles(const std::string& path) {
    std::vector<std::string> files;
    
    for (const auto& entry : fsys::directory_iterator(path)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path().string());
        }
    }
    
    return files;
}

std::vector<std::string> File::listDirectories(const std::string& path) {
    std::vector<std::string> dirs;
    
    for (const auto& entry : fsys::directory_iterator(path)) {
        if (entry.is_directory()) {
            dirs.push_back(entry.path().string());
        }
    }
    
    return dirs;
}

bool File::copy(const std::string& src, const std::string& dst) {
    try {
        fsys::copy_file(src, dst, fsys::copy_options::overwrite_existing);
        return true;
    } catch (...) {
        return false;
    }
}

bool File::move(const std::string& src, const std::string& dst) {
    try {
        fsys::rename(src, dst);
        return true;
    } catch (...) {
        return false;
    }
}

bool File::remove(const std::string& path) {
    return fsys::remove(path);
}

bool File::removeDirectory(const std::string& path) {
    return fsys::remove_all(path) > 0;
}

std::string File::createTempFile() {
    char tmpFile[] = "/tmp/XXXXXX";
#ifdef _WIN32
    char tmpPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpPath);
    GetTempFileNameA(tmpPath, "tmp", 0, tmpFile);
    return tmpFile;
#else
    int fd = mkstemp(tmpFile);
    if (fd != -1) {
        close(fd);
        return tmpFile;
    }
    throw RuntimeException("无法创建临时文件");
#endif
}

std::string File::createTempFile(const std::string& content) {
    std::string path = createTempFile();
    writeText(path, content);
    return path;
}

} // namespace fs
} // namespace foundation