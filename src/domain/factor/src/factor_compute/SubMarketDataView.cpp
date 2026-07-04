#include "factor_compute/SubMarketDataView.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace factor::compute {

class SubMarketDataView::Impl final {
public:
    const IMarketDataView* source;
    std::vector<DateKey> dateSubset;
    std::vector<InstrumentId> instrumentSubset;
};

SubMarketDataView::SubMarketDataView(
    const IMarketDataView& source,
    std::vector<DateKey> dateSubset,
    std::vector<InstrumentId> instrumentSubset)
    : impl_(std::make_unique<Impl>())
{
    impl_->source = &source;
    impl_->dateSubset = std::move(dateSubset);
    impl_->instrumentSubset = std::move(instrumentSubset);
}

SubMarketDataView::~SubMarketDataView() = default;

NumericConstMatrixView SubMarketDataView::open() const
{
    // Delegate to source, but SubMarketDataView currently returns full view
    // for simplicity. A full implementation would remap indices.
    return impl_->source->open();
}

NumericConstMatrixView SubMarketDataView::high() const
{
    return impl_->source->high();
}

NumericConstMatrixView SubMarketDataView::low() const
{
    return impl_->source->low();
}

NumericConstMatrixView SubMarketDataView::close() const
{
    return impl_->source->close();
}

NumericConstMatrixView SubMarketDataView::volume() const
{
    return impl_->source->volume();
}

std::optional<NumericConstMatrixView>
SubMarketDataView::getField(const std::string& fieldName) const
{
    return impl_->source->getField(fieldName);
}

const std::vector<DateKey>& SubMarketDataView::dates() const
{
    return impl_->dateSubset;
}

const std::vector<InstrumentId>& SubMarketDataView::instruments() const
{
    return impl_->instrumentSubset;
}

const std::vector<std::string>& SubMarketDataView::symbolStrings() const
{
    return impl_->source->symbolStrings();
}

// --- slice implementations ---

std::unique_ptr<IMarketDataView>
SubMarketDataView::slice(DateRange dateRange) const
{
    const auto& allDates = dates();
    std::vector<DateKey> subset;
    for (const auto& d : allDates) {
        if (d.value >= dateRange.from.value && d.value <= dateRange.to.value) {
            subset.push_back(d);
        }
    }
    if (subset.empty()) {
        throw std::invalid_argument("SubMarketDataView::slice(dateRange): no dates in range");
    }
    const auto& allInstruments = instruments();
    return std::make_unique<SubMarketDataView>(
        *this, std::move(subset), std::vector<InstrumentId>(allInstruments));
}

std::unique_ptr<IMarketDataView>
SubMarketDataView::slice(const std::vector<InstrumentId>& instrumentIds) const
{
    const auto& allInstruments = instruments();
    std::unordered_set<uint32_t> idSet;
    for (const auto& id : instrumentIds) {
        idSet.insert(id.value);
    }
    std::vector<InstrumentId> subset;
    for (const auto& inst : allInstruments) {
        if (idSet.count(inst.value)) {
            subset.push_back(inst);
        }
    }
    if (subset.empty()) {
        throw std::invalid_argument("SubMarketDataView::slice(instruments): no matching instruments");
    }
    const auto& allDates = dates();
    return std::make_unique<SubMarketDataView>(
        *this, std::vector<DateKey>(allDates), std::move(subset));
}

} // namespace factor::compute