// fs/file_reader.cpp
#include "foundation/Fs/file_reader.hpp"
#include "../foundation.hpp"

namespace foundation {
namespace fs {

FileReader::FileReader(const std::string& filename) : impl_(std::make_unique<Impl>(filename)) {
    if (!impl_->isOpen()) {
        throw FileException("无法打开文件: " + filename);
    }
}

FileReader::~FileReader() = default;

FileReader::FileReader(FileReader&& other) noexcept = default;
FileReader& FileReader::operator=(FileReader&& other) noexcept = default;

std::string FileReader::readAll() {
    std::ostringstream oss;
    oss << impl_->stream().rdbuf();
    return oss.str();
}

std::vector<std::string> FileReader::readLines() {
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(impl_->stream(), line)) {
        lines.push_back(line);
    }
    return lines;
}

bool FileReader::readLine(std::string& line) {
    return bool(std::getline(impl_->stream(), line));
}

bool FileReader::isOpen() const { return impl_->isOpen(); }
bool FileReader::eof() const { return impl_->eof(); }
size_t FileReader::size() const { return impl_->size(); }
std::string FileReader::filename() const { return impl_->filename(); }

} // namespace fs
} // namespace foundation