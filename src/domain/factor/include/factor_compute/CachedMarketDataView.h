#pragma once

#include "factor_compute/IMarketDataView.h"
#include "factor_compute/FactorSignalTypes.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <Eigen/Dense>

namespace factor::compute {

/// @brief 基于缓存数据的行情视图（桥接 DataServiceCache）
///
/// 注意：完全无 Qt 依赖（纯 C++ 类型），Qt 类型仅在桥接层使用。
/// 数据在构造时由桥接层传入（已转换为 std 类型）。
class CachedMarketDataView final : public IMarketDataView {
public:
    struct ColumnData {
        std::vector<signal_value_t> values;  // 扁平化数据 [dateCount][instrumentCount], float32
        std::vector<DateKey> dates;
        std::vector<InstrumentId> instruments;
        int dateCount{0};
        int instrumentCount{0};
    };

    explicit CachedMarketDataView();
    ~CachedMarketDataView() override;

    void loadFromColumnData(ColumnData open,
                            ColumnData high,
                            ColumnData low,
                            ColumnData close,
                            ColumnData volume);

    /// @brief 加载额外命名字段（如 pb_ratio, pe_ratio, market_cap, roe 等）
    /// @param fieldName 字段名称（键）
    /// @param column 该字段的列式数据
    void loadAdditionalField(const std::string& fieldName, ColumnData column);

    [[nodiscard]] NumericConstMatrixView open() const override;
    [[nodiscard]] NumericConstMatrixView high() const override;
    [[nodiscard]] NumericConstMatrixView low() const override;
    [[nodiscard]] NumericConstMatrixView close() const override;
    [[nodiscard]] NumericConstMatrixView volume() const override;

    /// @brief 按字段名获取矩阵视图
    [[nodiscard]] std::optional<NumericConstMatrixView>
    getField(const std::string& fieldName) const override;

    [[nodiscard]] const std::vector<DateKey>& dates() const override;
    [[nodiscard]] const std::vector<InstrumentId>& instruments() const override;

    [[nodiscard]] std::unique_ptr<IMarketDataView>
    slice(DateRange dateRange) const override;

    [[nodiscard]] std::unique_ptr<IMarketDataView>
    slice(const std::vector<InstrumentId>& instrumentIds) const override;

    /// @brief 查询是否有指定字段
    [[nodiscard]] bool hasField(const std::string& fieldName) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace factor::compute
