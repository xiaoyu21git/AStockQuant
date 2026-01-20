// 文件：market/src/market/repository/MemoryMarketDataRepository.hpp
#pragma once
#include "market/repository/IMarketDataRepository.hpp"
#include <mutex>
#include <map>

namespace domain::market::repository {

class MemoryMarketDataRepository : public IMarketDataRepository {
public:
    MemoryMarketDataRepository();
    ~MemoryMarketDataRepository() override;
    
    // 实现所有接口方法...
    
private:
    std::mutex mutex_;
    bool connected_ = false;
    
    // 内存存储结构
    std::map<std::string, std::vector<Bar>> bars_;
    std::map<std::string, std::vector<Tick>> ticks_;
    std::map<std::string, SymbolInfo> symbols_;
    
    // 索引加速查询
    std::map<std::string, std::map<Timestamp, Bar*>> bar_time_index_;
    std::map<std::string, std::map<Timestamp, Tick*>> tick_time_index_;
};

} // namespace domain::market::repository