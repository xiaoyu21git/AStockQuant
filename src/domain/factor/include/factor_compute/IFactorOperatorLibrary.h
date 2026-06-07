#pragma once

#include "FactorSignalTypes.h"

namespace factor::compute {

struct NumericMatrixView final {
    signal_value_t* data{nullptr};
    int32_t rowCount{0};
    int32_t columnCount{0};
    int32_t rowStride{0};

    [[nodiscard]] bool isValid() const noexcept
    {
        return data != nullptr && rowCount > 0 && columnCount > 0 && rowStride >= columnCount;
    }
};

struct NumericConstMatrixView final {
    const signal_value_t* data{nullptr};
    int32_t rowCount{0};
    int32_t columnCount{0};
    int32_t rowStride{0};

    [[nodiscard]] bool isValid() const noexcept
    {
        return data != nullptr && rowCount > 0 && columnCount > 0 && rowStride >= columnCount;
    }
};

struct GroupKeyView final {
    const uint32_t* data{nullptr};
    int32_t count{0};

    [[nodiscard]] bool isValid() const noexcept
    {
        return data != nullptr && count > 0;
    }
};

class IFactorOperatorLibrary {
public:
    virtual ~IFactorOperatorLibrary() = default;

    virtual void lag(NumericConstMatrixView input, int32_t window, NumericMatrixView output) const = 0;
    virtual void rollingMean(NumericConstMatrixView input, int32_t window, NumericMatrixView output) const = 0;
    virtual void rollingSum(NumericConstMatrixView input, int32_t window, NumericMatrixView output) const = 0;
    virtual void rank(NumericConstMatrixView input, NumericMatrixView output) const = 0;
    virtual void groupByMean(
        NumericConstMatrixView input,
        GroupKeyView groupKeys,
        NumericMatrixView output) const = 0;
};

} // namespace factor::compute

