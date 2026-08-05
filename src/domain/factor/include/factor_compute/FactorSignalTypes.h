#pragma once

#include "AnalysisReportTypes.h"
#include "../../../types/InstrumentId.h"
#include "../../../types/DomainDate.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace factor::compute {

/// @brief 统一数值类型别名，全链路使用 float32 以降低内存占用与带宽消耗
using signal_value_t = float;

struct FactorId final {
    uint32_t value{0U};

    [[nodiscard]] bool isValid() const noexcept { return value != 0U; }
};

struct SignalId final {
    uint32_t value{0U};

    [[nodiscard]] bool isValid() const noexcept { return value != 0U; }
};

struct FactorName final {
    uint32_t value{0U};

    [[nodiscard]] bool isValid() const noexcept { return value != 0U; }
};

struct FormulaExpr final {
    uint32_t token{0U};

    [[nodiscard]] bool isValid() const noexcept { return token != 0U; }
};

struct FieldKey final {
    uint32_t value{0U};

    [[nodiscard]] bool isValid() const noexcept { return value != 0U; }
};

enum class Frequency : uint8_t {
    Daily = 0,
    Weekly = 1,
    Monthly = 2,
    Quarterly = 3,
    Yearly = 4
};

using InstrumentId = ::domain::InstrumentId; // 规范定义在 domain/types/InstrumentId.h

/// @brief 日期键，统一使用 domain::DomainDate（int32_t YYYYMMDD）
using DateKey = domain::DomainDate;

struct DateRange final {
    DateKey from{};
    DateKey to{};

    [[nodiscard]] bool isValid() const noexcept
    {
        return from.isValid() && to.isValid() && from.value <= to.value;
    }
};

struct RuntimeBudget final {
    int64_t timeoutMilliseconds{0};
    uint64_t memoryLimitBytes{0U};

    [[nodiscard]] bool isValid() const noexcept
    {
        return timeoutMilliseconds > 0 && memoryLimitBytes > 0U;
    }
};

struct ChunkPolicy final {
    uint32_t dateChunkSize{0U};
    uint32_t instrumentChunkSize{0U};

    [[nodiscard]] bool isValid() const noexcept
    {
        return dateChunkSize > 0U && instrumentChunkSize > 0U;
    }
};

struct PostProcessingConfig final {
    double winsorizeStdBand{1.0};
    double stdEpsilon{1e-12};
    int32_t minimumValidSampleCount{2};

    [[nodiscard]] bool isValid() const noexcept
    {
        return winsorizeStdBand > 0.0
            && stdEpsilon > 0.0
            && minimumValidSampleCount > 1;
    }
};

/// @brief 信号引擎运行模式
///
/// 底层通用模块通过此枚举感知当前由哪种上层入口驱动，
/// 并据此选择符合当前模式的业务逻辑路径。
///
/// SignalOnly     — 因子评价：仅计算因子信号 + IC 分析
/// FullPipeline   — 策略回测：信号 → 构仓 → 风控 → 成交 → 指标聚合
/// Incremental    — 实盘增量：仅计算当前日期信号，无历史回溯
enum class SignalEngineMode : uint8_t {
    SignalOnly = 0,
    FullPipeline = 1,
    Incremental = 2
};

enum class FactorError : uint8_t {
    None = 0,
    InvalidFormula = 1,
    CircularDependency = 2,
    InvalidUniverse = 3,
    InsufficientData = 4,
    Timeout = 5,
    MemoryExceeded = 6,
    InternalError = 7
};

template <typename TValue>
class FactorResult final {
public:
    static FactorResult success(const TValue& value)
    {
        FactorResult result;
        result.value_ = value;
        return result;
    }

    static FactorResult success(TValue&& value)
    {
        FactorResult result;
        result.value_ = std::move(value);
        return result;
    }

    static FactorResult failure(FactorError error)
    {
        FactorResult result;
        result.error_ = error;
        return result;
    }

    [[nodiscard]] bool hasValue() const noexcept { return value_.has_value(); }
    [[nodiscard]] const TValue& value() const noexcept { return *value_; }
    [[nodiscard]] TValue& value() noexcept { return *value_; }
    [[nodiscard]] FactorError error() const noexcept { return *error_; }

private:
    std::optional<TValue> value_;
    std::optional<FactorError> error_;
};

struct GenerateSpec final {
    SignalEngineMode mode{SignalEngineMode::FullPipeline};
    DateRange dateRange{};
    RuntimeBudget runtimeBudget{};
    ChunkPolicy chunkPolicy{};
    PostProcessingConfig postProcessingConfig{};
    std::vector<InstrumentId> instrumentUniverse;
    std::vector<FactorId> requestedFactors;

    [[nodiscard]] bool isValid() const noexcept
    {
        return dateRange.isValid()
            && runtimeBudget.isValid()
            && chunkPolicy.isValid()
            && postProcessingConfig.isValid()
            && !instrumentUniverse.empty()
            && !requestedFactors.empty();
    }
};

struct QuerySpec final {
    DateKey date{};
    InstrumentId instrument{};
    FactorId factor{};

    [[nodiscard]] bool isValid() const noexcept
    {
        return date.isValid() && instrument.isValid() && factor.isValid();
    }
};

/// @brief 计算模式
enum class ComputeMode : uint8_t {
    Batch = 0,         ///< 全量并行模式（回测场景）
    Incremental = 1,   ///< 单线程低延迟模式（实盘场景）
};

struct SignalValue final {
    signal_value_t value{0.0f};
    bool isMissing{false};
};

struct SignalSetIndex final {
    int32_t timeStride{0};
    int32_t instrumentStride{0};
    int32_t factorStride{0};

    [[nodiscard]] bool isValid() const noexcept
    {
        return timeStride > 0 && instrumentStride > 0 && factorStride > 0;
    }
};

struct SignalProgress final {
    uint32_t plannedFactorCount{0U};
    uint32_t completedFactorCount{0U};

    [[nodiscard]] bool isValid() const noexcept
    {
        return plannedFactorCount > 0U && completedFactorCount <= plannedFactorCount;
    }
};

struct SignalSet final {
    // 设计文档 Section 8 合同字段
    std::vector<DateKey> dates;          // 时间维度
    std::vector<InstrumentId> instruments; // 标的维度 (设计文档: insts)
    std::vector<SignalId> signalIds;     // 信号维度 (重命名避免 Qt signals 宏冲突)
    SignalSetIndex index{};
    SignalProgress progress{};
    std::vector<signal_value_t> values;  // float32 轻量数值存储
    std::vector<uint8_t> mask;
    bool isPartial{false};               // 设计文档: is_partial

    [[nodiscard]] bool isValid() const noexcept
    {
        return !dates.empty()
            && !instruments.empty()
            && !signalIds.empty()
            && index.isValid()
            && progress.isValid()
            && progress.plannedFactorCount == static_cast<uint32_t>(signalIds.size())
            && values.size() == mask.size();
    }

    /// @brief 三维访问函数（设计文档 Section 8）
    /// 逻辑索引为 [time][instrument][signal]
    signal_value_t operator()(int timeIdx, int instrumentIdx, int signalIdx) const noexcept
    {
        const size_t flatIdx = static_cast<size_t>(timeIdx) * static_cast<size_t>(index.timeStride)
            + static_cast<size_t>(instrumentIdx) * static_cast<size_t>(index.instrumentStride)
            + static_cast<size_t>(signalIdx) * static_cast<size_t>(index.factorStride);
        if (flatIdx >= values.size()) {
            return signal_value_t{0};
        }
        return values[flatIdx];
    }

    /// @brief 获取缺失标记
    bool isMissing(int timeIdx, int instrumentIdx, int signalIdx) const noexcept
    {
        const size_t flatIdx = static_cast<size_t>(timeIdx) * static_cast<size_t>(index.timeStride)
            + static_cast<size_t>(instrumentIdx) * static_cast<size_t>(index.instrumentStride)
            + static_cast<size_t>(signalIdx) * static_cast<size_t>(index.factorStride);
        if (flatIdx >= mask.size()) {
            return true;
        }
        return mask[flatIdx] != 0U;
    }
};

} // namespace factor::compute

