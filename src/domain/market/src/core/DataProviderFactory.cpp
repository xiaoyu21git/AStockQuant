// src/market/core/DataProviderFactory.cpp
#include "market/core/MarketData.h"
#include "market/providers/FileProvider.h"
#include "market/providers/ApiProvider.h"
#include "market/providers/SimProvider.h"
#include "market/providers/DatabaseProvider.h"
#include <memory>
#include <iostream>
#include <memory>

namespace astock::market {

std::shared_ptr<IDataProvider> DataProviderFactory::create_provider(
    DataProviderFactory::ProviderType type,
    const std::string& config) {
    
    switch (type) {
        case DataProviderFactory::ProviderType::FILE:
            return std::make_shared<FileProvider>(config);
            
        case DataProviderFactory::ProviderType::DATABASE:
            // 返回数据库数据源（当前为占位实现）
            return std::make_shared<DatabaseProvider>(config);
            
        case DataProviderFactory::ProviderType::API:
            return std::make_shared<ApiProvider>(config);
            
        case DataProviderFactory::ProviderType::SIMULATED:
            return std::make_shared<SimProvider>(config);
            
        default:
            return nullptr;
    }
}

} // namespace astock::market