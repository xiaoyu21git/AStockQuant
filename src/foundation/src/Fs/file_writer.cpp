// fs/file_writer.cpp
#include "foundation/Fs/file_writer.hpp"
#include "../foundation.hpp"

namespace foundation {
namespace fs {

FileWriter::FileWriter(const std::string& filename, bool append, bool createDirs)
    : impl_(std::make_unique<Impl>(filename, append, createDirs)) {
    if (!impl_->isOpen()) {
        throw FileException("无法打开文件: " + filename);
    }
}

FileWriter::~FileWriter() = default;

FileWriter::FileWriter(FileWriter&& other) noexcept = default;
FileWriter& FileWriter::operator=(FileWriter&& other) noexcept = default;

bool FileWriter::write(const std::string& content) {
    impl_->stream() << content;
    return !impl_->stream().fail();
}

bool FileWriter::writeLine(const std::string& line) {
    impl_->stream() << line << '\n';
    return !impl_->stream().fail();
}

bool FileWriter::writeLines(const std::vector<std::string>& lines) {
    for (const auto& line : lines) {
        if (!writeLine(line)) {
            return false;
        }
    }
    return true;
}

void FileWriter::flush() {
    impl_->stream().flush();
}

bool FileWriter::isOpen() const { return impl_->isOpen(); }
std::string FileWriter::filename() const { return impl_->filename(); }

} // namespace fs
} // namespace foundation