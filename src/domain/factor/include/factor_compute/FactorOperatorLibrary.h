#pragma once

#include "IFactorOperatorLibrary.h"

namespace factor::compute {

class FactorOperatorLibrary final : public IFactorOperatorLibrary {
public:
    void lag(NumericConstMatrixView input, int32_t window, NumericMatrixView output) const override;
    void rollingMean(NumericConstMatrixView input, int32_t window, NumericMatrixView output) const override;
    void rollingSum(NumericConstMatrixView input, int32_t window, NumericMatrixView output) const override;
    void rank(NumericConstMatrixView input, NumericMatrixView output) const override;
    void groupByMean(
        NumericConstMatrixView input,
        GroupKeyView groupKeys,
        NumericMatrixView output) const override;
};

} // namespace factor::compute

