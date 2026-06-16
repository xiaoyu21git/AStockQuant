// MarketDataServiceFactory.h — 纯 C++ MarketDataService 工厂
// 使用 NativeMySQLConnectionPool 初始化
#pragma once
#include "database/MarketDataService.h"
#include "database/NativeMySQLConnectionPool.h"
#include <memory>

namespace astock::infrastructure::database {

inline std::unique_ptr<MarketDataService> createMarketDataService() {
    auto& pool = astock::database::NativeMySQLConnectionPool::instance();
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
