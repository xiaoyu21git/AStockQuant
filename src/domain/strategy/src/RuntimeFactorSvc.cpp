#include "../include/RuntimeFactorSvc.h"
#include "../../factor/include/FactorInstanceManager.h"
#include "../../factor/include/BaseFactor.h"

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace domain::strategy {

RuntimeFactorSvc::RuntimeFactorSvc(
    factor::FactorInstanceManager& instanceManager,
    SymbolResolver symbolResolver,
    FactorNameResolver factorNameResolver)
    : m_instanceManager(instanceManager)
    , m_symbolResolver(std::move(symbolResolver))
    , m_factorNameResolver(std::move(factorNameResolver))
{
}

std::unordered_map<std::uint32_t, double> RuntimeFactorSvc::getValues(
    ::domain::strategies::FactorId factorId,
    std::int32_t date,
    const std::vector<std::uint32_t>& symbolIds)
{
    std::unordered_map<std::uint32_t, double> result;

    if (!m_factorNameResolver || !m_symbolResolver) {
        std::fprintf(stderr, "[RuntimeFactorSvc] resolver not set\n");
        return result;
    }

    // 1. 因子 ID → 因子名称
    const std::string factorName = m_factorNameResolver(factorId);
    if (factorName.empty()) {
        std::fprintf(stderr, "[RuntimeFactorSvc] factor name not resolved for id=%llu\n",
                     static_cast<unsigned long long>(factorId));
        return result;
    }

    // 2. 获取因子实例（createInstance 内部有缓存，不会重复创建）
    auto factorPtr = m_instanceManager.createInstance(factorName);
    if (!factorPtr) {
        std::fprintf(stderr, "[RuntimeFactorSvc] createInstance failed for factor=%s\n",
                     factorName.c_str());
        return result;
    }

    // 3. 标的 ID → 标的代码字符串
    std::vector<std::string> symbolStrList;
    symbolStrList.reserve(symbolIds.size());
    for (std::uint32_t id : symbolIds) {
        const std::string sym = m_symbolResolver(id);
        if (!sym.empty()) {
            symbolStrList.push_back(sym);
        }
    }
    if (symbolStrList.empty()) {
        return result;
    }

    // 4. 构造 CalculationContext
    //    date 格式为 YYYYMMDD int32，转为 "YYYY-MM-DD"
    const int year  = date / 10000;
    const int month = (date / 100) % 100;
    const int day   = date % 100;
    char dateBuf[16];
    std::snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", year, month, day);

    factor::CalculationContext ctx;
    ctx.date    = dateBuf;                   // "YYYY-MM-DD"
    ctx.symbols = std::move(symbolStrList);  // "000001.SZ", ...
    // historicalView 为 nullptr — 因子内部会使用自己的缓存数据集

    // 5. 计算
    factor::CalculationResult calcResult = factorPtr->calculate(ctx);

    // 6. 结果回映射：string → uint32_t
    for (const auto& [symbolStr, value] : calcResult.values) {
        // 通过 symbol → id 反向查找（遍历 symbolIds，简单匹配）
        for (std::uint32_t id : symbolIds) {
            if (m_symbolResolver(id) == symbolStr) {
                result[id] = value;
                break;
            }
        }
    }

    return result;
}

} // namespace domain::strategy