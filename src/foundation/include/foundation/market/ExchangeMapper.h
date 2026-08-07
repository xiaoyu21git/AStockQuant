#pragma once
// ExchangeMapper.h — 交易所映射表
// 将 AStockSymbol / PostMarketSyncService 中的硬编码映射提取为独立数据表
// 当前为编译期静态表, 预留 loadConfig() 接口以支持未来动态配置

#include "AStockSymbol.h"

#include <string>
#include <vector>

namespace foundation {
namespace market {

/// @brief 交易所前缀/后缀 ↔ 枚举映射表
/// 仅 AStockSymbol 和 PostMarketSyncService 使用, 外部一般无需直接调用
class ExchangeMapper final {
public:
    struct Entry {
        std::string suffix;   // ".SZ", ".SH", ".BJ"
        std::string prefix;   // "SZSE", "SHSE", "BSE"
        Exchange exchange;
    };

    /// @brief 获取所有映射条目
    [[nodiscard]] static const std::vector<Entry>& table();

    /// @brief 后缀 → 前缀, 如 ".SZ" → "SZSE"
    [[nodiscard]] static std::string suffixToPrefix(const std::string& suffix);

    /// @brief 前缀 → 后缀, 如 "SZSE" → ".SZ"
    [[nodiscard]] static std::string prefixToSuffix(const std::string& prefix);

    /// @brief 后缀 → 枚举, 如 ".SZ" → Exchange::SZSE
    [[nodiscard]] static Exchange fromSuffix(const std::string& suffix);

    /// @brief 前缀 → 枚举, 如 "SHSE" → Exchange::SSE
    [[nodiscard]] static Exchange fromPrefix(const std::string& prefix);

    /// @brief 预留扩展点: 从 JSON 配置文件动态加载交易所映射。当前为空实现, 请勿调用。
    /// 未来启用时需保证线程安全 (映射表热更新需加锁)。
    static void loadConfig(const std::string& jsonPath);
};

} // namespace market
} // namespace foundation
