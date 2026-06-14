#pragma once

#include "factor_compute/FactorSignalTypes.h"
#include "ISqlDatabase.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace astock::infrastructure::database {

/// @brief 符号映射器：统一管理三种 symbol 表示
///
/// - std::string symbol: "000001.SZ", "600000.SH"
/// - uint32_t symbol_id: 数据库内部 ID
/// - factor::compute::InstrumentId: 因子引擎使用的 uint32
///
/// 构造时从数据库加载全量映射表，构造后不可变，线程安全只读。
class SymbolMapper {
public:
    SymbolMapper() = default;

    /// 从数据库 symbol_mapping 表加载全量映射
    void loadFromDatabase(std::shared_ptr<astock::database::ISqlDatabase> db);

    /// string -> InstrumentId
    [[nodiscard]] factor::compute::InstrumentId toInstrumentId(const std::string& symbol) const;

    /// InstrumentId -> string
    [[nodiscard]] std::string toSymbol(factor::compute::InstrumentId id) const;

    /// string -> 数据库 symbol_id
    [[nodiscard]] uint32_t toSymbolId(const std::string& symbol) const;

    /// 批量 string -> vector<InstrumentId>
    [[nodiscard]] std::vector<factor::compute::InstrumentId> toInstrumentIds(
        const std::vector<std::string>& symbols) const;

    /// 全部已注册标的
    [[nodiscard]] const std::vector<std::string>& allSymbols() const { return allSymbols_; }

    /// 是否已加载
    [[nodiscard]] bool isLoaded() const { return !symbolToInstId_.empty(); }

private:
    uint32_t nextId_ = 1;

    std::unordered_map<std::string, factor::compute::InstrumentId> symbolToInstId_;
    std::unordered_map<uint32_t, std::string> instIdToSymbol_;     // key = InstrumentId.value
    std::unordered_map<std::string, uint32_t> symbolToDbId_;
    std::vector<std::string> allSymbols_;
};

} // namespace astock::infrastructure::database