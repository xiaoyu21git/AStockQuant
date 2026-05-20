// CleaningEngine.h
// 数据清洗引擎 —— 唯一职责：数据净化
// 无回测、无风控、无策略
#pragma once

#include "CleaningRuleContract.h"

#include <QObject>
#include <QDateTime>
#include <QSet>
#include <QVector>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <vector>
#include <mutex>
#include <QtConcurrent/QtConcurrent>

namespace factor::bridge {

// ============================================================================
// 清洗规则 —— 抽象策略
// ============================================================================
class ICleaningRule {
public:
    virtual ~ICleaningRule() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual int executionOrder() const = 0;

    // 是否应用到该笔记录
    virtual bool appliesTo(const QVariantMap& record) const = 0;

    // 单行清洗：返回 true 保留，false 剔除
    virtual bool clean(QVariantMap& record) = 0;

    // 横截面清洗（可选）
    virtual void cleanCrossSectional(QVariantList& records) {
        Q_UNUSED(records);
    }
};

// ============================================================================
// 清洗统计
// ============================================================================
struct CleaningStats {
    int totalRecords{0};
    int keptRecords{0};
    int removedRecords{0};
    QDateTime startTime;
    QDateTime endTime;
    qint64 durationMs{0};

    struct RuleStat {
        QString ruleId;
        int passed{0};
        int removed{0};
    };
    QVector<RuleStat> ruleStats;
};

// ============================================================================
// 清洗引擎
// ============================================================================
class CleaningEngine : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(CleaningEngine)

public:
    explicit CleaningEngine(QObject* parent = nullptr);
    ~CleaningEngine() override;

    // 规则注册
    void addRule(std::unique_ptr<ICleaningRule> rule);
    void addRules(std::vector<std::unique_ptr<ICleaningRule>> rules);

    // 同步清洗
    QVariantList clean(const QVariantList& data);

    // 异步清洗
    void cleanAsync(const QVariantList& data);

    // 统计数据
    CleaningStats lastStats() const;

signals:
    void progress(int percent, const QString& message);
    void progressDetail(int percent, const QString& message,
                        const QString& currentSymbol, int kept, int removed);
    void completed(const CleaningStats& stats);
    void errorOccurred(const QString& message);

private:
    void sortRules();

    std::vector<std::unique_ptr<ICleaningRule>> m_rules;
    CleaningStats m_lastStats;
    mutable std::mutex m_mutex;
};

} // namespace factor::bridge
