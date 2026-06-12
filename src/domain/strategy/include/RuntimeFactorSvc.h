#pragma once

#include "IFactorSvc.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace factor {
class FactorInstanceManager;
}

namespace domain::strategy {

/// @brief IFactorSvc 实现 — 将策略引擎的因子查询适配到 FactorInstanceManager。
///
/// 职责：
/// - 因子 ID (uint64_t) → 因子名字符串 (通过注入的 resolver)
/// - 标的 ID (uint32_t) → 标的代码字符串 (通过注入的 resolver)
/// - 调用 FactorInstanceManager::createInstance() → BaseFactor::calculate()
/// - 将计算结果 (map<string, double>) 转回 (map<uint32_t, double>)
///
/// 实盘和回测共用。
class RuntimeFactorSvc final : public IFactorSvc {
public:
    using SymbolResolver = std::function<std::string(std::uint32_t)>;
    using FactorNameResolver = std::function<std::string(std::uint64_t)>;

    RuntimeFactorSvc(factor::FactorInstanceManager& instanceManager,
                     SymbolResolver symbolResolver,
                     FactorNameResolver factorNameResolver);
    ~RuntimeFactorSvc() override = default;

    [[nodiscard]] std::unordered_map<std::uint32_t, double> getValues(
        ::domain::strategies::FactorId factorId,
        std::int32_t date,
        const std::vector<std::uint32_t>& symbolIds) override;

private:
    factor::FactorInstanceManager& m_instanceManager;
    SymbolResolver m_symbolResolver;
    FactorNameResolver m_factorNameResolver;
};

} // namespace domain::strategy