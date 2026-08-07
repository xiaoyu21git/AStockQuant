#include "foundation/market/ExchangeMapper.h"

#include <algorithm>
#include <cctype>

namespace foundation {
namespace market {

const std::vector<ExchangeMapper::Entry>& ExchangeMapper::table() {
    static const std::vector<Entry> s_table = {
        {".SH", "SHSE", Exchange::SSE},
        {".SZ", "SZSE", Exchange::SZSE},
        {".BJ", "BSE",  Exchange::BSE},
    };
    return s_table;
}

std::string ExchangeMapper::suffixToPrefix(const std::string& suffix) {
    for (const auto& e : table()) {
        if (e.suffix == suffix) return e.prefix;
    }
    return "";
}

std::string ExchangeMapper::prefixToSuffix(const std::string& prefix) {
    for (const auto& e : table()) {
        if (e.prefix == prefix) return e.suffix;
    }
    return "";
}

Exchange ExchangeMapper::fromSuffix(const std::string& suffix) {
    for (const auto& e : table()) {
        if (e.suffix == suffix) return e.exchange;
    }
    return Exchange::Unknown;
}

Exchange ExchangeMapper::fromPrefix(const std::string& prefix) {
    for (const auto& e : table()) {
        if (e.prefix == prefix) return e.exchange;
    }
    return Exchange::Unknown;
}

void ExchangeMapper::loadConfig(const std::string& /*jsonPath*/) {
    // 预留扩展点: 当前为空实现, 请勿调用
    // 未来从 JSON 文件加载交易所映射, 启用时需加锁保证线程安全
}

} // namespace market
} // namespace foundation
