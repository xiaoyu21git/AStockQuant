#pragma once
// ═════════════════════════════════════════════════════════════════════════
// MarketDataService — tick → LiveData 更新 (纯 C++，零 Qt)
//
// 职责: 接收 GmSessionEngine 推送的实时 tick，分发给各标的 LiveData。
// 不查数据库、不碰策略、不生成信号。
// ═════════════════════════════════════════════════════════════════════════

// 前向声明，避免域层依赖引擎层
namespace engine { struct GmTickData; }

#include "LiveData.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace domain::market {

/// @brief 行情数据服务（单例）
///
/// GmSessionEngine::on_tick() 调用 onTick()，内部更新对应标的的
/// LiveData（日K + 各分钟周期K线）。
///
/// 外部查询：liveData(symbol) 返回 LiveData 引用，读取一线数据。
class MarketDataService final {
public:
    static MarketDataService& instance();

    /// @brief 接收单笔 tick，更新对应标的的全部 K 线
    void onTick(const engine::GmTickData& td);

    /// @brief 获取某标的的实时行情（不存在则创建空数据）
    [[nodiscard]] const LiveData& liveData(const std::string& symbol) const;

    /// @brief 获取某标的的实时行情（可写，仅 MarketDataService 使用）
    LiveData& mutableLiveData(const std::string& symbol);

    /// @brief 已追踪的标的列表
    [[nodiscard]] std::vector<std::string> symbols() const;

private:
    MarketDataService() = default;

    mutable std::mutex mutex_;
    // mutable 允许 liveData() 内部惰性插入
    mutable std::unordered_map<std::string, LiveData> data_;
};

} // namespace domain::market
