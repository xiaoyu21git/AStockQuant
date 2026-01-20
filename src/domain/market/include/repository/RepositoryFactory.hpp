// 文件：market/include/market/repository/RepositoryFactory.hpp
#pragma once
#include "IMarketDataRepository.hpp"
#include <memory>
#include <string>

namespace domain::market::repository {

class RepositoryFactory {
public:
    /**
     * @brief 创建仓储实例
     */
    static std::shared_ptr<IMarketDataRepository> create(
        RepositoryType type,
        const std::string& config = "");
    
    /**
     * @brief 从配置文件创建仓储
     */
    static std::shared_ptr<IMarketDataRepository> createFromConfig(
        const std::string& config_path);
    
    /**
     * @brief 创建默认仓储（根据环境自动选择）
     */
    static std::shared_ptr<IMarketDataRepository> createDefault();
    
    /**
     * @brief 创建混合仓储（内存+数据库）
     */
    static std::shared_ptr<IMarketDataRepository> createHybrid(
        RepositoryType primary,
        RepositoryType secondary,
        const std::string& config = "");
};

} // namespace domain::market::repository