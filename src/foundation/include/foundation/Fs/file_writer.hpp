// fs/file_writer.hpp
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <filesystem>

namespace foundation {
namespace fs {

class FileWriter::Impl {
private:
    std::ofstream stream_;
    std::string filename_;
    bool isOpen_ = false;

public:
    Impl(const std::string& filename, bool append, bool createDirs) : filename_(filename) {
        if (createDirs) {
            auto dir = std::filesystem::path(filename).parent_path();
            if (!dir.empty()) {
                std::filesystem::create_directories(dir);
            }
        }
        
        auto mode = std::ios::binary;
        if (append) {
            mode |= std::ios::app;
        } else {
            mode |= std::ios::trunc;
        }
        
        stream_.open(filename, mode);
        isOpen_ = stream_.is_open();
    }

    ~Impl() {
        if (stream_.is_open()) {
            stream_.close();
        }
    }

    std::ofstream& stream() { return stream_; }
    bool isOpen() const { return isOpen_; }
    std::string filename() const { return filename_; }

    template<typename T>
    void operator<<(const T& value) {
        stream_ << value;
    }
};

} // namespace fs
} // namespace foundation