#pragma once

#include "IMarketDataView.h"

#include <memory>
#include <string>
#include <vector>

namespace factor::compute {

/// @brief Parquet 文件内存映射列式行情视图（设计文档 Section 5.2）
///
/// 约束：
/// - 从 Parquet 文件内存映射，按列访问，禁止全量拷贝。
/// - 底层列式数据通过 Eigen::Map 暴露为只读视图。
/// - 构造后不可变，线程安全只读。
class ParquetMarketDataView final : public IMarketDataView {
public:
    /// @brief 从 Parquet 文件路径构造
    ///
    /// @param parquetPath Parquet 文件路径
    /// @throws std::runtime_error 如果文件不可读或 Schema 不匹配
    explicit ParquetMarketDataView(const std::string& parquetPath);

    ~ParquetMarketDataView() override;

    // 禁止拷贝和移动
    ParquetMarketDataView(const ParquetMarketDataView&) = delete;
    ParquetMarketDataView& operator=(const ParquetMarketDataView&) = delete;
    ParquetMarketDataView(ParquetMarketDataView&&) = delete;
    ParquetMarketDataView& operator=(ParquetMarketDataView&&) = delete;

    [[nodiscard]] NumericConstMatrixView open() const override;
    [[nodiscard]] NumericConstMatrixView high() const override;
    [[nodiscard]] NumericConstMatrixView low() const override;
    [[nodiscard]] NumericConstMatrixView close() const override;
    [[nodiscard]] NumericConstMatrixView volume() const override;

    [[nodiscard]] const std::vector<DateKey>& dates() const override;
    [[nodiscard]] const std::vector<InstrumentId>& instruments() const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace factor::compute