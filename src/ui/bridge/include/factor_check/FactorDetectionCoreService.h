#pragma once

#include <string>
#include <vector>

#include "factor_check/FactorSupportCheckCore.h"
#include "../../../domain/factor/include/factor_enums.h"

namespace factor::bridge::check {

struct FactorId final {
    std::string value;
};

struct InstanceId final {
    std::string value;
};

struct FieldKey final {
    std::string value;
};

enum class SupportCategory {
    Supported = 0,
    RuntimeInitFailed = 1,
    InstanceMissing = 2,
    InstanceCreateFailed = 3,
    InvalidBacktestWindow = 4,
    MissingField = 5,
    DatasetMissing = 6,
    DatasetInvalid = 7,
    DatasetEmpty = 8,
    InsufficientHistory = 9,
};

enum class SupportReason {
    FieldCheckPassed = 0,
    RuntimeInitFailure = 1,
    MissingInstanceId = 2,
    InstanceCreationFailure = 3,
    InvalidWindow = 4,
    MissingCustomExpression = 5,
    MissingDeclaredFields = 6,
    MissingDataset = 7,
    InvalidDataset = 8,
    EmptyDataset = 9,
    MissingOrEmptyFields = 10,
    InsufficientHistory = 11,
};

enum class RunFailureCode {
    None = 0,
    InvalidRunSpec = 1,
    MissingExecutionModule = 2,
    MissingResolvedSymbols = 3,
    MissingSelectedFactors = 4,
};

struct SupportInfo final {
    FactorId factorId;
    InstanceId instanceId;
    factor::FactorType runtimeType{factor::FactorType::UNKNOWN};
    SupportCategory category{SupportCategory::MissingField};
    SupportReason reason{SupportReason::MissingDeclaredFields};
    std::vector<FieldKey> requiredFields;
    std::vector<FieldKey> missingFields;
    factor::SourceTable sourceTable{factor::SourceTable::UNKNOWN};
    bool supported{false};
    int availableTradeDateCount{0};
    int requiredWarmupTradingDays{0};
    bool useCacheMode{false};
    std::string runtimeErrorDetail;
    std::vector<FieldKey> emptyValueFields;
    RunFailureCode runFailureCode{RunFailureCode::None};
};

struct OutcomeSupportRequest final {
    FactorId factorId;
    InstanceId instanceId;
    factor::FactorType runtimeType{factor::FactorType::UNKNOWN};
    factor::SourceTable sourceTable{factor::SourceTable::UNKNOWN};
    bool useCacheMode{false};
    int availableTradeDateCount{0};
    int requiredWarmupTradingDays{0};
    std::vector<FieldKey> requiredFields;
    std::vector<FieldKey> missingFields;
    std::vector<FieldKey> emptyValueFields;
    OutcomeCode code{OutcomeCode::MissingRequiredFields};
    bool supported{false};
};

class FactorDetectionCoreService final {
public:
    static SupportInfo makeRuntimeInitFailure(const FactorId& factorId,
                                              const std::string& runtimeErrorDetail);

    static SupportInfo makeInstanceMissing(const FactorId& factorId);

    static SupportInfo makeInstanceCreateFailed(const FactorId& factorId,
                                                const InstanceId& instanceId,
                                                factor::FactorType runtimeType);

    static SupportInfo makeInvalidBacktestWindow(const FactorId& factorId,
                                                 const InstanceId& instanceId,
                                                 factor::FactorType runtimeType);

    static SupportInfo makeOutcomeBased(const OutcomeSupportRequest& request);

    static std::string categoryToken(SupportCategory category);
    static std::string reasonMessage(const SupportInfo& info);
};

} // namespace factor::bridge::check
