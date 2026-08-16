#pragma once

#include "IMarketDataView.h"

#include <memory>
#include <string>
#include <vector>

// Arrow 类型前向声明 (保持头文件轻量, 完整定义仅进 .cpp)
namespace arrow {
namespace io {
class RandomAccessFile;
}
namespace ipc {
class RecordBatchFileReader;
}
} // namespace arrow

namespace factor::compute {

/// @brief 独立 Arrow 文件读句柄 — 每 worker 一个, 并发读不加全局锁
///
/// RecordBatchFileReader 内部持有顺序读状态, 共享 reader 并发 ReadRecordBatch
/// 状态不可靠。块级并行时每个 worker 持独立句柄 (mmap 页缓存由 OS 共享,
/// 物理内存零拷贝不变); 日期/标的/跳读索引仍从主视图共享 (只读)。
/// 打开逻辑与 ArrowMarketDataView::Impl 构造共用 (createReaderHandle 静态工厂)。
class ArrowReaderHandle {
public:
    ArrowReaderHandle() = default;

    /// @brief 打开 IPC 文件: mmap + RecordBatchFileReader
    [[nodiscard]] static std::shared_ptr<ArrowReaderHandle> open(const std::string& path);

    [[nodiscard]] bool valid() const noexcept { return m_reader != nullptr; }
    [[nodiscard]] const std::string& lastError() const noexcept { return m_lastError; }
    [[nodiscard]] std::shared_ptr<arrow::ipc::RecordBatchFileReader> reader() const noexcept { return m_reader; }
    [[nodiscard]] std::shared_ptr<arrow::io::RandomAccessFile> input() const noexcept { return m_input; }

private:
    std::shared_ptr<arrow::io::RandomAccessFile> m_input;
    std::shared_ptr<arrow::ipc::RecordBatchFileReader> m_reader;
    std::string m_lastError;
};

/// @brief Arrow IPC 文件流式行情视图
///
/// 读取 DataCache 写入的 .arrow 文件 (Arrow IPC File format)
/// 构造时仅建立日期/标的索引（~30KB），不持有全量表。
/// 核心列 (open/high/low/close/volume) 首次访问时懒加载并缓存。
/// 额外字段通过 getField() 按需加载。
///
/// makeChunkView() 创建仅包含指定日期区间的自包含视图，
/// 数据直接从 Arrow batches 按需加载，适用于分块回测。
class ArrowMarketDataView final : public IMarketDataView {
public:
    explicit ArrowMarketDataView(const std::string& arrowPath);
    ~ArrowMarketDataView() override;

    ArrowMarketDataView(const ArrowMarketDataView&) = delete;
    ArrowMarketDataView& operator=(const ArrowMarketDataView&) = delete;
    ArrowMarketDataView(ArrowMarketDataView&&) = delete;
    ArrowMarketDataView& operator=(ArrowMarketDataView&&) = delete;

    [[nodiscard]] NumericConstMatrixView open() const override;
    [[nodiscard]] NumericConstMatrixView high() const override;
    [[nodiscard]] NumericConstMatrixView low() const override;
    [[nodiscard]] NumericConstMatrixView close() const override;
    [[nodiscard]] NumericConstMatrixView volume() const override;

    [[nodiscard]] std::optional<NumericConstMatrixView>
    getField(const std::string& fieldName) const override;

    [[nodiscard]] const std::vector<DateKey>& dates() const override;
    [[nodiscard]] const std::vector<InstrumentId>& instruments() const override;
    [[nodiscard]] const std::vector<std::string>& symbolStrings() const;

    [[nodiscard]] std::vector<std::string> fieldNames() const override;

    [[nodiscard]] std::unique_ptr<IMarketDataView>
    slice(DateRange dateRange) const override;

    [[nodiscard]] std::unique_ptr<IMarketDataView>
    slice(const std::vector<InstrumentId>& instrumentIds) const override;

    /// @brief 创建仅含指定日期区间的自包含分块视图
    /// @param dateRange  目标日期列表
    /// @param columns    需要加载的列名（不含 symbol/trade_date）
    /// @return 分块视图，独立持有数据，释放后内存回收
    /// 内存 = len(dateRange) × instrumentCount × len(columns) × 4 bytes
    [[nodiscard]] std::unique_ptr<IMarketDataView>
    makeChunkView(const std::vector<DateKey>& dateRange,
                  const std::vector<std::string>& columns) const;

    /// @brief 用独立读句柄创建分块视图 (并行 worker 路径)
    /// 与 makeChunkView 共用同一行映射填充实现; 日期/标的/跳读索引仍取自本视图
    /// (只读共享), 仅 RecordBatch 读取经独立句柄。
    [[nodiscard]] std::unique_ptr<IMarketDataView>
    makeChunkViewConcurrent(const std::vector<DateKey>& dateRange,
                            const std::vector<std::string>& columns,
                            const ArrowReaderHandle& handle) const;

    /// @brief 数据集文件路径 (worker 创建独立读句柄用)
    [[nodiscard]] const std::string& arrowPath() const;

    /// @brief 创建独立读句柄 (块级并行 worker 用, 与构造器共用同一打开实现)
    [[nodiscard]] static std::shared_ptr<ArrowReaderHandle>
    createReaderHandle(const std::string& path);

    /// @brief 预加载指定列为全量矩阵（供非分块路径使用）
    void ensureColumns(const std::vector<std::string>& columnNames) const;

    /// @brief 清除所有懒加载列缓存，保留 mmap + 日期/标的索引
    /// 下次访问列时触发重新懒加载（从已打开的 mmap 读取，速度快）。
    /// 不影响 makeChunkView() 的行为（分块视图直接从 Arrow batches 读取）。
    void clearColumnCaches() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace factor::compute
