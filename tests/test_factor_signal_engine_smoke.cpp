#include "factor_compute/FactorComputeDispatcher.h"
#include "factor_compute/FactorComputeToken.h"
#include "factor_compute/FactorRegistry.h"
#include "factor_compute/FactorOperatorLibrary.h"
#include "factor_compute/FactorComputeEngine.h"
#include "factor_compute/FactorSignalSetAssembler.h"
#include "factor_compute/IMarketDataView.h"

namespace {

using namespace factor::compute;

constexpr int kExitSuccess = 0;
constexpr int kExitFailure = 1;
constexpr int32_t kSingleRowCount = 1;
constexpr int32_t kSingleColumnCount = 1;
constexpr int32_t kSingleRowStride = 1;
constexpr double kDummyPrice = 1.0;
constexpr uint32_t kFactorNameToken = 1001U;
constexpr uint32_t kFieldToken = 3001U;
constexpr uint32_t kInstrumentToken = 600001U;
constexpr int32_t kStartDate = 20200102;
constexpr int32_t kEndDate = 20200131;
constexpr int64_t kTimeoutMilliseconds = 1000;
constexpr uint64_t kMemoryLimitBytes = 1024U * 1024U;
constexpr uint32_t kDateChunkSize = 8U;
constexpr uint32_t kInstrumentChunkSize = 16U;

class MockMarketDataView final : public IMarketDataView {
public:
    [[nodiscard]] NumericConstMatrixView open() const override { return buildSinglePointView(); }
    [[nodiscard]] NumericConstMatrixView high() const override { return buildSinglePointView(); }
    [[nodiscard]] NumericConstMatrixView low() const override { return buildSinglePointView(); }
    [[nodiscard]] NumericConstMatrixView close() const override { return buildSinglePointView(); }
    [[nodiscard]] NumericConstMatrixView volume() const override { return buildSinglePointView(); }
    [[nodiscard]] std::optional<NumericConstMatrixView> getField(const std::string&) const override {
        return buildSinglePointView();
    }

    [[nodiscard]] const std::vector<DateKey>& dates() const override { return dates_; }
    [[nodiscard]] const std::vector<InstrumentId>& instruments() const override { return instruments_; }

private:
    [[nodiscard]] NumericConstMatrixView buildSinglePointView() const
    {
        NumericConstMatrixView view;
        view.data = &dummyValue_;
        view.rowCount = kSingleRowCount;
        view.columnCount = kSingleColumnCount;
        view.rowStride = kSingleRowStride;
        return view;
    }

    double dummyValue_{kDummyPrice};
    std::vector<DateKey> dates_{DateKey{kStartDate}};
    std::vector<InstrumentId> instruments_{InstrumentId{kInstrumentToken}};
};

bool runGenerateSuccessCase()
{
    FactorRegistry factorRegistry;
    FactorSignalSetAssembler signalSetAssembler;
    FactorOperatorLibrary factorOperatorLibrary;
    FactorComputeDispatcher factorComputeDispatcher(factorOperatorLibrary);
    MockMarketDataView marketDataView;

    const FactorResult<FactorId> registeredFactor = factorRegistry.registerFormula(
        FactorName{kFactorNameToken},
        FormulaExpr{toToken(ComputeToken::Close)},
        std::vector<FieldKey>{FieldKey{kFieldToken}});
    if (!registeredFactor.hasValue()) {
        return false;
    }

    FactorComputeEngine factorComputeEngine(
        factorRegistry,
        signalSetAssembler,
        factorComputeDispatcher,
        marketDataView);

    GenerateSpec generateSpec;
    generateSpec.mode = SignalEngineMode::FullPipeline;
    generateSpec.dateRange = DateRange{DateKey{kStartDate}, DateKey{kEndDate}};
    generateSpec.runtimeBudget = RuntimeBudget{kTimeoutMilliseconds, kMemoryLimitBytes};
    generateSpec.chunkPolicy = ChunkPolicy{kDateChunkSize, kInstrumentChunkSize};
    generateSpec.instrumentUniverse = std::vector<InstrumentId>{InstrumentId{kInstrumentToken}};
    generateSpec.requestedFactors = std::vector<FactorId>{registeredFactor.value()};

    const FactorResult<SignalSet> generateResult = factorComputeEngine.generate(generateSpec);
    if (!generateResult.hasValue()) {
        return false;
    }

    const SignalSet& signalSet = generateResult.value();
    return signalSet.isValid();
}

bool runGenerateFailureCase()
{
    FactorRegistry factorRegistry;
    FactorSignalSetAssembler signalSetAssembler;
    FactorOperatorLibrary factorOperatorLibrary;
    FactorComputeDispatcher factorComputeDispatcher(factorOperatorLibrary);
    MockMarketDataView marketDataView;
    FactorComputeEngine factorComputeEngine(
        factorRegistry,
        signalSetAssembler,
        factorComputeDispatcher,
        marketDataView);

    const GenerateSpec invalidGenerateSpec{};
    const FactorResult<SignalSet> generateResult = factorComputeEngine.generate(invalidGenerateSpec);

    return !generateResult.hasValue() && generateResult.error() == FactorError::InvalidUniverse;
}

bool runQuerySuccessCase()
{
    FactorRegistry factorRegistry;
    FactorSignalSetAssembler signalSetAssembler;
    FactorOperatorLibrary factorOperatorLibrary;
    FactorComputeDispatcher factorComputeDispatcher(factorOperatorLibrary);
    MockMarketDataView marketDataView;

    const FactorResult<FactorId> registeredFactor = factorRegistry.registerFormula(
        FactorName{kFactorNameToken},
        FormulaExpr{toToken(ComputeToken::Close)},
        std::vector<FieldKey>{FieldKey{kFieldToken}});
    if (!registeredFactor.hasValue()) {
        return false;
    }

    FactorComputeEngine factorComputeEngine(
        factorRegistry,
        signalSetAssembler,
        factorComputeDispatcher,
        marketDataView);

    QuerySpec querySpec;
    querySpec.date = DateKey{kStartDate};
    querySpec.instrument = InstrumentId{kInstrumentToken};
    querySpec.factor = registeredFactor.value();

    const FactorResult<SignalValue> queryResult = factorComputeEngine.query(querySpec);
    return queryResult.hasValue()
        && !queryResult.value().isMissing
        && queryResult.value().value == kDummyPrice;
}

bool runQueryFailureCase()
{
    FactorRegistry factorRegistry;
    FactorSignalSetAssembler signalSetAssembler;
    FactorOperatorLibrary factorOperatorLibrary;
    FactorComputeDispatcher factorComputeDispatcher(factorOperatorLibrary);
    MockMarketDataView marketDataView;
    FactorComputeEngine factorComputeEngine(
        factorRegistry,
        signalSetAssembler,
        factorComputeDispatcher,
        marketDataView);

    const QuerySpec invalidQuerySpec{};
    const FactorResult<SignalValue> queryResult = factorComputeEngine.query(invalidQuerySpec);

    return !queryResult.hasValue() && queryResult.error() == FactorError::InvalidUniverse;
}

} // namespace

int main()
{
    if (!runGenerateSuccessCase()) {
        return kExitFailure;
    }
    if (!runGenerateFailureCase()) {
        return kExitFailure;
    }
    if (!runQuerySuccessCase()) {
        return kExitFailure;
    }
    if (!runQueryFailureCase()) {
        return kExitFailure;
    }

    return kExitSuccess;
}



