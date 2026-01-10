#include <gtest/gtest.h>
#include <iostream>

int main(int argc, char** argv) {
    std::cout << "========================================" << std::endl;
    std::cout << "  Foundation Module GTest Suite" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    ::testing::InitGoogleTest(&argc, argv);
    
    // 设置随机种子
    ::testing::UnitTest::GetInstance()->random_seed();
    
    int result = RUN_ALL_TESTS();
    
    std::cout << "\n========================================" << std::endl;
    if (result == 0) {
        std::cout << "  ALL TESTS PASSED!" << std::endl;
    } else {
        std::cout << "  SOME TESTS FAILED!" << std::endl;
    }
    std::cout << "========================================\n" << std::endl;
    return result;
}