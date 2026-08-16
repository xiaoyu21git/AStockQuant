#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// factor::compute::FactorWorkerContext — 块级并行 worker 上下文 (每 worker 一个)
//
// - 构造时创建并持有独立 ArrowReaderHandle (RecordBatchFileReader 并发共享不可靠,
//   每 worker 用自己的句柄; mmap 页缓存由 OS 共享, 物理内存零拷贝不变)
// - factorName → createIsolatedInstance 实例缓存: 不进入全局实例缓存, worker 内
//   跨块复用 — 消除 BaseFactor::m_finSeriesCache 等实例级记忆化缓存的跨 worker 竞态
//   (记忆化只影响速度不影响值, 隔离实例数值与共享实例位级一致)
// ══════════════════════════════════════════════════════════════════════════════

#include <memory>
#include <string>
#include <unordered_map>

namespace factor {
class FactorInstanceManager;
class BaseFactor;
} // namespace factor

namespace factor::compute {

class ArrowMarketDataView;
class ArrowReaderHandle;

class FactorWorkerContext {
public:
    FactorWorkerContext(factor::FactorInstanceManager& instanceManager,
                        const ArrowMarketDataView& arrowView);

    /// @brief 获取 (必要时创建) 本 worker 的隔离因子实例
    [[nodiscard]] std::shared_ptr<factor::BaseFactor> factor(const std::string& factorId);

    /// @brief 本 worker 的独立 Arrow 读句柄
    [[nodiscard]] const ArrowReaderHandle& readerHandle() const;

private:
    factor::FactorInstanceManager& m_instanceManager;
    std::shared_ptr<ArrowReaderHandle> m_handle;
    std::unordered_map<std::string, std::shared_ptr<factor::BaseFactor>> m_factors;
};

} // namespace factor::compute
