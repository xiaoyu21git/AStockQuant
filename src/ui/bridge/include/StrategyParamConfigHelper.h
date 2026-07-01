#pragma once
// StrategyParamConfigHelper — 策略参数配置辅助 (替代 StrategyCreationUtils.js)
// 职责: 枚举映射 / 参数规范化 / 参数配置构建 / 策略数据组装
#include <QObject>
#include <QVariantMap>
#include <QVariantList>

class StrategyParamConfigHelper : public QObject {
    Q_OBJECT

    // ── 枚举常量 (QML 直接读) ──
    Q_PROPERTY(QVariantMap StrategyTypeIndex    READ strategyTypeIndex    CONSTANT)
    Q_PROPERTY(QVariantMap StrategyBehaviorKind READ strategyBehaviorKind CONSTANT)
    Q_PROPERTY(QVariantMap AssetTypeIndex       READ assetTypeIndex       CONSTANT)
    Q_PROPERTY(QVariantMap TimeFrameIndex        READ timeFrameIndex        CONSTANT)
    Q_PROPERTY(QVariantMap RiskLevelIndex        READ riskLevelIndex        CONSTANT)

public:
    explicit StrategyParamConfigHelper(QObject* parent = nullptr);

    // ── 枚举 ──
    QVariantMap strategyTypeIndex()    const { return m_strategyTypeIndex; }
    QVariantMap strategyBehaviorKind() const { return m_strategyBehaviorKind; }
    QVariantMap assetTypeIndex()       const { return m_assetTypeIndex; }
    QVariantMap timeFrameIndex()        const { return m_timeFrameIndex; }
    QVariantMap riskLevelIndex()        const { return m_riskLevelIndex; }

    // ── 类型映射 (Q_INVOKABLE) ──
    Q_INVOKABLE int normalizeStrategyTypeIndex(int v) const;
    Q_INVOKABLE int strategyBehaviorKindFromTypeIndex(int typeIndex) const;
    Q_INVOKABLE int strategyTypeIndexFromBehaviorKind(int behaviorKind) const;
    Q_INVOKABLE QString strategyTypeName(int typeIndex) const;
    Q_INVOKABLE QString strategyTypeDescription(int typeIndex) const;
    Q_INVOKABLE QString strategyIcon(int typeIndex) const;
    Q_INVOKABLE QString strategyBehaviorKindLabel(int behaviorKind) const;
    Q_INVOKABLE QString defaultStrategyDescription(int typeIndex) const;
    Q_INVOKABLE QVariantList defaultStrategyTags(int typeIndex) const;

    // ── 资产/时间/风险 ──
    Q_INVOKABLE QString assetTypeName(int idx) const;
    Q_INVOKABLE QString timeFrameName(int idx) const;
    Q_INVOKABLE QString riskLevelName(int idx) const;
    Q_INVOKABLE QString riskLevelColor(int idx) const;

    // ── 核心: 参数配置构建 ──
    Q_INVOKABLE QVariantList buildParamConfigs(int strategyTypeIndex) const;

    // ── 核心: 参数规范化 ──
    Q_INVOKABLE QVariantMap normalizeStrategyParameters(int strategyTypeIndex, const QVariantMap& raw) const;

    // ── 核心: 构建完整策略数据 ──
    Q_INVOKABLE QVariantMap buildCompleteStrategyData(const QVariantMap& context) const;

    // ── 步骤信息 ──
    Q_INVOKABLE QString stepLabel(int step) const;
    Q_INVOKABLE QString stepTitle(int step) const;
    Q_INVOKABLE QString stepDescription(int step) const;
    Q_INVOKABLE bool isStepValid(int step, const QVariantMap& data) const;

private:
    static QVariantMap makeEnum(std::initializer_list<std::pair<const char*, int>> vals);
    void assignIfPresent(const QVariantMap& src, QVariantMap& dest,
                         const QString& targetKey, const QStringList& sourceKeys,
                         std::function<QVariant(const QVariant&)> transform = nullptr) const;

    QVariantMap m_strategyTypeIndex;
    QVariantMap m_strategyBehaviorKind;
    QVariantMap m_assetTypeIndex;
    QVariantMap m_timeFrameIndex;
    QVariantMap m_riskLevelIndex;
};
