// fs/file_reader.hpp
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <fstream>

namespace foundation {
namespace fs {

class FileReader::Impl {
private:
    std::ifstream stream_;
    std::string filename_;
    size_t size_ = 0;
    bool isOpen_ = false;

public:
    Impl(const std::string& filename) : filename_(filename) {
        stream_.open(filename, std::ios::binary);
        isOpen_ = stream_.is_open();
        if (isOpen_) {
            stream_.seekg(0, std::ios::end);
            size_ = stream_.tellg();
            stream_.seekg(0, std::ios::beg);
        }
    }

    ~Impl() {
        if (stream_.is_open()) {
            stream_.close();
        }
    }

    std::ifstream& stream() { return stream_; }
    bool isOpen() const { return isOpen_; }
    bool eof() const { return stream_.eof(); }
    size_t size() const { return size_; }
    std::string filename() const { return filename_; }

    template<typename T>
    void operator>>(T& value) {
        stream_ >> value;
    }
};

} // namespace fs
} // namespace foundation