#pragma once

#include "IMarketDataView.h"
#include "IMarketDataStream.h"

#include <mutex>
#include <vector>

namespace factor::compute {

/// @brief 实时行情数据视图
///
/// 同时实现 IMarketDataView（全量查询）和 IMarketDataSubscriber（增量接收）。
/// 内部使用环形缓冲区存储最新 N 天的行情数据。
class LiveMarketDataView final : public IMarketDataView, public IMarketDataSubscriber {
public:
    /// @brief 构造
    /// @param capacity 最多保留的天数
    explicit LiveMarketDataView(int32_t capacity = 252) noexcept;

    ~LiveMarketDataView() override = default;

    // ---- IMarketDataView 接口 ----
    [[nodiscard]] NumericConstMatrixView open() const override;
    [[nodiscard]] NumericConstMatrixView high() const override;
    [[nodiscard]] NumericConstMatrixView low() const override;
    [[nodiscard]] NumericConstMatrixView close() const override;
    [[nodiscard]] NumericConstMatrixView volume() const override;

    [[nodiscard]] std::optional<NumericConstMatrixView>
    getField(const std::string& fieldName) const override;

    [[nodiscard]] const std::vector<DateKey>& dates() const override;
    [[nodiscard]] const std::vector<InstrumentId>& instruments() const override;

    [[nodiscard]] std::unique_ptr<IMarketDataView>
    slice(DateRange dateRange) const override;

    [[nodiscard]] std::unique_ptr<IMarketDataView>
    slice(const std::vector<InstrumentId>& instrumentIds) const override;

    // ---- IMarketDataSubscriber 接口 ----
    void onData(const DeltaMarketData& delta) override;
    void onStatusChange(StreamStatus status) override;

private:
    mutable std::mutex mutex_;
    std::vector<DateKey> dates_;               ///< 所有已接收的日期
    std::vector<InstrumentId> instruments_;    ///< 所有已接收的标的
    std::vector<signal_value_t> closeBuffer_;  ///< 收盘价环形缓冲区
    std::vector<signal_value_t> highBuffer_;
    std::vector<signal_value_t> lowBuffer_;
    std::vector<signal_value_t> volumeBuffer_;
    int32_t capacity_{252};
    int32_t currentDayCount_{0};

    /// @brief 构建矩阵视图（仅用于 close 等列）
    [[nodiscard]] NumericConstMatrixView buildView(
        const std::vector<signal_value_t>& buffer) const noexcept;
};

} // namespace factor::compute