// 文件：market/include/market/repository/BaseMarketDataRepository.hpp
#pragma once
#include "IMarketDataRepository.hpp"
#include "foundation/log/Logger.hpp"

namespace domain::market::repository {

class BaseMarketDataRepository : public IMarketDataRepository {
public:
    BaseMarketDataRepository();
    virtual ~BaseMarketDataRepository();
    
    // 提供一些默认实现和工具方法
protected:
    std::shared_ptr<foundation::log::Logger> logger_;
    
    // 数据验证
    bool validateBar(const Bar& bar) const;
    bool validateTick(const Tick& tick) const;
    bool validateSymbolInfo(const SymbolInfo& info) const;
    
    // 时间范围验证
    bool validateTimeRange(Timestamp start, Timestamp end) const;
    
    // 批量操作限制
    size_t batch_size_ = 1000;
    
    // 缓存相关
    bool cache_enabled_ = false;
    mutable std::mutex cache_mutex_;
    
private:
    // 纯虚函数，子类必须实现
    virtual bool doConnect() = 0;
    virtual void doDisconnect() = 0;
};

} // namespace domain::market::repository