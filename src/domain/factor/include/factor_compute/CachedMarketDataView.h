#pragma once

#include "factor_compute/IMarketDataView.h"
#include "factor_compute/FactorSignalTypes.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <Eigen/Dense>
#include "database/MarketDataRepository.h"

namespace foundation { namespace json { class JsonFacade; } }

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

    /// 真实股票代码 (InstrumentId → "000001.SZ" 等)
    [[nodiscard]] const std::vector<std::string>& symbolStrings() const override;

    [[nodiscard]] std::unique_ptr<IMarketDataView>
    slice(DateRange dateRange) const override;

    [[nodiscard]] std::unique_ptr<IMarketDataView>
    slice(const std::vector<InstrumentId>& instrumentIds) const override;

    /// @brief 查询是否有指定字段
    [[nodiscard]] bool hasField(const std::string& fieldName) const;

    /// @brief 从 DailyBarRow 向量直接构建（零 JSON，零 QVariant）
    static std::unique_ptr<CachedMarketDataView> fromDailyBarRows(
        const std::vector<astock::infrastructure::database::DailyBarRow>& rows);

    /// @brief 从 SQL 原始行+额外字段直接构建（含 PE/PB 等自定义字段）
    static std::unique_ptr<CachedMarketDataView> fromSqlRows(
        const std::vector<astock::database::SqlQueryResultRow>& rows,
        const std::vector<std::string>& extraFields);

    /// @brief 从 JSON 数组构建 CachedMarketDataView
    [[nodiscard]] static std::unique_ptr<CachedMarketDataView>
    fromJson(const foundation::json::JsonFacade& root,
             const std::vector<std::string>& extraFields = {});

    /// @brief 保存为二进制文件（快速缓存，跳过 JSON 解析）
    /// @return 成功返回 true
    bool saveToBinary(const std::string& filePath) const;

    /// @brief 从二进制文件加载（重建 CachedMarketDataView）
    /// @return 成功返回视图，失败返回 nullptr
    [[nodiscard]] static std::unique_ptr<CachedMarketDataView>
    fromBinary(const std::string& filePath);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace factor::compute
