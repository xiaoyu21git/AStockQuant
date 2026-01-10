#include "test_base.hpp"
#include "foundation/utils/random.hpp"
#include <unordered_set>
#include <algorithm>
#include <limits>
#include <thread>

namespace foundation::test {

class RandomTest : public TestBase {};

TEST_F(RandomTest, GetIntWithinRange) {
    // Random 使用的是静态方法，直接调用
    for (int i = 0; i < 1000; ++i) {
        int value = utils::Random::getInt(0, 100);
        EXPECT_GE(value, 0);
        EXPECT_LE(value, 100);
    }
}

TEST_F(RandomTest, DifferentTypes) {
    // 测试不同整数类型
    int8_t int8_val = utils::Random::getInt<int8_t>(-128, 127);
    EXPECT_GE(int8_val, -128);
    EXPECT_LE(int8_val, 127);
    
    uint32_t uint32_val = utils::Random::getInt<uint32_t>(0, 1000);
    EXPECT_LE(uint32_val, 1000u);
    
    int64_t int64_val = utils::Random::getInt<int64_t>(-1000000, 1000000);
    EXPECT_GE(int64_val, -1000000);
    EXPECT_LE(int64_val, 1000000);
}

TEST_F(RandomTest, GetFloat) {
    constexpr int SAMPLES = 10000;
    double sum = 0.0;
    double min_val = std::numeric_limits<double>::max();
    double max_val = std::numeric_limits<double>::lowest();
    
    for (int i = 0; i < SAMPLES; ++i) {
        double val = utils::Random::getFloat(0.0, 1.0);
        sum += val;
        min_val = std::min(min_val, val);
        max_val = std::max(max_val, val);
        EXPECT_GE(val, 0.0);
        EXPECT_LT(val, 1.0);  // 注意：uniform_real_distribution 是 [min, max)
    }
    
    double mean = sum / SAMPLES;
    // 均值应该在0.5附近
    EXPECT_NEAR(mean, 0.5, 0.05);
    EXPECT_GE(min_val, 0.0);
    EXPECT_LT(max_val, 1.0);  // 最大值应该小于1.0
}

TEST_F(RandomTest, GetBoolProbability) {
    constexpr int TRIALS = 10000;
    int true_count = 0;
    
    for (int i = 0; i < TRIALS; ++i) {
        if (utils::Random::getBool()) {
            true_count++;
        }
    }
    
    double probability = static_cast<double>(true_count) / TRIALS;
    EXPECT_NEAR(probability, 0.5, 0.05);
}

TEST_F(RandomTest, GetBoolWithCustomProbability) {
    constexpr int TRIALS = 10000;
    constexpr double PROB = 0.3;
    int true_count = 0;
    
    for (int i = 0; i < TRIALS; ++i) {
        if (utils::Random::getBool(PROB)) {
            true_count++;
        }
    }
    
    double probability = static_cast<double>(true_count) / TRIALS;
    EXPECT_NEAR(probability, PROB, 0.02);
}

TEST_F(RandomTest, GetStringLength) {
    for (int length : {1, 10, 100, 1000}) {
        std::string str = utils::Random::getString(length);
        EXPECT_EQ(str.length(), length);
        
        // 检查字符是否来自字母数字字符集
        for (char c : str) {
            EXPECT_TRUE(std::isalnum(c) || c == '+' || c == '/' || c == '=');
        }
    }
}

TEST_F(RandomTest, GetHexString) {
    // 测试小写
    std::string lower = utils::Random::getHexString(32, false);
    EXPECT_EQ(lower.length(), 32);
    for (char c : lower) {
        EXPECT_TRUE(std::isxdigit(c));
        EXPECT_FALSE(std::isupper(c));
    }
    
    // 测试大写
    std::string upper = utils::Random::getHexString(32, true);
    EXPECT_EQ(upper.length(), 32);
    for (char c : upper) {
        EXPECT_TRUE(std::isxdigit(c));
        EXPECT_TRUE(std::isupper(c) || std::isdigit(c));
    }
}

TEST_F(RandomTest, GetStringWithCustomCharset) {
    std::string charset = "abc123";
    std::string str = utils::Random::getString(50, charset);
    EXPECT_EQ(str.length(), 50);
    
    for (char c : str) {
        EXPECT_NE(charset.find(c), std::string::npos);
    }
}

TEST_F(RandomTest, GenerateUuid) {
    std::string uuid = utils::Random::generateUuid();
    
    // 检查格式: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    EXPECT_EQ(uuid.length(), 36);
    EXPECT_EQ(uuid[8], '-');
    EXPECT_EQ(uuid[13], '-');
    EXPECT_EQ(uuid[18], '-');
    EXPECT_EQ(uuid[23], '-');
    
    // 检查字符是十六进制
    for (int i = 0; i < 36; ++i) {
        if (i != 8 && i != 13 && i != 18 && i != 23) {
            EXPECT_TRUE(std::isxdigit(uuid[i]));
        }
    }
}

TEST_F(RandomTest, GenerateSimpleUuid) {
    std::string uuid = utils::Random::generateSimpleUuid();
    EXPECT_EQ(uuid.length(), 32);
    
    for (char c : uuid) {
        EXPECT_TRUE(std::isxdigit(c));
    }
}

TEST_F(RandomTest, Choice) {
    std::vector<int> numbers = {1, 2, 3, 4, 5};
    std::unordered_set<int> seen;
    
    // 多次选择应该覆盖所有值
    for (int i = 0; i < 100; ++i) {
        int chosen = utils::Random::choice(numbers);
        seen.insert(chosen);
        EXPECT_GE(chosen, 1);
        EXPECT_LE(chosen, 5);
    }
    
    // 应该至少看到大部分值
    EXPECT_GT(seen.size(), 3);
}

TEST_F(RandomTest, ChoiceEmptyContainer) {
    std::vector<int> empty;
    EXPECT_THROW(utils::Random::choice(empty), std::invalid_argument);
}

TEST_F(RandomTest, Sample) {
    std::vector<int> numbers = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    
    // 测试不重复采样
    auto sample = utils::Random::sample(numbers, 3);
    EXPECT_EQ(sample.size(), 3);
    
    // 检查是否有重复
    std::unordered_set<int> unique(sample.begin(), sample.end());
    EXPECT_EQ(unique.size(), 3);
    
    // 检查所有元素都在原容器中
    for (int num : sample) {
        EXPECT_NE(std::find(numbers.begin(), numbers.end(), num), numbers.end());
    }
}

TEST_F(RandomTest, SampleTooMany) {
    std::vector<int> numbers = {1, 2, 3};
    
    // 当请求样本数大于容器大小时，期望抛出异常
    EXPECT_THROW({
        utils::Random::sample(numbers, 5);
    }, std::invalid_argument);
    
    // 验证异常消息
    try {
        utils::Random::sample(numbers, 5);
        FAIL() << "Expected std::invalid_argument exception";
    } catch (const std::invalid_argument& e) {
        EXPECT_STREQ(e.what(), "Sample size exceeds container size");
    }
    
    // 测试边界情况：等于容器大小应该正常工作
    EXPECT_NO_THROW({
        auto result = utils::Random::sample(numbers, 3);
        EXPECT_EQ(result.size(), 3);
    });
    
    // 测试正常情况：小于容器大小
    EXPECT_NO_THROW({
        auto result = utils::Random::sample(numbers, 2);
        EXPECT_EQ(result.size(), 2);
    });
}

TEST_F(RandomTest, Shuffle) {
    std::vector<int> numbers = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<int> original = numbers;
    
    utils::Random::shuffle(numbers);
    
    // 检查元素相同但顺序不同
    EXPECT_NE(numbers, original);
    
    std::sort(numbers.begin(), numbers.end());
    std::sort(original.begin(), original.end());
    EXPECT_EQ(numbers, original);
}

TEST_F(RandomTest, Permutation) {
    auto perm = utils::Random::permutation<int>(10);
    EXPECT_EQ(perm.size(), 10);
    
    // 检查是否是0-9的排列
    std::vector<int> sorted = perm;
    std::sort(sorted.begin(), sorted.end());
    
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(sorted[i], i);
    }
}

TEST_F(RandomTest, SeedManagement) {
    // 记录当前随机状态
    int val1 = utils::Random::getInt(0, 1000);
    int val2 = utils::Random::getInt(0, 1000);
    
    // 设置固定种子
    utils::Random::seed(42);
    
    int val3 = utils::Random::getInt(0, 1000);
    int val4 = utils::Random::getInt(0, 1000);
    
    // 重置相同种子应该产生相同序列
    utils::Random::seed(42);
    
    int val5 = utils::Random::getInt(0, 1000);
    int val6 = utils::Random::getInt(0, 1000);
    
    EXPECT_EQ(val3, val5);
    EXPECT_EQ(val4, val6);
    
    // 恢复随机状态
    utils::Random::seedWithRandomDevice();
}

TEST_F(RandomTest, NormalDistribution) {
    constexpr int SAMPLES = 10000;
    double sum = 0.0;
    double sum_sq = 0.0;
    
    for (int i = 0; i < SAMPLES; ++i) {
        double val = utils::Random::getNormal(0.0, 1.0);
        sum += val;
        sum_sq += val * val;
    }
    
    double mean = sum / SAMPLES;
    double variance = sum_sq / SAMPLES - mean * mean;
    
    // 检查均值和方差
    EXPECT_NEAR(mean, 0.0, 0.05);
    EXPECT_NEAR(variance, 1.0, 0.05);
}

TEST_F(RandomTest, ExponentialDistribution) {
    constexpr double LAMBDA = 1.5;
    constexpr int SAMPLES = 10000;
    double sum = 0.0;
    
    for (int i = 0; i < SAMPLES; ++i) {
        double val = utils::Random::getExponential(LAMBDA);
        EXPECT_GE(val, 0.0);
        sum += val;
    }
    
    double mean = sum / SAMPLES;
    // 指数分布的均值是 1/lambda
    EXPECT_NEAR(mean, 1.0 / LAMBDA, 0.05);
}

TEST_F(RandomTest, PoissonDistribution) {
    constexpr double LAMBDA = 3.0;
    constexpr int SAMPLES = 10000;
    double sum = 0.0;
    
    for (int i = 0; i < SAMPLES; ++i) {
        int val = utils::Random::getPoisson(LAMBDA);
        EXPECT_GE(val, 0);
        sum += val;
    }
    
    double mean = sum / SAMPLES;
    // 泊松分布的均值等于lambda
    EXPECT_NEAR(mean, LAMBDA, 0.1);
}

TEST_F(RandomTest, ThreadSafety) {
    constexpr int NUM_THREADS = 10;
    constexpr int CALLS_PER_THREAD = 1000;
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};
    
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&errors,CALLS_PER_THREAD = CALLS_PER_THREAD]() {  // 使用 = 或 & 捕获所有变量
            for (int i = 0; i < CALLS_PER_THREAD; ++i) {
                try {
                    // 测试各种随机函数
                    int int_val = utils::Random::getInt(0, 100);
                    if (int_val < 0 || int_val > 100) errors++;
                    
                    double float_val = utils::Random::getFloat(0.0, 1.0);
                    if (float_val < 0.0 || float_val >= 1.0) errors++;
                    
                    utils::Random::getBool();
                    
                    std::string str = utils::Random::getString(10);
                    if (str.length() != 10) errors++;
                    
                } catch (...) {
                    errors++;
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(errors.load(), 0);
}

TEST_F(RandomTest, ColorGeneration) {
    // 测试不带alpha的颜色（默认includeHash = true）
    std::string hex =utils::Random::getColorHex(false);
    
    // 应该是7位：#RRGGBB
    EXPECT_EQ(hex.length(), 7);
    
    // 第一个字符应该是 #
    EXPECT_EQ(hex[0], '#');
    
    // 验证后面6位都是有效的十六进制
    for (size_t i = 1; i < hex.length(); ++i) {
        char c = hex[i];
        EXPECT_TRUE((c >= '0' && c <= '9') || 
                   (c >= 'A' && c <= 'F') ||
                   (c >= 'a' && c <= 'f'));
    }
    
    // 测试带alpha的颜色
    std::string hex_alpha = utils::Random::getColorHex(true);
    
    // 应该是9位：#RRGGBBAA
    EXPECT_EQ(hex_alpha.length(), 9);
    
    // 第一个字符应该是 #
    EXPECT_EQ(hex_alpha[0], '#');
    
    // 验证后面8位都是有效的十六进制
    for (size_t i = 1; i < hex_alpha.length(); ++i) {
        char c = hex_alpha[i];
        EXPECT_TRUE((c >= '0' && c <= '9') || 
                   (c >= 'A' && c <= 'F') ||
                   (c >= 'a' && c <= 'f'));
    }
    
    // 可选：测试 includeHash = false 的情况
    std::string hex_no_hash = utils::Random::getColorHex(false, false);
    EXPECT_EQ(hex_no_hash.length(), 6);  // RRGGBB
    EXPECT_NE(hex_no_hash[0], '#');      // 不应该以#开头
}

TEST_F(RandomTest, NetworkAddressGeneration) {
    std::string ip = utils::Random::getIpAddress();
    // 检查IP地址格式
    EXPECT_NE(ip.find('.'), std::string::npos);
    
    std::string mac = utils::Random::getMacAddress();
    // 检查MAC地址格式
    EXPECT_NE(mac.find(':'), std::string::npos);
}

TEST_F(RandomTest, ArrayGeneration) {
    auto int_array = utils::Random::getIntArray<int>(10, 0, 100);
    EXPECT_EQ(int_array.size(), 10);
    for (int val : int_array) {
        EXPECT_GE(val, 0);
        EXPECT_LE(val, 100);
    }
    
    auto float_array = utils::Random::getFloatArray<double>(10, 0.0, 1.0);
    EXPECT_EQ(float_array.size(), 10);
    for (double val : float_array) {
        EXPECT_GE(val, 0.0);
        EXPECT_LT(val, 1.0);
    }
    
    auto string_array = utils::Random::getStringArray(5, 3, 10);
    EXPECT_EQ(string_array.size(), 5);
    for (const auto& str : string_array) {
        EXPECT_GE(str.length(), 3);
        EXPECT_LE(str.length(), 10);
    }
}

TEST_F(RandomTest, TestDataGeneration) {
    // 这些测试主要检查格式
    std::string name = utils::Random::getName();
    EXPECT_FALSE(name.empty());
    
    std::string email = utils::Random::getEmail();
    EXPECT_NE(email.find('@'), std::string::npos);
    
    std::string phone = utils::Random::getPhoneNumber();
    EXPECT_FALSE(phone.empty());
    
    std::string address = utils::Random::getAddress();
    EXPECT_FALSE(address.empty());
}

TEST_F(RandomTest, DateTimeGeneration) {
    std::string date = utils::Random::getDate(2020, 2024);
    EXPECT_EQ(date.length(), 10);  // YYYY-MM-DD
    EXPECT_EQ(date[4], '-');
    EXPECT_EQ(date[7], '-');
    
    std::string time = utils::Random::getTime();
    EXPECT_GE(time.length(), 5);  // HH:MM
    
    std::string datetime = utils::Random::getDateTime(2020, 2024);
    EXPECT_GE(datetime.length(), 16);  // YYYY-MM-DD HH:MM
}

TEST_F(RandomTest, WeightedChoice) {
    std::vector<std::string> items = {"A", "B", "C"};
    std::vector<double> weights = {1.0, 2.0, 3.0};
    
    // 多次选择应该覆盖所有项
    std::unordered_map<std::string, int> counts;
    constexpr int TRIALS = 10000;
    
    for (int i = 0; i < TRIALS; ++i) {
        std::string chosen = utils::Random::weightedChoice(items, weights);
        counts[chosen]++;
    }
    
    // C的权重是A的3倍，应该出现更频繁
    EXPECT_GT(counts["C"], counts["A"]);
    EXPECT_GT(counts["B"], counts["A"]);
    EXPECT_GT(counts["C"], counts["B"]);
}

} // namespace foundation::test