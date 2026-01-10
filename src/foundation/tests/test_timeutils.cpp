#include "test_base.hpp"
#include "foundation/utils/string_utils.hpp"
#include "foundation/utils/time.hpp"  // 包含正确的头文件
#include <chrono>
#include <thread>

namespace foundation::test {

// ==================== 时间工具测试 ====================
class TimeUtilsTest : public TestBase {};

TEST_F(TimeUtilsTest, GetTimestamp) {
    // 测试毫秒时间戳
    auto timestamp1 = foundation::utils::Time::getTimestampMilliseconds();
    EXPECT_GT(timestamp1, 0);
    
    // 等待一下
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    auto timestamp2 = foundation::utils::Time::getTimestampMilliseconds();
    EXPECT_GT(timestamp2, timestamp1);
    
    // 测试其他精度的时间戳
    auto timestamp_seconds = foundation::utils::Time::getTimestamp();
    auto timestamp_micro = foundation::utils::Time::getTimestampMicroseconds();
    auto timestamp_nano = foundation::utils::Time::getTimestampNanoseconds();
    
    EXPECT_GT(timestamp_micro, timestamp1 * 1000);
    EXPECT_GT(timestamp_nano, timestamp_micro * 1000);
}

TEST_F(TimeUtilsTest, GetCurrentTimeString) {
    // 测试默认格式
    std::string time_str = foundation::utils::Time::getCurrentTimeString();
    EXPECT_FALSE(time_str.empty());
    EXPECT_GE(time_str.length(), 19);  // YYYY-MM-DD HH:MM:SS
    
    // 测试自定义格式
    std::string custom_format = foundation::utils::Time::getCurrentTimeString("%Y/%m/%d");
    EXPECT_FALSE(custom_format.empty());
    EXPECT_EQ(custom_format.length(), 10);  // YYYY/MM/DD
}

TEST_F(TimeUtilsTest, FormatTime) {
    auto now = foundation::utils::Time::now();
    
    // 测试格式化
    std::string formatted = foundation::utils::Time::formatTime(now);
    EXPECT_FALSE(formatted.empty());
    
    // 测试自定义格式
    std::string custom = foundation::utils::Time::formatTime(now, "%H:%M:%S");
    EXPECT_EQ(custom.length(), 8);  // HH:MM:SS
}

TEST_F(TimeUtilsTest, DurationCalculation) {
    auto start = foundation::utils::Time::nowSteady();
    
    // 做一些工作
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    auto end = foundation::utils::Time::nowSteady();
    
    // 测试不同精度的时间差计算
    double seconds = foundation::utils::Time::durationSeconds(start, end);
    double milliseconds = foundation::utils::Time::durationMilliseconds(start, end);
    double microseconds = foundation::utils::Time::durationMicroseconds(start, end);
    
    EXPECT_NEAR(milliseconds, 50.0, 10.0);  // 允许10ms误差
    EXPECT_NEAR(seconds * 1000, milliseconds, 1.0);
    EXPECT_NEAR(milliseconds * 1000, microseconds, 1000.0);
}

TEST_F(TimeUtilsTest, DateOperations) {
    // 测试日期获取
    std::string today = foundation::utils::Time::getTodayDate();
    EXPECT_FALSE(today.empty());
    EXPECT_EQ(today.length(), 10);  // YYYY-MM-DD
    
    // 测试日期组件
    int year = foundation::utils::Time::getCurrentYear();
    int month = foundation::utils::Time::getCurrentMonth();
    int day = foundation::utils::Time::getCurrentDay();
    
    EXPECT_GE(year, 2023);
    EXPECT_GE(month, 1);
    EXPECT_LE(month, 12);
    EXPECT_GE(day, 1);
    EXPECT_LE(day, 31);
    
    // 测试时间组件
    int hour = foundation::utils::Time::getCurrentHour();
    int minute = foundation::utils::Time::getCurrentMinute();
    int second = foundation::utils::Time::getCurrentSecond();
    
    EXPECT_GE(hour, 0);
    EXPECT_LE(hour, 23);
    EXPECT_GE(minute, 0);
    EXPECT_LE(minute, 59);
    EXPECT_GE(second, 0);
    EXPECT_LE(second, 59);
}

TEST_F(TimeUtilsTest, TimerClass) {
    // 测试计时器
    foundation::utils::Time::Timer timer;
    
    timer.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    timer.stop();
    
    double elapsed_ms = timer.elapsedMilliseconds();
    double elapsed_s = timer.elapsedSeconds();
    
    EXPECT_NEAR(elapsed_ms, 50.0, 15.0);  // 允许10ms误差
    EXPECT_NEAR(elapsed_s * 1000, elapsed_ms, 1.0);
    
    // 测试重新开始
    timer.restart();
    EXPECT_TRUE(timer.isRunning());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    
    double elapsed_ms2 = timer.elapsedMilliseconds();
    EXPECT_GT(elapsed_ms2, 0);
    EXPECT_LT(elapsed_ms2, 100);  // 应该小于100ms
}

TEST_F(TimeUtilsTest, PerformanceMeasurement) {
    // 测试性能测量
    auto func = []() {
        // 模拟一些工作
        volatile int sum = 0;
        for (int i = 0; i < 10000; ++i) {
            sum += i;
        }
        return sum;
    };
    
    double execution_time = foundation::utils::Time::measureExecutionTimeMs(func);
    EXPECT_GT(execution_time, 0.0);
}

TEST_F(TimeUtilsTest, SleepFunctions) {
    auto start = foundation::utils::Time::nowSteady();
    
    // 测试毫秒睡眠
    foundation::utils::Time::sleepMs(100);
    
    auto end = foundation::utils::Time::nowSteady();
    double elapsed = foundation::utils::Time::durationMilliseconds(start, end);
    
    EXPECT_NEAR(elapsed, 100.0, 20.0);  // 允许20ms误差
    
    // 测试秒睡眠
    start = foundation::utils::Time::nowSteady();
    foundation::utils::Time::sleep(0.1);  // 0.1秒 = 100ms
    end = foundation::utils::Time::nowSteady();
    
    elapsed = foundation::utils::Time::durationMilliseconds(start, end);
    EXPECT_NEAR(elapsed, 100.0, 20.0);  // 允许20ms误差
}

TEST_F(TimeUtilsTest, TimezoneOperations) {
    // 测试时区偏移
    int offset = foundation::utils::Time::getLocalTimezoneOffset();
    EXPECT_GE(offset, -12);
    EXPECT_LE(offset, 14);
    
    // 注意：时区转换测试可能因平台而异
    // 这里只进行基本验证
}

TEST_F(TimeUtilsTest, TimeConstants) {
    // 测试时间常量
    EXPECT_EQ(foundation::utils::Time::SECONDS_PER_MINUTE, 60);
    EXPECT_EQ(foundation::utils::Time::SECONDS_PER_HOUR, 3600);
    EXPECT_EQ(foundation::utils::Time::SECONDS_PER_DAY, 86400);
    EXPECT_EQ(foundation::utils::Time::MILLISECONDS_PER_SECOND, 1000);
    EXPECT_EQ(foundation::utils::Time::MICROSECONDS_PER_SECOND, 1000000);
    EXPECT_EQ(foundation::utils::Time::NANOSECONDS_PER_SECOND, 1000000000);
}

} // namespace foundation::test