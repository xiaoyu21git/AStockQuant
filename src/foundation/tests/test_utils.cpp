#include "test_base.hpp"
#include "foundation/utils/string_utils.hpp"
#include "foundation/Fs/file.hpp"
//#include "foundation/utils/config_parser.hpp"
#include <chrono>

namespace foundation::test {

// // ==================== 字符串工具测试 ====================
// class StringUtilsTest : public TestBase {};

// TEST_F(StringUtilsTest, SplitString) {
//     std::string str = "a,b,c,d,e";
//     auto parts = utils::StringUtils::split(str, ',');
    
//     EXPECT_EQ(parts.size(), 5);
//     EXPECT_EQ(parts[0], "a");
//     EXPECT_EQ(parts[4], "e");
// }

// TEST_F(StringUtilsTest, SplitWithMultipleDelimiters) {
//     std::string str = "hello,world;from|foundation";
//     auto parts = utils::StringUtils::split(str, ",;|");
    
//     EXPECT_EQ(parts.size(), 4);
//     EXPECT_EQ(parts[0], "hello");
//     EXPECT_EQ(parts[3], "foundation");
// }

// TEST_F(StringUtilsTest, TrimString) {
//     EXPECT_EQ(utils::StringUtils::trim("  hello  "), "hello");
//     EXPECT_EQ(utils::StringUtils::trim("\t\nhello\r\n"), "hello");
//     EXPECT_EQ(utils::StringUtils::trim("hello"), "hello");
//     EXPECT_EQ(utils::StringUtils::trim(""), "");
// }

// TEST_F(StringUtilsTest, ToLowerToUpper) {
//     EXPECT_EQ(utils::StringUtils::toLower("Hello World!"), "hello world!");
//     EXPECT_EQ(utils::StringUtils::toUpper("Hello World!"), "HELLO WORLD!");
//     EXPECT_EQ(utils::StringUtils::toLower(""), "");
//     EXPECT_EQ(utils::StringUtils::toUpper("123"), "123");
// }

// TEST_F(StringUtilsTest, StartsWithEndsWith) {
//     EXPECT_TRUE(utils::StringUtils::startsWith("hello world", "hello"));
//     EXPECT_TRUE(utils::StringUtils::endsWith("hello world", "world"));
//     EXPECT_FALSE(utils::StringUtils::startsWith("hello", "world"));
//     EXPECT_FALSE(utils::StringUtils::endsWith("hello", "world"));
//     EXPECT_TRUE(utils::StringUtils::startsWith("hello", ""));
//     EXPECT_TRUE(utils::StringUtils::endsWith("hello", ""));
// }

// TEST_F(StringUtilsTest, ReplaceString) {
//     std::string str = "hello world, world is beautiful";
//     std::string result = utils::StringUtils::replace(str, "world", "universe");
    
//     EXPECT_EQ(result, "hello universe, universe is beautiful");
//     EXPECT_EQ(utils::StringUtils::replace("aaaa", "aa", "b"), "bb");
// }

// TEST_F(StringUtilsTest, JoinStrings) {
//     std::vector<std::string> words = {"hello", "world", "from", "foundation"};
//     std::string result = utils::StringUtils::join(words, " ");
    
//     EXPECT_EQ(result, "hello world from foundation");
//     EXPECT_EQ(utils::StringUtils::join({}, ","), "");
// }

// ==================== 文件工具测试 ====================
class FileTest : public TestBase {};

// TEST_F(FileTest, FileReadWriteText) {
//     std::string test_content = "Hello, Foundation!\nThis is a test file.";
//     std::string test_file = getTestPath("test.txt");
    
//     // 测试写入文本
//     EXPECT_TRUE(fs::File::writeText(test_file, test_content));
//     EXPECT_TRUE(fs::File::exists(test_file));
//     EXPECT_TRUE(fs::File::isFile(test_file));
    
//     // 测试读取文本
//     std::string read_content = fs::File::readText(test_file);
//     EXPECT_EQ(read_content, test_content);
    
//     // 测试文件大小
//     EXPECT_EQ(fs::File::size(test_file), test_content.size());
// }
TEST_F(FileTest, FileReadWriteText) {
    std::string test_file = fs::File::createTempFile();
    
    // 测试内容（使用 Windows 换行符）
    std::string test_content = "Hello, Foundation!\r\nThis is a test file.";
    
    // 写入文件
    EXPECT_TRUE(fs::File::writeText(test_file, test_content));
    
    // 读取文件
    std::string read_content = fs::File::readText(test_file);
    
    // 在比较前规范化换行符
    std::string normalized_read = read_content;
    std::string normalized_test = test_content;
    
    // 可选：将所有 \r\n 替换为 \n 进行比较
    // utils::StringUtils::replace(normalized_read, "\r\n", "\n");
    // utils::StringUtils::replace(normalized_test, "\r\n", "\n");
    
    // 直接比较原始内容
    EXPECT_EQ(read_content, test_content);
    
    // 文件大小检查（考虑换行符差异）
    size_t expected_size = test_content.size();
    size_t actual_size = fs::File::size(test_file);
    
    // 在 Windows 上，文本文件的大小可能与字符串长度不同
    // 因为换行符可能被转换
    EXPECT_EQ(actual_size, expected_size) 
        << "Expected: " << expected_size 
        << ", Actual: " << actual_size 
        << ", Content: " << read_content;
    
    // 清理
    fs::File::remove(test_file);
}
TEST_F(FileTest, FileReadWriteLines) {
    std::vector<std::string> lines = {
        "Line 1",
        "Line 2",
        "Line 3",
        "Line 4",
        "Line 5"
    };
    
    std::string test_file = getTestPath("lines.txt");
    
    // 测试写入多行
    EXPECT_TRUE(fs::File::writeLines(test_file, lines));
    
    // 测试读取多行
    auto read_lines = fs::File::readLines(test_file);
    EXPECT_EQ(read_lines.size(), lines.size());
    EXPECT_EQ(read_lines, lines);
}

TEST_F(FileTest, FileAppendText) {
    std::string test_file = getTestPath("append.txt");
    
    // 写入初始内容
    EXPECT_TRUE(fs::File::writeText(test_file, "Initial content.\n"));
    
    // 追加内容
    EXPECT_TRUE(fs::File::appendText(test_file, "Appended content.\n"));
    EXPECT_TRUE(fs::File::appendText(test_file, "More appended content.\n"));
    
    // 验证内容
    std::string content = fs::File::readText(test_file);
    EXPECT_NE(content.find("Initial content."), std::string::npos);
    EXPECT_NE(content.find("Appended content."), std::string::npos);
    EXPECT_NE(content.find("More appended content."), std::string::npos);
}

TEST_F(FileTest, FileBinaryReadWrite) {
    std::vector<uint8_t> binary_data = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    std::string test_file = getTestPath("binary.bin");
    
    // 测试写入二进制数据
    EXPECT_TRUE(fs::File::writeBinary(test_file, binary_data));
    
    // 测试读取二进制数据
    auto read_data = fs::File::readBinary(test_file);
    EXPECT_EQ(read_data.size(), binary_data.size());
    EXPECT_EQ(read_data, binary_data);
}

TEST_F(FileTest, FileInformation) {
    std::string test_file = getTestPath("info.txt");
    fs::File::writeText(test_file, "Test content");
    
    // 测试文件信息
    EXPECT_TRUE(fs::File::exists(test_file));
    EXPECT_TRUE(fs::File::isFile(test_file));
    EXPECT_FALSE(fs::File::isDirectory(test_file));
    
    // 测试路径处理
    EXPECT_EQ(fs::File::extension(test_file), ".txt");
    EXPECT_EQ(fs::File::filename(test_file), "info.txt");
    
    // 测试文件大小
    EXPECT_EQ(fs::File::size(test_file), 12);
}

TEST_F(FileTest, DirectoryOperations) {
    std::string test_dir = getTestPath("test_dir");
    std::string sub_dir = test_dir + "/subdir";
     // 先彻底清理目录
    if (fs::File::exists(test_dir)) {
        // 先删除所有子文件和子目录
        auto files = fs::File::listFiles(test_dir);  // 递归列出所有文件
        for (const auto& file : files) {
            fs::File::remove(file);
        }
        
        auto dirs = fs::File::listDirectories(test_dir);  // 递归列出所有目录
        // 按深度逆序删除（先删最深层的）
        std::reverse(dirs.begin(), dirs.end());
        for (const auto& dir : dirs) {
            fs::File::removeDirectory(dir);
        }
        
        // 最后删除主目录
        fs::File::removeDirectory(test_dir);
    }
    // 测试创建目录
    EXPECT_TRUE(fs::File::createDirectory(test_dir));
    EXPECT_TRUE(fs::File::exists(test_dir));
    EXPECT_TRUE(fs::File::isDirectory(test_dir));
    
    // 测试创建多级目录
    EXPECT_TRUE(fs::File::createDirectories(sub_dir));
    EXPECT_TRUE(fs::File::exists(sub_dir));
    EXPECT_TRUE(fs::File::isDirectory(sub_dir));
    
    // 创建一些测试文件
    fs::File::writeText(test_dir + "/file1.txt", "File 1");
    fs::File::writeText(test_dir + "/file2.txt", "File 2");
    fs::File::writeText(sub_dir + "/file3.txt", "File 3");
    
    // 测试列出文件
    auto files = fs::File::listFiles(test_dir);
    EXPECT_GE(files.size(), 2);
    
    // 测试列出目录
    auto dirs = fs::File::listDirectories(test_dir);
    EXPECT_GE(dirs.size(), 1);
}

TEST_F(FileTest, FileCopyMove) {
    std::string source_file = getTestPath("source.txt");
    std::string copy_file = getTestPath("copy.txt");
    std::string move_file = getTestPath("move.txt");
    
    // 创建源文件
    fs::File::writeText(source_file, "Source content");
    
    // 测试复制文件
    EXPECT_TRUE(fs::File::copy(source_file, copy_file));
    EXPECT_TRUE(fs::File::exists(copy_file));
    EXPECT_EQ(fs::File::readText(copy_file), "Source content");
    
    // 测试移动文件
    EXPECT_TRUE(fs::File::move(copy_file, move_file));
    EXPECT_FALSE(fs::File::exists(copy_file));
    EXPECT_TRUE(fs::File::exists(move_file));
    EXPECT_EQ(fs::File::readText(move_file), "Source content");
}

TEST_F(FileTest, FileRemove) {
    std::string test_file = getTestPath("to_remove.txt");
    std::string test_dir = getTestPath("to_remove_dir");
    
    // 创建文件和目录
    fs::File::writeText(test_file, "Content to remove");
    fs::File::createDirectory(test_dir);
    fs::File::writeText(test_dir + "/nested.txt", "Nested file");
    
    // 测试删除文件
    EXPECT_TRUE(fs::File::exists(test_file));
    EXPECT_TRUE(fs::File::remove(test_file));
    EXPECT_FALSE(fs::File::exists(test_file));
    
    // 测试删除目录
    EXPECT_TRUE(fs::File::exists(test_dir));
    EXPECT_TRUE(fs::File::removeDirectory(test_dir));
    EXPECT_FALSE(fs::File::exists(test_dir));
}

TEST_F(FileTest, TempFileOperations) {
    // 测试创建临时文件
    std::string temp_file1 = fs::File::createTempFile();
    EXPECT_FALSE(temp_file1.empty());
    EXPECT_TRUE(fs::File::exists(temp_file1));
    EXPECT_TRUE(fs::File::isFile(temp_file1));
    
    // 测试创建带内容的临时文件
    std::string test_content = "Temporary file content";
    std::string temp_file2 = fs::File::createTempFile(test_content);
    EXPECT_FALSE(temp_file2.empty());
    EXPECT_TRUE(fs::File::exists(temp_file2));
    EXPECT_EQ(fs::File::readText(temp_file2), test_content);
    
    // 清理临时文件
    fs::File::remove(temp_file1);
    fs::File::remove(temp_file2);
}

TEST_F(FileTest, FileReaderClass) {
    std::string test_file = getTestPath("reader_test.txt");
    std::string test_content = "Line 1\nLine 2\nLine 3\n";
    fs::File::writeText(test_file, test_content);
    
    // 测试 FileReader
    fs::FileReader reader(test_file);
    EXPECT_TRUE(reader.isOpen());
    EXPECT_EQ(reader.filename(), test_file);
    
    // 测试读取全部内容
    std::string all_content = reader.readAll();
    EXPECT_EQ(all_content, test_content);
    
    // 测试逐行读取
    std::vector<std::string> lines = reader.readLines();
    EXPECT_EQ(lines.size(), 3);
    EXPECT_EQ(lines[0], "Line 1");
    EXPECT_EQ(lines[1], "Line 2");
    EXPECT_EQ(lines[2], "Line 3");
}

TEST_F(FileTest, FileWriterClass) {
    std::string test_file = getTestPath("writer_test.txt");
    
    // 测试 FileWriter
    {
        fs::FileWriter writer(test_file, false, true);
        EXPECT_TRUE(writer.isOpen());
        EXPECT_EQ(writer.filename(), test_file);
        
        // 写入内容
        EXPECT_TRUE(writer.write("Hello, "));
        EXPECT_TRUE(writer.writeLine("World!"));
        EXPECT_TRUE(writer.writeLine("This is a test."));
        
        writer.flush();
    }
    
    // 验证写入的内容
    std::string content = fs::File::readText(test_file);
    EXPECT_NE(content.find("Hello, World!"), std::string::npos);
    EXPECT_NE(content.find("This is a test."), std::string::npos);
}

TEST_F(FileTest, StreamOperators) {
    std::string test_file = getTestPath("stream_test.txt");
    
    // 测试写入流操作符
    {
        fs::FileWriter writer(test_file);
        writer << "Line: " << 1 << "\n";
        writer << "Line: " << 2 << "\n";
        writer << "Line: " << 3 << "\n";
    }
    
    // 测试读取流操作符
    fs::FileReader reader(test_file);
    std::string line;
    int line_number;
    std::string separator;
    
    reader >> line >> line_number;
    EXPECT_EQ(line, "Line:");
    EXPECT_EQ(line_number, 1);
}

TEST_F(FileTest, LargeFileOperations) {
    // 测试大文件操作
    std::string large_file = getTestPath("large.txt");
    
    // 生成大量数据
    std::string large_content;
    for (int i = 0; i < 1000; ++i) {
        large_content += "Line " + std::to_string(i) + ": This is a test line for large file operations.\n";
    }
    
    // 写入大文件
    EXPECT_TRUE(fs::File::writeText(large_file, large_content));
    
    // 验证文件大小
    EXPECT_GT(fs::File::size(large_file), 50000);
    
    // 读取验证
    std::string read_content = fs::File::readText(large_file);
    EXPECT_EQ(read_content.size(), large_content.size());
    EXPECT_EQ(read_content, large_content);
    
    // 清理
    fs::File::remove(large_file);
}

TEST_F(FileTest, EdgeCases) {
    // 测试不存在的文件
    std::string non_existent = getTestPath("non_existent.txt");
    EXPECT_FALSE(fs::File::exists(non_existent));
    EXPECT_EQ(fs::File::size(non_existent), 0);
    EXPECT_TRUE(fs::File::readText(non_existent).empty());
    
    // 测试空文件
    std::string empty_file = getTestPath("empty.txt");
    EXPECT_TRUE(fs::File::writeText(empty_file, ""));
    EXPECT_TRUE(fs::File::exists(empty_file));
    EXPECT_EQ(fs::File::size(empty_file), 0);
    EXPECT_TRUE(fs::File::readText(empty_file).empty());
    
    // 测试特殊字符文件名
    std::string special_file = getTestPath("test_file@#$%^&().txt");
    EXPECT_TRUE(fs::File::writeText(special_file, "Special chars"));
    EXPECT_TRUE(fs::File::exists(special_file));
    EXPECT_EQ(fs::File::readText(special_file), "Special chars");
}


} // namespace foundation::test