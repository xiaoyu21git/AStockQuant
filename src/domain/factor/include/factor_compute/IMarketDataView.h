#pragma once

#include "FactorSignalTypes.h"
#include "IFactorOperatorLibrary.h"

namespace factor::compute {

class IMarketDataView {
public:
    virtual ~IMarketDataView() = default;

    [[nodiscard]] virtual NumericConstMatrixView open() const = 0;
    [[nodiscard]] virtual NumericConstMatrixView high() const = 0;
    [[nodiscard]] virtual NumericConstMatrixView low() const = 0;
    [[nodiscard]] virtual NumericConstMatrixView close() const = 0;
    [[nodiscard]] virtual NumericConstMatrixView volume() const = 0;

    [[nodiscard]] virtual const std::vector<DateKey>& dates() const = 0;
    [[nodiscard]] virtual const std::vector<InstrumentId>& instruments() const = 0;
};

} // namespace factor::compute

