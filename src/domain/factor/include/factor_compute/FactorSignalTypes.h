#pragma once

#include "AnalysisReportTypes.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace factor::compute {

struct FactorId final {
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

struct InstrumentId final {
    uint32_t value{0U};

    [[nodiscard]] bool isValid() const noexcept { return value != 0U; }
};

struct DateKey final {
    int32_t value{0};

    static constexpr int32_t kMinimumDate = 19000101;
    static constexpr int32_t kMaximumDate = 29991231;

    [[nodiscard]] bool isValid() const noexcept
    {
        return value >= kMinimumDate && value <= kMaximumDate;
    }
};

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

struct SignalValue final {
    double value{0.0};
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
    std::vector<DateKey> dates;
    std::vector<InstrumentId> instruments;
    std::vector<FactorId> factors;
    SignalSetIndex index{};
    SignalProgress progress{};
    std::vector<double> values;
    std::vector<uint8_t> mask;
    bool isPartial{false};

    [[nodiscard]] bool isValid() const noexcept
    {
        return !dates.empty()
            && !instruments.empty()
            && !factors.empty()
            && index.isValid()
            && progress.isValid()
            && progress.plannedFactorCount == static_cast<uint32_t>(factors.size())
            && values.size() == mask.size();
    }
};

} // namespace factor::compute

