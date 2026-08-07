// MarketDataServiceFactory.h — 纯 C++ MarketDataService 工厂
// 使用 NativePgConnectionPool 初始化
#pragma once
#include "database/MarketDataService.h"
#include "database/NativePgConnectionPool.h"
#include <memory>

namespace astock::infrastructure::database {

inline std::unique_ptr<MarketDataService> createMarketDataService() {
    auto& pool = astock::database::NativePgConnectionPool::instance();
    auto db = pool.getConnection();
    if (!db || !db->isOpen()) return nullptr;
    return std::make_unique<MarketDataService>(std::move(db));
}

} // namespace
