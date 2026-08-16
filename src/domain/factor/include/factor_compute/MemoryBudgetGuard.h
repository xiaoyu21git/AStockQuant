#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// factor::compute::MemoryBudgetGuard — 块级内存预算封账
//
// 块视图内存事实 (代码核实 2026-08-16): DenseChunkView 列存储恒为
// signal_value_t (float32) 4B/格 [ArrowMarketDataView.cpp], 与 Arrow schema
// 字段类型无关 — 预算按实际物化宽度精确估计, 另加固定开销常量保守计入。
// 提交与释放均发生在主线程 (提交循环 + sink 循环) → 普通记账, 无并发竞争。
// ══════════════════════════════════════════════════════════════════════════════

#include "factor_compute/FactorSignalTypes.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace factor::compute {

class MemoryBudgetGuard {
public:
    /// @param instrumentCount 标的数 (块视图列长)
    /// @param columns         块视图列名 (超上界异常明细用)
    MemoryBudgetGuard(std::size_t instrumentCount, std::vector<std::string> columns)
        : m_instrumentCount(instrumentCount)
        , m_columns(std::move(columns))
    {}

    /// @brief 块视图内存估计 (字节) = 行 × 标的 × 列 × 4B + 固定开销
    /// 超过单块硬上界抛含字段明细异常 (静默溢出不可接受)
    [[nodiscard]] std::size_t chunkBytes(std::size_t rowCount) const
    {
        const std::size_t cells = rowCount * m_instrumentCount * m_columns.size();
        const std::size_t bytes = cells * sizeof(signal_value_t) + kFixedOverheadBytes;
        if (bytes > kSingleChunkHardLimitBytes) throwChunkTooLarge(rowCount, bytes);
        return bytes;
    }

    /// @brief 在飞块数上限 = min(workerThreads, 预算/最大单块字节)
    [[nodiscard]] int getMaxInFlight(int workerThreads, std::size_t totalBudgetBytes,
                                     std::size_t maxChunkBytes) const
    {
        if (workerThreads <= 0 || maxChunkBytes == 0) return 1;
        const int byBudget = static_cast<int>(totalBudgetBytes / maxChunkBytes);
        const int limit = byBudget > 0 ? byBudget : 1;
        return workerThreads < limit ? workerThreads : limit;
    }

    /// @brief 单块硬上界 (800MB) — 超限块直接异常而非静默放行
    static constexpr std::size_t kSingleChunkHardLimitBytes = 800ULL * 1024ULL * 1024ULL;

private:
    /// @brief 每块固定开销: DenseChunkView 对象 + 适配器日期/标的索引 (保守计入)
    static constexpr std::size_t kFixedOverheadBytes = 8ULL * 1024ULL * 1024ULL;

    /// @brief 构建含字段明细的异常消息并抛出
    void throwChunkTooLarge(std::size_t rowCount, std::size_t bytes) const
    {
        std::string detail = "rows=" + std::to_string(rowCount)
            + " insts=" + std::to_string(m_instrumentCount)
            + " cols=" + std::to_string(m_columns.size()) + " (";
        for (std::size_t i = 0; i < m_columns.size() && i < 10; ++i) {
            if (i > 0) detail += ",";
            detail += m_columns[i];
        }
        if (m_columns.size() > 10) detail += ",...";
        detail += ") bytes=" + std::to_string(bytes) + " > 800MB 硬上界";
        throw std::runtime_error("[MemoryBudgetGuard] 单块内存超上界: " + detail);
    }

    std::size_t m_instrumentCount = 0;
    std::vector<std::string> m_columns;
};

} // namespace factor::compute
