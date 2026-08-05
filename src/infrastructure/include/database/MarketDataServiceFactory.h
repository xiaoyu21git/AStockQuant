// MarketDataServiceFactory.h — 纯 C++ MarketDataService 工厂
// 使用 NativePgConnectionPool 初始化
#pragma once
#include "database/MarketDataService.h"
#include "database/NativePgConnectionPool.h"
#include <memory>

namespace astock::infrastructure::database {

inline std::unique_ptr<MarketDataService> createMarketDataService() {
    auto& pool = astock::database::NativePgConnectionPool::instance();
    if (!pool.isInitialized()) {
        astock::database::DatabaseConfig cfg;
        cfg.host = "127.0.0.1";
        cfg.port = 3306;
        cfg.database = "astock_quant";
        cfg.username = "root";
        cfg.password = "123456a";
        pool.initialize(cfg);
    }
    auto db = pool.getConnection();
    if (!db || !db->isOpen()) return nullptr;
    return std::make_unique<MarketDataService>(std::move(db));
}

} // namespace
