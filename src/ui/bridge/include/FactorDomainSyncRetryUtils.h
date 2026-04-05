#pragma once

#include <QString>

#include <functional>

namespace factor::bridge {

using DomainSyncWriteRecordFn = std::function<bool(QString* persistedInstanceId, bool forceRequestedInstanceId)>;
using DomainSyncVerifyInstanceFn = std::function<bool(const QString& instanceId, QString* errorMessage)>;
using DomainSyncDeleteRecordsFn = std::function<void(const QString& factorId, const QString& instanceId)>;
using DomainSyncInitialFailureFn = std::function<void(const QString& failedInstanceId, const QString& errorMessage)>;
using DomainSyncRetryFailureFn = std::function<void(const QString& failedInstanceId, const QString& errorMessage)>;

inline bool executeDomainSyncWithRetry(const QString& requestedInstanceId,
                                       const QString& factorId,
                                       const DomainSyncWriteRecordFn& writeRecord,
                                       const DomainSyncVerifyInstanceFn& verifyInstance,
                                       const DomainSyncDeleteRecordsFn& deleteRecords,
                                       QString* persistedInstanceId = nullptr,
                                       const DomainSyncInitialFailureFn& onInitialFailure = {},
                                       const DomainSyncRetryFailureFn& onRetryFailure = {})
{
    QString canonicalInstanceId;
    if (!writeRecord(&canonicalInstanceId, false)) {
        return false;
    }

    QString verificationError;
    if (verifyInstance(canonicalInstanceId, &verificationError)) {
        if (persistedInstanceId) {
            *persistedInstanceId = canonicalInstanceId;
        }
        return true;
    }

    if (onInitialFailure) {
        onInitialFailure(canonicalInstanceId, verificationError);
    }

    deleteRecords(factorId, canonicalInstanceId);

    canonicalInstanceId = requestedInstanceId;
    if (!writeRecord(&canonicalInstanceId, true)) {
        return false;
    }

    verificationError.clear();
    if (!verifyInstance(canonicalInstanceId, &verificationError)) {
        if (onRetryFailure) {
            onRetryFailure(canonicalInstanceId, verificationError);
        }
        return false;
    }

    if (persistedInstanceId) {
        *persistedInstanceId = canonicalInstanceId;
    }
    return true;
}

}  // namespace factor::bridge