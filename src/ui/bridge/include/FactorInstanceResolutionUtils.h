#pragma once

#include <QString>
#include <QVector>

#include <limits>

namespace factor::bridge {

struct FactorInstanceLookupCandidates {
    QString primaryId;
    QString secondaryId;
};

struct FactorInstanceLookupRecord {
    QString instanceId;
    QString factorId;
    QString status;
};

inline FactorInstanceLookupCandidates buildFactorInstanceLookupCandidates(const QString& rawId)
{
    const QString trimmedId = rawId.trimmed();
    if (trimmedId.isEmpty()) {
        return {};
    }

    FactorInstanceLookupCandidates candidates;
    candidates.primaryId = trimmedId;
    candidates.secondaryId = trimmedId;

    if (trimmedId.endsWith("_instance")) {
        const QString baseId = trimmedId.left(trimmedId.size() - QString("_instance").size()).trimmed();
        if (!baseId.isEmpty()) {
            candidates.secondaryId = baseId;
        }
    }

    return candidates;
}

inline bool isActiveFactorInstanceStatus(const QString& status)
{
    return status.trimmed().compare("ACTIVE", Qt::CaseInsensitive) == 0;
}

inline int matchPriority(const FactorInstanceLookupCandidates& candidates,
                         const FactorInstanceLookupRecord& record)
{
    const QString instanceId = record.instanceId.trimmed();
    const QString factorId = record.factorId.trimmed();
    if (instanceId.isEmpty()) {
        return (std::numeric_limits<int>::max)();
    }

    if (instanceId == candidates.primaryId) {
        return 0;
    }
    if (instanceId == candidates.secondaryId) {
        return 1;
    }
    if (factorId == candidates.primaryId) {
        return 2;
    }
    if (factorId == candidates.secondaryId) {
        return 3;
    }

    return (std::numeric_limits<int>::max)();
}

inline QString selectBestFactorInstanceId(const FactorInstanceLookupCandidates& candidates,
                                          const QVector<FactorInstanceLookupRecord>& records,
                                          bool onlyActive)
{
    int bestPriority = (std::numeric_limits<int>::max)();
    QString bestInstanceId;

    // records are expected to already be ordered by recency; preserve first-hit wins within a priority bucket.
    for (const FactorInstanceLookupRecord& record : records) {
        if (onlyActive && !isActiveFactorInstanceStatus(record.status)) {
            continue;
        }

        const int priority = matchPriority(candidates, record);
        if (priority >= bestPriority) {
            continue;
        }

        const QString instanceId = record.instanceId.trimmed();
        if (instanceId.isEmpty()) {
            continue;
        }

        bestPriority = priority;
        bestInstanceId = instanceId;
        if (bestPriority == 0) {
            break;
        }
    }

    return bestInstanceId;
}

inline QString resolveFactorInstanceId(const QString& rawId,
                                       const QVector<FactorInstanceLookupRecord>& records)
{
    const FactorInstanceLookupCandidates candidates = buildFactorInstanceLookupCandidates(rawId);
    if (candidates.primaryId.isEmpty()) {
        return {};
    }

    const QString activeMatch = selectBestFactorInstanceId(candidates, records, true);
    if (!activeMatch.isEmpty()) {
        return activeMatch;
    }

    return selectBestFactorInstanceId(candidates, records, false);
}

}  // namespace factor::bridge