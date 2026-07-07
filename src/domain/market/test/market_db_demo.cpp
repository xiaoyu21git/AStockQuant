#include "market/core/MarketData.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

int main() {
    using namespace astock::market;

    auto &manager = MarketDataManager::instance();

    // 直接在配置串中写入数据库连接信息（不依赖环境变量）
    // 如需修改账号密码，请同步更新这里
    std::string config =
        "host=127.0.0.1;port=3306;database=astock_quant;"
        "username=root;password=123456a;charset=utf8mb4";

    if (!manager.initialize(DataProviderFactory::ProviderType::DATABASE, config)) {
        std::cerr << "Failed to initialize MarketDataManager with DATABASE provider" << std::endl;
        return 1;
    }

    std::cout << "MarketDataManager initialized with DATABASE provider" << std::endl;

    // 这里假定你已经在 astock_quant.minute_bar 里写入了对应 symbol_id 的数据
    // TODO: 根据数据库里实际存在的 symbol_id 调整这里
    std::uint32_t symbol_id = 1;       // 示例：1
    std::uint16_t period    = 60;      // 60 秒 = 1min，对应 timeframe = '1min'

    auto now_sec = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    std::uint64_t end_time   = now_sec;
    std::uint64_t start_time = (end_time > 3600 ? end_time - 3600 : 0); // 最近一小时

    std::size_t limit = 20;  // 只拉 20 根做演示

    // 使用最近一小时作为时间窗口进行查询
    auto batch = manager.get_history_klines(symbol_id, period, start_time, end_time, limit);

    std::cout << "Fetched " << batch.size() << " KLines from database" << std::endl;

    for (std::size_t i = 0; i < batch.size(); ++i) {
        const auto &k = batch[i];
        std::cout << "#" << i
                  << " ts=" << k.timestamp
                  << " o=" << k.open
                  << " h=" << k.high
                  << " l=" << k.low
                  << " c=" << k.close
                  << " v=" << k.volume
                  << " amt=" << k.amount
                  << std::endl;
    }

    return 0;
}
