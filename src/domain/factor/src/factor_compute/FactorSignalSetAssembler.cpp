#include "factor_compute/FactorSignalSetAssembler.h"

#include <cstdlib>

namespace factor::compute {

SignalSet FactorSignalSetAssembler::assemble(
    const ProcessedTensorView& tensor,
    const AssembleContext& context) const
{
    if (!tensor.isValid() || !context.isValid()) {
        std::abort();
    }

    SignalSet output;
    output.dates = context.dates;
    output.instruments = context.instruments;
    // Convert FactorId → SignalId (design doc Section 8: SignalSet uses SignalId)
    output.signals.reserve(context.factors.size());
    for (const FactorId& fid : context.factors) {
        SignalId sid;
        sid.value = fid.value;
        output.signals.push_back(sid);
    }
    output.progress = context.progress;
    output.isPartial = context.isPartial;

    output.index.factorStride = 1;
    output.index.instrumentStride = tensor.factorCount;
    output.index.timeStride = tensor.instrumentCount * tensor.factorCount;

    const size_t flatCount = static_cast<size_t>(tensor.timeCount)
        * static_cast<size_t>(tensor.instrumentCount)
        * static_cast<size_t>(tensor.factorCount);

    output.values.assign(tensor.values, tensor.values + flatCount);
    output.mask.assign(tensor.mask, tensor.mask + flatCount);
    return output;
}

} // namespace factor::compute