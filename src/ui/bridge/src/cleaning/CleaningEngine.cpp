// CleaningEngine.cpp
#include "cleaning/CleaningEngine.h"

#include <QPointer>
#include <QMetaObject>
#include <algorithm>

namespace factor::bridge {

CleaningEngine::CleaningEngine(QObject* parent)
    : QObject(parent)
{
}

CleaningEngine::~CleaningEngine() = default;

void CleaningEngine::addRule(std::unique_ptr<ICleaningRule> rule) {
    m_rules.push_back(std::move(rule));
    sortRules();
}

void CleaningEngine::addRules(std::vector<std::unique_ptr<ICleaningRule>> rules) {
    for (auto& rule : rules) {
        m_rules.push_back(std::move(rule));
    }
    sortRules();
}

void CleaningEngine::sortRules() {
    std::sort(m_rules.begin(), m_rules.end(),
              [](const std::unique_ptr<ICleaningRule>& a,
                 const std::unique_ptr<ICleaningRule>& b) {
                  return a->executionOrder() < b->executionOrder();
              });
}

QVariantList CleaningEngine::clean(const QVariantList& data) {
    if (data.isEmpty()) {
        qWarning() << "CleaningEngine: No data";
        return {};
    }

    emit progress(0, QStringLiteral("开始清洗..."));

    CleaningStats stats;
    stats.totalRecords = data.size();
    stats.startTime = QDateTime::currentDateTime();

    QVector<QVariantMap> records;
    records.reserve(data.size());
    for (const QVariant& item : data) {
        if (item.canConvert<QVariantMap>())
            records.append(item.toMap());
    }

    emit progress(5, QStringLiteral("解析完成: %1条").arg(records.size()));

    QVariantList cleaned;
    cleaned.reserve(records.size());

    for (int i = 0; i < records.size(); ++i) {
        QVariantMap row = records[i];
        bool keep = true;

        for (const auto& rule : m_rules) {
            if (!rule->appliesTo(row)) continue;

            if (!rule->clean(row)) {
                keep = false;
                auto it = std::find_if(stats.ruleStats.begin(), stats.ruleStats.end(),
                    [&](const CleaningStats::RuleStat& rs) { return rs.ruleId == rule->id(); });
                if (it != stats.ruleStats.end()) it->removed++;
                else stats.ruleStats.push_back({rule->id(), 0, 1});
                break;
            } else {
                auto it = std::find_if(stats.ruleStats.begin(), stats.ruleStats.end(),
                    [&](const CleaningStats::RuleStat& rs) { return rs.ruleId == rule->id(); });
                if (it != stats.ruleStats.end()) it->passed++;
                else stats.ruleStats.push_back({rule->id(), 1, 0});
            }
        }

        if (keep)
            cleaned.append(row);

        if (i % 500 == 0 || i == records.size() - 1) {
            int pct = 10 + static_cast<int>((i + 1) * 80.0 / records.size());
            emit progress(pct, QStringLiteral("清洗: %1/%2").arg(i + 1).arg(records.size()));
        }
    }

    emit progress(90, QStringLiteral("横截面处理..."));

    QVariantList crossRecords = cleaned;
    for (const auto& rule : m_rules)
        rule->cleanCrossSectional(crossRecords);

    stats.keptRecords = cleaned.size();
    stats.removedRecords = stats.totalRecords - cleaned.size();
    stats.endTime = QDateTime::currentDateTime();
    stats.durationMs = stats.startTime.msecsTo(stats.endTime);

    {
        std::lock_guard<std::mutex> locker(m_mutex);
        m_lastStats = stats;
    }

    emit progress(100, QStringLiteral("完成: 原始%1, 保留%2, 移除%3, %4ms")
                          .arg(stats.totalRecords).arg(stats.keptRecords)
                          .arg(stats.removedRecords).arg(stats.durationMs));
    emit completed(stats);

    return cleaned;
}

void CleaningEngine::cleanAsync(const QVariantList& data) {
    if (data.isEmpty()) {
        emit errorOccurred(QStringLiteral("无数据"));
        return;
    }

    QPointer<CleaningEngine> safe(this);
    QtConcurrent::run([safe, data]() {
        if (!safe) return;
        try {
            safe->clean(data);
        } catch (const std::exception& e) {
            QString msg = QStringLiteral("异常: %1").arg(e.what());
            QMetaObject::invokeMethod(safe, [safe, msg]() {
                if (safe) safe->errorOccurred(msg);
            }, Qt::QueuedConnection);
        }
    });
}

CleaningStats CleaningEngine::lastStats() const {
    std::lock_guard<std::mutex> locker(m_mutex);
    return m_lastStats;
}

} // namespace factor::bridge
