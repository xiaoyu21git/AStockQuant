// src/market/core/DataProviderFactory.cpp
#include "core/IDataProvider.h"
#include "providers/FileProvider.h"
#include "providers/ApiProvider.h"
#include "providers/SimProvider.h"
#include <memory>
#include <iostream>
#include <memory>
namespace domain {
namespace market {

std::shared_ptr<IDataProvider> DataProviderFactory::create_provider(
    ProviderType type,
    const std::string& config) {
    
    switch (type) {
        case ProviderType::FILE:
            return std::make_shared<providers::FileProvider>(config);
            
        case ProviderType::DATABASE:
            // 返回数据库数据源
            // return std::make_shared<DatabaseProvider>(config);
            return nullptr; // 暂未实现
            
        case ProviderType::API:
            return std::make_shared<ApiProvider>(config);
            
        case ProviderType::SIMULATED:
            return std::make_shared<SimProvider>(config);
            
        default:
            return nullptr;
    }
}

} // namespace market
}