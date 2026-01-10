#pragma once
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <filesystem>
#include <fstream>

namespace foundation::test {

class TestBase : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建临时测试目录
        test_temp_dir_ = "test_temp_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed());
        test_dir_ = test_temp_dir_;  // 保持兼容性
        std::filesystem::create_directories(test_temp_dir_);
    }
    
    void TearDown() override {
        // 清理临时目录
        cleanupTestDirectory();
    }
    
    // 清理测试目录的核心函数
    void cleanupTestDirectory() {
        if (test_temp_dir_.empty()) {
            return;
        }
        
        if (std::filesystem::exists(test_temp_dir_)) {
            try {
                // 尝试删除整个目录
                std::filesystem::remove_all(test_temp_dir_);
            } catch (const std::exception& e) {
                // 如果失败，尝试手动清理
                cleanupDirectoryManually(test_temp_dir_);
            }
        }
    }
    
    // 手动清理目录（当 remove_all 失败时使用）
    void cleanupDirectoryManually(const std::string& dir_path) {
        if (!std::filesystem::exists(dir_path)) {
            return;
        }
        
        // 多次尝试清理
        for (int attempt = 0; attempt < 3; ++attempt) {
            try {
                // 先删除所有文件
                for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
                    if (std::filesystem::is_regular_file(entry.path())) {
                        std::filesystem::remove(entry.path());
                    } else if (std::filesystem::is_directory(entry.path())) {
                        // 递归删除子目录
                        std::filesystem::remove_all(entry.path());
                    }
                }
                
                // 删除空目录
                std::filesystem::remove(dir_path);
                break;  // 成功退出
                
            } catch (const std::exception& e) {
                std::cerr << "Warning: Failed to clean directory (attempt " 
                          << attempt + 1 << "): " << e.what() << std::endl;
                
                if (attempt < 2) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                } else {
                    std::cerr << "Error: Could not clean test directory: " << dir_path << std::endl;
                }
            }
        }
    }
    
    // 保持兼容的 getTestPath 函数
    std::string getTestPath(const std::string& filename = "") const {
        if (filename.empty()) {
            return test_dir_;
        }
        return test_dir_ + "/" + filename;
    }
    
    // 获取临时文件路径（新名称，更清晰）
    std::string getTempPath(const std::string& filename = "") const {
        return getTestPath(filename);  // 两者功能相同
    }
    
    // 创建测试文件
    void createTestFile(const std::string& filename, const std::string& content) {
        std::string filepath = getTestPath(filename);
        std::ofstream file(filepath);
        if (!file) {
            throw std::runtime_error("Failed to create test file: " + filepath);
        }
        file << content;
        file.close();
    }
    
    // 读取测试文件
    std::string readTestFile(const std::string& filename) {
        std::string filepath = getTestPath(filename);
        std::ifstream file(filepath);
        if (!file) {
            throw std::runtime_error("Failed to read test file: " + filepath);
        }
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        return content;
    }
    
    // 清理特定的测试文件
    void cleanupTestFile(const std::string& filename) {
        std::string filepath = getTestPath(filename);
        if (std::filesystem::exists(filepath)) {
            for (int attempt = 0; attempt < 3; ++attempt) {
                try {
                    if (std::filesystem::remove(filepath)) {
                        break;
                    }
                } catch (...) {
                    // 忽略异常
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
    
    // 断言文件存在
    ::testing::AssertionResult assertFileExists(const std::string& filename) {
        std::string filepath = getTestPath(filename);
        if (std::filesystem::exists(filepath)) {
            return ::testing::AssertionSuccess() 
                << "File exists: " << filepath;
        } else {
            return ::testing::AssertionFailure() 
                << "File does not exist: " << filepath;
        }
    }
    
    // 断言文件内容
    ::testing::AssertionResult assertFileContent(
        const std::string& filename, 
        const std::string& expected_content) {
        
        if (!assertFileExists(filename)) {
            return ::testing::AssertionFailure() 
                << "Cannot check content of non-existent file: " << filename;
        }
        
        try {
            std::string actual_content = readTestFile(filename);
            if (actual_content == expected_content) {
                return ::testing::AssertionSuccess() 
                    << "File content matches expected";
            } else {
                return ::testing::AssertionFailure() 
                    << "File content mismatch.\n"
                    << "Expected: \"" << expected_content << "\"\n"
                    << "Actual:   \"" << actual_content << "\"";
            }
        } catch (const std::exception& e) {
            return ::testing::AssertionFailure() 
                << "Failed to read file content: " << e.what();
        }
    }
    
    // 获取测试目录路径（不含文件名）
    std::string getTestDir() const {
        return test_dir_;
    }
    
private:
    std::string test_temp_dir_;
    std::string test_dir_;
};

} // namespace foundation::test