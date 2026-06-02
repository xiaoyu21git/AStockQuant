#include "factor_compute/FactorComputeDispatcher.h"

#include "factor_compute/DefaultComputeOpRegistrar.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace factor::compute {

NumericMatrixView FactorComputeDispatcher::buildMutableMatrixView(
    double* data,
    int32_t rowCount,
    int32_t columnCount) noexcept
{
    NumericMatrixView view;
    view.data = data;
    view.rowCount = rowCount;
    view.columnCount = columnCount;
    view.rowStride = columnCount;
    return view;
}

FactorComputeDispatcher::FactorComputeDispatcher(
    const IFactorOperatorLibrary& factorOperatorLibrary) noexcept
    : factorOperatorLibrary_(factorOperatorLibrary)
    , dispatchTable_{}
{
    registerDefaultOperators();
}

FactorComputeDispatcher::FactorComputeDispatcher(
    const IFactorOperatorLibrary& factorOperatorLibrary,
    const std::vector<const IComputeOpRegistrar*>& extensionRegistrars) noexcept
    : factorOperatorLibrary_(factorOperatorLibrary)
    , dispatchTable_{}
{
    registerDefaultOperators();
    if (!registerExtensionOperators(extensionRegistrars)) {
        hasInvalidRegistration_ = true;
    }
}

void FactorComputeDispatcher::registerDefaultOperators()
{
    const DefaultOpRegistrar defaultRegistrar;
    if (!defaultRegistrar.registerOperators(*this)) {
        hasInvalidRegistration_ = true;
    }
}

bool FactorComputeDispatcher::registerOperator(
    uint32_t computeToken,
    std::unique_ptr<IComputeOp> computeOperator)
{
    if (computeToken == 0U || computeOperator == nullptr) {
        return false;
    }
    if (dispatchTable_.find(computeToken) != dispatchTable_.end()) {
        return false;
    }

    dispatchTable_.emplace(computeToken, std::move(computeOperator));
    return true;
}

bool FactorComputeDispatcher::registerExtensionOperators(
    const std::vector<const IComputeOpRegistrar*>& extensionRegistrars)
{
    for (const IComputeOpRegistrar* registrar : extensionRegistrars) {
        if (registrar == nullptr) {
            return false;
        }
        if (!registrar->registerOperators(*this)) {
            return false;
        }
    }
    return true;
}

const IComputeOp* FactorComputeDispatcher::findOperator(
    uint32_t computeToken) const noexcept
{
    const auto strategyIt = dispatchTable_.find(computeToken);
    if (strategyIt == dispatchTable_.end()) {
        return nullptr;
    }
    return strategyIt->second.get();
}

FactorResult<std::vector<double>> FactorComputeDispatcher::evaluateOnClose(
    NumericConstMatrixView closeView,
    uint32_t computeToken) const
{
    if (hasInvalidRegistration_) {
        return FactorResult<std::vector<double>>::failure(FactorError::InternalError);
    }

    if (!closeView.isValid()) {
        return FactorResult<std::vector<double>>::failure(FactorError::InsufficientData);
    }

    const IComputeOp* const computeOperator = findOperator(computeToken);
    if (computeOperator == nullptr) {
        return FactorResult<std::vector<double>>::failure(FactorError::InvalidFormula);
    }

    const size_t rowCount = static_cast<size_t>(closeView.rowCount);
    const size_t columnCount = static_cast<size_t>(closeView.columnCount);
    if (rowCount > (std::numeric_limits<size_t>::max() / columnCount)) {
        return FactorResult<std::vector<double>>::failure(FactorError::MemoryExceeded);
    }

    const size_t bufferSize = rowCount * columnCount;
    const size_t maxBufferSize = std::vector<double>().max_size();
    if (bufferSize > maxBufferSize) {
        return FactorResult<std::vector<double>>::failure(FactorError::MemoryExceeded);
    }

    std::vector<double> outputBuffer;
    try {
        outputBuffer.assign(bufferSize, std::numeric_limits<double>::quiet_NaN());
    } catch (const std::length_error&) {
        return FactorResult<std::vector<double>>::failure(FactorError::MemoryExceeded);
    } catch (const std::bad_alloc&) {
        return FactorResult<std::vector<double>>::failure(FactorError::MemoryExceeded);
    }

    NumericMatrixView outputView = FactorComputeDispatcher::buildMutableMatrixView(
        outputBuffer.data(),
        closeView.rowCount,
        closeView.columnCount);

    computeOperator->execute(factorOperatorLibrary_, closeView, outputView);
    return FactorResult<std::vector<double>>::success(std::move(outputBuffer));
}

} // namespace factor::compute


